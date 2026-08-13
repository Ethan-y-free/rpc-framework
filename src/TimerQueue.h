#pragma once

#include <functional>
#include <set>
#include <stdint.h>
#include <utility>
#include <vector>

#include "Timestamp.h"
#include "Timer.h"
#include "Channel.h"

class EventLoop;

// TimerQueue：定时器管理中心，timerfd 并进 epoll 事件模型。
// 全部操作在 loop 线程内（timers_ 无锁），跨线程投递靠 EventLoop::runInLoop。
class TimerQueue
{
public:
    using TimerCallback = std::function<void()>;

    explicit TimerQueue(EventLoop* loop);
    ~TimerQueue();                        // 析构时在 loop 线程，释放所有 Timer*

    // 跨线程安全：任何线程可调，内部 runInLoop 转发到 loop 线程。
    // 返回 TimerId 作句柄（v2 第一版不做 cancel，但类型定型）。
    TimerId addTimer(TimerCallback cb, Timestamp when, double interval);

private:
    using Entry = std::pair<Timestamp, Timer*>;   // 字典序：先比到期时间，相同再比 Timer* 地址
    using TimerList = std::set<Entry>;

    void addTimerInLoop(Timer* timer);              // loop 线程内：插入 + 可能重设 timerfd
    void handleRead();                              // timerfd 到期回调（loop 线程）
    std::vector<Entry> getExpired(Timestamp now);   // 取所有到期（<= now）并移出 set
    void reset(const std::vector<Entry>& expired, Timestamp now);  // 重复定时器重排
    bool insert(Timer* timer);                      // 返回"是否成为最早到期"
    void resetTimerfd(Timestamp expiration);        // timerfd_settime 重设到期

    EventLoop* loop_;
    const int timerfd_;                     // timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK|TFD_CLOEXEC)
    Channel timerfdChannel_;                // 按值成员，注册进 loop 的 poller
    TimerList timers_;                      // std::set，仅 loop 线程访问
};
