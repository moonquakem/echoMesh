#pragma once

#include "audio/VoicePacket.h"
#include <string>
#include <vector>
#include <cstdint>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

class UdpSender {
public:
    UdpSender(const std::string& host, int port);
    ~UdpSender();

    void send(const VoicePacket& packet);

private:
    int sockfd_;
    struct sockaddr_in servaddr_;
};
