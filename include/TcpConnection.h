#pragma once

#include "Buffer.h"
#include "Channel.h"
#include <memory>
#include <functional>
#include <string>
#include <netinet/in.h>

class EventLoop;
class TcpConnection;

using TcpConnectionPtr = std::shared_ptr<TcpConnection>;
using ConnectionCallback = std::function<void(const TcpConnectionPtr&)>;
using MessageCallback = std::function<void(const TcpConnectionPtr&, Buffer*)>;
using CloseCallback = std::function<void(const TcpConnectionPtr&)>;
using WriteCompleteCallback = std::function<void(const TcpConnectionPtr&)>;

class TcpConnection : public std::enable_shared_from_this<TcpConnection> {
public:
    TcpConnection(EventLoop* loop, std::string name, int sockfd, const sockaddr_in& localAddr, const sockaddr_in& peerAddr);
    ~TcpConnection();

    void setConnectionCallback(const ConnectionCallback& cb) { connectionCallback_ = cb; }
    void setMessageCallback(const MessageCallback& cb) { messageCallback_ = cb; }
    void setCloseCallback(const CloseCallback& cb) { closeCallback_ = cb; }
    void setWriteCompleteCallback(const WriteCompleteCallback& cb) { writeCompleteCallback_ = cb; }

    void connectEstablished();
    void connectDestroyed();

    void send(const std::string& message);
    void shutdown();

    EventLoop* getLoop() const { return loop_; }
    const std::string& name() const { return name_; }
    bool connected() const { return state_ == kConnected; }

private:
    void handleRead();
    void handleWrite();
    void handleClose();
    void handleError();

    void sendInLoop(const std::string& message);
    void shutdownInLoop();

    enum StateE { kConnecting, kConnected, kDisconnecting, kDisconnected };
    void setState(StateE s) { state_ = s; }

    EventLoop* loop_;
    std::string name_;
    StateE state_;
    int sockfd_;
    std::unique_ptr<Channel> channel_;
    sockaddr_in localAddr_;
    sockaddr_in peerAddr_;

    ConnectionCallback connectionCallback_;
    MessageCallback messageCallback_;
    CloseCallback closeCallback_;
    WriteCompleteCallback writeCompleteCallback_;

    Buffer inputBuffer_;
    Buffer outputBuffer_;
};
