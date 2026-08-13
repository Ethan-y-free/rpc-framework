#include "TimerQueue.h"
#include "EventLoop.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iterator>
#include <sys/timerfd.h>
#include <unistd.h>

namespace
{
// timerfd_create：CLOCK_MONOTONIC 单调时钟（不受系统时间调整影响），非阻塞 + CLOEXEC
int createTimerfd()
{
    int timerfd = ::timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    if (timerfd < 0)
    {
        perror("TimerQueue::createTimerfd");
        abort();
    }
    return timerfd;
}

// 距离到期还有多久，转成 timespec 给 timerfd_settime。
// 下限 100 微秒：避免"立即到期"导致的忙循环（timerfd 会立刻再唤醒）。
struct timespec howMuchTimeFromNow(Timestamp when)
{
    int64_t microSeconds = when.microSecondsSinceEpoch() - Timestamp::now().microSecondsSinceEpoch();
    if (microSeconds < 100)
    {
        microSeconds = 100;
    }
    struct timespec ts;
    ts.tv_sec = static_cast<time_t>(microSeconds / Timestamp::kMicroSecondsPerSecond);
    ts.tv_nsec = static_cast<long>((microSeconds % Timestamp::kMicroSecondsPerSecond) * 1000);
    return ts;
}

// 读走 timerfd 的到期计数（一次读出即清零）。非阻塞下 EAGAIN 无害，忽略即可。
void readTimerfd(int timerfd)
{
    uint64_t howmany;
    ssize_t n = ::read(timerfd, &howmany, sizeof howmany);
    (void)n;
}
}  // namespace

TimerQueue::TimerQueue(EventLoop* loop)
    : loop_(loop),
      timerfd_(createTimerfd()),
      timerfdChannel_(loop, timerfd_)
{
    timerfdChannel_.setReadCallback(std::bind(&TimerQueue::handleRead, this));
    timerfdChannel_.enableReading();   // 注册进 loop 的 poller，到期 = fd 可读 = 普通事件
}

TimerQueue::~TimerQueue()
{
    timerfdChannel_.disableAll();
    timerfdChannel_.remove();
    ::close(timerfd_);
    for (const Entry& entry : timers_)
    {
        delete entry.second;
    }
}

// 跨线程安全：new Timer 后转发到 loop 线程插入。
// 返回的 TimerId 句柄指向堆上 Timer（由 TimerQueue 持有所有权）。
TimerId TimerQueue::addTimer(TimerCallback cb, Timestamp when, double interval)
{
    Timer* timer = new Timer(std::move(cb), when, interval);
    loop_->runInLoop(std::bind(&TimerQueue::addTimerInLoop, this, timer));
    return TimerId(timer, timer->sequence());
}

// loop 线程内：插入，若成为最早到期则重设 timerfd（内核据此决定下次唤醒时刻）。
void TimerQueue::addTimerInLoop(Timer* timer)
{
    bool earliestChanged = insert(timer);
    if (earliestChanged)
    {
        resetTimerfd(timer->expiration());
    }
}

// timerfd 到期回调：清计数 → 取所有到期 → 执行回调 → 重复定时器重排。
void TimerQueue::handleRead()
{
    Timestamp now(Timestamp::now());
    readTimerfd(timerfd_);

    std::vector<Entry> expired = getExpired(now);
    for (const Entry& entry : expired)
    {
        entry.second->run();
    }
    reset(expired, now);
}

// 取所有到期（<= now）并移出 set。
// 哨兵：pair(now, 最大 Timer*)——所有 timestamp <= now 的 entry 都小于它，
// lower_bound 返回"第一个未到期"的位置，前面的全是到期的。
std::vector<TimerQueue::Entry> TimerQueue::getExpired(Timestamp now)
{
    std::vector<Entry> expired;
    Entry sentry(now, reinterpret_cast<Timer*>(UINTPTR_MAX));
    TimerList::iterator end = timers_.lower_bound(sentry);
    std::copy(timers_.begin(), end, std::back_inserter(expired));
    timers_.erase(timers_.begin(), end);
    return expired;
}

// 重复定时器重排回 set；一次性定时器释放。
// 重排后必须重查最早到期并重设 timerfd——新最早可能早于当前 timerfd 设的时刻。
void TimerQueue::reset(const std::vector<Entry>& expired, Timestamp now)
{
    Timestamp nextExpire;
    for (const Entry& entry : expired)
    {
        if (entry.second->repeat())
        {
            entry.second->restart(now);
            insert(entry.second);
        }
        else
        {
            delete entry.second;
        }
    }

    if (!timers_.empty())
    {
        nextExpire = timers_.begin()->second->expiration();
    }
    if (nextExpire.valid())
    {
        resetTimerfd(nextExpire);
    }
}

// timerfd_settime 重设到期：把整个 itimerspec 清零，避免旧值残留影响新设置。
void TimerQueue::resetTimerfd(Timestamp expiration)
{
    struct itimerspec newValue;
    memset(&newValue, 0, sizeof newValue);
    newValue.it_value = howMuchTimeFromNow(expiration);
    int ret = ::timerfd_settime(timerfd_, 0, &newValue, nullptr);
    if (ret != 0)
    {
        perror("TimerQueue::resetTimerfd");
    }
}

// 插入并返回"是否成为最早到期"（决定要不要重设 timerfd）。
bool TimerQueue::insert(Timer* timer)
{
    bool earliestChanged = false;
    Timestamp when = timer->expiration();
    TimerList::iterator it = timers_.begin();
    if (it == timers_.end() || when < it->first)
    {
        earliestChanged = true;
    }
    timers_.insert(Entry(when, timer));
    return earliestChanged;
}
