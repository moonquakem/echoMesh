#pragma once

#include "audio/VoicePacket.h"
#include <queue>
#include <vector>
#include <mutex>

class JitterBuffer {
public:
    JitterBuffer(size_t max_size = 20);

    void push(const VoicePacket& packet);
    VoicePacket pop();
    bool empty() const;
    size_t size() const;

private:
    struct ComparePacket {
        bool operator()(const VoicePacket& a, const VoicePacket& b) {
            // Min-heap, so we get the smallest sequence number
            return a.sequence > b.sequence;
        }
    };

    std::priority_queue<VoicePacket, std::vector<VoicePacket>, ComparePacket> pq_;
    size_t max_size_;
    mutable std::mutex mutex_;

    void clean();
};
