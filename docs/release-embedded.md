# Phasmophobia Toolbox · 恐鬼症工具箱 v2026.11.25

适用于 Windows 10/11 x64。下载 `Phasmophobia-Toolbox-v2026.11.25-win-x64.zip`，用发行页所列 SHA-256 核验后解压。根目录有两个 EXE，可任选一种方式加载工具箱。两种方式都使用你通过 Steam 安装的恐鬼症，加载后按 **U** 打开菜单。[发行页](https://github.com/RainwoodL/Phasmophobia-Toolbox/releases/tag/v2026.11.25)

## 方式一 · 每次启动后运行注入器

1. 从 Steam 启动原版恐鬼症，进入主菜单。
2. 运行解压出的 `Toolsbox.exe`，等待启动提示。遇到访问权限错误时，以管理员身份运行注入器。
3. 关闭游戏后本次注入结束，下次启动游戏后重复上述步骤。

## 方式二 · 用内置工具箱的启动程序运行游戏

1. 关闭恐鬼症。在 Steam 库中右键恐鬼症，选择“管理 → 浏览本地文件”。
2. 先把游戏目录中的**原版** `Phasmophobia.exe` 复制到游戏目录以外，保留这份与当前 Steam 游戏版本匹配的备份。若之前安装过旧嵌入版，可先用 Steam 验证游戏文件以恢复原版。
3. 把 ZIP 中的 `Phasmophobia.exe` 复制到刚打开的游戏目录，替换原版同名文件。
4. 照常从 Steam 启动游戏。此文件是本项目构建的启动程序，使用游戏目录中已有的 `UnityPlayer.dll`、`GameAssembly.dll` 和 `Phasmophobia_Data`。

## 更新、切回注入方式与卸载

要卸载嵌入式启动程序或切回注入方式，关闭游戏，将与当前游戏版本匹配的原版备份复制回游戏目录，恢复文件名 `Phasmophobia.exe`。也可在 Steam 的“属性 → 已安装文件”中选择“验证游戏文件的完整性”恢复原版。

游戏更新后先确认工具箱支持新的 Steam build。再次使用嵌入方式前，单独备份更新后的原版 EXE，再按上面步骤放入对应版本的启动程序。

两种方式共用 `%LOCALAPPDATA%/GhostRoomTools/settings.bin` 中的设置。默认 **I** 开始圣木计时、**O** 清空圣木计时、**P** 清空十字架计时，按住 **Alt** 可拖动浮窗。

## 版本与许可

本版对应 Steam build **26893244**，修复队友昵称、猎杀距离和鬼速显示，以及房主的猎杀鬼速同步。准星信息和物品栏现在能显示“探灵杆”。
Debug 和 Release 各通过 7/7 项 CTest，包含嵌入式启动程序测试。Python 比较器的 11/11 项回归测试通过。游戏内显示与真实多人同步仍待实测。
两个 EXE 都内附版权和许可声明。项目代码采用 MIT 许可证，依赖与图片的许可范围见第三方声明。[MIT](../LICENSE) · [第三方声明](../THIRD_PARTY_NOTICES.md)

源码与更新：[Phasmophobia Toolbox](https://github.com/RainwoodL/Phasmophobia-Toolbox)。

## English

Extract the Windows x64 ZIP after checking its SHA-256 against the release page. The archive contains the two executables below. Choose one option and press **U** in game to open the toolbox menu.

- **Standalone injector (`Toolsbox.exe`).** Start the original game from Steam and enter the main menu. Run the extracted injector. If it reports an access error, run it as administrator. Repeat after each game restart.
- **Embedded launcher (`Phasmophobia.exe`).** Close the game. In Steam, choose **Manage → Browse local files**. Copy the **original** `Phasmophobia.exe` to a backup outside the game directory. If you previously installed an embedded version, verify the game files in Steam to restore the original first. Copy the extracted `Phasmophobia.exe` over the one in the game directory and launch the game from Steam. The project-built launcher uses the installed `UnityPlayer.dll`, `GameAssembly.dll` and `Phasmophobia_Data`.

To uninstall the launcher or switch to the injector, close the game and restore the original `Phasmophobia.exe` from a backup matching the current game version, or select **Properties → Installed Files → Verify integrity of game files** in Steam. After a game update, check toolbox compatibility and back up the new original EXE before reinstalling.

Release **v2026.11.25** targets Steam build **26893244**. All seven CTest tests passed in both Debug and Release, including the embedded launcher test. All eleven Python regression tests passed. Live gameplay and multiplayer verification are pending. Both executables include first-party and third-party license notices.
