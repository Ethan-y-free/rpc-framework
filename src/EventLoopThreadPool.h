#pragma once

#include <map>
#include <memory>
#include <mutex>
#include <vector>

#include "EventLoopThread.h"
#include "Timer.h"   // TimerId（runEvery 返回值完整类型）

class EventLoop;

// ---- EventLoopThreadPool：动态伸缩线程池管理对象 ----
//
// 线程不足（连接/任务积压超阈值）→ 新建线程；线程多余（空 loop 连续空闲）→ 回收。
// 关键约束：连接绑定 loop 后不可迁移，因此只回收"无连接且无积压"的 loop，绝不强杀。
// minThreads_=0 时退化为纯单线程（getNextLoop 恒返回 baseLoop），兼容最小形态。
class EventLoopThreadPool
{
public:
    explicit EventLoopThreadPool(EventLoop* baseLoop);   // baseLoop = mainLoop
    ~EventLoopThreadPool();

    EventLoopThreadPool(const EventLoopThreadPool&) = delete;
    EventLoopThreadPool& operator=(const EventLoopThreadPool&) = delete;

    void setThreadNum(int min) { minThreads_ = min; }      // 保底活跃线程数
    void setMaxThreadNum(int max) { maxThreads_ = max; }   // 上限，防无限扩展
    void start();                                          // 建初始线程 + 启动周期评估

    EventLoop* getNextLoop();   // TcpServer 分配连接时调用（轮询 + 扩容判断）

    void registerConnection(EventLoop* loop);     // 连接 +1（任意线程安全）
    void unregisterConnection(EventLoop* loop);   // 连接 -1（任意线程安全）

private:
    void addThread();
    void evaluateResize();     // 周期评估：缩容空 loop
    void recycleLoop(EventLoop* loop);
    int activeConnCount() const;
    int activePendingCount() const;

    EventLoop* baseLoop_;
    int minThreads_ = 0;
    int maxThreads_ = 0;
    bool started_ = false;
    int next_ = 0;   // round-robin 轮询下标

    std::vector<std::unique_ptr<EventLoopThread>> threads_;   // 与 loops_ 一一对应
    std::vector<EventLoop*> loops_;                            // 活跃 loop

    mutable std::mutex mutex_;             // 只护 connCount_ / idleRounds_
    std::map<EventLoop*, int> connCount_;  // 每个活跃 loop 的连接数
    std::map<EventLoop*, int> idleRounds_; // 连续空闲评估轮数

    static const int kHighConnPerLoop = 2;       // 单 loop 平均连接数阈值 → 扩容（echo 轻量场景，连接一多就扩）
    static const int kHighPendingPerLoop = 8;    // 单 loop 平均积压任务阈值 → 扩容
    static const int kIdleRoundsToRecycle = 2;   // 连续空闲 2 轮（约 6s）→ 回收
    static const double kResizeInterval;         // 评估周期（秒）

    TimerId resizeTimerId_;
};
