#include "Acceptor.h"
#include "EventLoop.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#include <cerrno>
#include <iostream>

namespace {
    int createNonblocking() {
        int sockfd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, IPPROTO_TCP);
        if (sockfd < 0) {
            // Log error
        }
        return sockfd;
    }
}

Acceptor::Acceptor(EventLoop* loop, const sockaddr_in& listenAddr)
    : loop_(loop),
      acceptSocket_(createNonblocking()),
      acceptChannel_(loop, acceptSocket_),
      listening_(false),
      idleFd_(::open("/dev/null", O_RDONLY | O_CLOEXEC)) {
    
    int optval = 1;
    ::setsockopt(acceptSocket_, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
    ::setsockopt(acceptSocket_, SOL_SOCKET, SO_REUSEPORT, &optval, sizeof(optval));

    if (::bind(acceptSocket_, (struct sockaddr*)&listenAddr, sizeof(listenAddr)) < 0) {
        // Log error
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
        // Log error
        abort();
    }
    acceptChannel_.enableReading();
}

void Acceptor::handleRead() {
    loop_->assertInLoopThread();
    sockaddr_in peerAddr;
    socklen_t addrLen = sizeof(peerAddr);
    
    // ET mode requires us to accept all pending connections
    while (true) {
        int connfd = ::accept4(acceptSocket_, (struct sockaddr*)&peerAddr, &addrLen, SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (connfd >= 0) {
            if (newConnectionCallback_) {
                newConnectionCallback_(connfd, peerAddr);
            } else {
                ::close(connfd);
            }
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // All pending connections accepted
                break;
            } else if (errno == EMFILE) {
                // Ran out of file descriptors.
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