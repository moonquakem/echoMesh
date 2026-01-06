#pragma once

#include <vector>
#include <memory>
#include <functional>

class EventLoop;
class EventLoopThread;

class ThreadPool {
public:
    ThreadPool(int threadNum);
    ~ThreadPool();

    void start();
    EventLoop* getNextLoop();

private:
    int numThreads_;
    int next_;
    std::vector<std::unique_ptr<EventLoopThread>> threads_;
    std::vector<EventLoop*> loops_;
};