# 等级枚举与局初始化重置 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 修正装备等级的最新 0..2 枚举映射，并让圣木与十字架事件仅在 `LevelStats.ResetStats` 成功执行后统一清除。

**Architecture:** `smudge_timer` 继续独占 `LevelStats.ResetStats` Hook，并通过受 Hook 互斥锁保护的函数指针通知 `crucifix_timer`。十字架删除逐帧游戏时间回退检测，仅保留游戏时间提供器用于燃烧记录。

**Tech Stack:** C++17、Windows x64、Unity IL2CPP、CMake/CTest、MSVC

---

### Task 1: 装备等级枚举 TDD

**Files:**
- Modify: `payload/timer_logic_tests.cpp`
- Modify: `payload/feature_logic.cpp`

- [ ] 将测试改为覆盖 `tier` 0、1、2 分别显示一级、二级、三级，`showTier=false` 和异常等级不追加。
- [ ] 构建并运行 `timer_logic_tests`，确认旧实现因 tier 0 不显示、tier 1 显示一级而 RED。
- [ ] 将 `FormatEquipmentLabel` 改为直接用 0..2 索引 `tiers[tier]`，异常值不追加。
- [ ] 再次运行测试确认 GREEN。

### Task 2: ResetStats 统一清理事件

**Files:**
- Modify: `payload/smudge_timer.h`
- Modify: `payload/smudge_timer.cpp`
- Modify: `payload/crucifix_timer.h`
- Modify: `payload/crucifix_timer.cpp`
- Modify: `payload/overlay.cpp`

- [ ] 为 `smudge_timer::Initialize` 增加 `SessionResetCallback` 参数，并在 `g_hookMutex` 保护下保存和清空回调。
- [ ] `DetourResetStats` 仅在成功恢复并调用原方法后先 `ClearEvent()`，再调用回调；调用回调时不持有事件锁。
- [ ] 调整安装工作线程，优先安装并记录 `LevelStats.ResetStats` Hook，再安装 `WhiteSage.UseNetworked` Hook。
- [ ] 在 `crucifix_timer` 公开 `ResetEvent()`，删除逐帧回退状态、检测函数和 `Update` API。
- [ ] Overlay 按 game time、crucifix、smudge 顺序初始化并注入 `ResetEvent`；按 smudge、crucifix 的安全顺序关闭；删除每帧十字架 Update。

### Task 3: 完整验证

**Files:**
- Verify: `payload/*`

- [ ] 串行构建 Debug 并运行 Debug CTest。
- [ ] 串行构建 Release 并运行 Release CTest。
- [ ] 检查 IDE 诊断。
- [ ] 静态确认十字架中无 `Update`、`DetectSessionReset`、`SessionTimeWentBack`，且装备等级仅接受 0..2。
