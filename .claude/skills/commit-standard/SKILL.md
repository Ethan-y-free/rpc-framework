---
name: commit-standard
description: 当用户要求"提交 / commit / push / 推送到GitHub"时触发。按 Conventional Commits 规范组织提交，防敏感文件泄漏。
---

# 规范提交（rpc-framework）

## 触发条件

用户说"提交"、"commit"、"push"、"推到 GitHub"等相关指令时使用。

## 执行流程

1. `git status` 列出全部变更，**按逻辑分组**（每个 commit 单一职责）
2. 类型前缀：`feat`(新功能) / `fix`(修bug) / `docs`(文档) / `chore`(杂务) / `refactor`(重构) / `test`(测试)
3. 提交信息格式：`type: 简述（可选：细节说明）`
   - 参照历史：`feat: EventLoop 事件循环（one loop per thread + 三条件唤醒）`
4. 逐组 `git add` + `git commit`，最后 `git push origin main`
5. 推送前**复查敏感文件未被跟踪**

## 安全红线（绝不提交）

- `CMakeSettings.json`（VM 内网连接信息）
- `.mcp.json`（GitHub PAT token）
- `out/`、`.vs/`、编译产物（已在 .gitignore）

## 参考

- 仓库：https://github.com/Ethan-y-free/rpc-framework （main 分支）
- 现有 5 commit 作格式参照
