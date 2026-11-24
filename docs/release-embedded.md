# Phasmophobia Toolbox · 恐鬼症工具箱 v2026.11.25

适用于 Windows 10/11 x64。将整个 ZIP 解压到固定目录，例如 `D:\PhasmophobiaToolbox`。包内已包含编译好的工具和 DLL。两种使用方式任选其一，加载后都可按 **U** 打开菜单。

## 方式一 · 每次启动后运行注入器

1. 从 Steam 启动原版恐鬼症，进入主菜单。
2. 运行解压目录中的 `release/standalone/PhasmophobiaInjector.exe`，等待启动提示。遇到访问权限错误时，以管理员身份运行注入器。
3. 关闭游戏后本次注入结束，下次启动游戏后重复上述步骤。

## 方式二 · 安装一次，随游戏启动

1. 关闭恐鬼症。
2. 在 Steam 中右键恐鬼症 → 管理 → 浏览本地文件，复制你本机原版 `Phasmophobia.exe` 的完整路径。
3. 双击 `install-embedded.cmd`，粘贴路径并回车。遇到文件写入权限错误时，以管理员身份运行。
4. 安装成功后从 Steam 启动游戏，按 **U** 打开菜单；启动后约 5 秒开始加载工具箱。

安装脚本为你指定的游戏位置生成嵌入版 EXE。其他玩家在自己的电脑上运行同一安装脚本，并选择各自的原版游戏 EXE。也可以在解压目录打开 PowerShell，按以下示例传入本机路径。

```powershell
./scripts/install-embedded.ps1 -GameExecutable 'D:/Steam/steamapps/common/Phasmophobia/Phasmophobia.exe'
```

添加 `-PackageOnly` 时，脚本只生成候选文件 `release/embedded/Phasmophobia.embedded.exe` 和清单。

## 更新、切回注入方式与卸载

安装前会备份原版到发行包的 `release/backups/`；清单保存在 `release/embedded/embed-manifest.json`。
保留发行包目录及备份。脚本拒绝在已有 `.tbx` 嵌入节的 EXE 上重复安装，部署失败时自动回滚。

Steam 更新或验证游戏文件会恢复游戏 EXE。更新后用新版发行包重新安装。
移动游戏安装目录后也需先恢复原版，再重新安装。

卸载嵌入版或切回注入方式时，先关闭游戏，通过 Steam 验证游戏文件，或用与当前游戏版本匹配的备份恢复原版 EXE。
游戏目录中生成的 `.toolsbox.payload.dll` 可在游戏关闭后删除。

两种方式共用 `%LOCALAPPDATA%/GhostRoomTools/settings.bin` 中的设置。默认 **I** 开始圣木计时、**O** 清空圣木计时、**P** 清空十字架计时，按住 **Alt** 可拖动浮窗。

## 版本与许可

本版对应 Steam build **26893244**，修复队友昵称、猎杀距离和鬼速显示，以及房主的猎杀鬼速同步。准星信息和物品栏现在能显示“探灵杆”。
Debug 和 Release 各通过 6 项 CTest，Python 兼容性检查工具的 11 项回归测试通过。游戏内显示与真实多人同步仍待实测。
`SHA256SUMS.txt` 和 `build-manifest.json` 记录发行文件校验值及源码提交。
第一方代码采用 MIT 许可证，详见包内 `LICENSE`。依赖与图片的许可范围见 `THIRD_PARTY_NOTICES.md`。

源码与更新：[Phasmophobia Toolbox](https://github.com/RainwoodL/Phasmophobia-Toolbox)。

## English

Extract the entire ZIP to a permanent directory on Windows 10/11 x64. Choose one of the two options below. Press **U** in game to open the toolbox menu.

- **Standalone injector.** Start the original game from Steam and enter the main menu. Run `release/standalone/PhasmophobiaInjector.exe` from the extracted directory. If it reports an access error, run it as administrator. Repeat after each game restart.
- **Embedded installation.** Close the game. Double-click `install-embedded.cmd` and enter the full path to your own original `Phasmophobia.exe`. Steam’s **Manage → Browse local files** opens its folder. Each player runs this installer on their own PC. It backs up the original, builds a copy for that installation and rolls back a failed deployment. Start the game from Steam. The toolbox begins loading after about five seconds.

To generate a candidate only, use `-PackageOnly` with `scripts/install-embedded.ps1`. Keep the extracted directory, including `release/backups/` and `release/embedded/embed-manifest.json`. To uninstall the embedded version or switch back to the injector, close the game and verify the game files in Steam or restore the original EXE from a backup matching your current game version. After a game update or a move to another directory, install again using the original EXE at its current location.

Release **v2026.11.25** targets Steam build **26893244**. All six CTest tests passed in both Debug and Release, and all eleven Python regression tests passed. In-game displays and multiplayer synchronization still need a live playtest. The archive includes `SHA256SUMS.txt`, `build-manifest.json`, the MIT `LICENSE` and `THIRD_PARTY_NOTICES.md`.
