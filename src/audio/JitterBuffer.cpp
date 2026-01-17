#include "audio/JitterBuffer.h"
#include <stdexcept>

JitterBuffer::JitterBuffer(size_t max_size) : max_size_(max_size) {}

void JitterBuffer::push(const VoicePacket& packet) {
    std::lock_guard<std::mutex> lock(mutex_);
    pq_.push(packet);
    if (pq_.size() > max_size_) {
        clean();
    }
}

VoicePacket JitterBuffer::pop() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (pq_.empty()) {
        throw std::runtime_error("Jitter buffer is empty");
    }
    VoicePacket packet = pq_.top();
    pq_.pop();
    return packet;
}

bool JitterBuffer::empty() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return pq_.empty();
}

size_t JitterBuffer::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return pq_.size();
}

void JitterBuffer::clean() {
    // For now, we just drop the oldest packet. A more sophisticated
    // implementation might have more complex logic.
    while (pq_.size() > max_size_) {
        pq_.pop();
    }
}
