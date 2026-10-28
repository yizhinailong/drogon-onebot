# 文档索引

[English](../en/README.md) · 简体中文

[项目首页](../../README.zh-CN.md)

本目录是简体中文版，英文版位于 `docs/en/`，两边的文件一一对应。文档保留 API 标识符、命令、协议值和真实终端输出。代码块未包含完整 `main()` 时视为用法片段，需要在 drogon 初始化插件之后执行，例如在 beginning advice 或请求处理函数里。

## 使用

| 文档 | 内容 |
| --- | --- |
| [drogon 插件使用说明](drogon-plugin.md) | 接入、注册与配置、NapCat 配置、生命周期、调用动作、接收事件、错误处理、线程模型、已知限制与测试 |

API 参考由 Doxygen 从 `include/` 和 `src/` 的注释生成，见 [Doxygen 使用说明](doxygen.md)。

## 项目维护

| 文档 | 内容 |
| --- | --- |
| [Doxygen 使用说明](doxygen.md) | 本地生成、GitHub Pages 自动发布、配置与警告 |
| [版本标签、CI 与发布](ci.md) | 版本变更自动打标签、Linux 与 Windows 构建和测试、把包发布到 GitHub Release |
| [仓库规范](../../AGENTS.md) | 设计决策、构建、测试与代码风格（英文） |
