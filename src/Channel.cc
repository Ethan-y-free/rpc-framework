#include "Channel.h"
#include "EventLoop.h"

#include <poll.h>   // POLLNVAL：glibc 的 <sys/epoll.h> 不提供 EPOLLNVAL

Channel::Channel(EventLoop* loop, int fd)
	: loop_(loop),
	  fd_(fd),
	  events_(0),
	  revents_(0),
	  index_(-1) {}

Channel::~Channel()
{
    // 只检查事件分发中析构（真 bug）。不再检查是否仍注册在 poller：
    // TcpConnection 由 shared_ptr 管理，最后一个引用可能在任意线程释放
    // （如 TcpServer 的 functor 在 mainLoop 析构），hasChannel 的 assertInLoopThread
    // 会误伤。fd 由 Socket 拥有，关闭时内核 epoll 自动移除该 channel 的注册，
    // 不会再有事件回调，残留的 poller 条目（按 fd 索引）无害。
    if (eventHandling_)
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
    if (revents_ & (EPOLLERR | POLLNVAL))
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