#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "Timer.h"   // TimerId / Timestamp 完整类型（runAt 等按值返回）

class Channel;
class Poller;
class TimerQueue;

class EventLoop
{
public:
    using Functor = std::function<void()>;
    using TimerCallback = std::function<void()>;   // 定时器回调（与 Timer 同款签名）

    EventLoop();
    ~EventLoop();
    EventLoop(const EventLoop&) = delete;
    EventLoop& operator=(const EventLoop&) = delete;

    // ---- 主循环与退出 ----
    void loop();                        // 必须在构造线程调用，assertInLoopThread()
    void quit();                        // 线程安全；非 loop 线程调用会自动唤醒

    // ---- 任务投递 ----
    void runInLoop(Functor cb);         // loop 线程内直接执行，否则入队
    void queueInLoop(Functor cb);       // 总是入队，延迟到本轮结束
    void wakeup();                      // eventfd write，线程安全

    // ---- Channel 注册 ----
    void updateChannel(Channel* channel);
    void removeChannel(Channel* channel);
    bool hasChannel(Channel* channel);

    // ---- 定时器（透传 TimerQueue）----
    TimerId runAt(Timestamp when, TimerCallback cb);
    TimerId runAfter(double delay, TimerCallback cb);
    TimerId runEvery(double interval, TimerCallback cb);

    // ---- 线程归属 ----
    bool isInLoopThread() const;
    void assertInLoopThread() const;
    static EventLoop* getEventLoopOfCurrentThread();
    size_t pendingTaskCount() const;   // 只读积压任务量：动态线程池评估负载用

    // ---- 内部使用 ----
    Timestamp pollReturnTime() const { return pollReturnTime_; }

private:
    // 私有嵌套守卫类：异常安全置/复位 callingPendingFunctors_
    class PendingFunctorGuard
    {
    public:
        explicit PendingFunctorGuard(EventLoop* loop);
        ~PendingFunctorGuard();
        PendingFunctorGuard(const PendingFunctorGuard&) = delete;
        PendingFunctorGuard& operator=(const PendingFunctorGuard&) = delete;
    private:
        EventLoop* loop_;
    };

    void abortNotInLoopThread() const;  // 只 fprintf + abort，不修改成员，可被 const 方法调
    void handleRead();                  // eventfd 到期：read 8 字节清零
    void doPendingFunctors();           // swap 快照 + PendingFunctorGuard

    using ChannelList = std::vector<Channel*>;

    static const int kPollTimeMs = 10000;   // poll 超时：空闲时最多睡多久醒一次

    bool looping_ = false;
    bool quit_ = false;
    bool eventHandling_ = false;   // 条件3：正在分发本批事件
    bool callingPendingFunctors_ = false;   // 条件2：正在执行 pending 快照
    const std::thread::id threadId_;        // ▲ const，归属唯一

    std::unique_ptr<Poller> poller_;
    std::unique_ptr<TimerQueue> timerQueue_;

    int wakeupFd_;                          // eventfd(0, EFD_NONBLOCK|EFD_CLOEXEC)
    std::unique_ptr<Channel> wakeupChannel_;

    Timestamp pollReturnTime_;              // 本轮 poll 返回时刻（handleEvent 分发用）
    ChannelList activeChannels_;            // 本轮 epoll 返回待分发
    Channel* currentActiveChannel_ = nullptr;

    mutable std::mutex mutex_;              // 只护 pendingFunctors_
    std::vector<Functor> pendingFunctors_;  // 软件任务队列（swap 快照消费）
};