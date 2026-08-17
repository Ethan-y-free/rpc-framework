#pragma once

#include <functional>
#include <sys/epoll.h>
#include "Timestamp.h"

class EventLoop;

class Channel
{
public:
    using EventCallback = std::function<void()>;
    using ReadEventCallback = std::function<void(Timestamp)>;

    Channel(EventLoop* loop, int fd);
    ~Channel();

    void handleEvent(Timestamp receiveTime);      // 由 EventLoop 分发时调用

    // 回调注册
    void setReadCallback(ReadEventCallback cb) { readCallback_ = std::move(cb); }
    void setWriteCallback(EventCallback cb) { writeCallback_ = std::move(cb); }
    void setCloseCallback(EventCallback cb) { closeCallback_ = std::move(cb); }
    void setErrorCallback(EventCallback cb) { errorCallback_ = std::move(cb); }

    // 关注事件开关（改 events_ 并通知 poller）
    void enableReading();
    void disableReading();
    void enableWriting();
    void disableWriting();
    void disableAll();
    bool isNoneEvent() const { return events_ == kNoneEvent; }
    bool isWriting() const { return events_ & kWriteEvent; }   // 发送缓冲是否还有 EPOLLOUT 待写

    // 供 poller 填/读
    int fd() const { return fd_; }
    int events() const { return events_; }
    void setRevents(int revents) { revents_ = revents; }
    int index() const { return index_; }
    void setIndex(int index) { index_ = index; }
    EventLoop* ownerLoop() const { return loop_; }

    void remove();                                // disableAll + ownerLoop_->removeChannel(this)

private:
    static const int kNoneEvent = 0;
    static const int kReadEvent = EPOLLIN | EPOLLPRI;
    static const int kWriteEvent = EPOLLOUT;

    void update();                                // 调 ownerLoop_->updateChannel(this)

    EventLoop* loop_;
    const int fd_;
    int events_;       // 关注的（用户设置）
    int revents_;      // 就绪的（poller 回填）
    int index_;        // poller 内状态：kNew=-1 / kAdded / kDeleted
    bool eventHandling_ = false;

    ReadEventCallback readCallback_;
    EventCallback writeCallback_;
    EventCallback closeCallback_;
    EventCallback errorCallback_;
};