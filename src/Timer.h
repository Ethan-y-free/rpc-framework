#pragma once

#include <functional>
#include <utility>
#include <stdint.h>
#include <atomic>

#include "Timestamp.h"

class Timer
{
public:
    using TimerCallback = std::function<void()>;

    Timer(TimerCallback cb, Timestamp when, double interval);

    void run() const { callback_(); }

    void restart(Timestamp now);          

    bool repeat() const { return repeat_; }

    Timestamp expiration() const { return expiration_; }

    int64_t sequence() const { return sequence_; }

private:
    const TimerCallback callback_;

    Timestamp expiration_;

    const double interval_;               

    const bool repeat_;

    const int64_t sequence_;    // 全局唯一序号：TimerId 靠它对抗"销毁后地址复用"

    static std::atomic<int64_t> s_sequenceCreator;
};

// 定时器句柄：Timer* + 全局唯一序号。
// 不用裸 Timer*：Timer 销毁后地址可被复用，cancel 时靠 sequence_ 确认"还是不是那个 Timer"。
class TimerId
{
public:
    TimerId() = default;
    TimerId(Timer* timer, int64_t sequence)
        : timer_(timer),
          sequence_(sequence)
    {
    }

private:
    Timer* timer_ = nullptr;
    int64_t sequence_ = 0;
};