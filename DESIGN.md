# 项目二 · 仿muduo网络库 设计文档（DESIGN.md）

> **日期**：2026/08/04
> **阶段**：Day 3 — 文档重构（对齐文件 ↔ 实现顺序 ↔ 设计章节）
> **前置**：`day01-v1-vs-muduo.md`（v1 vs muduo 差异对照 + 重构清单）
> **协作模式**：架构师写文档，开发自主实现。本文件 = v2 实现蓝本。
> **用法**：照 §三 总表从第 1 步做起，每步对应的设计章节已标注；填完一步构建验证一步。

---

## 一、项目定位与设计主线

仿 muduo 的高性能 Reactor 网络库，采用 **one loop per thread + 一切皆 fd + 无锁事件循环** 线程模型。

一句话贯穿全库：

> **每线程至多一个 EventLoop；所有可等待的东西（socket / eventfd / timerfd）都是 fd，注册进同一个 epoll；数据只属于它所在的 loop 线程，跨线程访问靠"任务投递 + 唤醒"，不靠锁。**

v2 相对 v1 只改事件循环核心（EventLoop/Poller/Channel/TimerQueue），业务层（TcpServer 等）后续在 v2 核心之上重建。

---

## 二、决策记录（ADR）

| 编号 | 决策 | 选择 | 理由 |
|------|------|------|------|
| D1 | 文件组织 | **`.h` 声明 + `.cc` 实现分离** | 编译只重编改动的 .cc；符号可见性可控；工程形态贴近生产；面试可讲"为什么拆" |
| D2 | 命名风格 | **全库统一 muduo 小驼峰** | 逐行对照 muduo 零转换成本；面试官看代码像看 muduo |
| D3 | TimerQueue | **`std::set<Timer>` + timerfd** | v1 时间轮精度 1s 是已确认缺陷；set 任意精度；网络库定时任务量小，O(log n) 无压力；可逐行对照 muduo |
| D4 | 唤醒条件 | **三条件**（含 `eventHandling_` 防御保险） | 封死"可能错过立即处理"的窗口，宁可多唤醒不挂起 |
| D5 | TLS 关键字 | `thread_local`（C++11） | `__thread` 是 GCC 扩展，thread_local 全平台标准 |

> D1/D2/D3 为 2026/08/04 用户拍板。D4/D5 沿袭 Day 1 结论。

---

## 三、文件结构 ↔ 实现顺序 总表（唯一权威路径）

> **src/ 下每个文件都对应一个实现步号和一个设计章节。照表做，不迷路。**
> 依赖 = 该步编译前必须已存在的前置模块。

| 步 | 文件 | 模块 | 设计章节 | 依赖 | 产出/验证 |
|----|------|------|---------|------|-----------|
| 1 | `src/Timestamp.h` | 时间戳（工具） | §四·4.1 | 无 | 独立可编 |
| 2 | `src/Timer.h` / `.cc` | 定时器条目 | §四·4.2 | Timestamp | 独立可编 |
| 3 | `src/Poller.h` + `src/EPollPoller.h`/`.cc` | epoll 封装 | §四·4.3 / 4.4 | Channel（前向声明） | 独立可编 |
| 4 | `src/Channel.h` / `.cc` | 事件分发 | §四·4.5 | EventLoop（前向声明） | 独立可编 |
| 5 | `src/EventLoop.h` / `.cc` | 事件循环核心 ★ | **§五** | Poller / Channel | 编出 libnet.a |
| 6 | `src/TimerQueue.h` / `.cc` | 定时器队列 ★ | **§六** | Timer / Timestamp / EventLoop | 编出 libnet.a |
| 7 | `demo_echo.cpp`（根目录） | 冒烟测试 | §七·7.1 | net 库 | 跑通 echo |
| 8 | — | 多线程验证 | §七·7.2 | net 库 | 三条件正确 |
| 9 | — | 压测对比 | §七·7.3 | net 库 | QPS 数据 |

**目录结构**（文件框架已全部就位）：

