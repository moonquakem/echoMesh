#pragma once

#include "TcpConnection.h"
#include <memory>
#include <functional>
#include <string>
#include <netinet/in.h>
#include <map>

class EventLoop;
class Acceptor;
class ThreadPool;

class TcpServer {
public:
    using ConnectionCallback = std::function<void(const TcpConnectionPtr&)>;

    TcpServer(EventLoop* loop, uint16_t port, int threadNum);
    ~TcpServer();

    void start();

    void setConnectionCallback(const ConnectionCallback& cb) { connectionCallback_ = cb; }

private:
    void newConnection(int sockfd, const sockaddr_in& peerAddr);
    void removeConnection(const TcpConnectionPtr& conn);
    void removeConnectionInLoop(const TcpConnectionPtr& conn);

    using ConnectionMap = std::map<std::string, TcpConnectionPtr>;

    EventLoop* loop_; // The main event loop
    std::unique_ptr<Acceptor> acceptor_;
    std::shared_ptr<ThreadPool> threadPool_;

    ConnectionCallback connectionCallback_;
    MessageCallback messageCallback_;

    bool started_;
    int nextConnId_;
    ConnectionMap connections_;
};
