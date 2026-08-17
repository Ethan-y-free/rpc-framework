---
name: build-verify
description: 当用户要求"编译验证 / 构建项目 / build / 编译报错 / 修编译错误"时触发。验证rpc-framework项目在远程 Linux 上能否编译通过，并修复错误。
---

# 构建验证（rpc-framework）

## 触发条件

用户说编译验证、构建项目、build、cmake、编译报错、修编译错误等相关指令时使用。

## 执行流程

1. **明确构建目标**：VS 远程 **Linux-GCC-Debug** 配置（VM 192.168.73.128）
   - 构建对象：`net` 静态库 + 当前存在的 `demo_*.cpp` 可执行文件
2. **用户执行构建**：编译在远程 VM 上进行，需用户切换到 Linux-GCC-Debug 配置点 Build
   - 我负责解读错误、定位根因、修复代码
3. **收集编译错误**：按 `文件:行号` 分组列出，标注错误类型
4. **修复并复查**：修复后让用户重新 Build，直到零错误
5. **报告产物**：通过后列出生成的库/可执行文件

## 常见坑

- ⚠️ **本地 x64-Debug（MSVC）必然失败**：代码依赖 Linux API（epoll / eventfd / timerfd），不要在此配置下验证
- 新增 `.cc` 文件**不需要**改 CMakeLists（自动 GLOB）
- 头文件缺失 `#pragma once`、const 方法迭代器类型不匹配、`\ No newline at end of file`——历史踩坑点
- 代码风格：Allman 大括号、4 空格缩进、小驼峰命名、成员尾下划线

## 参考

- 构建配置：`CMakeLists.txt`、`CMakeSettings.json`
- 架构约定：项目根目录 `CLAUDE.md`
