# Phasmophobia Toolbox · 独立注入器使用说明

适用于 Windows 10/11 x64。**v2026.11.25** 的 Windows x64 ZIP 在根目录提供独立注入器 `Toolsbox.exe` 和内置工具箱的游戏启动程序 `Phasmophobia.exe`。工具箱为中文界面，默认按 **U** 显示或隐藏菜单。

## 使用

1. 解压整个 ZIP。若之前使用了嵌入安装，先关闭游戏，通过 Steam 验证游戏文件，或用与当前游戏版本匹配的备份恢复原版 EXE。
2. 从 Steam 启动原版恐鬼症，进入主菜单。
3. 运行解压目录中的 `Toolsbox.exe`，等待启动提示。
4. 返回游戏按 **U**。遇到访问权限提示时，以管理员身份运行注入器。

EXE 已包含工具箱 DLL，不需要复制其他 DLL。每次重新启动游戏后重新运行注入器。
设置保存在 `%LOCALAPPDATA%/GhostRoomTools/settings.bin`。

默认 **I** 开始圣木计时、**O** 清空圣木计时、**P** 清空十字架计时，按住 **Alt** 拖动功能位置。
快捷键、显示位置、字体和颜色可在菜单中修改。

## 版本与恢复

本版已对照 Steam build **26893244** 的元数据核验。Debug 和 Release 各通过 7/7 项 CTest，包含嵌入式启动程序测试。Python 比较器的 11/11 项回归测试通过。游戏内显示与真实多人同步仍待实测。
游戏更新后请查看新版发行说明。

关闭游戏后本次注入结束。可用发行页列出的 SHA-256 核验下载的 ZIP。两个 EXE 都内附版权和许可声明。
第一方代码采用 MIT 许可证，依赖与图片范围见第三方声明。[MIT](../LICENSE) · [第三方声明](../THIRD_PARTY_NOTICES.md)

源码与更新：[Phasmophobia Toolbox](https://github.com/RainwoodL/Phasmophobia-Toolbox)。

## English

Extract the **v2026.11.25** Windows x64 ZIP. If you previously installed the embedded launcher, close the game and restore the original EXE by verifying game files in Steam or using a backup for your current game version. Start the original game from Steam, enter the main menu, then run `Toolsbox.exe` from the extracted directory.
Press **U** in game to toggle the toolbox. Run the injector again after each game restart.
If access is denied, run the injector as administrator. No separate payload DLL is required.
Settings: `%LOCALAPPDATA%/GhostRoomTools/settings.bin`.

Checked against Steam build **26893244**. All seven CTest tests passed in both Debug and Release, including the embedded launcher test. All eleven Python regression tests passed. In-game displays and multiplayer synchronization still need a live playtest.
The release page lists the ZIP’s SHA-256. Both executables include license notices.
