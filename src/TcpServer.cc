#include "TcpServer.h"

#include <cstdio>

#include "EventLoop.h"

TcpServer::TcpServer(EventLoop* loop, const InetAddress& listenAddr, const std::string& name)
    : loop_(loop),
      ipPort_(listenAddr.toIpPort()),
      name_(name),
      acceptor_(new Acceptor(loop, listenAddr)),
      threadPool_(new EventLoopThreadPool(loop))
{
    acceptor_->setNewConnectionCallback(std::bind(&TcpServer::newConnection, this,
                                                  std::placeholders::_1, std::placeholders::_2));
}

TcpServer::~TcpServer()
{
    // 残留连接显式拆除（connectDestroyed 会摘除 channel，避免析构 abort）。
    // 先把 map 里持有的引用清空，由局部 conn 保活到 connectDestroyed 执行完——
    // 否则 map 析构可能先于异步的 connectDestroyed，TcpConnection 析构时 channel 仍注册。
    // 顺序保证：入队后才析构 threadPool_，subLoop 线程此刻仍存活。
    for (auto& kv : connections_)
    {
        TcpConnectionPtr conn = kv.second;
        kv.second.reset();
        conn->getLoop()->runInLoop(std::bind(&TcpConnection::connectDestroyed, conn));
    }
}

void TcpServer::setThreadNum(int min)
{
    threadPool_->setThreadNum(min);
}

void TcpServer::setMaxThreadNum(int max)
{
    threadPool_->setMaxThreadNum(max);
}

void TcpServer::start()
{
    if (started_)
    {
        return;
    }
    started_ = true;
    threadPool_->start();
    if (!acceptor_->listening())
    {
        loop_->runInLoop(std::bind(&Acceptor::listen, acceptor_.get()));
    }
}

void TcpServer::newConnection(int sockfd, const InetAddress& peerAddr)
{
    loop_->assertInLoopThread();
    EventLoop* ioLoop = threadPool_->getNextLoop();   // 分配 subLoop（内部含扩容判断）
    ++nextConnId_;
    char buf[64];
    snprintf(buf, sizeof buf, "-%s#%d", ipPort_.c_str(), nextConnId_);
    std::string connName = name_ + buf;

    InetAddress localAddr(Socket::getLocalAddr(sockfd));
    TcpConnectionPtr conn(new TcpConnection(ioLoop, connName, sockfd, localAddr, peerAddr));
    connections_[connName] = conn;

    conn->setConnectionCallback(connectionCallback_);
    conn->setMessageCallback(messageCallback_);
    conn->setWriteCompleteCallback(writeCompleteCallback_);
    conn->setCloseCallback(std::bind(&TcpServer::removeConnection, this, std::placeholders::_1));
    threadPool_->registerConnection(ioLoop);   // 上报线程池：该 loop 连接 +1
    ioLoop->runInLoop(std::bind(&TcpConnection::connectEstablished, conn));
}

void TcpServer::removeConnection(const TcpConnectionPtr& conn)
{
    // 跨线程安全：转发到 mainLoop 统一操作连接表
    loop_->runInLoop(std::bind(&TcpServer::removeConnectionInLoop, this, conn));
}

void TcpServer::removeConnectionInLoop(const TcpConnectionPtr& conn)
{
    loop_->assertInLoopThread();
    connections_.erase(conn->name());   // 幂等：重复关闭时不存在也不报错
    threadPool_->unregisterConnection(conn->getLoop());   // 上报线程池：连接 -1（已防御负数）
    EventLoop* ioLoop = conn->getLoop();
    // 无论 map 中是否已移除，都调度 connectDestroyed：
    // 它负责在 ioLoop 线程摘除 channel，并持有 conn 直到摘除之后——
    // 否则 conn 可能被 mainLoop 的 functor 在错误线程释放，Channel 析构跨线程 abort。
    ioLoop->queueInLoop(std::bind(&TcpConnection::connectDestroyed, conn));
}
