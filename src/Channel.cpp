#include "Channel.h"
#include "EventLoop.h"
#include <sys/epoll.h>

const int Channel::kNoneEvent = 0;
const int Channel::kReadEvent = EPOLLIN | EPOLLPRI;
const int Channel::kWriteEvent = EPOLLOUT;

Channel::Channel(EventLoop* loop, int fd)
    : loop_(loop),
      fd_(fd),
      events_(0),
      revents_(0),
      index_(-1) {}

Channel::~Channel() {
    // In a real app with proper resource management,
    // we would assert that the channel is not in any poller.
}

void Channel::update() {
    loop_->updateChannel(this);
}

void Channel::handleEvent() {
    if ((revents_ & EPOLLHUP) && !(revents_ & EPOLLIN)) {
        if (errorCallback_) errorCallback_();
    }
    if (revents_ & (EPOLLERR)) {
        if (errorCallback_) errorCallback_();
    }
    if (revents_ & (EPOLLIN | EPOLLPRI | EPOLLRDHUP)) {
        if (readCallback_) readCallback_();
    }
    if (revents_ & EPOLLOUT) {
        if (writeCallback_) writeCallback_();
    }
}

void Channel::remove() {
    loop_->removeChannel(this);
}
