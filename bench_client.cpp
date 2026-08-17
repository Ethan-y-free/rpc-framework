// ---- bench_client：echo 服务器 QPS 压测客户端 ----
// ping-pong 模式：每连接串行「发请求 → 等服务端 echo → 发下一个」，
// 这是 RPC 请求/响应的真实往返形态（不是纯打流）。
//
// 用法: ./bench_client [连接数] [每连接请求数] [payload字节数]
// 默认: ./bench_client 4 10000 32
//
// 指标: 总请求 / 总墙钟耗时 = QPS；平均往返延迟 us

#include <arpa/inet.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

// 读满 n 字节：echo 响应 = 请求长度，但 TCP 可能分包，必须循环读到够
static bool readFull(int fd, char* buf, size_t n)
{
    size_t got = 0;
    while (got < n)
    {
        ssize_t r = ::read(fd, buf + got, n - got);
        if (r <= 0)
        {
            return false;   // EOF 或出错
        }
        got += r;
    }
    return true;
}

int main(int argc, char* argv[])
{
    int conns = argc > 1 ? std::atoi(argv[1]) : 4;
    int requests = argc > 2 ? std::atoi(argv[2]) : 10000;
    int payload = argc > 3 ? std::atoi(argv[3]) : 32;

    std::vector<double> latencies(conns, 0.0);     // 每线程累计往返延迟(us)
    std::vector<long long> completed(conns, 0);    // 每线程实际完成的请求数（失败则为 0）
    auto start = std::chrono::steady_clock::now();

    std::vector<std::thread> threads;
    for (int c = 0; c < conns; ++c)
    {
        threads.emplace_back([=, &latencies, &completed]()
        {
            int fd = ::socket(AF_INET, SOCK_STREAM, 0);
            if (fd < 0)
            {
                perror("socket");
                return;   // completed[c] 保持 0
            }
            int one = 1;
            ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);   // 小包不能被 Nagle 攒住

            struct sockaddr_in addr;
            memset(&addr, 0, sizeof addr);
            addr.sin_family = AF_INET;
            addr.sin_port = htons(9000);
            ::inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
            if (::connect(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof addr) < 0)
            {
                perror("connect");
                ::close(fd);
                return;   // completed[c] 保持 0
            }

            std::string msg(static_cast<size_t>(payload), 'a');
            std::vector<char> buf(static_cast<size_t>(payload));
            double totalUs = 0.0;
            int done = 0;
            for (; done < requests; ++done)
            {
                auto t0 = std::chrono::steady_clock::now();
                ::write(fd, msg.data(), msg.size());
                if (!readFull(fd, buf.data(), static_cast<size_t>(payload)))
                {
                    break;   // 中途断开：记录已完成数
                }
                auto t1 = std::chrono::steady_clock::now();
                totalUs += std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
            }
            completed[c] = done;
            latencies[c] = totalUs;
            ::close(fd);
        });
    }
    for (auto& t : threads)
    {
        t.join();
    }
    auto end = std::chrono::steady_clock::now();
    double elapsedUs = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

    long long totalCompleted = 0;
    for (long long v : completed)
    {
        totalCompleted += v;
    }
    if (totalCompleted == 0)
    {
        printf("所有连接失败（Connection refused?），请确认 demo_echo_tcp 正在运行\n");
        return 1;
    }

    double qps = totalCompleted / (elapsedUs / 1e6);
    double avgLatUs = 0.0;
    for (double v : latencies)
    {
        avgLatUs += v;
    }
    avgLatUs /= totalCompleted;

    printf("压测: %d 连接 × %d 请求/连接, payload %d 字节\n", conns, requests, payload);
    printf("完成请求: %lld, 总耗时: %.1f ms\n", totalCompleted, elapsedUs / 1e3);
    printf("QPS: %.0f req/s\n", qps);
    printf("平均往返延迟: %.1f us\n", avgLatUs);
    return 0;
}
