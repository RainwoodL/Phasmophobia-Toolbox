# Phasmophobia Toolbox · 恐鬼症工具箱

[English](README.en.md) · [使用与开发指南](docs/README.md) · [更新记录](CHANGELOG.md)

面向 Windows x64 的开源恐鬼症工具箱，使用 C++17、IL2CPP、Direct3D 11、ImGui 和 Detours。
界面和文档以中文为主，提供英文使用说明。第一方代码采用 MIT 许可证。

当前源码使用明文字符串与原始 DLL 字节，已移除字符串混淆、payload 加密、密钥拆分和运行时防篡改认证。
支持独立单文件注入器和游戏 EXE 嵌入两种加载方式。

最新兼容核对：**2026-10-11**，用户提供的最新 dump.cs 的 **100/100 项既有元数据契约**通过。
这包括字段布局、方法签名、固定偏移与枚举值；详见[本次更新记录](docs/game-update-2026-10-11.md)。
静态核对和本地测试不能替代游戏内验收。

## 功能

- 圣木计时、十字架计时、猎杀计时与游戏时间。
- 鬼魂状态、猎杀时的鬼魂距离、鬼魂移速与联机鬼速同步。
- 玩家理智、体力、玩家昵称与通灵板翻译。
- 准星交互信息、物品栏和屏幕准星。
- 可配置快捷键、显示位置、字体、颜色与持久化设置。

默认按 **U** 显示/隐藏菜单，按住 **Alt** 拖动功能位置。
默认 **I** 开始圣木计时、**O** 清空圣木计时、**P** 清空十字架计时；可在菜单中修改。

## 使用

1. 从源码构建 Release，或使用仓库发布的构建产物。
2. 启动游戏，进入主菜单。
3. 运行 `release/standalone/PhasmophobiaInjector.exe`，等待工具箱启动提示。
4. 游戏内按 **U** 打开菜单。遇到权限提示时以管理员身份运行注入器。

独立注入器内嵌原始 `payload.dll`，单独分发 EXE 即可。
设置保存在 `%LOCALAPPDATA%/GhostRoomTools/settings.bin`。

## 构建与验证

需要 Windows 10/11 x64、Visual Studio 2022 或更新版本的 C++ 桌面开发工具、Windows SDK、CMake 3.15+ 和 Ninja。
脚本自动载入 MSVC x64 环境；普通 PowerShell 也可运行：

```powershell
./scripts/check-all.ps1 -Configuration Release
```

该命令执行仓库卫生检查、秘密扫描、构建、**6 项 CTest** 和 **7 个嵌入脚本回归场景**。
Release 产物位于 `release/`；Debug 中间文件与产物位于 `out/`。

```powershell
./scripts/test.ps1 -Configuration Debug
./scripts/test.ps1 -Configuration Release -StaticAnalysis
./scripts/embed-game-tests.ps1
```

GitHub Actions 对 push 和 pull request 执行仓库检查、Debug 构建与测试、MSVC 原生静态分析。

## 游戏更新后核对

核对工具需要 Python 3.10+，只使用标准库。先保留上一版 dump，再比较最新 dump：

```powershell
python ./tools/dump_compatibility.py 'D:/dumps/previous.cs' 'D:/dumps/latest.cs' --output './out/dump-audit.json'
python ./tools/test_dump_compatibility.py 'D:/dumps/previous.cs' 'D:/dumps/latest.cs'
```

退出码 `0` 表示所检查的契约一致，`1` 表示需要复核，`2` 表示输入或解析错误。
游戏的混淆标识会标准化；相同偏移和类型无法单独证明字段语义一致。
项目通过 IL2CPP 动态查找类型、字段和方法，仍需在游戏内验证猎杀、场景切换、RPC 和联机同步。

`dump.cs`、游戏程序集、反编译工具和编译产物只保存在本机，不进入源码仓库。

## 嵌入游戏 EXE

关闭游戏，以新的原版 x64 EXE 为输入。推荐先仅生成候选包与清单：

```powershell
./scripts/embed-game.ps1 -GameExecutable 'D:/Steam/steamapps/common/Phasmophobia/Phasmophobia.exe' -DumpPath 'D:/dumps/latest.cs' -PackageOnly
```

删除 `-PackageOnly` 后，脚本会核验并备份原版 EXE，然后部署内嵌版。

| 产物 | 位置 |
| --- | --- |
| 独立注入器 | `release/standalone/PhasmophobiaInjector.exe` |
| 内嵌版游戏副本 | `release/embedded/Phasmophobia.embedded.exe` |
| 原始 payload | `release/embedded/payload.dll` |
| 嵌入辅助工具 | `release/tools/pe_embed.exe` |
| 来源与产物哈希清单 | `release/embedded/embed-manifest.json` |
| 原版备份 | `release/backups/` |

脚本拒绝重复 `.tbx` 嵌入，验证候选包后通过同目录原子替换部署，失败时回滚。
启动引导等待 5 秒，并检查 Unity、IL2CPP 与 D3D11 模块后写入普通属性的 `.toolsbox.payload.dll`。
Steam 更新或验证文件后需重新基于原版 EXE 生成；移动安装目录后也需重新生成。
清单中的 SHA-256 用于追踪来源和验证备份。

## 项目结构

| 目录 | 内容 |
| --- | --- |
| `payload/` | 游戏元数据访问、叠加界面、功能逻辑与测试 |
| `injector/` | 原始 DLL 嵌入生成器、独立注入器与 PE 加载器 |
| `tools/` | 游戏 EXE 嵌入工具与 dump 契约核对工具 |
| `scripts/` | 构建、测试、打包与部署脚本 |
| `docs/` | 中文指南、兼容核对记录与历史设计文档 |
| `imgui/`、`third_party/` | 保留原许可证的第三方依赖 |
| `resource/` | 界面使用的图片资源 |

## 许可证与交流

第一方代码使用 [MIT](LICENSE)。第三方依赖与图片资源的范围见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。
问题与建议请提交 [GitHub Issue](https://github.com/RainwoodL/Phasmophobia-Toolbox/issues)；安全问题见 [SECURITY.md](SECURITY.md)。
作者：霖木 / RainwoodL；交流群：304393689。
