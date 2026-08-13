#pragma once

#include <sys/epoll.h>
#include <unordered_map>
#include <vector>
#include "Poller.h"

class EventLoop;

class EPollPoller : public Poller
{
public:
    explicit EPollPoller(EventLoop* loop);
    ~EPollPoller() override;

    Timestamp poll(int timeoutMs, ChannelList* activeChannels) override;
    void updateChannel(Channel* channel) override;
    void removeChannel(Channel* channel) override;
    bool hasChannel(Channel* channel) const override;

private:
    static const int kInitEventListSize = 16;

    int epollfd_;
    std::vector<struct epoll_event> events_;      
    using ChannelMap = std::unordered_map<int, Channel*>;
    ChannelMap channels_;                         

    void update(int operation, Channel* channel);
    void fillActiveChannels(int numEvents, ChannelList* activeChannels) const;
};