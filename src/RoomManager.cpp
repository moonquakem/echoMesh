#include "RoomManager.h"
#include "UserManager.h"
#include <iostream>
#include <vector>
#include <atomic>

// Global atomic to track pending broadcast packets across all streams
std::atomic<int> g_pending_packets(0);

// --- StreamWrapper Implementation ---

bool StreamWrapper::enqueue(const echomesh::VoicePacket& packet, ThreadPool& pool) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (closed_) return false;

        // Limit individual queue size to 50 packets (approx 1s of audio)
        if (write_queue_.size() > 50) {
            write_queue_.pop();
            g_pending_packets--;
        }

        write_queue_.push(packet);
        g_pending_packets++;
    }

    // Only enqueue a drain task if one isn't already running
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
        echomesh::VoicePacket packet;
        AudioStream* current_stream = nullptr;

        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (closed_ || write_queue_.empty()) {
                is_draining_ = false;
                close_cv_.notify_all(); // Notify close() that we are done
                return;
            }
            packet = std::move(write_queue_.front());
            write_queue_.pop();
            g_pending_packets--;
            current_stream = stream_;
        }

        if (current_stream) {
            // Blocking write happens here, but only affects this stream's dedicated drain task
            if (!current_stream->Write(packet)) {
                std::lock_guard<std::mutex> lock(mutex_);
                closed_ = true; // Mark as closed if write fails
                stream_ = nullptr;
                is_draining_ = false;
                close_cv_.notify_all();
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
    // Global protection: if total pending packets exceed a threshold, drop this broadcast
    if (g_pending_packets.load() > 5000) {
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

    // BROADCAST OPTIMIZATION:
    // Broadcaster thread now only performs ultra-fast push operations.
    // This removes O(N^2) lock contention during gRPC Write calls.
    for (auto& stream_wrapper : targets) {
        stream_wrapper->enqueue(packet, pool);
    }
}


// --- RoomManager Implementation ---

RoomManager::RoomManager() {
    // 64 threads is a healthy amount for a pool where tasks spend time in IO (Write)
    m_threadPool = std::make_unique<ThreadPool>(64);
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
