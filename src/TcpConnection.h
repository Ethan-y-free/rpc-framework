#pragma once

#include <functional>
#include <memory>
#include <string>

#include "Buffer.h"
#include "Channel.h"
#include "InetAddress.h"
#include "Socket.h"
#include "Timestamp.h"

class EventLoop;

// ---- TcpConnection：一个连接，绑定一个 EventLoop ----
//
// 生命周期用 shared_ptr 管理：用户回调可能持有一份 ptr 延后使用（如 RPC 挂起请求），
// 必须保证「回调期间连接一定活着」。只有用户手里无引用时连接才真正析构。
// 数据流单线程化：所有操作都在 loop_ 线程内做，无需加锁；跨线程只走 runInLoop/queueInLoop。
class TcpConnection : public std::enable_shared_from_this<TcpConnection>
{
public:
    using TcpConnectionPtr = std::shared_ptr<TcpConnection>;
    using ConnectionCallback = std::function<void(const TcpConnectionPtr&)>;
    using MessageCallback = std::function<void(const TcpConnectionPtr&, const char* data,
                                               size_t len, Timestamp)>;
    using WriteCompleteCallback = std::function<void(const TcpConnectionPtr&)>;
    using CloseCallback = std::function<void(const TcpConnectionPtr&)>;   // 内部：通知 TcpServer 移除

    TcpConnection(EventLoop* loop, const std::string& name, int sockfd,
                  const InetAddress& localAddr, const InetAddress& peerAddr);
    ~TcpConnection();

    TcpConnection(const TcpConnection&) = delete;
    TcpConnection& operator=(const TcpConnection&) = delete;

    EventLoop* getLoop() const { return loop_; }
    const std::string& name() const { return name_; }
    const InetAddress& localAddress() const { return localAddr_; }
    const InetAddress& peerAddress() const { return peerAddr_; }
    bool connected() const { return state_ == kConnected; }

    void setConnectionCallback(ConnectionCallback cb) { connectionCallback_ = std::move(cb); }
    void setMessageCallback(MessageCallback cb) { messageCallback_ = std::move(cb); }
    void setWriteCompleteCallback(WriteCompleteCallback cb) { writeCompleteCallback_ = std::move(cb); }
    void setCloseCallback(CloseCallback cb) { closeCallback_ = std::move(cb); }

    void connectEstablished();   // 注册进 loop 后由 TcpServer 调用
    void connectDestroyed();     // 从 loop 移除时由 TcpServer 调用

    void send(const std::string& message);   // 线程安全：跨线程自动入队
    void shutdown();                          // 半关闭：发完缓冲后关写
    void forceClose();                        // 立即关闭（不保证发完）

    void setTcpNoDelay(bool on);

private:
    enum StateE { kConnecting, kConnected, kDisconnecting, kDisconnected };

    void setState(StateE s) { state_ = s; }
    void handleRead(Timestamp receiveTime);
    void handleWrite();
    void handleClose();
    void handleError();
    void sendInLoop(const std::string& message);
    void shutdownInLoop();
    void forceCloseInLoop();

    EventLoop* loop_;
    std::string name_;
    StateE state_;
    Channel channel_;    // 先声明、后析构：析构时 fd 已被 socket_ 关闭，epoll 自动清理
    Socket socket_;      // 后声明、先析构：先关 fd，杜绝残留 channel 被 epoll 回调
    InetAddress localAddr_;
    InetAddress peerAddr_;

    Buffer inputBuffer_;    // 收：readFd 填，交给 messageCallback_
    Buffer outputBuffer_;   // 发：写不完暂存，EPOLLOUT 续写

    ConnectionCallback connectionCallback_;
    MessageCallback messageCallback_;
    WriteCompleteCallback writeCompleteCallback_;
    CloseCallback closeCallback_;
};
