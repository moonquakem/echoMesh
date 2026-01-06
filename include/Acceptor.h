#pragma once

#include "Channel.h"
#include <functional>
#include <netinet/in.h>

class EventLoop;

class Acceptor {
public:
    using NewConnectionCallback = std::function<void(int sockfd, const sockaddr_in& peerAddr)>;

    Acceptor(EventLoop* loop, uint16_t port, bool reuseport);
    ~Acceptor();

    void setNewConnectionCallback(const NewConnectionCallback& cb) {
        newConnectionCallback_ = cb;
    }

    void listen();
    bool listening() const { return listening_; }

private:
    void handleRead();

    EventLoop* loop_;
    int acceptSocket_;
    Channel acceptChannel_;
    NewConnectionCallback newConnectionCallback_;
    bool listening_;
    int idleFd_; // To handle running out of file descriptors
};