```
仿muduo网络库/
├── CMakeLists.txt          ← 构建脚本（src/ 收库，根目录收 demo）
├── DESIGN.md
├── .gitignore
├── demo_*.cpp              ← 演示入口（用户写 demo 时自建）
└── src/                    ← 核心库（编成 libnet.a）
    ├── Timestamp.h         （步1）
    ├── Timer.h/.cc         （步2）
    ├── Poller.h            （步3）
    ├── EPollPoller.h/.cc   （步3）
    ├── Channel.h/.cc       （步4）
    ├── EventLoop.h/.cc     （步5）★
    ├── TimerQueue.h/.cc    （步6）★
    ├── Socket.h/.cc        ← 业务层，v2 核心完成后重建
    ├── Acceptor.h/.cc
    ├── InetAddress.h/.cc
    ├── TcpConnection.h/.cc
    └── TcpServer.h/.cc
```

**依赖方向**（单向，低层不反向依赖）：

```
Timestamp ──► Timer ──► TimerQueue ──► EventLoop
    ▲                      │              │
    └──────────────────────┘              │
Channel ──► Poller/EPollPoller ──► (epoll_wait)
    ▲                              
    └── EventLoop 持有并分发 Channel
```

---

## 四、前置模块设计（实现顺序 1–4）

### 4.1 步1：Timestamp.h（header-only）

**需求**：全局统一的时间类型，供 TimerQueue 排序、EventLoop 记 poll 返回时刻。用 `int64_t` 微秒存绝对时间（整数精确，无浮点误差）。

```cpp
// Timestamp.h
#include <stdint.h>
#include <time.h>

class Timestamp
{
public:
    static const int kMicroSecondsPerSecond = 1000 * 1000;

    Timestamp();                                    // 无效时间戳（0）
    explicit Timestamp(int64_t microSecondsSinceEpoch);
    static Timestamp now();                         // std::chrono 取当前
    static Timestamp invalid();

    int64_t microSecondsSinceEpoch() const { return microSecondsSinceEpoch_; }
    time_t secondsSinceEpoch() const;               // 整秒，供 C 时间函数（localtime 等）
    bool valid() const { return microSecondsSinceEpoch_ > 0; }
    void swap(Timestamp& that);

private:
    int64_t microSecondsSinceEpoch_;
};

// 全 inline，供全局使用
inline bool operator<(Timestamp lhs, Timestamp rhs);
inline bool operator==(Timestamp lhs, Timestamp rhs);
inline double timeDifference(Timestamp high, Timestamp low);     // 秒差（double，保留亚秒）
inline Timestamp addTime(Timestamp timestamp, double seconds);
```

**实现要点**：
- `now()`：`std::chrono::system_clock::now().time_since_epoch()` 转微秒
- **类型分工（对齐 muduo）**：`secondsSinceEpoch()` 返回 `time_t` 整秒（喂给 `localtime`/`gmtime` 等 C 时间函数，它们只收整秒）；`timeDifference()` 返回 `double` 秒差（内部 `diff / kMicroSecondsPerSecond`，保留亚秒）；`addTime()` 参数为 `double` 秒（外部传 `0.3` 秒这种小数）
- 比较/加减都转成微秒 int64 运算，避免浮点误差
- 为什么 header-only：全是 3 行小函数，放 .cc 反而要加链接

---

### 4.2 步2：Timer.h/.cc（定时器条目）

**需求**：一条"到期执行"的定时记录，是 TimerQueue 集合的元素。只有数据 + 执行回调，**不含任何队列逻辑**。

```cpp
// Timer.h
#include <functional>
#include <stdint.h>
#include "Timestamp.h"

class Timer
{
public:
    using TimerCallback = std::function<void()>;

    Timer(TimerCallback cb, Timestamp when, double interval);
    void run() const { callback_(); }
    void restart(Timestamp now);          // 重复定时器：expiration_ += interval
    bool repeat() const { return repeat_; }
    Timestamp expiration() const { return expiration_; }
    int64_t sequence() const { return sequence_; }

private:
    const TimerCallback callback_;
    Timestamp expiration_;
    const double interval_;               // 0 = 一次性；>0 = 重复间隔（秒）
    const bool repeat_;
    const int64_t sequence_;              // 递增序号，解决"同一时刻多个定时器"排序歧义
};
```

