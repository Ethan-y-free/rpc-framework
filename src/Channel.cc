#include "Channel.h"
#include "EventLoop.h"

Channel::Channel(EventLoop* loop, int fd)
	: loop_(loop),
	  fd_(fd),
	  events_(0),
	  revents_(0),
	  index_(-1) {}

Channel::~Channel()
{
	if (eventHandling_ || loop_->hasChannel(this))
	{
		abort();
	}
}

void Channel::handleEvent(Timestamp receiveTime)
{
	eventHandling_ = true;
    if ((revents_ & EPOLLHUP) && !(revents_ & EPOLLIN))
    {
        if (closeCallback_) closeCallback_();          
    }
    if (revents_ & (EPOLLERR | EPOLLNVAL))
    {
        if (errorCallback_) errorCallback_();          
    }
    if (revents_ & (EPOLLIN | EPOLLPRI | EPOLLRDHUP))
    {
        if (readCallback_) readCallback_(receiveTime);  
    }
    if (revents_ & EPOLLOUT)
    {
        if (writeCallback_) writeCallback_();           
    }
    eventHandling_ = false;
}

void Channel::enableReading()
{
    events_ |= kReadEvent;
    update();
}

void Channel::disableReading()
{
    events_ &= ~kReadEvent; 
    update();
}

void Channel::enableWriting()
{
    events_ |= kWriteEvent;
    update();
}

void Channel::disableWriting()
{
    events_ &= ~kWriteEvent; 
    update();
}

void Channel::disableAll()
{
    events_ = kNoneEvent;
    update();
}

void Channel::remove()
{
    disableAll();
    loop_->removeChannel(this);
}

void Channel::update()
{
    loop_->updateChannel(this);
}