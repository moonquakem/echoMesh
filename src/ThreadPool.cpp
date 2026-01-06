#include "ThreadPool.h"
#include "EventLoop.h"
#include "EventLoopThread.h"

ThreadPool::ThreadPool(int threadNum)
    : numThreads_(threadNum),
      next_(0) {
}

ThreadPool::~ThreadPool() {
}

void ThreadPool::start() {
    for (int i = 0; i < numThreads_; ++i) {
        threads_.emplace_back(std::make_unique<EventLoopThread>());
        loops_.push_back(threads_[i]->startLoop());
    }
}

EventLoop* ThreadPool::getNextLoop() {
    if (loops_.empty()) {
        return nullptr;
    }
    EventLoop* loop = loops_[next_];
    next_ = (next_ + 1) % loops_.size();
    return loop;
}