**实现要点**：
- `sequence_` 来自全局递增计数器：`static std::atomic<int64_t> s_sequenceCreator`，构造时 `s_sequenceCreator.fetch_add(1)`——给每个 Timer 发一个**永不重复的全局唯一序号**
- **它解决"地址复用"问题，不是 set 排序**：muduo 的 `TimerId = Timer* + sequence_`，cancel 时用 sequence_ 校验。Timer 销毁后 `new Timer` 可能拿到同一地址，单靠指针分不清"是不是原来那个"；sequence_ 永不复用，能可靠标识
- **set 排序的平局怎么破（muduo 做法）**：`Entry = pair<Timestamp, Timer*>` 字典序——先比到期时间，相同再比 `Timer*` 地址（同一时刻存活的 Timer 地址必然不同，可行）。排序不靠 sequence_
- `restart(now)`：`expiration_ = addTime(now, interval_)`，仅重复定时器调用

---

### 4.3 步3：Poller.h（抽象接口）

**需求**：epoll 的抽象壳。让 EventLoop 只依赖 `Poller` 接口，不感知具体实现——将来换 poll/select/io_uring 不动 EventLoop。

```cpp
// Poller.h
#include <vector>
#include "Timestamp.h"

class Channel;
class EventLoop;

class Poller
{
public:
    using ChannelList = std::vector<Channel*>;

    explicit Poller(EventLoop* loop) : ownerLoop_(loop) {}
    virtual ~Poller() = default;

    virtual Timestamp poll(int timeoutMs, ChannelList* activeChannels) = 0;
    virtual void updateChannel(Channel* channel) = 0;
    virtual void removeChannel(Channel* channel) = 0;
    virtual bool hasChannel(Channel* channel) const = 0;

    static Poller* newDefaultPoller(EventLoop* loop);   // 工厂：返回 EPollPoller

protected:
    EventLoop* ownerLoop_;
};
```

**实现要点**：
- 工厂 `newDefaultPoller`：`return new EPollPoller(loop);`，EventLoop 构造时用它创建 poller_，实现可替换

---

### 4.4 步3：EPollPoller.h/.cc（epoll 实现）

**需求**：epoll 的封装。维护 `fd → Channel` 映射；`updateChannel` 做 ADD/MOD，`removeChannel` 做 DEL；`poll` 等事件并回填 Channel 的 revents。

```cpp
// EPollPoller.h
#include <sys/epoll.h>
#include <unordered_map>
#include <vector>
#include "Poller.h"

class EventLoop;

class EPollPoller : public Poller
{
public:
    explicit EPollPoller(EventLoop* loop);
    ~EPollPoller() override;

    Timestamp poll(int timeoutMs, ChannelList* activeChannels) override;
    void updateChannel(Channel* channel) override;
    void removeChannel(Channel* channel) override;
    bool hasChannel(Channel* channel) const override;

private:
    static const int kInitEventListSize = 16;

    int epollfd_;
    std::vector<struct epoll_event> events_;      // epoll_wait 输出缓冲
    using ChannelMap = std::unordered_map<int, Channel*>;
    ChannelMap channels_;                         // fd → Channel

    void update(int operation, Channel* channel);
    void fillActiveChannels(int numEvents, ChannelList* activeChannels) const;
};
```

**关键流程**：

