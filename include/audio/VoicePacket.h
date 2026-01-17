#pragma once

#include <cstdint>
#include <vector>
#include <arpa/inet.h> // For htonl and ntohl
#include <algorithm>   // For std::copy

// Define a structure for the voice packet
struct VoicePacket {
    uint32_t sequence;
    uint32_t timestamp;
    uint32_t userId;
    std::vector<uint8_t> data;

    // Function to serialize the packet into a byte stream for UDP transmission
    std::vector<uint8_t> serialize() const {
        std::vector<uint8_t> buffer;
        buffer.resize(sizeof(sequence) + sizeof(timestamp) + sizeof(userId) + data.size());

        uint8_t* ptr = buffer.data();

        // Serialize sequence (convert to network byte order)
        *reinterpret_cast<uint32_t*>(ptr) = htonl(sequence);
        ptr += sizeof(sequence);

        // Serialize timestamp (convert to network byte order)
        *reinterpret_cast<uint32_t*>(ptr) = htonl(timestamp);
        ptr += sizeof(timestamp);

        // Serialize userId (convert to network byte order)
        *reinterpret_cast<uint32_t*>(ptr) = htonl(userId);
        ptr += sizeof(userId);

        // Serialize data
        std::copy(data.begin(), data.end(), ptr);

        return buffer;
    }

    // Function to deserialize a byte stream into a VoicePacket
    static VoicePacket deserialize(const std::vector<uint8_t>& buffer) {
        VoicePacket packet;
        const uint8_t* ptr = buffer.data();

        // Deserialize sequence (convert from network byte order)
        packet.sequence = ntohl(*reinterpret_cast<const uint32_t*>(ptr));
        ptr += sizeof(packet.sequence);

        // Deserialize timestamp (convert from network byte order)
        packet.timestamp = ntohl(*reinterpret_cast<const uint32_t*>(ptr));
        ptr += sizeof(packet.timestamp);

        // Deserialize userId (convert from network byte order)
        packet.userId = ntohl(*reinterpret_cast<const uint32_t*>(ptr));
        ptr += sizeof(packet.userId);

        // Deserialize data
        size_t dataSize = buffer.size() - (sizeof(packet.sequence) + sizeof(packet.timestamp) + sizeof(packet.userId));
        packet.data.resize(dataSize);
        std::copy(ptr, ptr + dataSize, packet.data.begin());

        return packet;
    }
};
