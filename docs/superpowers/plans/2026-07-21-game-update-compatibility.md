# Game Update Compatibility Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 兼容最新游戏字段布局，并保证 U/I/O/Alt 仅在非空游戏窗口处于前台时生效。

**Architecture:** 用 `timer_logic` 的纯逻辑 helper 判断前台窗口，overlay 将结果传给菜单与圣木热键，后台仍同步物理按键边沿。IL2CPP 公共层按字段名或唯一字段类型动态解析偏移；仅 PCPropGrab 的两个同类型 Collider 使用已核实的新固定偏移并记录日志，关键元数据缺失时功能安全停用并重试。

**Tech Stack:** C++17、Win32、IL2CPP 导出 API、CMake/CTest、MSVC x64

---

### Task 1: 前台判定 TDD

**Files:**
- Modify: `payload/timer_logic_tests.cpp`
- Modify: `payload/timer_logic.h`
- Modify: `payload/timer_logic.cpp`

- [ ] 增加 `IsGameForeground` 测试：`(0,0)`、任一为空、不同非零均为 false，仅相等非零为 true。
- [ ] 先构建 `timer_logic_tests`，记录未声明符号的 RED 证据。
- [ ] 最小实现 `return foreground != 0 && game != 0 && foreground == game;`。
- [ ] 重建并运行 Debug CTest，记录 GREEN。

### Task 2: 前台热键门控

**Files:**
- Modify: `payload/overlay.cpp`
- Modify: `payload/smudge_timer.h`
- Modify: `payload/smudge_timer.cpp`

- [ ] 将菜单更新改为 `UpdateMenuToggle(bool gameForeground)`；每帧采样 U 并同步边沿，仅前台上升沿切换。
- [ ] 将圣木热键改为 `UpdateHotkeys(bool gameForeground)`；每帧采样 I/O 并同步边沿，仅前台上升沿记录/清除。
- [ ] overlay 用 `GetForegroundWindow()`、非空 `g_hwnd` 和 helper 计算一次前台状态；Alt 动作也必须受该状态门控。
- [ ] 搜索全部 `GetAsyncKeyState`，确认后台采样不执行动作且后台按住切回不会补触发。

### Task 3: IL2CPP 字段解析 API

**Files:**
- Modify: `payload/il2cpp_runtime.h`
- Modify: `payload/il2cpp_runtime.cpp`

- [ ] 解析并清理 `il2cpp_field_get_name` 导出。
- [ ] 公开 `FindInstanceFieldByName(Class*, const char*, std::uintptr_t&)`，遍历字段、排除 static，并只接受 `0 < offset < 0x1000`。
- [ ] `FindInstanceFieldByType` 仅在目标类型字段唯一时成功，避免误选同类型第一项；保留返回全部候选的接口。

### Task 4: 十字架 useCount 动态解析

**Files:**
- Modify: `payload/crucifix_timer.cpp`
- Verify: `tools/Il2CppDumper/dump.cs`

- [ ] 核实最新 `Crucifix.useCount == 0x3E0` 且 `CrucifixUseNetworked` 存在。
- [ ] 删除旧 `0x3D8` 常量；安装 Hook 前按名解析 `useCount`，校验范围并输出 resolved offset，失败则不安装。
- [ ] detour 前后均通过 resolved offset 读取并保留 `0..5` 合法范围变化检测；Shutdown 清空 resolved offset。

### Task 5: 准星信息元数据兼容

**Files:**
- Modify: `payload/crosshair_info.cpp`
- Verify: `tools/Il2CppDumper/dump.cs`

- [ ] 动态解析 PCPropGrab 的唯一 PCCanvas、Equipment 的唯一 StoreItem、StoreItem 的 `equipmentType/tier`，可选动态解析 PhotonObjectInteract 的唯一 Prop。
- [ ] PCPropGrab 两个 Collider 因类型相同无法区分，固定使用最新 `0x80/0xD8` 并明确日志；PCCanvas state 保持 `0x88`。
- [ ] 所有读取改用 Metadata offset，不再读取旧 `0x78/0xD0/0x140`；关键 offset 缺失时 metadata 保持 invalid、发布空快照并允许后续重试。
- [ ] 输出 canvas、collider、storeItem、equipmentType、tier 与可选 photonProp 的 resolved offset 证据。

### Task 6: Dispatcher 与完整验证

**Files:**
- Verify: `payload/game_update_dispatcher.cpp`
- Verify: `tools/Il2CppDumper/dump.cs`
- Verify: `payload/*.cpp`, `payload/*.h`

- [ ] 确认最新 PCPropGrab 仍有 `Update()`；仅在不存在时调整 dispatcher。
- [ ] 构建前搜索相关模块，确认旧 `0x3D8`、`0x78/0xD0/0x140` 已消失，并检查所有热键门控。
- [ ] 运行工作区诊断。
- [ ] 依次执行 Debug 全量构建与 CTest，再执行 Release 全量构建与 CTest。
- [ ] 汇报 RED/GREEN、dump/resolved offset 证据和需在最新游戏内验证的行为；不提交代码。
