#include "audio/UdpSender.h"
#include <arpa/inet.h>
#include <unistd.h>
#include <stdexcept>
#include <cstring> // For memset

UdpSender::UdpSender(const std::string& host, int port) {
    sockfd_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd_ < 0) {
        throw std::runtime_error("Failed to create UDP socket");
    }

    memset(&servaddr_, 0, sizeof(servaddr_));

    servaddr_.sin_family = AF_INET;
    servaddr_.sin_port = htons(port);
    
    if (inet_pton(AF_INET, host.c_str(), &servaddr_.sin_addr) <= 0) {
        throw std::runtime_error("Invalid address/ Address not supported");
    }
}

UdpSender::~UdpSender() {
    if (sockfd_ >= 0) {
        close(sockfd_);
    }
}

void UdpSender::send(const VoicePacket& packet) {
    std::vector<uint8_t> buffer = packet.serialize();
    sendto(sockfd_, buffer.data(), buffer.size(), 0, (const struct sockaddr *)&servaddr_, sizeof(servaddr_));
}
