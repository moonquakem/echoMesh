#include "MsgDispatcher.h"
#include <iostream>

MsgDispatcher &MsgDispatcher::getInstance() {
  static MsgDispatcher instance;
  return instance;
}

void MsgDispatcher::registerHandler(echomesh::MsgType type, Handler handler) {
  std::lock_guard<std::mutex> lock(mutex_);
  handlers_[type] = handler;
}

void MsgDispatcher::dispatch(const ConnectionPtr &conn,
                           const echomesh::EchoMsg &msg) {
  Handler handler = nullptr;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = handlers_.find(msg.type());
    if (it != handlers_.end()) {
      handler = it->second;
    }
  }

  if (handler) {
    handler(conn, msg);
  } else {
    std::cerr << "No handler for message type: " << msg.type() << std::endl;
  }
}
