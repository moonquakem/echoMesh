#include "ThreadPool.h"

ThreadPool::ThreadPool(size_t threads) {
    for(size_t i = 0; i < threads; ++i) {
        workers.emplace_back([this](std::stop_token st) {
            while (!st.stop_requested()) {
                std::function<void()> task;
                {
                    std::unique_lock<std::mutex> lock(this->queue_mutex);
                    this->condition.wait(lock, st, [this, &st] { 
                        return st.stop_requested() || !this->tasks.empty(); 
                    });
                    if (st.stop_requested() && this->tasks.empty()) {
                        return;
                    }
                    task = std::move(this->tasks.front());
                    this->tasks.pop();
                }
                task();
            }
        });
    }
}

ThreadPool::~ThreadPool() {
    for (std::jthread& worker : workers) {
        worker.request_stop();
    }
    condition.notify_all();
}
