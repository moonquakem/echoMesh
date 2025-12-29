#include "EpollPoller.h"
#include "Channel.h"
#include <sys/epoll.h>
#include <unistd.h>
#include <cassert>
#include <cstring>

EpollPoller::EpollPoller(EventLoop* loop)
    : ownerLoop_(loop),
      epollfd_(::epoll_create1(EPOLL_CLOEXEC)),
      events_(kInitEventListSize) {
    if (epollfd_ < 0) {
        // In a real app, log this error
    }
}

EpollPoller::~EpollPoller() {
    ::close(epollfd_);
}

void EpollPoller::poll(int timeoutMs, ChannelList* activeChannels) {
    int numEvents = ::epoll_wait(epollfd_, &*events_.begin(), static_cast<int>(events_.size()), timeoutMs);
    int savedErrno = errno;
    if (numEvents > 0) {
        fillActiveChannels(numEvents, activeChannels);
        if (static_cast<size_t>(numEvents) == events_.size()) {
            events_.resize(events_.size() * 2);
        }
    } else if (numEvents == 0) {
        // Nothing happened
    } else {
        if (savedErrno != EINTR) {
            errno = savedErrno;
            // In a real app, log this error
        }
    }
}

void EpollPoller::fillActiveChannels(int numEvents, ChannelList* activeChannels) const {
    assert(static_cast<size_t>(numEvents) <= events_.size());
    for (int i = 0; i < numEvents; ++i) {
        Channel* channel = static_cast<Channel*>(events_[i].data.ptr);
        channel->set_revents(events_[i].events);
        activeChannels->push_back(channel);
    }
}

void EpollPoller::updateChannel(Channel* channel) {
    const int index = channel->index();
    if (index < 0) { // A new one, add with EPOLL_CTL_ADD
        assert(channels_.find(channel->fd()) == channels_.end());
        channels_[channel->fd()] = channel;
        channel->set_index(1); // Mark as added
        update(EPOLL_CTL_ADD, channel);
    } else { // Update existing one with EPOLL_CTL_MOD or EPOLL_CTL_DEL
        assert(channels_.find(channel->fd()) != channels_.end());
        assert(channels_[channel->fd()] == channel);
        if (channel->isNoneEvent()) {
            update(EPOLL_CTL_DEL, channel);
            channel->set_index(-1);
        } else {
            update(EPOLL_CTL_MOD, channel);
        }
    }
}

void EpollPoller::removeChannel(Channel* channel) {
    assert(channels_.find(channel->fd()) != channels_.end());
    assert(channels_[channel->fd()] == channel);
    assert(channel->isNoneEvent());
    int index = channel->index();
    assert(index == 1);
    channels_.erase(channel->fd());
    update(EPOLL_CTL_DEL, channel);
    channel->set_index(-1);
}

void EpollPoller::update(int operation, Channel* channel) {
    struct epoll_event event;
    memset(&event, 0, sizeof(event));
    event.events = channel->events();
    event.data.ptr = channel;
    int fd = channel->fd();
    if (::epoll_ctl(epollfd_, operation, fd, &event) < 0) {
        // Log error
    }
}
