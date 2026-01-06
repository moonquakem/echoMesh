#include "TcpServer.h"
#include "Acceptor.h"
#include "EventLoop.h"
#include "TcpConnection.h"
#include "ThreadPool.h"
#include "MsgDispatcher.h" 
#include <cassert>
#include <functional>
#include <iostream>

namespace {
void defaultConnectionCallback(const TcpConnectionPtr &conn) {
  if (conn->connected()) {
    std::cout << "New connection " << conn->name() << " from "
              << conn->peerAddress().sin_addr.s_addr << std::endl;
  } else {
    std::cout << "Connection " << conn->name() << " is down." << std::endl;
  }
}

void defaultMessageCallback(const TcpConnectionPtr &conn, Buffer *buffer) {
    while (buffer->readableBytes() >= 4) {
        int32_t len = buffer->peekInt32();
        if (len > 65536 || len < 0) {
            conn->shutdown();
            break;
        } else if (buffer->readableBytes() >= static_cast<size_t>(len) + 4) {
            buffer->retrieve(4);
            std::string msg_data(buffer->peek(), len);
            buffer->retrieve(len);

            echomesh::EchoMsg msg;
            if (msg.ParseFromString(msg_data)) {
                MsgDispatcher::getInstance().dispatch(conn, msg);
            } else {
                conn->shutdown();
                break;
            }
        } else {
            break;
        }
    }
}
}

TcpServer::TcpServer(EventLoop *loop, uint16_t port, int threadNum)
    : loop_(loop),
      acceptor_(std::make_unique<Acceptor>(loop, port, true)),
      threadPool_(std::make_shared<ThreadPool>(threadNum)), started_(false),
      nextConnId_(1),
      connectionCallback_(defaultConnectionCallback),
      messageCallback_(defaultMessageCallback) {
  acceptor_->setNewConnectionCallback(
      [this](int sockfd, const sockaddr_in &peerAddr) {
        this->newConnection(sockfd, peerAddr);
      });
}

TcpServer::~TcpServer() {
  loop_->assertInLoopThread();
  for (auto &item : connections_) {
    TcpConnectionPtr conn(item.second);
    item.second.reset();
    conn->getLoop()->runInLoop([conn] { conn->connectDestroyed(); });
  }
}

void TcpServer::start() {
  if (!started_) {
    started_ = true;
    threadPool_->start();
    loop_->runInLoop([this] { acceptor_->listen(); });
  }
}

void TcpServer::newConnection(int sockfd, const sockaddr_in &peerAddr) {
  loop_->assertInLoopThread();
  std::string connName = std::to_string(nextConnId_);
  ++nextConnId_;

  EventLoop *ioLoop = threadPool_->getNextLoop();

  sockaddr_in localAddr;
  socklen_t addrlen = sizeof(localAddr);
  if (::getsockname(sockfd, (struct sockaddr *)&localAddr, &addrlen) < 0) {
    // Log error
  }

  TcpConnectionPtr conn =
      std::make_shared<TcpConnection>(ioLoop, connName, sockfd, localAddr, peerAddr);
  connections_[connName] = conn;
  conn->setConnectionCallback(connectionCallback_);
  conn->setMessageCallback(messageCallback_);
  conn->setCloseCallback(
      [this](const TcpConnectionPtr &c) { this->removeConnection(c); });

  ioLoop->runInLoop([conn] { conn->connectEstablished(); });
}

void TcpServer::removeConnection(const TcpConnectionPtr &conn) {
  loop_->runInLoop([this, conn] { this->removeConnectionInLoop(conn); });
}

void TcpServer::removeConnectionInLoop(const TcpConnectionPtr &conn) {
  loop_->assertInLoopThread();
  size_t n = connections_.erase(conn->name());
  (void)n;
  assert(n == 1);

  EventLoop *ioLoop = conn->getLoop();
  ioLoop->queueInLoop([conn] { conn->connectDestroyed(); });
}