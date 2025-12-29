#include "EventLoop.h"
#include "Channel.h"
#include "EpollPoller.h"

#include <sys/eventfd.h>
#include <unistd.h>
#include <cassert>
#include <iostream>

namespace {
    // A thread-local pointer to the EventLoop for the current thread.
    thread_local EventLoop* t_loopInThisThread = nullptr;

    int createEventfd() {
        int evtfd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
        if (evtfd < 0) {
            // Log error
            abort();
        }
        return evtfd;
    }
}

EventLoop::EventLoop()
    : looping_(false),
      quit_(false),
      callingPendingFunctors_(false),
      threadId_(std::this_thread::get_id()),
      poller_(std::make_unique<EpollPoller>(this)),
      wakeupFd_(createEventfd()),
      wakeupChannel_(std::make_unique<Channel>(this, wakeupFd_))
      {
    if (t_loopInThisThread) {
        // Log error: another EventLoop already exists in this thread
        abort();
    } else {
        t_loopInThisThread = this;
    }
    wakeupChannel_->setReadCallback([this] { this->handleRead(); });
    wakeupChannel_->enableReading();
}

EventLoop::~EventLoop() {
    wakeupChannel_->disableAll();
    wakeupChannel_->remove();
    ::close(wakeupFd_);
    t_loopInThisThread = nullptr;
}

void EventLoop::loop() {
    assertInLoopThread();
    looping_ = true;
    quit_ = false;

    while (!quit_) {
        activeChannels_.clear();
        poller_->poll(10000, &activeChannels_);
        for (Channel* channel : activeChannels_) {
            channel->handleEvent();
        }
        doPendingFunctors();
    }

    looping_ = false;
}

void EventLoop::quit() {
    quit_ = true;
    if (!isInLoopThread()) {
        // Wake up the loop if it's blocked in poll()
        uint64_t one = 1;
        ssize_t n = ::write(wakeupFd_, &one, sizeof one);
        if (n != sizeof one) {
            // Log error
        }
    }
}

void EventLoop::runInLoop(Functor cb) {
    if (isInLoopThread()) {
        cb();
    } else {
        queueInLoop(std::move(cb));
    }
}

void EventLoop::queueInLoop(Functor cb) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pendingFunctors_.push_back(std::move(cb));
    }

    if (!isInLoopThread() || callingPendingFunctors_) {
        uint64_t one = 1;
        ssize_t n = ::write(wakeupFd_, &one, sizeof one);
        if (n != sizeof one) {
            // Log error
        }
    }
}

void EventLoop::updateChannel(Channel* channel) {
    assert(channel->ownerLoop() == this);
    assertInLoopThread();
    poller_->updateChannel(channel);
}

void EventLoop::removeChannel(Channel* channel) {
    assert(channel->ownerLoop() == this);
    assertInLoopThread();
    poller_->removeChannel(channel);
}

void EventLoop::abortNotInLoopThread() {
    // Log error and abort
    std::cerr << "EventLoop::abortNotInLoopThread() - EventLoop " << this
              << " was created in threadId_ = " << threadId_
              << ", current thread id = " << std::this_thread::get_id() << std::endl;
    abort();
}

void EventLoop::handleRead() {
    uint64_t one = 1;
    ssize_t n = ::read(wakeupFd_, &one, sizeof one);
    if (n != sizeof one) {
        // Log error
    }
}

void EventLoop::doPendingFunctors() {
    std::vector<Functor> functors;
    callingPendingFunctors_ = true;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        functors.swap(pendingFunctors_);
    }

    for (const Functor& functor : functors) {
        functor();
    }
    callingPendingFunctors_ = false;
}

// Add this method to Channel.h for EventLoop::~EventLoop
// void Channel::remove() {
//     assert(isNoneEvent());
//     loop_->removeChannel(this);
// }
// I will add it to the file in the next step
