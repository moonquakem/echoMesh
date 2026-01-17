#pragma once

#include <vector>
#include <mutex>
#include <condition_variable>
#include <optional>

template <typename T>
class RingBuffer {
public:
    explicit RingBuffer(size_t capacity)
        : capacity_(capacity), buffer_(capacity), head_(0), tail_(0), count_(0) {}

    bool push(const T& item) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (count_ == capacity_) {
            return false; // Buffer is full
        }
        buffer_[tail_] = item;
        tail_ = (tail_ + 1) % capacity_;
        count_++;
        cond_.notify_one();
        return true;
    }

    std::optional<T> pop() {
        std::unique_lock<std::mutex> lock(mutex_);
        cond_.wait(lock, [this] { return count_ > 0; });
        T item = buffer_[head_];
        head_ = (head_ + 1) % capacity_;
        count_--;
        return item;
    }

    bool empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return count_ == 0;
    }

    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return count_;
    }

private:
    size_t capacity_;
    std::vector<T> buffer_;
    size_t head_;
    size_t tail_;
    size_t count_;
    mutable std::mutex mutex_;
    std::condition_variable cond_;
};
