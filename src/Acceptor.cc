#include "Acceptor.h"

#include <fcntl.h>

#include "EventLoop.h"
#include <sys/socket.h>
#include <unistd.h>

Acceptor::Acceptor(EventLoop* loop, const InetAddress& listenAddr, bool reuseport)
    : loop_(loop),
      acceptSocket_(::socket(AF_INET, SOCK_STREAM, 0)),
      acceptChannel_(loop, acceptSocket_.fd())
{
    (void)reuseport;   // 最小形态不区分 reuseport，统一走 SO_REUSEADDR
    acceptSocket_.setReuseAddr(true);
    acceptSocket_.bindAddress(listenAddr);
    // listen fd 设非阻塞：handleRead 里 while accept 到 EAGAIN 即停，
    // 一个慢连接请求不会卡住整个 mainLoop。
    int flags = ::fcntl(acceptSocket_.fd(), F_GETFL, 0);
    ::fcntl(acceptSocket_.fd(), F_SETFL, flags | O_NONBLOCK);

    acceptChannel_.setReadCallback(std::bind(&Acceptor::handleRead, this));
}

Acceptor::~Acceptor()
{
    acceptChannel_.disableAll();
    acceptChannel_.remove();
}

void Acceptor::listen()
{
    loop_->assertInLoopThread();
    listening_ = true;
    acceptSocket_.listen();
    acceptChannel_.enableReading();
}

void Acceptor::handleRead()
{
    loop_->assertInLoopThread();
    // while 循环一次 accept 完所有待连（listen fd 非阻塞，EAGAIN 即停）
    while (true)
    {
        InetAddress peerAddr;
        int connfd = acceptSocket_.accept(&peerAddr);
        if (connfd >= 0)
        {
            if (newConnectionCallback_)
            {
                newConnectionCallback_(connfd, peerAddr);   // 上抛给 TcpServer
            }
            else
            {
                ::close(connfd);   // 没人接（如未 start），立即关，防 fd 泄漏
            }
        }
        else
        {
            break;   // EAGAIN / EINTR 等一律退出，等下一轮 EPOLLIN
        }
    }
}
