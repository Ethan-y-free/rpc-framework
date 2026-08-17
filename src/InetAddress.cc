#include "InetAddress.h"

#include <cstdio>
#include <cstring>
#include <netinet/in.h>

InetAddress::InetAddress(uint16_t port, bool loopbackOnly)
{
    memset(&addr_, 0, sizeof addr_);
    addr_.sin_family = AF_INET;
    in_addr_t ip = loopbackOnly ? htonl(INADDR_LOOPBACK) : htonl(INADDR_ANY);
    addr_.sin_addr.s_addr = ip;
    addr_.sin_port = htons(port);
}

InetAddress::InetAddress(const char* ip, uint16_t port)
{
    memset(&addr_, 0, sizeof addr_);
    addr_.sin_family = AF_INET;
    addr_.sin_port = htons(port);
    if (::inet_pton(AF_INET, ip, &addr_.sin_addr) <= 0)
    {
        // 非法 ip 字符串：inet_pton 返回 0，sin_addr 保持全 0（即 0.0.0.0）兜底
    }
}

InetAddress::InetAddress(const struct sockaddr_in& addr)
    : addr_(addr)
{
}

std::string InetAddress::toIp() const
{
    char buf[INET_ADDRSTRLEN];
    ::inet_ntop(AF_INET, &addr_.sin_addr, buf, sizeof buf);
    return buf;
}

std::string InetAddress::toIpPort() const
{
    char buf[INET_ADDRSTRLEN + 8];
    ::inet_ntop(AF_INET, &addr_.sin_addr, buf, sizeof buf);
    int port = ntohs(addr_.sin_port);
    snprintf(buf + strlen(buf), sizeof buf - strlen(buf), ":%d", port);
    return buf;
}

uint16_t InetAddress::toPort() const
{
    return ntohs(addr_.sin_port);
}
