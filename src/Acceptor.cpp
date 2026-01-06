#include "Acceptor.h"
#include "EventLoop.h"
#include <cerrno>
#include <fcntl.h>
#include <iostream>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {
int createNonblocking() {
  int sockfd =
      ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, IPPROTO_TCP);
  if (sockfd < 0) {
    // Log fatal error
    abort();
  }
  return sockfd;
}
} // namespace

Acceptor::Acceptor(EventLoop *loop, uint16_t port, bool reuseport)
    : loop_(loop), acceptSocket_(createNonblocking()),
      acceptChannel_(loop, acceptSocket_), listening_(false),
      idleFd_(::open("/dev/null", O_RDONLY | O_CLOEXEC)) {
  int optval = 1;
  ::setsockopt(acceptSocket_, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
  if (reuseport) {
    ::setsockopt(acceptSocket_, SOL_SOCKET, SO_REUSEPORT, &optval,
                 sizeof(optval));
  }

  sockaddr_in listenAddr;
  listenAddr.sin_family = AF_INET;
  listenAddr.sin_addr.s_addr = INADDR_ANY;
  listenAddr.sin_port = htons(port);

  if (::bind(acceptSocket_, (struct sockaddr *)&listenAddr,
             sizeof(listenAddr)) < 0) {
    // Log fatal error
    ::close(acceptSocket_);
    ::close(idleFd_);
    abort();
  }

  acceptChannel_.setReadCallback([this] { this->handleRead(); });
}

Acceptor::~Acceptor() {
  acceptChannel_.disableAll();
  acceptChannel_.remove();
  ::close(acceptSocket_);
  ::close(idleFd_);
}

void Acceptor::listen() {
  loop_->assertInLoopThread();
  listening_ = true;
  if (::listen(acceptSocket_, SOMAXCONN) < 0) {
    // Log fatal error
    abort();
  }
  acceptChannel_.enableReading();
}

void Acceptor::handleRead() {
  loop_->assertInLoopThread();
  sockaddr_in peerAddr;
  socklen_t addrLen = sizeof(peerAddr);

  while (true) {
    int connfd = ::accept4(acceptSocket_, (struct sockaddr *)&peerAddr, &addrLen,
                           SOCK_NONBLOCK | SOCK_CLOEXEC);
    if (connfd >= 0) {
      if (newConnectionCallback_) {
        newConnectionCallback_(connfd, peerAddr);
      } else {
        ::close(connfd);
      }
    } else {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        break;
      } else if (errno == EMFILE) {
        ::close(idleFd_);
        idleFd_ = ::accept(acceptSocket_, NULL, NULL);
        ::close(idleFd_);
        idleFd_ = ::open("/dev/null", O_RDONLY | O_CLOEXEC);
      } else {
        // Log error
        break;
      }
    }
  }
}