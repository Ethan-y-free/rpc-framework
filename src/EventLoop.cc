#include "EventLoop.h"
#include "Channel.h"
#include "Poller.h"
#include "TimerQueue.h"

#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <sstream>
#include <sys/eventfd.h>
#include <unistd.h>

namespace
{
// TLS 四钩子之首：当前线程的 EventLoop（不存在则为 nullptr）。
// 存指针而非对象：非每线程都需要 loop + 生命周期可控 + 构造时可做唯一性检查。
thread_local EventLoop* t_loopInThisThread = nullptr;

int createEventfd()
{
    int evtfd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (evtfd < 0)
    {
        perror("EventLoop::createEventfd");
        abort();
    }
    return evtfd;
}
}  // namespace

EventLoop::EventLoop()
    : looping_(false),
      quit_(false),
      eventHandling_(false),
      callingPendingFunctors_(false),
      threadId_(std::this_thread::get_id()),
      poller_(Poller::newDefaultPoller(this)),
      timerQueue_(new TimerQueue(this)),
      wakeupFd_(createEventfd()),
      wakeupChannel_(new Channel(this, wakeupFd_))
{
    // 同一线程创建第二个 EventLoop = 违反 one loop per thread，响亮失败。
    if (t_loopInThisThread)
    {
        fprintf(stderr, "EventLoop %p already exists in this thread, aborting\n",
                t_loopInThisThread);
        abort();
    }
    t_loopInThisThread = this;

    wakeupChannel_->setReadCallback(std::bind(&EventLoop::handleRead, this));
    wakeupChannel_->enableReading();
}

EventLoop::~EventLoop()
{
    wakeupChannel_->disableAll();
    wakeupChannel_->remove();
    ::close(wakeupFd_);
    t_loopInThisThread = nullptr;
}

void EventLoop::loop()
{
    assertInLoopThread();              // 修归属漂移：不重设，只断言
    looping_ = true;
    quit_ = false;
    while (!quit_)
    {
        activeChannels_.clear();
        pollReturnTime_ = poller_->poll(kPollTimeMs, &activeChannels_);

        eventHandling_ = true;                              // 置位条件3
        for (Channel* channel : activeChannels_)
        {
            currentActiveChannel_ = channel;
            channel->handleEvent(pollReturnTime_);
        }
        currentActiveChannel_ = nullptr;
        eventHandling_ = false;                             // 复位条件3

        doPendingFunctors();                                // 先网络事件，再 pending 任务
    }
    looping_ = false;
}

void EventLoop::quit()
{
    quit_ = true;
    if (!isInLoopThread())
    {
        wakeup();        // 别的线程叫停：把睡死的 poll() 唤醒
    }
}

void EventLoop::runInLoop(Functor cb)
{
    if (isInLoopThread())
    {
        cb();                            // 直接同步执行：无锁、零成本
    }
    else
    {
        queueInLoop(std::move(cb));      // 别的线程 → 入队等 loop 处理
    }
}

void EventLoop::queueInLoop(Functor cb)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pendingFunctors_.push_back(std::move(cb));
    }
    if (!isInLoopThread() || callingPendingFunctors_ || eventHandling_)
    {
        wakeup();                        // 三条件唤醒（D4）
    }
}

void EventLoop::wakeup()
{
    uint64_t one = 1;
    ssize_t n = ::write(wakeupFd_, &one, sizeof(one));   // eventfd 永不阻塞
    (void)n;
}

void EventLoop::handleRead()
{
    uint64_t one = 0;
    ssize_t n = ::read(wakeupFd_, &one, sizeof(one));    // 一次取走计数，清零
    (void)n;
}

void EventLoop::doPendingFunctors()
{
    std::vector<Functor> functors;
    PendingFunctorGuard guard(this);        // 构造置位 / 析构复位，异常也复位
    {
        std::lock_guard<std::mutex> lock(mutex_);
        functors.swap(pendingFunctors_);    // 锁只在这一瞬
    }
    for (const Functor& functor : functors)
    {
        functor();                          // 执行阶段完全无锁
    }
}

EventLoop::PendingFunctorGuard::PendingFunctorGuard(EventLoop* loop)
    : loop_(loop)
{
    loop_->callingPendingFunctors_ = true;
}

EventLoop::PendingFunctorGuard::~PendingFunctorGuard()
{
    loop_->callingPendingFunctors_ = false;   // 任何退出路径（含异常展开）都会执行
}

bool EventLoop::isInLoopThread() const
{
    return threadId_ == std::this_thread::get_id();
}

size_t EventLoop::pendingTaskCount() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return pendingFunctors_.size();
}

void EventLoop::assertInLoopThread() const
{
    if (!isInLoopThread())
    {
        abortNotInLoopThread();
    }
}

void EventLoop::abortNotInLoopThread() const
{
    std::ostringstream owner, current;
    owner << threadId_;
    current << std::this_thread::get_id();
    fprintf(stderr,
            "EventLoop::abortNotInLoopThread - EventLoop %p created in thread %s, "
            "current thread id %s\n",
            this,
            owner.str().c_str(),
            current.str().c_str());
    abort();
}

EventLoop* EventLoop::getEventLoopOfCurrentThread()
{
    return t_loopInThisThread;
}

void EventLoop::updateChannel(Channel* channel)
{
    assertInLoopThread();
    poller_->updateChannel(channel);
}

void EventLoop::removeChannel(Channel* channel)
{
    assertInLoopThread();
    poller_->removeChannel(channel);
}

bool EventLoop::hasChannel(Channel* channel)
{
    assertInLoopThread();
    return poller_->hasChannel(channel);
}

TimerId EventLoop::runAt(Timestamp when, TimerCallback cb)
{
    return timerQueue_->addTimer(std::move(cb), when, 0.0);
}

TimerId EventLoop::runAfter(double delay, TimerCallback cb)
{
    Timestamp when = addTime(Timestamp::now(), delay);
    return runAt(when, std::move(cb));
}

TimerId EventLoop::runEvery(double interval, TimerCallback cb)
{
    Timestamp when = addTime(Timestamp::now(), interval);
    return timerQueue_->addTimer(std::move(cb), when, interval);
}
