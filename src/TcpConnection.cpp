#include "TcpConnection.h"
#include "EventLoop.h"
#include "MsgDispatcher.h"
#include "RoomManager.h"
#include "UserManager.h"
#include "message.pb.h"
#include <arpa/inet.h>
#include <cassert>
#include <cerrno>
#include <iostream>
#include <unistd.h>

void defaultConnectionCallback(const TcpConnectionPtr &conn) {
  // Log connection status
}

TcpConnection::TcpConnection(EventLoop *loop, std::string name, int sockfd,
                           const sockaddr_in &localAddr,
                           const sockaddr_in &peerAddr)
    : loop_(loop), name_(std::move(name)), state_(kConnecting), sockfd_(sockfd),
      channel_(std::make_unique<Channel>(loop, sockfd)), localAddr_(localAddr),
      peerAddr_(peerAddr) {
  channel_->setReadCallback([this] { this->handleRead(); });
  channel_->setWriteCallback([this] { this->handleWrite(); });
  channel_->setErrorCallback([this] { this->handleError(); });
}

TcpConnection::~TcpConnection() { assert(state_ == kDisconnected); }

void TcpConnection::send(const std::string &message) {
  if (state_ == kConnected) {
    if (loop_->isInLoopThread()) {
      sendInLoop(message);
    } else {
      loop_->runInLoop([ptr = shared_from_this(), message] {
        ptr->sendInLoop(message);
      });
    }
  }
}

void TcpConnection::send(const echomesh::EchoMsg &msg) {
  std::string serialized_msg;
  msg.SerializeToString(&serialized_msg);

  int32_t len = static_cast<int32_t>(serialized_msg.length());
  int32_t be32 = htonl(len);

  std::string packet;
  packet.append(reinterpret_cast<char *>(&be32), sizeof(be32));
  packet.append(serialized_msg);

  send(packet);
}

void TcpConnection::sendInLoop(const std::string &message) {
  loop_->assertInLoopThread();
  ssize_t nwrote = 0;
  size_t remaining = message.size();
  bool faultError = false;

  if (state_ == kDisconnected) {
    // Log warning
    return;
  }

  if (!channel_->isWriting() && outputBuffer_.readableBytes() == 0) {
    nwrote = ::write(channel_->fd(), message.data(), message.size());
    if (nwrote >= 0) {
      remaining = message.size() - nwrote;
      if (remaining == 0 && writeCompleteCallback_) {
        loop_->queueInLoop([ptr = shared_from_this()] {
          ptr->writeCompleteCallback_(ptr);
        });
      }
    } else {
      nwrote = 0;
      if (errno != EWOULDBLOCK) {
        if (errno == EPIPE || errno == ECONNRESET) {
          faultError = true;
        }
      }
    }
  }

  if (!faultError && remaining > 0) {
    outputBuffer_.append(message.data() + nwrote, remaining);
    if (!channel_->isWriting()) {
      channel_->enableWriting();
    }
  }
}

void TcpConnection::shutdown() {
  if (state_ == kConnected) {
    setState(kDisconnecting);
    loop_->runInLoop([ptr = shared_from_this()] { ptr->shutdownInLoop(); });
  }
}

void TcpConnection::shutdownInLoop() {
  loop_->assertInLoopThread();
  if (!channel_->isWriting()) {
    if (::shutdown(sockfd_, SHUT_WR) < 0) {
      // Log error
    }
  }
}

void TcpConnection::connectEstablished() {
  loop_->assertInLoopThread();
  assert(state_ == kConnecting);
  setState(kConnected);
  channel_->enableReading();
  connectionCallback_(shared_from_this());
}

void TcpConnection::connectDestroyed() {
  loop_->assertInLoopThread();
  if (state_ == kConnected) {
    setState(kDisconnected);
    channel_->disableAll();
    connectionCallback_(shared_from_this());
  }
  channel_->remove();
}

void TcpConnection::handleRead() {
  loop_->assertInLoopThread();
  int savedErrno = 0;
  ssize_t n = inputBuffer_.readFd(channel_->fd(), &savedErrno);
  if (n > 0) {
    while (inputBuffer_.readableBytes() >= 4) {
      int32_t len = inputBuffer_.peekInt32();
      if (len > 65536 || len < 0) {
        // Invalid length, close connection
        handleClose();
        return;
      } else if (inputBuffer_.readableBytes() >=
                 static_cast<size_t>(len) + 4) {
        inputBuffer_.retrieve(4);
        std::string msg_data(inputBuffer_.peek(), len);
        inputBuffer_.retrieve(len);

        echomesh::EchoMsg msg;
        if (msg.ParseFromString(msg_data)) {
          // Get the dispatcher and dispatch the message
          MsgDispatcher::getInstance().dispatch(shared_from_this(), msg);
        } else {
          // Parse error, close connection
          handleClose();
          return;
        }
      } else {
        // Not enough data for a full message, wait for more
        break;
      }
    }
  } else if (n == 0) {
    handleClose();
  } else {
    errno = savedErrno;
    // Log error
    handleError();
  }
}

void TcpConnection::handleWrite() {
  loop_->assertInLoopThread();
  if (channel_->isWriting()) {
    ssize_t n = ::write(channel_->fd(), outputBuffer_.peek(),
                        outputBuffer_.readableBytes());
    if (n > 0) {
      outputBuffer_.retrieve(n);
      if (outputBuffer_.readableBytes() == 0) {
        channel_->disableWriting();
        if (writeCompleteCallback_) {
          loop_->queueInLoop(
              [ptr = shared_from_this()] { ptr->writeCompleteCallback_(ptr); });
        }
        if (state_ == kDisconnecting) {
          shutdownInLoop();
        }
      }
    } else {
      // Log error
    }
  } else {
    // Log trace: Connection is down, no more writing
  }
}

void TcpConnection::handleClose() {
  loop_->assertInLoopThread();
  assert(state_ == kConnected || state_ == kDisconnecting);
  setState(kDisconnected);
  channel_->disableAll();

  TcpConnectionPtr guardThis(shared_from_this());

  auto &userManager = UserManager::getInstance();
  UserId userId = userManager.getUserId(guardThis);
  if (userId != 0) {
    userManager.logout(userId);
    RoomManager::getInstance().userLogout(userId);
    std::cout << "User " << userId << " logged out." << std::endl;
  }

  connectionCallback_(guardThis);
  closeCallback_(guardThis);
}

void TcpConnection::handleError() {
  int err;
  socklen_t len = sizeof(err);
  if (::getsockopt(channel_->fd(), SOL_SOCKET, SO_ERROR, &err, &len) < 0) {
    err = errno;
  }
  // Log error
}