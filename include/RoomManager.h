#pragma once

#include "TcpConnection.h"
#include "message.pb.h"
#include <cstdint>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>

using UserId = int64_t;
using RoomId = std::string;

class Room {
public:
  void addUser(UserId userId);
  void removeUser(UserId userId);
  std::set<UserId> getUsers() const;
  void broadcast(const echomesh::EchoMsg &msg);

private:
  mutable std::mutex mutex_;
  std::set<UserId> users_;
};

class RoomManager {
public:
  static RoomManager &getInstance();

  bool createRoom(const RoomId &roomId);
  bool joinRoom(const RoomId &roomId, UserId userId);
  void leaveRoom(const RoomId &roomId, UserId userId);
  void userLogout(UserId userId);
  std::shared_ptr<Room> getRoom(const RoomId &roomId);

private:
  RoomManager() = default;
  ~RoomManager() = default;
  RoomManager(const RoomManager &) = delete;
  RoomManager &operator=(const RoomManager &) = delete;

  std::mutex mutex_;
  std::unordered_map<RoomId, std::shared_ptr<Room>> rooms_;
};
