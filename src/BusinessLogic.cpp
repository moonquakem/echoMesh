#include "BusinessLogic.h"
#include "MsgDispatcher.h"
#include "RoomManager.h"
#include "UserManager.h"
#include <iostream>

using ConnectionPtr = std::shared_ptr<TcpConnection>;

void sendRoomActionResponse(const ConnectionPtr &conn, echomesh::StatusCode code, const std::string& message) {
    echomesh::EchoMsg response_msg;
    response_msg.set_type(echomesh::MT_ROOM_ACTION_RESPONSE);
    auto *res = response_msg.mutable_room_action_response();
    res->set_status_code(code);
    res->set_message(message);
    conn->send(response_msg);
}

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
    sendRoomActionResponse(conn, echomesh::SC_ERROR, "Not logged in");
    return;
  }

  switch (action.action_type()) {
  case echomesh::RA_CREATE: // CREATE is now handled by JOIN
  case echomesh::RA_JOIN:
    if (roomManager.joinRoom(action.room_id(), userId)) {
      std::cout << "User " << userId << " joined room " << action.room_id()
                << std::endl;
      sendRoomActionResponse(conn, echomesh::SC_OK, "Joined room successfully");
    } else {
      std::cout << "User " << userId << " failed to join room " << action.room_id()
                << std::endl;
      sendRoomActionResponse(conn, echomesh::SC_ROOM_NOT_FOUND, "Failed to join room");
    }
    break;
  case echomesh::RA_LEAVE:
    roomManager.leaveRoom(action.room_id(), userId);
    std::cout << "User " << userId << " left room " << action.room_id()
              << std::endl;
    sendRoomActionResponse(conn, echomesh::SC_OK, "Left room successfully");
    break;
  default:
    sendRoomActionResponse(conn, echomesh::SC_ERROR, "Unknown room action");
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
