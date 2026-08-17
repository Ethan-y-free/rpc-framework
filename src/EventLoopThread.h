#pragma once

#include <condition_variable>
#include <mutex>
#include <thread>

class EventLoop;

// ---- EventLoopThread：一个线程 + 一个 loop 的绑定 ----
// 线程入口创建局部 EventLoop，就绪后通知主线程，然后永久 loop()。
// 必须等 loop 创建完才返回，否则 TcpServer 拿到的是空指针。
class EventLoopThread
{
public:
    EventLoopThread();
    ~EventLoopThread();   // quit + join

    EventLoopThread(const EventLoopThread&) = delete;
    EventLoopThread& operator=(const EventLoopThread&) = delete;

    EventLoop* startLoop();   // 返回线程内创建好的 loop（阻塞等待就绪）

private:
    void threadFunc();

    EventLoop* loop_ = nullptr;
    bool exiting_ = false;
    std::thread thread_;
    std::mutex mutex_;
    std::condition_variable cond_;   // 主线程等 loop 就绪
};
