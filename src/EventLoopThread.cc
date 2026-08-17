#include "EventLoopThread.h"

#include <cassert>

#include "EventLoop.h"

EventLoopThread::EventLoopThread()
    : thread_(std::bind(&EventLoopThread::threadFunc, this))
{
}

EventLoopThread::~EventLoopThread()
{
    exiting_ = true;
    if (loop_)
    {
        loop_->quit();   // 唤醒并让 loop 退出，threadFunc 随之返回
    }
    thread_.join();
}

EventLoop* EventLoopThread::startLoop()
{
    std::unique_lock<std::mutex> lock(mutex_);
    while (loop_ == nullptr)
    {
        cond_.wait(lock);
    }
    return loop_;
}

void EventLoopThread::threadFunc()
{
    EventLoop loop;
    {
        std::unique_lock<std::mutex> lock(mutex_);
        loop_ = &loop;
        cond_.notify_one();   // 就绪，通知主线程拿走
    }
    loop.loop();   // 永久阻塞，直到析构时 quit()
    loop_ = nullptr;
}
