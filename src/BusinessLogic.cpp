#include "BusinessLogic.h"
#include "MsgDispatcher.h"
#include "RoomManager.h"
#include "UserManager.h"
#include <iostream>

using ConnectionPtr = std::shared_ptr<TcpConnection>;

void handleLoginRequest(const ConnectionPtr &conn,
                        const echomesh::EchoMsg &msg) {
  auto &userManager = UserManager::getInstance();
  const auto &req = msg.login_request();

  UserId userId = userManager.login(req.username(), conn);

  echomesh::EchoMsg response_msg;
  response_msg.set_type(echomesh::MT_LOGIN_RESPONSE);
  auto *res = response_msg.mutable_login_response();
  res->set_status_code(echomesh::SC_OK);
  res->set_user_id(userId);
  res->set_message("Login successful");

  conn->send(response_msg);
  std::cout << "User " << userId << " logged in." << std::endl;
}

void handleRoomAction(const ConnectionPtr &conn, const echomesh::EchoMsg &msg) {
  auto &userManager = UserManager::getInstance();
  auto &roomManager = RoomManager::getInstance();
  const auto &action = msg.room_action();

  UserId userId = userManager.getUserId(conn);
  if (userId == 0) {
    // Not logged in
    return;
  }

  switch (action.action_type()) {
  case echomesh::RA_CREATE:
    if (roomManager.createRoom(action.room_id())) {
      roomManager.joinRoom(action.room_id(), userId);
      std::cout << "User " << userId << " created and joined room "
                << action.room_id() << std::endl;
    } else {
      // Room exists, handle error
    }
    break;
  case echomesh::RA_JOIN:
    if (roomManager.joinRoom(action.room_id(), userId)) {
      std::cout << "User " << userId << " joined room " << action.room_id()
                << std::endl;
    } else {
      // Room not found, handle error
    }
    break;
  case echomesh::RA_LEAVE:
    roomManager.leaveRoom(action.room_id(), userId);
    std::cout << "User " << userId << " left room " << action.room_id()
              << std::endl;
    break;
  default:
    break;
  }
}

void handleChatMsg(const ConnectionPtr &conn, const echomesh::EchoMsg &msg) {
  auto &userManager = UserManager::getInstance();
  auto &roomManager = RoomManager::getInstance();
  const auto &chat = msg.chat_msg();

  UserId userId = userManager.getUserId(conn);
  if (userId == 0 || userId != chat.user_id()) {
    // Not logged in or message impersonation
    return;
  }

  auto room = roomManager.getRoom(chat.room_id());
  if (room) {
    room->broadcast(msg);
    std::cout << "User " << userId << " sent message in room "
              << chat.room_id() << std::endl;
  }
}

void registerBusinessLogicHandlers() {
  auto &dispatcher = MsgDispatcher::getInstance();
  dispatcher.registerHandler(echomesh::MT_LOGIN_REQUEST, handleLoginRequest);
  dispatcher.registerHandler(echomesh::MT_ROOM_ACTION, handleRoomAction);
  dispatcher.registerHandler(echomesh::MT_CHAT_MSG, handleChatMsg);
  // Register other handlers...
}
