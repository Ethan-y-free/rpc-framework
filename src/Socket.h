#pragma once

#include "InetAddress.h"

// ---- Socket：fd 的 RAII 外壳 ----
// 构造持有 fd，析构自动 ::close，把 bind/listen/accept/半关闭等裸系统调用收进来。
class Socket
{
public:
    explicit Socket(int fd);    // 包住一个已创建的 fd（listen fd 或 accept 回的 connfd）
    ~Socket();

    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;

    int fd() const { return fd_; }

    void bindAddress(const InetAddress& addr);
    void listen();
    int accept(InetAddress* peerAddr);   // 成功返回 connfd（非阻塞+CLOEXEC），失败 -1
    void shutdownWrite();                // 半关闭：只关写，对端读到 EOF

    void setTcpNoDelay(bool on);   // RPC 小包多，Nagle 粘延迟，必须关
    void setReuseAddr(bool on);    // 重启时 TIME_WAIT 端口还能立即 bind

    static InetAddress getLocalAddr(int sockfd);   // getsockname：填本端地址

private:
    const int fd_;
};
