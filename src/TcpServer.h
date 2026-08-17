#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>

#include "Acceptor.h"
#include "EventLoopThreadPool.h"
#include "TcpConnection.h"

class EventLoop;
class InetAddress;

// TcpConnectionPtr 是 TcpConnection 的类内别名，提到全局方便 TcpServer 直接引用
using TcpConnectionPtr = TcpConnection::TcpConnectionPtr;

// ---- TcpServer：组织者，不写业务 ----
//
// 持有 mainLoop + 线程池：accept 新连接 → 分配 subLoop → 建 TcpConnection → 注册。
// 用户只关心三个回调（连接建立/断开、消息、写完成），连接管理细节全在这里。
class TcpServer
{
public:
    using ConnectionCallback = TcpConnection::ConnectionCallback;
    using MessageCallback = TcpConnection::MessageCallback;
    using WriteCompleteCallback = TcpConnection::WriteCompleteCallback;

    TcpServer(EventLoop* loop, const InetAddress& listenAddr, const std::string& name);
    ~TcpServer();

    TcpServer(const TcpServer&) = delete;
    TcpServer& operator=(const TcpServer&) = delete;

    void setConnectionCallback(ConnectionCallback cb) { connectionCallback_ = std::move(cb); }
    void setMessageCallback(MessageCallback cb) { messageCallback_ = std::move(cb); }
    void setWriteCompleteCallback(WriteCompleteCallback cb) { writeCompleteCallback_ = std::move(cb); }

    void setThreadNum(int min);      // 最少活跃线程数（0 → 纯单线程）
    void setMaxThreadNum(int max);   // 动态上限

    EventLoop* getLoop() const { return loop_; }
    const std::string& name() const { return name_; }
    const std::string& ipPort() const { return ipPort_; }

    void start();   // 启动线程池 + 开始监听

private:
    void newConnection(int sockfd, const InetAddress& peerAddr);
    void removeConnection(const TcpConnectionPtr& conn);
    void removeConnectionInLoop(const TcpConnectionPtr& conn);

    EventLoop* loop_;                        // mainLoop：只管 accept 与连接表
    const std::string ipPort_;
    const std::string name_;
    std::unique_ptr<Acceptor> acceptor_;     // 监听与 accept
    std::unique_ptr<EventLoopThreadPool> threadPool_;   // 连接分配 + 动态扩缩容

    ConnectionCallback connectionCallback_;
    MessageCallback messageCallback_;
    WriteCompleteCallback writeCompleteCallback_;

    bool started_ = false;
    int nextConnId_ = 0;
    std::map<std::string, TcpConnectionPtr> connections_;   // 所有活跃连接（shared_ptr 持有）
};