```cpp
// poll：等事件 → 回填 revents → 返回当前时间
Timestamp EPollPoller::poll(int timeoutMs, ChannelList* activeChannels)
{
    int numEvents = ::epoll_wait(epollfd_, &*events_.begin(),
                                 static_cast<int>(events_.size()), timeoutMs);
    Timestamp now = Timestamp::now();
    if (numEvents > 0)
    {
        fillActiveChannels(numEvents, activeChannels);
        if (static_cast<size_t>(numEvents) == events_.size())
            events_.resize(events_.size() * 2);   // 满频 → 扩容
    }
    return now;
}

// updateChannel：已注册 MOD，未注册 ADD
void EPollPoller::updateChannel(Channel* channel)
{
    ChannelMap::iterator it = channels_.find(channel->fd());
    if (it == channels_.end())
    {
        channels_[channel->fd()] = channel;
        update(EPOLL_CTL_ADD, channel);
    }
    else
    {
        update(EPOLL_CTL_MOD, channel);
    }
}

// fillActiveChannels：epoll 返回的每个事件 → 找到 Channel → 设 revents
void EPollPoller::fillActiveChannels(int numEvents, ChannelList* activeChannels) const
{
    for (int i = 0; i < numEvents; ++i)
    {
        Channel* channel = static_cast<Channel*>(events_[i].data.ptr);
        channel->setRevents(events_[i].events);
        activeChannels->push_back(channel);
    }
}
```

**实现要点**：
- **关键技巧：注册时 `events_[i].data.ptr = channel`**——epoll_event 的 data 联合体直接存 Channel*，epoll_wait 返回零查找成本拿到 Channel
- 构造：`epollfd_ = ::epoll_create1(EPOLL_CLOEXEC)`，`events_.resize(kInitEventListSize)`
- 析构：`::close(epollfd_)`
- `update(op, channel)`：填 `epoll_event{ .events = channel->events(), .data = { .ptr = channel } }` 后 `::epoll_ctl`

---

### 4.5 步4：Channel.h/.cc（事件分发）

**需求**：一个 fd + 它关注的事件（events）+ 就绪事件（revents）+ 回调集合。是"一切皆 fd"模型的通用载体——socket / eventfd / timerfd 都用 Channel 挂进 epoll。

```cpp
// Channel.h
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
    void setReadCallback(ReadEventCallback cb)  { readCallback_  = std::move(cb); }
    void setWriteCallback(EventCallback cb)     { writeCallback_ = std::move(cb); }
    void setCloseCallback(EventCallback cb)     { closeCallback_ = std::move(cb); }
    void setErrorCallback(EventCallback cb)     { errorCallback_ = std::move(cb); }

    // 关注事件开关（改 events_ 并通知 poller）
    void enableReading();
    void disableReading();
    void enableWriting();
    void disableWriting();
    void disableAll();
    bool isNoneEvent() const { return events_ == kNoneEvent; }

    // 供 poller 填/读
    int fd() const { return fd_; }
    int events() const { return events_; }
    void setRevents(int revents) { revents_ = revents; }
    int index() const { return index_; }
    void setIndex(int index) { index_ = index; }
    EventLoop* ownerLoop() const { return loop_; }

    void remove();                                // disableAll + ownerLoop_->removeChannel(this)

private:
    static const int kNoneEvent  = 0;
    static const int kReadEvent  = EPOLLIN | EPOLLPRI;
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
```

**关键流程**（handleEvent——就绪事件的分发逻辑，面试重点）：

```cpp
void Channel::handleEvent(Timestamp receiveTime)
{
    eventHandling_ = true;
    if ((revents_ & EPOLLHUP) && !(revents_ & EPOLLIN))
    {
        if (closeCallback_) closeCallback_();          // 对端关闭
    }
    if (revents_ & (EPOLLERR | EPOLLNVAL))
    {
        if (errorCallback_) errorCallback_();           // 错误
    }
    if (revents_ & (EPOLLIN | EPOLLPRI | EPOLLRDHUP))
    {
        if (readCallback_) readCallback_(receiveTime);  // 可读（含对端半关闭）
    }
    if (revents_ & EPOLLOUT)
    {
        if (writeCallback_) writeCallback_();           // 可写
    }
    eventHandling_ = false;
}
```

**实现要点**：
- `enableReading()`：`events_ |= kReadEvent; update();`；`disableAll()`：`events_ = kNoneEvent; update();`
- `update()` 转发给 `loop_->updateChannel(this)`——Channel 自己不碰 epoll，统一由 EventLoop 走 Poller，保持"事件注册只此一条路"
- `index_` 的 `kNew/kAdded/kDeleted` 是 Poller 内部状态标记，EPollPoller 用它判断 ADD vs MOD

