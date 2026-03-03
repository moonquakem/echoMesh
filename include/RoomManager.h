#pragma once

#include "message.grpc.pb.h"
#include <grpcpp/grpcpp.h>
#include <cstdint>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>
#include <optional>

using UserId = int64_t;
using RoomId = std::string;

// A type alias for the bidirectional audio stream
using AudioStream = grpc::ServerReaderWriter<echomesh::VoicePacket, echomesh::VoicePacket>;

// Safe wrapper for AudioStream to prevent concurrent writes and use-after-free
class StreamWrapper {
public:
    StreamWrapper(AudioStream* stream) : stream_(stream), closed_(false) {}
    
    bool write(const echomesh::VoicePacket& packet) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (closed_ || !stream_) return false;
        return stream_->Write(packet);
    }
    
    void close() {
        std::lock_guard<std::mutex> lock(mutex_);
        closed_ = true;
        stream_ = nullptr;
    }

private:
    AudioStream* stream_;
    bool closed_;
    std::mutex mutex_;
};

class Room {
public:
  void addUser(UserId userId);
  void removeUser(UserId userId);
  std::set<UserId> getUsers() const;

  // Stream management
  void addAudioStream(UserId userId, AudioStream* stream);
  void removeAudioStream(UserId userId);
  void broadcastAudio(UserId senderId, const echomesh::VoicePacket& packet);

private:
  mutable std::mutex mutex_;
  std::set<UserId> users_;
  // Use shared_ptr to StreamWrapper for safe lifetime management
  std::unordered_map<UserId, std::shared_ptr<StreamWrapper>> audio_streams_;
};

class RoomManager {
public:
  static RoomManager &getInstance();

  bool createRoom(const RoomId &roomId);
  bool joinRoom(const RoomId &roomId, UserId userId);
  void leaveRoom(const RoomId &roomId, UserId userId);
  void userLogout(UserId userId); // Called when a user disconnects entirely
  std::shared_ptr<Room> getRoom(const RoomId &roomId);
  std::set<UserId> getUsersInRoom(const RoomId &roomId);

  // Interface for the service to manage audio streams
  void addAudioStream(const RoomId& roomId, UserId userId, AudioStream* stream);
  void removeAudioStream(const RoomId& roomId, UserId userId);
  void broadcastAudio(const RoomId& roomId, UserId senderId, const echomesh::VoicePacket& packet);


private:
  RoomManager() = default;
  ~RoomManager() = default;
  RoomManager(const RoomManager &) = delete;
  RoomManager &operator=(const RoomManager &) = delete;

  bool createRoom_nl(const RoomId &roomId);

  mutable std::mutex mutex_;
  std::unordered_map<RoomId, std::shared_ptr<Room>> rooms_;
  // A reverse map to find which room a user is in, useful for logout
  std::unordered_map<UserId, RoomId> user_to_room_map_;
};
