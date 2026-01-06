#pragma once

#include "TcpConnection.h"
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

using UserId = int64_t;

class UserManager {
public:
  using ConnectionPtr = std::shared_ptr<TcpConnection>;

  static UserManager &getInstance();

  UserId login(const std::string &username, const ConnectionPtr &conn);
  void logout(UserId userId);
  ConnectionPtr getConnection(UserId userId);
  UserId getUserId(const ConnectionPtr &conn);

private:
  UserManager() = default;
  ~UserManager() = default;
  UserManager(const UserManager &) = delete;
  UserManager &operator=(const UserManager &) = delete;

  std::mutex mutex_;
  std::unordered_map<UserId, ConnectionPtr> online_users_;
  std::unordered_map<ConnectionPtr, UserId> connections_;
  UserId next_user_id_ = 1;
};
