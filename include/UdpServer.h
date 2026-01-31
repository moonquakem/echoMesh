#pragma once

#include "EventLoop.h"
#include "Channel.h"
#include <netinet/in.h>
#include <memory>

class UdpServer {
public:
    UdpServer(EventLoop* loop, uint16_t port);
    ~UdpServer();

    void start();
    void send(const void* data, size_t len, const sockaddr_in& addr);

private:
    void handleRead();

    EventLoop* loop_;
    int sockfd_;
    std::unique_ptr<Channel> channel_;
};