---

## 五、EventLoop 详细设计（步5）★

### 5.1 职责与需求

一个线程一个事件循环，负责：

1. 运行主事件循环（epoll_wait → 分发事件 → 执行任务）
2. 提供任务投递接口（runInLoop / queueInLoop），支持跨线程安全调用
3. 线程归属自证（TLS 登记），违规调用响亮失败（FATAL）
4. 自身可被唤醒（eventfd），可被安全退出（quit）
5. 挂载 TimerQueue，定时器并入统一事件模型

### 5.2 线程模型：TLS 生命周期

```cpp
// EventLoop.cc 匿名 namespace 内
namespace
{
    thread_local EventLoop* t_loopInThisThread = nullptr;   // 每线程一份
}
```

**生命周期契约**（四个钩子，缺一不可）：

| 时机 | 动作 | 检查 |
|------|------|------|
| 构造 | `t_loopInThisThread` 非空 → `LOG_FATAL`（该线程已有 loop）；否则登记自己 | 拒绝每线程第二个 loop |
| 析构 | 断言自己仍是本线程指针；然后置空 | 拒绝"别的线程析构我的 loop" |
| `loop()` 开头 | `assertInLoopThread()` | loop 必须在构造线程运行 |
| `getEventLoopOfCurrentThread()` | 直接返回 TLS 指针 | 别的线程查"当前线程的 loop" |

**为什么 `threadId_` 是 `const`**（修 v1 归属漂移）：

```cpp
const std::thread::id threadId_;   // 构造时固化，之后任何代码都改不了
```

v1 的 `Loop()` 里 `tid_ = get_id()` 重设 owner，导致 loop 可在非构造线程运行、`IsInLoopThread` 语义错乱。`const` 成员从语言层面封死。

**归属检查三件套**：

```cpp
bool isInLoopThread() const { return threadId_ == std::this_thread::get_id(); }

void assertInLoopThread() const
{
    if (!isInLoopThread())
        abortNotInLoopThread();    // LOG_FATAL + abort()
}
```

### 5.3 公开 API 签名（v2 完整）

```cpp
// EventLoop.h
#pragma once
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

class Channel;
class Poller;
class TimerQueue;
class Timestamp;
class TimerId;

class EventLoop
{
public:
    using Functor = std::function<void()>;
    using TimerCallback = std::function<void()>;   // 定时器回调（与 Timer 同款签名）

    EventLoop();
    ~EventLoop();
    EventLoop(const EventLoop&)            = delete;
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

    // ---- 内部使用 ----
    Timestamp pollReturnTime() const { return pollReturnTime_; }

private:
    // 私有嵌套守卫类：异常安全置/复位 callingPendingFunctors_
    class PendingFunctorGuard
    {
    public:
        explicit PendingFunctorGuard(EventLoop* loop);
        ~PendingFunctorGuard();
        PendingFunctorGuard(const PendingFunctorGuard&)            = delete;
        PendingFunctorGuard& operator=(const PendingFunctorGuard&) = delete;
    private:
        EventLoop* loop_;
    };

    void abortNotInLoopThread();
    void handleRead();                  // eventfd 到期：read 8 字节清零
    void doPendingFunctors();           // swap 快照 + PendingFunctorGuard

    using ChannelList = std::vector<Channel*>;

    bool looping_                = false;
    bool quit_                   = false;
    bool eventHandling_          = false;   // 条件3：正在分发本批事件
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
```

> `std::mutex` + `std::lock_guard` 即可，v2 不引入自旋锁。

### 5.4 关键流程

**① 主循环 `loop()`**

```cpp
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

        doPendingFunctors();                                // 批量执行投递任务
    }
    looping_ = false;
}
```

> 顺序刻意固定：**先分发网络事件，再执行 pending functors**。保证"入队延迟到本轮末"的语义可预期。

**② 任务投递 `runInLoop` / `queueInLoop`**

