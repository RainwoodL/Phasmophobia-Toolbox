# 文档导航

本项目以中文文档为主。英文入门说明见 [README.en.md](../README.en.md)。

| 文档 | 用途 |
| --- | --- |
| [中文入门](../README.md) | 功能、快捷键、构建、运行和嵌入部署 |
| [发行包使用说明](release-embedded.md) | ZIP 中两份 EXE 的使用方法、原版备份与恢复 |
| [注入使用说明](release-injector.md) | 独立注入器的路径、使用步骤与快捷键 |
| [更新记录](../CHANGELOG.md) | 公开版本的主要变化 |
| [2026-11-25 游戏兼容核对](game-update-2026-11-25.md) | Steam build 26893244 的字段和方法差异、昵称与猎杀数据显示、房主鬼速同步 |
| [2026-10-11 兼容与开源准备](game-update-2026-10-11.md) | 10 月基线、保护移除与验证结果 |
| [第三方声明](../THIRD_PARTY_NOTICES.md) | 依赖与资源的许可证范围 |
| [安全问题报告](../SECURITY.md) | 私下报告安全问题的方式 |

## 开发流程

1. 保留上一版 dump.cs，使用 `tools/dump_compatibility.py` 比较新旧元数据契约。
2. 对变化的字段偏移、字段唯一性、枚举值和方法签名逐项复核；保持检查清单与 payload 的实际调用一致。
3. 用 `scripts/check-all.ps1` 执行构建与测试，用 `scripts/test.ps1 -StaticAnalysis` 执行 MSVC 静态分析。
4. 先生成仅打包候选，再在游戏内确认菜单、猎杀、计时、地图切换和联机行为。

`payload/` 通过 IL2CPP API 动态解析字段与方法，少量固定偏移仍需要每次更新复核。
`scripts/package-release.ps1` 将项目构建的独立注入器和嵌入式启动程序打成一个 ZIP。
开发者也可用 `tools/pe_embed.cpp` 与 `scripts/embed-game.ps1` 在本机生成原版游戏 EXE 的嵌入副本，清单记录文件来源与备份哈希。

## 历史记录

`game-update-*`、`game-update-audit-*` 和 `superpowers/` 中的旧报告保留当时的实现与验证结果。
旧文档中的 ToolsBOX 名称、AES 加密和混淆描述属于历史状态；当前构建与使用方式以根目录文档和最新更新记录为准。
