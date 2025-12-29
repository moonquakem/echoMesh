#pragma once

#include <vector>
#include <memory>
#include <functional>
#include <mutex>
#include "ThreadPool.h"

class Channel;
class EpollPoller;

class EventLoop {
public:
    using Functor = std::function<void()>;

    EventLoop();
    ~EventLoop();

    void loop();
    void quit();

    void runInLoop(Functor cb);
    void queueInLoop(Functor cb);

    void updateChannel(Channel* channel);
    void removeChannel(Channel* channel);

    bool isInLoopThread() const { return threadId_ == std::this_thread::get_id(); }
    void assertInLoopThread() {
        if (!isInLoopThread()) {
            abortNotInLoopThread();
        }
    }

private:
    void abortNotInLoopThread();
    void handleRead();  // for wakeup
    void doPendingFunctors();

    using ChannelList = std::vector<Channel*>;

    bool looping_;
    bool quit_;
    bool callingPendingFunctors_;
    const std::thread::id threadId_;
    
    std::unique_ptr<EpollPoller> poller_;
    ChannelList activeChannels_;

    int wakeupFd_; // Used to wake up the loop
    std::unique_ptr<Channel> wakeupChannel_;

    std::mutex mutex_;
    std::vector<Functor> pendingFunctors_;
};
