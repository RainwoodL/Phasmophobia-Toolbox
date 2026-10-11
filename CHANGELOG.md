# 更新记录

## 2026-10-11 · Phasmophobia Toolbox 开源准备

- 核对用户提供的最新 dump.cs，100 项既有元数据契约通过；当前动态字段/方法绑定和所检查的固定偏移保持一致。
- 移除字符串混淆实现、宏包装与构建开关，源码直接使用明文字符串。
- 移除 AES-256-GCM、密钥拆分、payload 运行时认证和相关模块及测试；独立 EXE 直接嵌入原始 DLL 字节。
- 保留独立加载与 EXE 嵌入功能；嵌入引导写出的 DLL 改为普通文件属性。
- 采用 Phasmophobia Toolbox 名称，补充中文主文档、英文说明、MIT 许可证和第三方声明。
- 本地构建和静态元数据核对通过；游戏内运行需要单独验收。

## 历史

旧项目 ToolsBOX 的更新核对和设计记录保留在 [docs/](docs/README.md)。

## English

2026-10-11: verified 100 existing metadata contracts against the latest supplied dump, removed string obfuscation and payload encryption/authentication, retained both loading modes, and prepared Chinese-first documentation with MIT licensing for first-party code.