```cpp
void EventLoop::runInLoop(Functor cb)
{
    if (isInLoopThread())
        cb();                            // 直接同步执行：无锁、零成本，但可能重入
    else
        queueInLoop(std::move(cb));      // 别的线程 → 入队等 loop 处理
}

void EventLoop::queueInLoop(Functor cb)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pendingFunctors_.push_back(std::move(cb));
    }
    if (!isInLoopThread() || callingPendingFunctors_ || eventHandling_)
        wakeup();                        // 三条件唤醒（D4）
}
```

**③ 唤醒三条件**（D4，每个都封死一个"可能错过立即处理"的窗口）：

| 条件 | 触发时机 | 为什么必须唤醒 |
|------|---------|----------------|
| `!isInLoopThread()` | 别的线程入队 | loop 大概率在 `poll()` 里睡死，不唤醒要等超时 |
| `callingPendingFunctors_` | 正在执行 doPendingFunctors | 当前批已 swap 走，新入队在**下一批**，必须给下一轮设闹钟 |
| `eventHandling_` | 正在分发本批事件（事件回调中途） | 防御性保险：本批剩余事件处理时间不可控 |

> **对齐说明**：muduo 现代版本只有前两条件；条件 3 我们保留为防御保险（等价于 muduo 早期 `poller_->hasPendingEvents()`，用 `eventHandling_` 标志实现更直接、不依赖 Poller 状态）。面试被问"muduo 为什么只有两条件"时，答"条件 2 已覆盖核心挂死路径，条件 3 是防御性的取舍"。

> ⚠️ **易混点**：`activeChannels_`（本轮 epoll 返回待分发的事件）与 `pendingFunctors_`（软件任务队列）是**两个队列**。入队 cb 不构成"未处理事件"。条件 3 只在「站在事件回调里且本轮还没处理完」时为 true。

**④ 批量执行 `doPendingFunctors`（swap 快照 + RAII）**

```cpp
void EventLoop::doPendingFunctors()
{
    std::vector<Functor> functors;
    PendingFunctorGuard guard(this);        // ▲ 构造置位 / 析构复位，异常也复位
    {
        std::lock_guard<std::mutex> lock(mutex_);
        functors.swap(pendingFunctors_);    // 锁只在这一瞬
    }
    for (const Functor& functor : functors)
        functor();                          // 执行阶段完全无锁
}
```

两个好处（缺一不可）：

1. **执行阶段无锁**——持锁时间从"整个执行"压到"swap 一瞬"。执行期间其他线程可随时入队不被阻塞；functor 里调 EventLoop 接口不会死锁。
2. **快照语义**——只执行入队时刻已有的一批；执行期间新入队的留到下一轮。防重入、防无限循环（若直接遍历，functor 每跑一次产出新任务，for 永远跑不完）。

`PendingFunctorGuard` 的职责：`callingPendingFunctors_` 置位/复位**不许依赖手动写对每个分支**。functor 抛异常 → 栈展开 → guard 析构必然执行 → 标志复位。没有它：标志卡 true → 之后每次 queueInLoop 都白唤醒 → 慢性性能退化。

```cpp
// PendingFunctorGuard 实现（私有嵌套类）
class EventLoop::PendingFunctorGuard
{
public:
    explicit PendingFunctorGuard(EventLoop* loop) : loop_(loop)
    {
        loop_->callingPendingFunctors_ = true;
    }
    ~PendingFunctorGuard()
    {
        loop_->callingPendingFunctors_ = false;   // 任何退出路径（含异常展开）都会执行
    }
private:
    EventLoop* loop_;
};
```

**⑤ 唤醒 `wakeup()` / `handleRead()`**

```cpp
void EventLoop::wakeup()
{
    uint64_t one = 1;
    ssize_t n = write(wakeupFd_, &one, sizeof(one));   // eventfd 永不阻塞
    (void)n;
}

void EventLoop::handleRead()
{
    uint64_t one = 0;
    ssize_t n = read(wakeupFd_, &one, sizeof(one));    // 一次取走计数，清零
    (void)n;
}
```

**⑥ 退出 `quit()`**

