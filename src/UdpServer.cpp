#include "UdpServer.h"
#include "RoomManager.h"
#include "UserManager.h"
#include "audio/VoicePacket.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <unistd.h>
#include <iostream>
#include <vector>

namespace {
    // Utility function to set a socket to non-blocking mode
    bool setNonBlocking(int sockfd) {
        int flags = fcntl(sockfd, F_GETFL, 0);
        if (flags == -1) {
            perror("fcntl F_GETFL");
            return false;
        }
        if (fcntl(sockfd, F_SETFL, flags | O_NONBLOCK) == -1) {
            perror("fcntl F_SETFL O_NONBLOCK");
            return false;
        }
        return true;
    }
}

UdpServer::UdpServer(EventLoop* loop, uint16_t port)
    : loop_(loop),
      sockfd_(::socket(AF_INET, SOCK_DGRAM, 0)) {
    
    if (sockfd_ < 0) {
        throw std::runtime_error("Failed to create UDP socket");
    }

    setNonBlocking(sockfd_);

    sockaddr_in servaddr;
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(port);

    if (::bind(sockfd_, (const struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
        throw std::runtime_error("Failed to bind UDP socket");
    }

    channel_ = std::make_unique<Channel>(loop, sockfd_);
    channel_->setReadCallback([this] { this->handleRead(); });
}

UdpServer::~UdpServer() {
    ::close(sockfd_);
}

void UdpServer::start() {
    channel_->enableReading();
    // The following line is simplified because Channel doesn't expose the address directly
    std::cout << "UDP Server listening for voice packets..." << std::endl;
}

void UdpServer::send(const void* data, size_t len, const sockaddr_in& addr) {
    ::sendto(sockfd_, data, len, 0, (const struct sockaddr*)&addr, sizeof(addr));
}

void UdpServer::handleRead() {
    sockaddr_in cliaddr;
    socklen_t len = sizeof(cliaddr);
    std::vector<uint8_t> buffer(2048); // Max UDP packet size

    ssize_t n = ::recvfrom(sockfd_, buffer.data(), buffer.size(), 0, (struct sockaddr *)&cliaddr, &len);

    if (n > 0) {
        buffer.resize(n);
        
        // Deserialize to get user and room info
        VoicePacket packet = VoicePacket::deserialize(buffer);
        uint32_t userId = packet.userId;

        auto& userMgr = UserManager::getInstance();
        std::string roomId = userMgr.getRoomId(userId);

        if (roomId.empty()) {
            // User is not in a room, or invalid user ID
            return;
        }

        auto& roomMgr = RoomManager::getInstance();
        
        // IMPORTANT: Update the user's UDP address. This is how the server learns it.
        roomMgr.updateUserAddress(roomId, userId, cliaddr);

        // Get all users in the room
        auto users = roomMgr.getUsersInRoom(roomId);
        
        // Forward the packet to all *other* users in the room
        for (const auto& user : users) {
            if (user != userId) {
                auto userAddr = roomMgr.getUserAddress(roomId, user);
                if (userAddr) {
                    send(buffer.data(), buffer.size(), *userAddr);
                }
            }
        }
    }
}
