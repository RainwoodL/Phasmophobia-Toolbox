# Phasmophobia Toolbox · 嵌入式发行版

Windows 10/11 x64，无需安装 Visual Studio、CMake 或 Python。
此包包含已编译的嵌入工具和 DLL，由安装脚本基于你本机的原版游戏 EXE 生成嵌入版。

## 安装

1. 关闭恐鬼症，将 ZIP 完整解压到固定目录，例如 `D:\PhasmophobiaToolbox`。
2. 在 Steam 中右键恐鬼症 → 管理 → 浏览本地文件，复制 `Phasmophobia.exe` 的完整路径。
3. 双击 `install-embedded.cmd`，粘贴路径并回车。遇到文件写入权限错误时，以管理员身份运行。
4. 安装成功后从 Steam 启动游戏，按 **U** 打开菜单；启动后约 5 秒开始加载工具箱。

也可以在发行包目录的 PowerShell 中指定路径：

```powershell
./scripts/install-embedded.ps1 -GameExecutable 'D:/Steam/steamapps/common/Phasmophobia/Phasmophobia.exe'
```

添加 `-PackageOnly` 可仅生成 `release/embedded/Phasmophobia.embedded.exe` 候选，不替换游戏文件。
完成安装后游戏启动会自动加载，无需运行注入器。

## 更新与卸载

安装前会备份原版到发行包的 `release/backups/`；清单保存在 `release/embedded/embed-manifest.json`。
保留发行包目录及备份。脚本拒绝在已有 `.tbx` 嵌入节的 EXE 上重复安装，部署失败时自动回滚。

Steam 更新或验证游戏文件会恢复游戏 EXE。更新后用新版发行包重新安装。
移动游戏安装目录后也需先恢复原版，再重新安装。

卸载时关闭游戏，通过 Steam 验证文件恢复原版 EXE；也可用当前游戏版本对应的原版备份替换。
游戏目录中生成的 `.toolsbox.payload.dll` 可在游戏关闭后删除。

嵌入包同时附带 `release/standalone/PhasmophobiaInjector.exe`，恢复原版后可选择该方式加载。
设置保存在 `%LOCALAPPDATA%/GhostRoomTools/settings.bin`。

## 版本与许可

本次版本核对日期为 2026-10-11，Steam build 25811160；最新 dump 的 100 项既有元数据契约通过。
构建、单元测试和隔离嵌入验证通过，尚未完成真实游戏内运行验收。
`SHA256SUMS.txt` 和 `build-manifest.json` 记录发行文件校验值及源码提交。
第一方代码采用 [MIT](LICENSE)，依赖与图片范围见 [第三方声明](THIRD_PARTY_NOTICES.md)。

源码与更新：[Phasmophobia Toolbox](https://github.com/RainwoodL/Phasmophobia-Toolbox)。

## English

Extract the entire ZIP to a permanent directory and close the game.
Double-click `install-embedded.cmd`, then enter the full path to your original `Phasmophobia.exe`.
No compiler is required. The installer creates a patched EXE for your local game path, backs up the original,
and deploys it with rollback on failure. Launch from Steam and press **U**; loading starts after about five seconds.

Use `-PackageOnly` with `scripts/install-embedded.ps1` to generate a candidate without changing the game.
Keep `release/backups/`. To uninstall, close the game and verify its files in Steam, or restore the matching original backup.
After a game update or installation-directory change, restore the original EXE and install again.
`release/standalone/PhasmophobiaInjector.exe` is also included for optional use after restoring the original game EXE.

Checked on 2026-10-11 against Steam build 25811160 and 100 metadata contracts. Live game validation is still pending.
