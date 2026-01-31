#include "RoomManager.h"
#include "UserManager.h"
#include <cstring> // For memset

// --- Room Implementation ---

void Room::addUser(UserId userId) {
  std::lock_guard<std::mutex> lock(mutex_);
  users_.insert(userId);
}

void Room::removeUser(UserId userId) {
  std::lock_guard<std::mutex> lock(mutex_);
  users_.erase(userId);
  udp_addresses_.erase(userId); // Also remove address mapping
}

std::set<UserId> Room::getUsers() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return users_;
}

void Room::broadcast(const echomesh::EchoMsg &msg) {
  std::string serialized_msg;
  msg.SerializeToString(&serialized_msg);

  std::set<UserId> users_copy;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    users_copy = users_;
  }

  auto &userManager = UserManager::getInstance();
  for (UserId userId : users_copy) {
    auto conn = userManager.getConnection(userId);
    if (conn && conn->connected()) {
      conn->send(msg);
    }
  }
}

void Room::updateUserAddress(UserId userId, const sockaddr_in& addr) {
    std::lock_guard<std::mutex> lock(mutex_);
    udp_addresses_[userId] = addr;
}

std::optional<sockaddr_in> Room::getUserAddress(UserId userId) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = udp_addresses_.find(userId);
    if (it != udp_addresses_.end()) {
        return it->second;
    }
    return std::nullopt;
}


// --- RoomManager Implementation ---

RoomManager &RoomManager::getInstance() {
  static RoomManager instance;
  return instance;
}

bool RoomManager::createRoom(const RoomId &roomId) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (rooms_.count(roomId)) {
    return false; // Room already exists
  }
  rooms_[roomId] = std::make_shared<Room>();
  return true;
}

bool RoomManager::joinRoom(const RoomId &roomId, UserId userId) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = rooms_.find(roomId);
  if (it == rooms_.end()) {
    // Room not found, let's create it
    if (!createRoom(roomId)) return false;
    it = rooms_.find(roomId);
  }
  it->second->addUser(userId);
  UserManager::getInstance().joinRoom(userId, roomId);
  return true;
}

void RoomManager::leaveRoom(const RoomId &roomId, UserId userId) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = rooms_.find(roomId);
  if (it != rooms_.end()) {
    it->second->removeUser(userId);
  }
  UserManager::getInstance().leaveRoom(userId);
}

void RoomManager::userLogout(UserId userId) {
    auto& userMgr = UserManager::getInstance();
    RoomId roomId = userMgr.getRoomId(userId);
    if (!roomId.empty()) {
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

void RoomManager::updateUserAddress(const RoomId& roomId, UserId userId, const sockaddr_in& addr) {
    auto room = getRoom(roomId);
    if (room) {
        room->updateUserAddress(userId, addr);
    }
}

std::optional<sockaddr_in> RoomManager::getUserAddress(const RoomId& roomId, UserId userId) {
    auto room = getRoom(roomId);
    if (room) {
        return room->getUserAddress(userId);
    }
    return std::nullopt;
}
