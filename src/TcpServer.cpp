#include "TcpServer.h"
#include "Acceptor.h"
#include "EventLoop.h"
#include "TcpConnection.h"
#include "ThreadPool.h"
#include <functional>
#include <iostream>
#include <cassert>

TcpServer::TcpServer(EventLoop* loop, const sockaddr_in& listenAddr, int threadNum)
    : loop_(loop),
      acceptor_(std::make_unique<Acceptor>(loop, listenAddr)),
      threadPool_(std::make_shared<ThreadPool>(threadNum)),
      started_(false),
      nextConnId_(1) {
    acceptor_->setNewConnectionCallback(
        [this](int sockfd, const sockaddr_in& peerAddr) {
            this->newConnection(sockfd, peerAddr);
        });
}

TcpServer::~TcpServer() {
    loop_->assertInLoopThread();
    for (auto& item : connections_) {
        TcpConnectionPtr conn(item.second);
        item.second.reset();
        conn->getLoop()->runInLoop([conn] { conn->connectDestroyed(); });
    }
}

void TcpServer::start() {
    if (!started_) {
        started_ = true;
        // In a multi-loop version, you would start the thread pool
        // and create EventLoops in other threads here.
        
        loop_->runInLoop([this] {
            acceptor_->listen();
        });
    }
}

void TcpServer::newConnection(int sockfd, const sockaddr_in& peerAddr) {
    loop_->assertInLoopThread();
    std::string connName = std::to_string(nextConnId_);
    ++nextConnId_;
    
    // In a multi-loop version, you would choose an I/O loop from the pool
    EventLoop* ioLoop = loop_;

    sockaddr_in localAddr;
    socklen_t addrlen = sizeof(localAddr);
    if (::getsockname(sockfd, (struct sockaddr*)&localAddr, &addrlen) < 0) {
        // Log error
    }

    TcpConnectionPtr conn = std::make_shared<TcpConnection>(ioLoop, connName, sockfd, localAddr, peerAddr);
    connections_[connName] = conn;
    conn->setConnectionCallback(connectionCallback_);
    conn->setMessageCallback(messageCallback_);
    conn->setCloseCallback([this](const TcpConnectionPtr& c) { this->removeConnection(c); });
    
    // This should be called in the I/O loop's thread.
    // In a single-loop setup, this is fine.
    ioLoop->runInLoop([conn] { conn->connectEstablished(); });
}

void TcpServer::removeConnection(const TcpConnectionPtr& conn) {
    loop_->runInLoop([this, conn] { this->removeConnectionInLoop(conn); });
}

void TcpServer::removeConnectionInLoop(const TcpConnectionPtr& conn) {
    loop_->assertInLoopThread();
    size_t n = connections_.erase(conn->name());
    (void)n;
    assert(n == 1);
    
    // This should be called in the I/O loop's thread.
    EventLoop* ioLoop = conn->getLoop();
    ioLoop->queueInLoop([conn] { conn->connectDestroyed(); });
}