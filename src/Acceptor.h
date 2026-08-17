#pragma once

#include <functional>

#include "Channel.h"
#include "Socket.h"

class EventLoop;
class InetAddress;

// ---- Acceptor：监听 + accept，新连接上抛 ----
// 职责单一：只管 listen fd。accept 到的 connfd 与对端地址通过回调交给 TcpServer，
// TcpServer 决定建 TcpConnection 并分配 subLoop。改线程池分配策略时只动 TcpServer。
class Acceptor
{
public:
    using NewConnectionCallback = std::function<void(int connfd, const InetAddress& peerAddr)>;

    Acceptor(EventLoop* loop, const InetAddress& listenAddr, bool reuseport = false);
    ~Acceptor();

    Acceptor(const Acceptor&) = delete;
    Acceptor& operator=(const Acceptor&) = delete;

    void setNewConnectionCallback(NewConnectionCallback cb) { newConnectionCallback_ = std::move(cb); }
    bool listening() const { return listening_; }
    void listen();   // TcpServer::start 时调用：真正开始监听（此前只 bind，不挂 EPOLLIN）

private:
    void handleRead();   // listen fd 可读 → accept 到 EAGAIN

    EventLoop* loop_;
    Socket acceptSocket_;        // listen fd（RAII，析构自动 close）
    Channel acceptChannel_;      // 注册进 mainLoop 的 poller，关注 EPOLLIN
    NewConnectionCallback newConnectionCallback_;
    bool listening_ = false;
};
