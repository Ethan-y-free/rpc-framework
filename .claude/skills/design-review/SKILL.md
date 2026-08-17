---
name: design-review
description: 当用户要求"设计讲解 / 验收 / 进下一个模块 / 讲一下X的设计"时触发。按协作协议 v2：先讲设计→用户验收→代写。
---

# 设计讲解与验收（协作模式 v2）

## 触发条件

用户说"设计讲解"、"验收"、"进业务层/新模块"、"讲一下 X 的设计"等相关指令时使用。

## 执行流程

1. **讲设计**：每个设计决策给「备选方案 + 权衡 + 为什么选它」
   - 面向面试：讲完要能答"面试官为什么这么问"
2. **用户验收**：用户理解确认后进入下一步
3. **代写**（按 v2 差异化协议）：
   - 与项目一重复/相似的代码 → **直接代写，不打扰用户**
   - 新增设计（TimerQueue / TLS / 三条件唤醒 / 业务层回调机制 / 连接生命周期）→ **先讲 → 验收 → 再写**
4. **验证**：语法级验证 + 规范提交（走 `commit-standard`）

## 当前模块进度

- ✅ 核心层（网络库底座）：EventLoop / TimerQueue / Channel / Poller / EPollPoller（已提交）
- 🔄 业务层（RPC 驱动最小形态）：TcpServer / TcpConnection / Acceptor / Socket / InetAddress（0 字节占位，待实现）
- ⬜ RPC 主链路：协议（定长头+变长体）/ JSON 序列化 / RpcServer / RpcClient（下一步）
- 💡 protobuf / etcd / 负载均衡：只讲思路，不实现

## 参考

- 设计文档：`DESIGN.md`（v2 EventLoop 蓝本）
- 架构约定：根目录 `CLAUDE.md`
- 面试 Q&A 沉淀：`笔记-仿muduo网络库.txt`
