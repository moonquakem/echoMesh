#pragma once

#include <vector>
#include <map>
#include <memory>

class Channel;
class EventLoop;

class EpollPoller {
public:
    using ChannelList = std::vector<Channel*>;

    EpollPoller(EventLoop* loop);
    ~EpollPoller();

    void poll(int timeoutMs, ChannelList* activeChannels);
    void updateChannel(Channel* channel);
    void removeChannel(Channel* channel);

private:
    void fillActiveChannels(int numEvents, ChannelList* activeChannels) const;
    void update(int operation, Channel* channel);

    using ChannelMap = std::map<int, Channel*>;
    using EventList = std::vector<struct epoll_event>;

    EventLoop* ownerLoop_;
    int epollfd_;
    EventList events_;
    ChannelMap channels_;

    static const int kInitEventListSize = 16;
};
