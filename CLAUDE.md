# rpc-framework 基于自研网络库的 RPC 框架

> 基于自研网络库（one loop per thread，C++17，Linux 平台）之上的 RPC 框架。
> 简历核心项目，投递 8 月底-9 月初日常实习。

## 关键信息

- **源码目录**：`src/`
- **GitHub**：https://github.com/Ethan-y-free/rpc-framework （main 分支）
- **构建**：VS 远程 **Linux-GCC-Debug**（VM 192.168.73.128，ssh 22，用户 ethany，密码认证，rsync 同步）
- ⚠️ **本地 x64-Debug（MSVC）不可用**：代码依赖 Linux API（epoll / eventfd / timerfd），验证只能在远程 Linux 配置下进行
- **CMakeSettings.json 含 VM 连接信息，已被 .gitignore 排除，不推送**

## 架构

- **核心层 = 网络库底座**（已完成 + GitHub 上线）：
  - `EventLoop`：one loop per thread + TLS 四钩子 + 三条件唤醒 + swap+RAII 守卫
  - `TimerQueue`：timerfd + std::set（按到期时间排序）+ TimerId 包装
  - `Channel` / `Poller` / `EPollPoller`：事件注册与分发
  - `Timer` / `Timestamp`：基础类型
- **业务层**（已完成，含压测与崩溃修复）：
  - `TcpServer` / `TcpConnection` / `Acceptor` / `Socket` / `InetAddress`
  - `Buffer` 完整环形缓冲 + `EventLoopThread` / `EventLoopThreadPool`（动态伸缩线程池）
  - VM 双核实测：单连接 23614 QPS / 41.7us 往返延迟
- **RPC 主链路**（下一步）：
  - 协议：定长头 + 变长体
  - JSON 序列化
  - `RpcServer` / `RpcClient`
  - protobuf / etcd / 负载均衡只讲思路，不实现

## 工程约定

- **代码风格**：Allman 大括号（左大括号换行）、4 空格缩进、muduo 小驼峰命名、成员变量尾下划线
- **注释**：中文注释，`// ---- 模块边界 ----` 分节
- **构建系统**：CMake，`net` 静态库自动 GLOB `src/*.cc`，新增模块**不需要**改 CMakeLists
- **新增 demo 入口**：写 `demo_xxx.cpp` 即自动被 CMakeLists 识别为可执行文件
- **面试 Q&A 沉淀**：`笔记-仿muduo网络库.txt`（面试前复习用）

## 里程碑

- ✅ 核心层（网络库底座）完成 + 5 commit 上线
- ✅ 业务层（TcpServer 全链路 + 动态线程池 + Buffer + QPS 压测）完成
- ⬜ RPC 主链路（协议定长头+变长体 / JSON 序列化 / RpcServer / RpcClient）
- 🎯 8 月底投递第一波日常实习