```cpp
void EventLoop::quit()
{
    quit_ = true;
    if (!isInLoopThread())
        wakeup();        // 别的线程叫停：把睡死的 poll() 唤醒
}
```

### 5.5 成员线程安全属性一览

| 成员 | 归属线程 | 访问保护 | 说明 |
|------|---------|---------|------|
| `threadId_` | 构造线程 | const 只读 | 全线程可安全读 |
| `looping_` / `quit_` / `eventHandling_` / `callingPendingFunctors_` | loop 线程 | 仅 loop 线程写 | 其他线程**不碰** |
| `poller_` / `timerQueue_` | loop 线程 | 仅 loop 线程 | 回调里可能查，全在 loop 内 |
| `wakeupFd_` | 任意线程写，loop 线程读 | write/read 原子 | eventfd 内核计数器 |
| `pendingFunctors_` | 任意线程写，loop 线程 swap 消费 | `mutex_` | 唯一需要锁的数据 |

> **无锁的根基**：不是靠锁，是靠 one loop per thread——单线程访问自己的数据没有竞态，锁的唯一作用是挡来自其他线程的错误访问。`isInLoopThread()` 一行线程 ID 比较，换整段代码不需要锁。

---

## 六、TimerQueue 详细设计（步6）★

### 6.1 职责与需求

- 支持"一次性/重复"定时器，任意精度（毫秒级）
- 定时器并入 epoll：`timerfd` 到期 = fd 可读，统一事件模型
- **全部操作在 loop 线程内**，`timers_` 集合无锁
- 免开定时器线程：内核（timerfd 超时）+ epoll（就绪通知）接管"睡到点→叫醒→执行"全流程

> 依赖 Timer 类（§4.2）。

### 6.2 数据结构

```cpp
// TimerQueue.h
class TimerQueue
{
public:
    using TimerCallback = std::function<void()>;

    explicit TimerQueue(EventLoop* loop);
    ~TimerQueue();                        // 析构时在 loop 线程，释放所有 Timer*

    TimerId addTimer(TimerCallback cb, Timestamp when, double interval);

private:
    using Entry = std::pair<Timestamp, Timer*>;   // 字典序：先比到期时间，相同再比 Timer* 地址
    using TimerList = std::set<Entry>;

    void addTimerInLoop(Timer* timer);              // 增删改查全在 loop 线程
    void handleRead();                              // timerfd 到期回调
    std::vector<Entry> getExpired(Timestamp now);   // 取所有到期
    void reset(const std::vector<Entry>& expired, Timestamp now);  // 重复定时器重排

    EventLoop* loop_;
    const int timerfd_;                     // timerfd_create(CLOCK_MONOTONIC, ...)
    Channel timerfdChannel_;                // 注册进 loop 的 poller
    TimerList timers_;                      // std::set，仅 loop 线程访问
    // cancelTimer() v2 第一版可省略（记 TODO）
};
```

> `TimerId` = `std::pair<Timer*, int64_t>`（Timer 指针 + 全局唯一序号）。`addTimer` 返回它作为句柄；v2 第一版不做 cancel，但类型先定型。**sequence_ 的用途**：Timer 销毁后地址可被复用，cancel 时靠 `sequence_` 确认"还是不是那个 Timer"，防止误杀地址相同的新 Timer。

### 6.3 关键流程

**addTimer（跨线程安全）**：

```cpp
TimerId TimerQueue::addTimer(TimerCallback cb, Timestamp when, double interval)
{
    Timer* timer = new Timer(std::move(cb), when, interval);
    loop_->runInLoop(std::bind(&TimerQueue::addTimerInLoop, this, timer));
    return TimerId(timer, timer->sequence());   // 返回句柄，指向堆上 Timer
}
```

**addTimerInLoop（loop 线程内）**：

```cpp
void TimerQueue::addTimerInLoop(Timer* timer)
{
    bool earliestChanged = insert(timer);        // 插入 set，返回"是否成为最早到期"
    if (earliestChanged)
        resetTimerfd(timer->expiration());       // timerfd_settime 重设到期
}
```

**timerfd 到期 → handleRead**：

