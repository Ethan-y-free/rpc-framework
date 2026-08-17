// ---- demo_echo_tcp：业务层验证 demo ----
// 基于 TcpServer 的 echo 服务器：收到什么原样发回。
// 验证点：
//   1. 连接建立 / 断开回调（onConnection）
//   2. 消息收发全链路（onMessage → conn->send 回写）
//   3. 动态线程池（setThreadNum + setMaxThreadNum，连接 round-robin 分配 subLoop）
//
// 运行方式：
//   终端1: ./demo_echo_tcp            （监听 9000）
//   终端2: nc 127.0.0.1 9000          （连上后输入任意字符串，原样返回）
//   或   : telnet 127.0.0.1 9000

#include <cstdio>
#include <functional>
#include <string>

#include "EventLoop.h"
#include "InetAddress.h"
#include "TcpServer.h"
#include "Timestamp.h"

using std::placeholders::_1;
using std::placeholders::_2;
using std::placeholders::_3;
using std::placeholders::_4;

class EchoServer
{
public:
    EchoServer(EventLoop* loop, const InetAddress& listenAddr)
        : server_(loop, listenAddr, "EchoServer")
    {
        server_.setConnectionCallback(std::bind(&EchoServer::onConnection, this, _1));
        server_.setMessageCallback(std::bind(&EchoServer::onMessage, this, _1, _2, _3, _4));
    }

    void start()
    {
        // 2 个保底线程，最多扩到 8；连接多了自动扩容，空 loop 连续空闲自动回收
        server_.setThreadNum(2);
        server_.setMaxThreadNum(8);
        server_.start();
    }

private:
    void onConnection(const TcpConnectionPtr& conn)
    {
        if (conn->connected())
        {
            printf("[conn] %s -> %s 连接建立\n",
                   conn->peerAddress().toIpPort().c_str(),
                   conn->localAddress().toIpPort().c_str());
        }
        else
        {
            printf("[conn] %s 连接断开\n", conn->peerAddress().toIpPort().c_str());
        }
    }

    void onMessage(const TcpConnectionPtr& conn, const char* data, size_t len, Timestamp)
    {
        std::string message(data, len);
        printf("[msg] %s 收到 %zu 字节: %s\n",
               conn->peerAddress().toIpPort().c_str(), len, message.c_str());
        conn->send(message);   // echo：原样回写
    }

    TcpServer server_;
};

int main()
{
    setvbuf(stdout, nullptr, _IONBF, 0);   // 取消 stdout 缓冲：printf 立即可见（VS 远程控制台是管道非 tty）
    EventLoop loop;
    InetAddress listenAddr(9000);
    EchoServer server(&loop, listenAddr);
    server.start();
    printf("EchoServer 监听 %s，Ctrl+C 退出\n", listenAddr.toIpPort().c_str());
    loop.loop();
    return 0;
}
