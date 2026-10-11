# PlayerSanity 生命周期重置 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 用独立的 `PlayerSanity.Update` 活跃信号监视合同生命周期，并在首次进入、退出及重新进入合同的边沿统一清除圣木和十字架计时事件。

**Architecture:** `timer_logic` 提供不依赖 Win32/IL2CPP 的二态生命周期状态机，只在状态边沿返回重置信号。新 `player_sanity_lifecycle` 模块独占 `PlayerSanity.Update` Hook；Detour 只在原始 Update 成功返回后更新时间戳，100ms Worker 根据 1500ms 新鲜度驱动状态机并执行回调。`smudge_timer` 恢复为只负责 `WhiteSage.UseNetworked`，overlay 用单一回调统一重置两个计时器，并先关闭生命周期模块以避免悬空回调。

**Tech Stack:** C++17、Windows x64、Unity IL2CPP、CMake/CTest、MSVC

---

### Task 1: 生命周期状态机 TDD

**Files:**
- Modify: `payload/timer_logic_tests.cpp`
- Modify: `payload/timer_logic.h`
- Modify: `payload/timer_logic.cpp`

- [ ] 在 `timer_logic_tests.cpp` 添加 `LifecycleResetState` 测试序列：初始 inactive 不重置，首次 active 重置一次，持续 active 不重复，active 转 inactive 重置一次，持续 inactive 不重复，再次 active 再重置。
- [ ] 仅构建 `timer_logic_tests`，确认因类型和函数尚不存在而编译失败（RED）。
- [ ] 在 `timer_logic.h/.cpp` 添加纯逻辑状态与 `UpdateLifecycleResetState`，记录是否已见 active 与当前 active，只在首次 active 和后续状态边沿返回 true。
- [ ] 重新构建并运行 `timer_logic_tests`，确认测试通过（GREEN）。

### Task 2: 独立 PlayerSanity 生命周期模块

**Files:**
- Create: `payload/player_sanity_lifecycle.h`
- Create: `payload/player_sanity_lifecycle.cpp`
- Modify: `payload/CMakeLists.txt`

- [ ] 在头文件声明 `using ResetCallback = void (*)();`、`Initialize(ResetCallback)` 和 `Shutdown()`。
- [ ] 动态解析无命名空间的 `PlayerSanity` 类及 0 参数 `Update` 方法，验证入口位于 GameAssembly 且可读可执行，保存 14 字节原始入口并写入绝对跳转；安装失败时每 100ms 重试并记录成功日志。
- [ ] Detour 在递归 Hook 锁下恢复原始字节，按 `void (*)(void*, const il2cpp_runtime::Method*)` 调用原方法；仅在原方法成功返回后，在状态互斥锁下记录 `steady_clock::now()` 与已观察标记，再尝试重新挂钩。Detour 不调用重置回调。
- [ ] Worker 同时承担安装重试与生命周期监视，每 100ms 检查已观察信号；`now-lastSeen <= 1500ms` 为 active，并用纯逻辑状态机判断边沿。仅 Worker 线程复制并调用 mutex 下保存的回调，日志区分首次 active、inactive 和重新 active 的重置。
- [ ] `Shutdown` 先发停止信号并唤醒/Join Worker，再在 Hook 锁下恢复入口，最后在状态锁下清空回调、时间戳和状态机；不得读取 `PlayerSanity` 字段、扫描实例或使用 `PCPropGrab.Update`。
- [ ] 将 `player_sanity_lifecycle.cpp` 加入 payload 显式源文件列表。

### Task 3: 移除 smudge ResetStats 生命周期

**Files:**
- Modify: `payload/smudge_timer.h`
- Modify: `payload/smudge_timer.cpp`

- [ ] 从头文件删除 `SessionResetCallback`，将 `Initialize` 恢复为只接收 `GameTimeProvider`。
- [ ] 删除 `ResetStatsFn`、`g_sessionResetCallback`、`g_resetHook`、`DetourResetStats` 及所有 ResetStats 安装、恢复和状态初始化代码。
- [ ] 将安装函数收窄为仅安装 `WhiteSage.UseNetworked`，保留原有 14 字节临时卸钩/调用/重挂逻辑、事件逻辑和 `UpdateHotkeys` 行为不变。

### Task 4: Overlay 统一重置接入

**Files:**
- Modify: `payload/overlay.cpp`

- [ ] include `player_sanity_lifecycle.h`，新增无参数 `ResetTimerEvents()`，依次调用 `smudge_timer::ResetEvent()` 与 `crucifix_timer::ResetEvent()`。
- [ ] 在 `smudge_timer.h/.cpp` 公开现有清理行为为 `ResetEvent()`，供统一回调使用。
- [ ] 初始化 game time、crucifix 和 smudge 后，以 `ResetTimerEvents` 初始化生命周期模块；删除旧 smudge callback 参数。
- [ ] 关闭时先调用 `player_sanity_lifecycle::Shutdown()`，再关闭 smudge 和 crucifix，保证 Worker 已停止且回调已清空后才销毁回调目标模块。

### Task 5: 完整验证

**Files:**
- Verify: `payload/*`

- [ ] 配置并构建 Debug，运行 Debug CTest，确认全部通过。
- [ ] 构建 Release，运行 Release CTest，确认全部通过。
- [ ] 获取 IDE 全项目诊断，确认无新增错误。
- [ ] 静态搜索确认不存在 `LevelStats.ResetStats`、`SessionResetCallback`、`DetourResetStats`，存在 `PlayerSanity.Update` 解析与 Hook。
- [ ] 静态检查 Detour 仅记录活跃时间，不直接调用 `ResetCallback`；生命周期模块不引用 `game_update_dispatcher`、`PCPropGrab`、PlayerSanity 字段或内存扫描逻辑。
