#include "UserManager.h"
#include "RoomManager.h"

UserManager &UserManager::getInstance() {
  static UserManager instance;
  return instance;
}

UserId UserManager::login(const std::string &username,
                        const ConnectionPtr &conn) {
  std::lock_guard<std::mutex> lock(mutex_);
  UserId userId = next_user_id_++;
  online_users_[userId] = conn;
  connections_[conn] = userId;
  // In a real application, you'd check username/password against a database
  return userId;
}

void UserManager::logout(UserId userId) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = online_users_.find(userId);
  if (it != online_users_.end()) {
    connections_.erase(it->second);
    online_users_.erase(it);
  }
  // Also remove user from any room they might be in
  RoomManager::getInstance().userLogout(userId);
  leaveRoom(userId);
}

UserManager::ConnectionPtr UserManager::getConnection(UserId userId) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = online_users_.find(userId);
  if (it != online_users_.end()) {
    return it->second;
  }
  return nullptr;
}

UserId UserManager::getUserId(const ConnectionPtr &conn) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = connections_.find(conn);
  if (it != connections_.end()) {
    return it->second;
  }
  return 0; // Invalid user id
}

void UserManager::joinRoom(UserId userId, const RoomId& roomId) {
    std::lock_guard<std::mutex> lock(mutex_);
    user_to_room_[userId] = roomId;
}

void UserManager::leaveRoom(UserId userId) {
    std::lock_guard<std::mutex> lock(mutex_);
    user_to_room_.erase(userId);
}

RoomId UserManager::getRoomId(UserId userId) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = user_to_room_.find(userId);
    if (it != user_to_room_.end()) {
        return it->second;
    }
    return ""; // Return empty string if not in a room
}
