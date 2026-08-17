#pragma once

#include <arpa/inet.h>
#include <cstdint>
#include <string>

// ---- InetAddress：sockaddr_in 的薄壳 ----
// 系统调用层要网络字节序的 sockaddr_in，业务/日志层要主机字节序的 ip:port 字符串。
// 本类只做这两层转换，不掺任何业务逻辑。
class InetAddress
{
public:
    InetAddress() : addr_() {}   // 全零占位：accept 前先声明，随后由 setSockAddr 填充
    explicit InetAddress(uint16_t port, bool loopbackOnly = false);   // 绑定用：只填端口，ip 自动
    InetAddress(const char* ip, uint16_t port);                        // 连接用：指定 ip:port
    explicit InetAddress(const struct sockaddr_in& addr);             // accept 回来的对端地址

    std::string toIp() const;        // "127.0.0.1"
    std::string toIpPort() const;    // "127.0.0.1:8080"（日志/注册用）
    uint16_t toPort() const;

    void setSockAddr(const struct sockaddr_in& addr) { addr_ = addr; }   // accept 回填
    const struct sockaddr_in* getSockAddr() const { return &addr_; }

private:
    struct sockaddr_in addr_;   // 网络字节序原样存
};
