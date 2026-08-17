#include "TcpConnection.h"

#include <cerrno>
#include <cstring>
#include <sys/socket.h>
#include <unistd.h>   // ::write

#include "EventLoop.h"

TcpConnection::TcpConnection(EventLoop* loop, const std::string& name, int sockfd,
                             const InetAddress& localAddr, const InetAddress& peerAddr)
    : loop_(loop),
      name_(name),
      state_(kConnecting),
      channel_(loop, sockfd),   // 先初始化（声明顺序：channel_ 在 socket_ 前）
      socket_(sockfd),
      localAddr_(localAddr),
      peerAddr_(peerAddr)
{
    channel_.setReadCallback(std::bind(&TcpConnection::handleRead, this, std::placeholders::_1));
    channel_.setWriteCallback(std::bind(&TcpConnection::handleWrite, this));
    channel_.setCloseCallback(std::bind(&TcpConnection::handleClose, this));
    channel_.setErrorCallback(std::bind(&TcpConnection::handleError, this));
    socket_.setTcpNoDelay(true);   // RPC 请求是小包，Nagle 会粘延迟，必须关
}

TcpConnection::~TcpConnection()
{
    // 此时 channel_ 必须已被 connectDestroyed() remove 掉，否则 Channel 析构会 abort
}

void TcpConnection::connectEstablished()
{
    loop_->assertInLoopThread();
    setState(kConnected);
    channel_.enableReading();
    if (connectionCallback_)
    {
        connectionCallback_(shared_from_this());   // 通知用户：连接已建立
    }
}

void TcpConnection::connectDestroyed()
{
    loop_->assertInLoopThread();
    if (state_ == kConnected)
    {
        // 兜底：连接还活着就被 server 销毁（如析构），补一次断开通知
        setState(kDisconnected);
        channel_.disableAll();
        if (connectionCallback_)
        {
            connectionCallback_(shared_from_this());
        }
    }
    channel_.remove();   // 从 poller 摘除，保证析构不 abort
}

void TcpConnection::handleRead(Timestamp receiveTime)
{
    TcpConnectionPtr guardThis(shared_from_this());   // 事件处理期间保活
    int savedErrno = 0;
    ssize_t n = inputBuffer_.readFd(socket_.fd(), &savedErrno);
    if (n > 0)
    {
        if (messageCallback_)
        {
            messageCallback_(shared_from_this(), inputBuffer_.peek(),
                             inputBuffer_.readableBytes(), receiveTime);
        }
        // 最小形态：回调应一次消费完，整块回收
        inputBuffer_.retrieveAll();
    }
    else if (n == 0)
    {
        handleClose();   // 对端 FIN，读 0 → 关闭
    }
    else
    {
        errno = savedErrno;
        handleError();
    }
}

void TcpConnection::handleWrite()
{
    TcpConnectionPtr guardThis(shared_from_this());
    loop_->assertInLoopThread();
    if (channel_.isWriting())
    {
        ssize_t n = ::write(socket_.fd(), outputBuffer_.peek(), outputBuffer_.readableBytes());
        if (n > 0)
        {
            outputBuffer_.retrieve(static_cast<size_t>(n));
            if (outputBuffer_.readableBytes() == 0)
            {
                channel_.disableWriting();   // 写完了，撤掉 EPOLLOUT
                if (writeCompleteCallback_)
                {
                    loop_->queueInLoop(std::bind(writeCompleteCallback_, shared_from_this()));
                }
                if (state_ == kDisconnecting)
                {
                    shutdownInLoop();   // 半关闭收尾：缓冲已空，关写
                }
            }
        }
        // else: EAGAIN，等下一次 EPOLLOUT 续写
    }
}

void TcpConnection::handleClose()
{
    TcpConnectionPtr guardThis(shared_from_this());
    loop_->assertInLoopThread();
    if (state_ == kDisconnected)
    {
        return;   // 防重入：同一批事件可能 EPOLLERR 与读0 同时触发 → handleClose 会被调两次
    }
    channel_.disableAll();
    setState(kDisconnected);
    if (connectionCallback_)
    {
        connectionCallback_(shared_from_this());   // 通知用户：已断开
    }
    if (closeCallback_)
    {
        closeCallback_(shared_from_this());        // 通知 TcpServer：从连接表移除
    }
}

void TcpConnection::handleError()
{
    int err = 0;
    socklen_t len = sizeof err;
    ::getsockopt(socket_.fd(), SOL_SOCKET, SO_ERROR, &err, &len);
    // 出错即视为连接失效，走统一关闭流程
    handleClose();
}

void TcpConnection::send(const std::string& message)
{
    if (state_ != kConnected)
    {
        return;
    }
    if (loop_->isInLoopThread())
    {
        sendInLoop(message);
    }
    else
    {
        // 跨线程：把 message 按值拷进 bind 存储，入队到 loop 线程执行。
        // bind 到 shared_from_this() 而非裸 this：防止 send 后、执行前连接被销毁导致悬空。
        loop_->queueInLoop(std::bind(&TcpConnection::sendInLoop, shared_from_this(), message));
    }
}

void TcpConnection::sendInLoop(const std::string& message)
{
    loop_->assertInLoopThread();
    ssize_t nwrote = 0;
    // 内核缓冲空且无积压时才直接写；否则全走 outputBuffer_ 排队，保证发送有序
    if (!channel_.isWriting() && outputBuffer_.readableBytes() == 0)
    {
        nwrote = ::write(socket_.fd(), message.data(), message.size());
        if (nwrote < 0)
        {
            if (errno == EAGAIN)
            {
                nwrote = 0;   // 内核缓冲满，全部进 outputBuffer_
            }
            else
            {
                handleClose();   // 写失败视为连接损坏，走统一关闭流程
                return;
            }
        }
    }

    if (static_cast<size_t>(nwrote) < message.size())
    {
        outputBuffer_.append(message.data() + nwrote, message.size() - nwrote);
        if (!channel_.isWriting())
        {
            channel_.enableWriting();   // 关注 EPOLLOUT，可写时续写
        }
    }
}

void TcpConnection::shutdown()
{
    if (state_ == kConnected)
    {
        setState(kDisconnecting);
        loop_->runInLoop(std::bind(&TcpConnection::shutdownInLoop, shared_from_this()));
    }
}

void TcpConnection::shutdownInLoop()
{
    loop_->assertInLoopThread();
    if (!channel_.isWriting())
    {
        // 输出缓冲已空，直接关写；否则等 handleWrite 写完后再关
        socket_.shutdownWrite();
    }
}

void TcpConnection::forceClose()
{
    if (state_ == kConnected || state_ == kDisconnecting)
    {
        loop_->queueInLoop(std::bind(&TcpConnection::forceCloseInLoop, shared_from_this()));
    }
}

void TcpConnection::forceCloseInLoop()
{
    loop_->assertInLoopThread();
    if (state_ == kConnected || state_ == kDisconnecting)
    {
        handleClose();
    }
}

void TcpConnection::setTcpNoDelay(bool on)
{
    socket_.setTcpNoDelay(on);
}
