#include "RoomManager.h"
#include "UserManager.h"
#include <iostream>
#include <vector>
#include <atomic>
#include <gflags/gflags.h>
#include <spdlog/spdlog.h>
#include <grpcpp/support/proto_buffer_writer.h>
#include <grpcpp/support/proto_buffer_reader.h>

DECLARE_int32(max_threads);
DECLARE_int32(max_pending_packets);

// Global atomic to track pending broadcast packets across all streams
std::atomic<int> g_pending_packets(0);

// --- StreamWrapper Implementation ---

bool StreamWrapper::enqueue(std::shared_ptr<const grpc::ByteBuffer> buffer, ThreadPool& pool) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (closed_) return false;

        // Increased individual queue size to 200 packets
        if (write_queue_.size() > 200) {
            write_queue_.pop();
            g_pending_packets--;
            static uint64_t stream_drop_count = 0;
            if (stream_drop_count++ % 100 == 0) {
                spdlog::warn("Stream queue full, dropping oldest pre-serialized packet (sampled 1/100)");
            }
        }

        write_queue_.push(buffer);
        g_pending_packets++;
    }

    bool expected = false;
    if (is_draining_.compare_exchange_strong(expected, true)) {
        auto self = shared_from_this();
        pool.enqueue([self] {
            self->drain();
        });
    }
    return true;
}

void StreamWrapper::drain() {
    while (true) {
        std::shared_ptr<const grpc::ByteBuffer> buffer;
        AudioStream* current_stream = nullptr;

        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (closed_ || write_queue_.empty()) {
                is_draining_ = false;
                close_cv_.notify_all();
                return;
            }
            buffer = std::move(write_queue_.front());
            write_queue_.pop();
            g_pending_packets--;
            current_stream = stream_;
        }

        if (current_stream && buffer) {
            // Write the pre-serialized ByteBuffer directly. 
            // This bypasses the serialization step in gRPC for this stream.
            if (!current_stream->Write(*buffer, grpc::WriteOptions())) {
                std::lock_guard<std::mutex> lock(mutex_);
                closed_ = true;
                stream_ = nullptr;
                is_draining_ = false;
                close_cv_.notify_all();
                spdlog::error("gRPC Write (ByteBuffer) failed, closing stream");
                return;
            }
        }
    }
}

// --- Room Implementation ---

void Room::addUser(UserId userId) {
  std::lock_guard<std::mutex> lock(mutex_);
  users_.insert(userId);
}

void Room::removeUser(UserId userId) {
  std::lock_guard<std::mutex> lock(mutex_);
  users_.erase(userId);
  auto it = audio_streams_.find(userId);
  if (it != audio_streams_.end()) {
      it->second->close();
      audio_streams_.erase(it);
  }
}

std::set<UserId> Room::getUsers() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return users_;
}

void Room::addAudioStream(UserId userId, AudioStream* stream) {
    std::lock_guard<std::mutex> lock(mutex_);
    audio_streams_[userId] = std::make_shared<StreamWrapper>(stream);
}

void Room::removeAudioStream(UserId userId) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = audio_streams_.find(userId);
    if (it != audio_streams_.end()) {
        it->second->close();
        audio_streams_.erase(it);
    }
}

void Room::broadcastAudio(UserId senderId, const echomesh::VoicePacket& packet, ThreadPool& pool) {
    if (g_pending_packets.load() > FLAGS_max_pending_packets) {
        static uint64_t drop_count = 0;
        if (drop_count++ % 100 == 0) {
            spdlog::warn("Global pending packets ({}) exceeds limit ({}), dropping broadcast (sampled 1/100)", 
                         g_pending_packets.load(), FLAGS_max_pending_packets);
        }
        return;
    }

    // PRE-SERIALIZATION OPTIMIZATION:
    // Serialize the packet ONCE into a ByteBuffer.
    auto shared_buffer = std::make_shared<grpc::ByteBuffer>();
    bool own_buffer = false;
    grpc::Status status = grpc::GenericSerialize<grpc::ProtoBufferWriter, echomesh::VoicePacket>(
        packet, shared_buffer.get(), &own_buffer
    );

    if (!status.ok()) {
        spdlog::error("Failed to pre-serialize VoicePacket: {}", status.error_message());
        return;
    }

    std::vector<std::shared_ptr<StreamWrapper>> targets;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& pair : audio_streams_) {
            if (pair.first != senderId) {
                targets.push_back(pair.second);
            }
        }
    }

    // Push the pre-serialized buffer to all targets.
    // Each targets[i]->Write(*shared_buffer) will now be a simple bit-copy 
    // instead of a full protobuf serialization.
    for (auto& stream_wrapper : targets) {
        stream_wrapper->enqueue(shared_buffer, pool);
    }
}


// --- RoomManager Implementation ---

RoomManager::RoomManager() {
    spdlog::info("Initializing RoomManager with {} threads", FLAGS_max_threads);
    m_threadPool = std::make_unique<ThreadPool>(FLAGS_max_threads);
}

RoomManager &RoomManager::getInstance() {
  static RoomManager instance;
  return instance;
}

bool RoomManager::createRoom_nl(const RoomId &roomId) {
    if (rooms_.count(roomId)) {
        return false;
    }
    rooms_[roomId] = std::make_shared<Room>();
    return true;
}

bool RoomManager::createRoom(const RoomId &roomId) {
  std::lock_guard<std::mutex> lock(mutex_);
  return createRoom_nl(roomId);
}

bool RoomManager::joinRoom(const RoomId &roomId, UserId userId) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = rooms_.find(roomId);
  if (it == rooms_.end()) {
    if (!createRoom_nl(roomId)) return false;
    it = rooms_.find(roomId);
  }
  it->second->addUser(userId);
  user_to_room_map_[userId] = roomId;
  return true;
}

void RoomManager::leaveRoom(const RoomId &roomId, UserId userId) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = rooms_.find(roomId);
  if (it != rooms_.end()) {
    it->second->removeUser(userId);
  }
  user_to_room_map_.erase(userId);
}

void RoomManager::userLogout(UserId userId) {
    std::unique_lock<std::mutex> lock(mutex_);
    auto it = user_to_room_map_.find(userId);
    if (it != user_to_room_map_.end()) {
        RoomId roomId = it->second;
        lock.unlock();
        leaveRoom(roomId, userId);
    }
}

std::shared_ptr<Room> RoomManager::getRoom(const RoomId &roomId) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = rooms_.find(roomId);
  if (it != rooms_.end()) {
    return it->second;
  }
  return nullptr;
}

std::set<UserId> RoomManager::getUsersInRoom(const RoomId &roomId) {
    auto room = getRoom(roomId);
    if (room) {
        return room->getUsers();
    }
    return {};
}

void RoomManager::addAudioStream(const RoomId& roomId, UserId userId, AudioStream* stream) {
    auto room = getRoom(roomId);
    if (room) {
        room->addAudioStream(userId, stream);
    }
}

void RoomManager::removeAudioStream(const RoomId& roomId, UserId userId) {
    auto room = getRoom(roomId);
    if (room) {
        room->removeAudioStream(userId);
    }
}

void RoomManager::broadcastAudio(const RoomId& roomId, UserId senderId, const echomesh::VoicePacket& packet) {
    auto room = getRoom(roomId);
    if (room && m_threadPool) {
        room->broadcastAudio(senderId, packet, *m_threadPool);
    }
}
