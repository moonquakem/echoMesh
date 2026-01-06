#pragma once

#include "message.pb.h"
#include "TcpConnection.h"
#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>

class MsgDispatcher {
public:
  using ConnectionPtr = std::shared_ptr<TcpConnection>;
  using Handler =
      std::function<void(const ConnectionPtr &, const echomesh::EchoMsg &)>;

  static MsgDispatcher &getInstance();

  void registerHandler(echomesh::MsgType type, Handler handler);
  void dispatch(const ConnectionPtr &conn, const echomesh::EchoMsg &msg);

private:
  MsgDispatcher() = default;
  ~MsgDispatcher() = default;
  MsgDispatcher(const MsgDispatcher &) = delete;
  MsgDispatcher &operator=(const MsgDispatcher &) = delete;

  std::unordered_map<echomesh::MsgType, Handler> handlers_;
  std::mutex mutex_;
};
