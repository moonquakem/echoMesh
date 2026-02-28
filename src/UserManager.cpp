#include "UserManager.h"
#include "RoomManager.h"

UserManager &UserManager::getInstance() {
  static UserManager instance;
  return instance;
}

UserId UserManager::login(const std::string &username, const Token &token) {
  std::lock_guard<std::mutex> lock(mutex_);
  
  // In a real app, you might check if the username is already taken.
  // For this project, we allow multiple logins with the same username.
  
  UserId userId = next_user_id_++;
  
  User newUser;
  newUser.id = userId;
  newUser.username = username;
  newUser.token = token;
  
  users_[userId] = newUser;
  token_to_user_[token] = userId;
  
  return userId;
}

void UserManager::logout(UserId userId) {
  std::lock_guard<std::mutex> lock(mutex_);
  
  auto it = users_.find(userId);
  if (it != users_.end()) {
    // Remove token mapping first
    token_to_user_.erase(it->second.token);
    // Then remove user object
    users_.erase(it);

    // Also remove user from any room they might be in.
    // This maintains the logic from the old implementation.
    RoomManager::getInstance().userLogout(userId);
  }
}

UserId UserManager::getUserIdByToken(const Token &token) {
  std::lock_guard<std::mutex> lock(mutex_);
  
  auto it = token_to_user_.find(token);
  if (it != token_to_user_.end()) {
    return it->second;
  }
  
  return 0; // Invalid user id
}

void UserManager::joinRoom(UserId userId, const RoomId& roomId) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = users_.find(userId);
    if (it != users_.end()) {
        it->second.current_room = roomId;
    }
}

void UserManager::leaveRoom(UserId userId) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = users_.find(userId);
    if (it != users_.end()) {
        it->second.current_room.clear();
    }
}

RoomId UserManager::getRoomId(UserId userId) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = users_.find(userId);
    if (it != users_.end()) {
        return it->second.current_room;
    }
    return ""; // Return empty string if user not found or not in a room
}
