# Phasmophobia Toolbox · 注入式发行版

Windows 10/11 x64。中文界面，默认按 **U** 显示/隐藏菜单。

## 使用

1. 解压整个 ZIP。
2. 从 Steam 启动恐鬼症，进入主菜单。
3. 运行 `PhasmophobiaInjector.exe`，等待启动提示。
4. 返回游戏按 **U**。遇到访问权限提示时，以管理员身份运行注入器。

EXE 已包含工具箱 DLL，不需要复制其他 DLL。每次重新启动游戏后重新运行注入器。
设置保存在 `%LOCALAPPDATA%/GhostRoomTools/settings.bin`。

默认 **I** 开始圣木计时、**O** 清空圣木计时、**P** 清空十字架计时，按住 **Alt** 拖动功能位置。
快捷键、显示位置、字体和颜色可在菜单中修改。

## 版本与恢复

本次版本核对日期为 2026-10-11，Steam build 25811160；最新 dump 的 100 项既有元数据契约通过。
构建、单元测试和隔离嵌入验证通过，尚未完成真实游戏内运行验收。
游戏更新后请查看新版发行说明。

关闭游戏后本次注入结束，无需替换游戏文件。`SHA256SUMS.txt` 和 `build-manifest.json` 用于核对发行文件及源码提交。
第一方代码采用 [MIT](LICENSE)，依赖与图片范围见 [第三方声明](THIRD_PARTY_NOTICES.md)。

源码与更新：[Phasmophobia Toolbox](https://github.com/RainwoodL/Phasmophobia-Toolbox)。

## English

Extract the ZIP, start Phasmophobia from Steam, enter the main menu, then run `PhasmophobiaInjector.exe`.
Press **U** in game to toggle the toolbox. Run the injector again after each game restart.
If access is denied, run the injector as administrator. No separate payload DLL is required.
Settings: `%LOCALAPPDATA%/GhostRoomTools/settings.bin`.

Checked on 2026-10-11 against Steam build 25811160 and 100 metadata contracts. Live game validation is still pending.
See `SHA256SUMS.txt`, `build-manifest.json`, `LICENSE` and `THIRD_PARTY_NOTICES.md` for provenance and licensing.
