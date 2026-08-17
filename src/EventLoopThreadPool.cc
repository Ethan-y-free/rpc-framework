#include "EventLoopThreadPool.h"

#include <algorithm>
#include <functional>   // std::bind

#include "EventLoop.h"

const double EventLoopThreadPool::kResizeInterval = 3.0;   // 周期评估间隔（秒）

EventLoopThreadPool::EventLoopThreadPool(EventLoop* baseLoop)
    : baseLoop_(baseLoop)
{
}

EventLoopThreadPool::~EventLoopThreadPool()
{
    // 无需显式清理：threads_ 析构时各自 quit + join；定时器随 baseLoop 关闭
}

void EventLoopThreadPool::start()
{
    baseLoop_->assertInLoopThread();
    started_ = true;
    for (int i = 0; i < minThreads_; ++i)
    {
        addThread();
    }
    // 动态模式（max > min）才启动周期缩容评估；纯固定池不引入定时器
    if (maxThreads_ > minThreads_)
    {
        resizeTimerId_ = baseLoop_->runEvery(
            kResizeInterval,
            std::bind(&EventLoopThreadPool::evaluateResize, this));
    }
}

// 分配路径触发扩容判断：活跃线程不足（连接或积压超阈值）且未达上限 → 新建线程。
// 只在这里扩容，缩容评估里绝不建线程，避免 baseLoop 被反复阻塞。
EventLoop* EventLoopThreadPool::getNextLoop()
{
    baseLoop_->assertInLoopThread();
    if (static_cast<int>(loops_.size()) < maxThreads_)
    {
        int active = static_cast<int>(loops_.size());
        int highConn = active * kHighConnPerLoop;
        int highPending = active * kHighPendingPerLoop;
        // active == 0 时阈值也为 0，恒触发 → 至少建一个线程
        if (active == 0 || activeConnCount() > highConn || activePendingCount() > highPending)
        {
            addThread();
        }
    }

    if (loops_.empty())
    {
        return baseLoop_;   // 纯单线程退化
    }
    if (next_ >= static_cast<int>(loops_.size()))
    {
        next_ = 0;   // 缩容后下标可能越界，归零
    }
    EventLoop* loop = loops_[next_];
    next_ = (next_ + 1) % static_cast<int>(loops_.size());
    return loop;
}

void EventLoopThreadPool::addThread()
{
    std::unique_ptr<EventLoopThread> thread(new EventLoopThread());
    EventLoop* loop = thread->startLoop();
    loops_.push_back(loop);
    threads_.push_back(std::move(thread));
}

void EventLoopThreadPool::registerConnection(EventLoop* loop)
{
    std::lock_guard<std::mutex> lock(mutex_);
    ++connCount_[loop];
    idleRounds_[loop] = 0;   // 有活了，清空闲标记
}

void EventLoopThreadPool::unregisterConnection(EventLoop* loop)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = connCount_.find(loop);
    if (it != connCount_.end() && it->second > 0)
    {
        --it->second;
    }
}

// 周期评估（baseLoop 线程）：只回收"无连接且无积压"的空 loop，且不跌破 minThreads_。
void EventLoopThreadPool::evaluateResize()
{
    baseLoop_->assertInLoopThread();
    if (static_cast<int>(loops_.size()) <= minThreads_)
    {
        return;   // 已到保底下限，不再回收，防频繁创建/销毁抖动
    }
    std::vector<EventLoop*> toRecycle;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (EventLoop* loop : loops_)
        {
            if (connCount_[loop] == 0 && loop->pendingTaskCount() == 0)
            {
                ++idleRounds_[loop];
                if (idleRounds_[loop] >= kIdleRoundsToRecycle)
                {
                    toRecycle.push_back(loop);
                }
            }
            else
            {
                idleRounds_[loop] = 0;
            }
        }
    }
    for (EventLoop* loop : toRecycle)
    {
        recycleLoop(loop);
    }
}

// 回收：用末尾补位后 pop，threads_/loops_ 始终一一对应。
// 被回收的 EventLoopThread 在 unique_ptr 析构时 quit + join，线程安全退出。
void EventLoopThreadPool::recycleLoop(EventLoop* loop)
{
    auto it = std::find(loops_.begin(), loops_.end(), loop);
    if (it == loops_.end())
    {
        return;   // 已被回收（重复评估）
    }
    size_t idx = static_cast<size_t>(it - loops_.begin());
    size_t last = loops_.size() - 1;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        connCount_.erase(loop);
        idleRounds_.erase(loop);
    }
    if (idx != last)
    {
        loops_[idx] = loops_[last];
        threads_[idx] = std::move(threads_[last]);
    }
    loops_.pop_back();
    threads_.pop_back();   // 析构被回收的 EventLoopThread → quit + join
    if (next_ >= static_cast<int>(loops_.size()))
    {
        next_ = 0;
    }
}

int EventLoopThreadPool::activeConnCount() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    int total = 0;
    for (const auto& kv : connCount_)
    {
        total += kv.second;
    }
    return total;
}

int EventLoopThreadPool::activePendingCount() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    int total = 0;
    for (const auto& kv : connCount_)
    {
        total += static_cast<int>(kv.first->pendingTaskCount());
    }
    return total;
}
