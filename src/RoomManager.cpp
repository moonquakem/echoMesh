#include "RoomManager.h"
#include "UserManager.h"

// --- Room Implementation ---

void Room::addUser(UserId userId) {
  std::lock_guard<std::mutex> lock(mutex_);
  users_.insert(userId);
}

void Room::removeUser(UserId userId) {
  std::lock_guard<std::mutex> lock(mutex_);
  users_.erase(userId);
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
    return false; // Room not found
  }
  it->second->addUser(userId);
  return true;
}

void RoomManager::leaveRoom(const RoomId &roomId, UserId userId) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = rooms_.find(roomId);
  if (it != rooms_.end()) {
    it->second->removeUser(userId);
  }
}

void RoomManager::userLogout(UserId userId) {
  std::lock_guard<std::mutex> lock(mutex_);
  for (auto &pair : rooms_) {
    pair.second->removeUser(userId);
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
