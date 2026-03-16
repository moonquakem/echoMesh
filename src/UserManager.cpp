#include "UserManager.h"
#include "RoomManager.h"
#include "DatabaseManager.h"
#include <spdlog/spdlog.h>

UserManager &UserManager::getInstance() {
  static UserManager instance;
  return instance;
}

UserId UserManager::login(const std::string &username, const std::string &password, const Token &token) {
  std::lock_guard<std::mutex> lock(mutex_);
  
  auto& db = DatabaseManager::getInstance();
  auto user_opt = db.getUser(username);

  UserId actual_userId = 0;

  if (!user_opt) {
      // User not found, create one (auto-register)
      spdlog::info("User '{}' not found, creating new account", username);
      if (!db.createUser(username, password)) {
          spdlog::error("Failed to create user '{}' in database", username);
          return 0;
      }
      user_opt = db.getUser(username);
      if (!user_opt) return 0;
  }

  // Password verification (using simple comparison for now)
  if (user_opt->password_hash != password) {
      spdlog::warn("Invalid password for user '{}'", username);
      return 0;
  }

  actual_userId = user_opt->id;

  // Check if this specific session is already active
  if (token_to_user_.count(token)) {
      return token_to_user_[token];
  }

  User newUser;
  newUser.id = actual_userId;
  newUser.username = username;
  newUser.token = token;
  
  users_[actual_userId] = newUser;
  token_to_user_[token] = actual_userId;
  
  spdlog::info("User '{}' logged in with database ID {}", username, actual_userId);
  
  return actual_userId;
}

void UserManager::logout(UserId userId) {
  std::lock_guard<std::mutex> lock(mutex_);
  
  auto it = users_.find(userId);
  if (it != users_.end()) {
    std::string username = it->second.username;
    token_to_user_.erase(it->second.token);
    users_.erase(it);

    spdlog::info("User '{}' (ID {}) logged out", username, userId);
    RoomManager::getInstance().userLogout(userId);
  }
}

UserId UserManager::getUserIdByToken(const Token &token) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = token_to_user_.find(token);
  if (it != token_to_user_.end()) {
    return it->second;
  }
  return 0;
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
    return "";
}
