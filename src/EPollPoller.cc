#include <cstdlib>  
#include <cstdio>
#include <errno.h>
#include <unistd.h>

#include "EPollPoller.h"
#include "Channel.h"

EPollPoller::EPollPoller(EventLoop* loop) 
	: Poller(loop),
	  epollfd_(::epoll_create1(EPOLL_CLOEXEC)),
	  events_(kInitEventListSize)
{
	if (epollfd_ < 0)
	{
		perror("EPollPoller::EPollPoller");
		abort();
	}
}

EPollPoller::~EPollPoller()
{
	::close(epollfd_);
}

Timestamp EPollPoller::poll(int timeoutMs, Poller::ChannelList* activeChannels)
{
	int nfds = ::epoll_wait(epollfd_, &*events_.begin(), static_cast<int>(events_.size()), timeoutMs);
	int savedErrno = errno;
	Timestamp now(Timestamp::now());

	if (nfds > 0)
	{
		fillActiveChannels(nfds, activeChannels);
		if (static_cast<int>(nfds) == events_.size())
		{
			events_.resize(events_.size() * 2);
		}
	}
	else if (nfds == 0)
	{
		
	}
	else
	{
		if (savedErrno != EINTR)
		{
			errno = savedErrno;
		}
	}
	return now;
}

void EPollPoller::updateChannel(Channel* channel)
{
	ChannelMap::iterator it = channels_.find(channel->fd());
	if (it == channels_.end())
	{
		channels_[channel->fd()] = channel;
		update(EPOLL_CTL_ADD, channel);
	}
	else
	{
		update(EPOLL_CTL_MOD, channel);
	}
}

void EPollPoller::removeChannel(Channel* channel)
{
	ChannelMap::iterator it = channels_.find(channel->fd());
	if (it == channels_.end())
	{
		perror("channel not found");
	}
	else
	{
		update(EPOLL_CTL_DEL, channel);
		channels_.erase(it);
	}
}

bool EPollPoller::hasChannel(Channel* channel) const
{
	ChannelMap::const_iterator it = channels_.find(channel->fd());
	return it != channels_.end();
}

void EPollPoller::update(int operation, Channel* channel)
{
	struct epoll_event event;
	event.events = channel->events();
	event.data.ptr = channel;
	int fd = channel->fd();
	if (::epoll_ctl(epollfd_, operation, fd, &event) < 0)
	{
		if (operation == EPOLL_CTL_DEL)
		{
			
		}
		else
		{
			
		}
	}
}

void EPollPoller::fillActiveChannels(int numEvents, ChannelList* activeChannels) const
{
	for (int i = 0; i < numEvents; ++i)
	{
		Channel* channel = static_cast<Channel*>(events_[i].data.ptr);
		ChannelMap::const_iterator it = channels_.find(channel->fd());
		if (it != channels_.end())
		{
			channel->setRevents(events_[i].events);
			activeChannels->push_back(channel);
		}
	}
}