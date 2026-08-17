#include "Socket.h"   // accept4 等 GNU 扩展由 CMakeLists 全局 _GNU_SOURCE 提供

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

Socket::Socket(int fd)
    : fd_(fd)
{
}

Socket::~Socket()
{
    ::close(fd_);
}

void Socket::bindAddress(const InetAddress& addr)
{
    int ret = ::bind(fd_,
                     reinterpret_cast<const struct sockaddr*>(addr.getSockAddr()),
                     sizeof(struct sockaddr_in));
    if (ret < 0)
    {
        perror("Socket::bindAddress");
        abort();
    }
}

void Socket::listen()
{
    int ret = ::listen(fd_, SOMAXCONN);
    if (ret < 0)
    {
        perror("Socket::listen");
        abort();
    }
}

int Socket::accept(InetAddress* peerAddr)
{
    struct sockaddr_in addr;
    socklen_t len = sizeof addr;
    memset(&addr, 0, sizeof addr);
    // accept4 一次搞定 非阻塞 + CLOEXEC，避免 accept 后再手动 fcntl 的竞态窗口
    int connfd = ::accept4(fd_,
                           reinterpret_cast<struct sockaddr*>(&addr),
                           &len,
                           SOCK_NONBLOCK | SOCK_CLOEXEC);
    if (connfd >= 0 && peerAddr)
    {
        peerAddr->setSockAddr(addr);
    }
    return connfd;
}

void Socket::shutdownWrite()
{
    ::shutdown(fd_, SHUT_WR);
}

void Socket::setTcpNoDelay(bool on)
{
    int optval = on ? 1 : 0;
    ::setsockopt(fd_, IPPROTO_TCP, TCP_NODELAY, &optval, sizeof optval);
}

void Socket::setReuseAddr(bool on)
{
    int optval = on ? 1 : 0;
    ::setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof optval);
}

InetAddress Socket::getLocalAddr(int sockfd)
{
    struct sockaddr_in addr;
    socklen_t len = sizeof addr;
    memset(&addr, 0, sizeof addr);
    if (::getsockname(sockfd, reinterpret_cast<struct sockaddr*>(&addr), &len) == 0)
    {
        return InetAddress(addr);
    }
    return InetAddress(0);   // 失败兜底：端口 0
}