```cpp
void TimerQueue::handleRead()
{
    Timestamp now(Timestamp::now());
    uint64_t expirations = 0;
    read(timerfd_, &expirations, sizeof(expirations));   // 清计数（忽略值）
    std::vector<Entry> expired = getExpired(now);        // 取所有到期 Timer*
    for (const Entry& entry : expired)
        entry.second->run();                              // 执行回调
    reset(expired, now);                                  // 重复定时器重排，若重排成最早则重设 timerfd
}
```

**reset 后必须重新检查最早到期**：重复定时器重排进 set 后，可能早于当前 timerfd 的到期时间，需要 `timerfd_settime` 更新。

### 6.4 为什么 set 而非时间轮（面试点）

| | v1 时间轮 | v2 `std::set` |
|---|---|---|
| 插入/删除 | O(1) | O(log n) |
| 精度 | 受轮大小限制（v1 为 1s） | 任意精度 |
| 适用场景 | 海量定时任务（内核级，百万级） | 网络库少量定时（几十上百个） |

**按需选结构**：网络库定时器数量小，O(log n) 毫无压力；set 免去"轮大小定多少"的精度纠结；且 `std::set` 按到期时间天然有序，`getExpired`（lower_bound 取前驱）实现直接。时间轮是"海量任务"的答案，不是"网络库"的答案。

---

## 七、验证清单（步7–9）

### 7.1 冒烟测试（步7，demo_echo.cpp）

单线程 echo：一个 EventLoop + 一个 listen socket 的 Channel。先跑通：

- [ ] `runInLoop` / `queueInLoop` 路径正常（loop 线程内直接执行、入队延迟执行）
- [ ] `quit()` 从其他线程调用 → loop 立即退出
- [ ] 单 Reactor echo 多客户端并发正常

### 7.2 多线程验证（步8）

- [ ] 同一线程 `new EventLoop` 两次 → FATAL 崩（响亮失败）
- [ ] 别的线程 `loop()` → assert 崩
- [ ] 两个线程各自建 loop 互投 `queueInLoop`，10 万次无丢、无乱序（快照语义）
- [ ] 在 `doPendingFunctors` 执行的 functor 里再 `queueInLoop` → 任务下一轮执行（不挂死，条件2 生效）
- [ ] 在事件回调里 `queueInLoop` → 本轮末执行（条件3 兜底）
- [ ] 一个 300ms 重复定时器跑 20 次，抖动记录在案（TimerQueue 任意精度验证）
- [ ] eventfd 唤醒 busy loop 检测：空闲时 CPU 占用趋近 0

### 7.3 压测对比（步9，v2 vs v1）

| 指标 | v1 | v2 | 目标 |
|------|-----|-----|------|
| QPS（wrk，同参数） | 记录基线 | 记录 | 不低于 v1 |
| 空转 CPU（无连接 idle） | 记录 | 记录 | 趋近 0% |
| 唤醒次数（可加计数器临时打点） | — | 记录 | 三条件不过度唤醒 |

---

## 八、面试可能追问（落地后自检）

1. "为什么 `thread_local` 指针而不是对象？" → 至多一个 + 生命周期可控 + FATAL 检查点
2. "eventfd 和 pipe 区别？" → write 永不阻塞 + 计数清零 + 信号聚合
3. "queueInLoop 为什么三条件唤醒？" → 封死"可能错过立即处理"的窗口
4. "doPendingFunctors 为什么 swap？" → 无锁执行 + 快照语义（防重入/防无限循环）
5. "为什么不开定时器线程？" → timerfd + epoll 接管，保住 one loop per thread
6. "无锁的根基是什么？" → 单线程访问自己的数据，靠线程模型不靠锁
7. "set vs 时间轮？" → 按需选结构，网络库少量定时用 set
8. "Channel 的 events 和 revents 谁填？" → user 设 events，poller 回填 revents
9. "为什么 epoll_event.data 存 Channel*？" → epoll_wait 返回零查找成本拿到对象
10. "为什么 Channel 不直接碰 epoll？" → 统一走 EventLoop→Poller，事件注册只此一条路
