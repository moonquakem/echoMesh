#include "RoomManager.h"
#include "UserManager.h"
#include <iostream>
#include <vector>

// --- Room Implementation ---

void Room::addUser(UserId userId) {
  std::lock_guard<std::mutex> lock(mutex_);
  users_.insert(userId);
}

void Room::removeUser(UserId userId) {
  std::lock_guard<std::mutex> lock(mutex_);
  users_.erase(userId);
  audio_streams_.erase(userId); // Also remove stream mapping
}

std::set<UserId> Room::getUsers() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return users_;
}

void Room::addAudioStream(UserId userId, AudioStream* stream) {
    std::lock_guard<std::mutex> lock(mutex_);
    audio_streams_[userId] = stream;
}

void Room::removeAudioStream(UserId userId) {
    std::lock_guard<std::mutex> lock(mutex_);
    audio_streams_.erase(userId);
}

void Room::broadcastAudio(UserId senderId, const echomesh::VoicePacket& packet) {
    //
    // IMPORTANT: The gRPC Write() operation can be blocking.
    // To prevent a single slow client from deadlocking the entire room,
    // we copy the list of streams under the lock, and then release the lock
    // before performing the writes.
    //
    std::vector<AudioStream*> streams_to_write;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& pair : audio_streams_) {
            if (pair.first != senderId) {
                streams_to_write.push_back(pair.second);
            }
        }
    }

    for (auto* stream : streams_to_write) {
        // This is a blocking write. A more advanced implementation might use
        // a work queue and a thread pool to handle writes asynchronously.
        stream->Write(packet);
    }
}


// --- RoomManager Implementation ---

RoomManager &RoomManager::getInstance() {
  static RoomManager instance;
  return instance;
}

bool RoomManager::createRoom_nl(const RoomId &roomId) {
    if (rooms_.count(roomId)) {
        return false; // Room already exists
    }
    rooms_[roomId] = std::make_shared<Room>();
    std::cout << "Room '" << roomId << "' created." << std::endl;
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
    // Room not found, let's create it
    if (!createRoom_nl(roomId)) return false;
    it = rooms_.find(roomId);
  }
  it->second->addUser(userId);
  user_to_room_map_[userId] = roomId; // Update reverse map
  // Note: We don't call UserManager::joinRoom anymore, that's handled in UserManager directly
  return true;
}

void RoomManager::leaveRoom(const RoomId &roomId, UserId userId) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = rooms_.find(roomId);
  if (it != rooms_.end()) {
    it->second->removeUser(userId);
    std::cout << "User " << userId << " removed from room '" << roomId << "'." << std::endl;
  }
  user_to_room_map_.erase(userId); // Update reverse map
}

void RoomManager::userLogout(UserId userId) {
    std::unique_lock<std::mutex> lock(mutex_);
    auto it = user_to_room_map_.find(userId);
    if (it != user_to_room_map_.end()) {
        RoomId roomId = it->second;
        lock.unlock(); // Unlock before calling leaveRoom which locks again
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
    if (room) {
        room->broadcastAudio(senderId, packet);
    }
}
