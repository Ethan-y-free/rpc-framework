#pragma once

#include <vector>
#include "Timestamp.h"

class Channel;
class EventLoop;

class Poller
{
public:
    using ChannelList = std::vector<Channel*>;

    explicit Poller(EventLoop* loop) : ownerLoop_(loop) {}
    virtual ~Poller() = default;

    virtual Timestamp poll(int timeoutMs, ChannelList* activeChannels) = 0;
    virtual void updateChannel(Channel* channel) = 0;
    virtual void removeChannel(Channel* channel) = 0;
    virtual bool hasChannel(Channel* channel) const = 0;

    static Poller* newDefaultPoller(EventLoop* loop);   // 工厂：返回 EPollPoller

protected:
    EventLoop* ownerLoop_;
};