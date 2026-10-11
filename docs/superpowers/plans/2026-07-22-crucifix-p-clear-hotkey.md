# 十字架 P 键清空记录 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:test-driven-development throughout, then use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在游戏窗口位于前台时，让 P 键按下边沿通过现有线程安全的 `crucifix_timer::ResetEvent()` 清空十字架记录，即使十字架显示未启用也能生效。

**Architecture:** 在无 Win32 依赖的 `feature_logic` 中增加纯函数 `HotkeyPressed(gameForeground, down, previousDown)`，集中定义“仅前台且仅按下边沿触发”的判定并用现有 `timer_logic_tests` 覆盖状态序列。`crucifix_timer` 只负责读取 P 键物理状态、保存上一帧状态并调用 `ResetEvent()`；Overlay 在与圣木热键相同的每帧位置传入既有 `gameForeground`，不读取 `CrucifixSettings::enabled`，也不改变配置结构。

**Tech Stack:** C++17、Win32 `GetAsyncKeyState`、CMake、CTest、MSVC

---

### Task 1: 用纯函数测试前台与按键边沿语义

**Files:**
- Modify: `payload/timer_logic_tests.cpp:18-46`
- Modify: `payload/feature_logic.h:17-29`
- Modify: `payload/feature_logic.cpp:31-35`

- [ ] **Step 1: 在测试中加入 P 键状态序列的失败断言**

在 `payload/timer_logic_tests.cpp` 现有 `IsGameForeground` 断言之后加入以下代码；用 `previousDown` 模拟每帧保存的物理键状态：

```cpp
    bool previousDown = false;
    assert(!HotkeyPressed(false, false, previousDown));

    // 游戏在前台时，首次按下产生且只产生一个上升沿。
    assert(HotkeyPressed(true, true, previousDown));
    previousDown = true;
    assert(!HotkeyPressed(true, true, previousDown));

    // 松开不触发；松开后重新按下可再次触发。
    assert(!HotkeyPressed(true, false, previousDown));
    previousDown = false;
    assert(HotkeyPressed(true, true, previousDown));

    // 非前台按下不触发，但调用方仍记录物理按下状态；按住切回前台也不触发。
    previousDown = false;
    assert(!HotkeyPressed(false, true, previousDown));
    previousDown = true;
    assert(!HotkeyPressed(true, true, previousDown));

    // 必须先松开，再重新按下才触发。
    assert(!HotkeyPressed(true, false, previousDown));
    previousDown = false;
    assert(HotkeyPressed(true, true, previousDown));
```

- [ ] **Step 2: 构建测试目标并确认 RED**

Run:

```powershell
cmake --build "K:\ToolsBOX\out" --config Debug --target timer_logic_tests
```

Expected: 编译失败，错误指向 `HotkeyPressed` 未声明；失败原因仅来自新增测试 API 尚不存在。

- [ ] **Step 3: 声明最小纯函数接口**

在 `payload/feature_logic.h` 的其他布尔纯函数旁加入：

```cpp
bool HotkeyPressed(bool gameForeground, bool down, bool previousDown);
```

- [ ] **Step 4: 实现最小边沿判定**

在 `payload/feature_logic.cpp` 的 `CanMoveOverlay` 附近加入：

```cpp
bool HotkeyPressed(bool gameForeground, bool down, bool previousDown) {
    return gameForeground && down && !previousDown;
}
```

该函数不持有状态；调用方必须在每帧判定后把当前物理 `down` 保存为下一帧的 `previousDown`，包括游戏不在前台的帧。

- [ ] **Step 5: 构建并运行聚焦测试，确认 GREEN**

Run:

```powershell
cmake --build "K:\ToolsBOX\out" --config Debug --target timer_logic_tests; ctest --test-dir "K:\ToolsBOX\out" -C Debug -R "^timer_logic_tests$" --output-on-failure
```

Expected: `timer_logic_tests` 构建成功并通过；断言覆盖前台首次按下、长按、松开重按、非前台按下和按住切回前台。

### Task 2: 在十字架计时器中接入 P 键清空

**Files:**
- Modify: `payload/crucifix_timer.h:14-19`
- Modify: `payload/crucifix_timer.cpp:10-20,41-56,124-149`

- [ ] **Step 1: 在十字架模块头文件公开每帧热键更新接口**

在 `ResetEvent()` 后加入：

```cpp
void ResetEvent();
void UpdateHotkeys(bool gameForeground);
void RequestResetPosition();
```

- [ ] **Step 2: 引入 Win32 按键读取 API**

在 `payload/crucifix_timer.cpp` 的 ImGui include 之后、标准库 include 之前加入：

```cpp
#include <windows.h>
```

继续使用项目已有的 `user32` 链接依赖，不修改 `payload/CMakeLists.txt`。

- [ ] **Step 3: 增加 P 键上一帧状态**

在匿名命名空间的模块状态中加入：

```cpp
bool g_pKeyDown = false;
```

它只表示上一帧读取到的 P 键物理状态，不进入设置存储，不增加配置字段或配置版本。

- [ ] **Step 4: 实现最小 `UpdateHotkeys`**

在 `ResetEvent()` 后加入：

```cpp
void UpdateHotkeys(bool gameForeground) {
    const bool down = (GetAsyncKeyState('P') & 0x8000) != 0;
    if (feature_logic::HotkeyPressed(gameForeground, down, g_pKeyDown)) ResetEvent();
    g_pKeyDown = down;
}
```

保持函数不读取 `settings_store::Get().crucifix.enabled`：十字架显示关闭时也必须清理旧事件。始终执行 `g_pKeyDown = down`，保证非前台按住 P 后切回游戏不会误触发。

- [ ] **Step 5: 在关闭模块时重置边沿状态**

在 `Shutdown()` 末尾现有 `ResetEvent();` 之前插入 `g_pKeyDown = false;`，使末尾保持为：

```cpp
    g_gameTimeProvider = nullptr;
    g_pKeyDown = false;
    ResetEvent();
}
```

不要增加第二次 `ResetEvent()` 调用。保留现有 `ResetEvent()` 实现：它继续在 `g_eventMutex` 保护下执行 `g_event = {}`，一次清除 `valid`、`burnedAt` 和 `gameSeconds`，从而清空燃烧计时、记录的游戏时间和当前十字架事件状态。不要改变启用、固定、位置、字体或颜色设置。

- [ ] **Step 6: 构建 payload 验证模块接口与 Win32 调用**

Run:

```powershell
cmake --build "K:\ToolsBOX\out" --config Debug --target payload
```

Expected: Debug `payload` 编译和链接成功；`UpdateHotkeys` 声明、实现及 `GetAsyncKeyState` 的既有 `user32` 依赖均解析成功。

### Task 3: 在 Overlay 每帧调用十字架热键

**Files:**
- Modify: `payload/overlay.cpp:390-398`

- [ ] **Step 1: 在圣木热键更新旁加入十字架热键更新**

将 `Render()` 中的热键更新保持在同一处，形成：

```cpp
    UpdateMenuToggle(gameForeground);
    smudge_timer::UpdateHotkeys(gameForeground);
    crucifix_timer::UpdateHotkeys(gameForeground);
    crosshair_info::SyncSettings();
```

调用必须位于读取 `settings_store::Get()` 和任何 `settings.crucifix.enabled` 判断之前，因此每个 Overlay 帧都会执行，不依赖十字架显示状态。

- [ ] **Step 2: 构建 Overlay 所属 payload 目标**

Run:

```powershell
cmake --build "K:\ToolsBOX\out" --config Debug --target payload
```

Expected: Debug `payload` 构建成功，Overlay 可解析 `crucifix_timer::UpdateHotkeys(bool)`。

- [ ] **Step 3: 运行 Debug 聚焦回归测试**

Run:

```powershell
ctest --test-dir "K:\ToolsBOX\out" -C Debug -R "^timer_logic_tests$" --output-on-failure
```

Expected: `timer_logic_tests` 通过，纯函数边沿语义未被集成改动破坏。

### Task 4: 全量构建、CTest 与默认 EXE 验证

**Files:**
- Verify only: `K:\ToolsBOX\out`
- Verify artifact: `K:\ToolsBOX\out\bin\Release\PhasmophobiaInjector.exe`

- [ ] **Step 1: 重新配置默认 `out` 构建目录**

Run:

```powershell
cmake -S "K:\ToolsBOX" -B "K:\ToolsBOX\out" -A x64
```

Expected: 配置和生成成功；沿用现有 C++17、CTest、payload 与 `PhasmophobiaInjector` 目标，不出现新的配置字段或迁移目标。

- [ ] **Step 2: 执行 Debug 全量构建**

Run:

```powershell
cmake --build "K:\ToolsBOX\out" --config Debug
```

Expected: Debug 全部目标构建成功，包括 payload、`timer_logic_tests`、`settings_store_tests` 和注入器。

- [ ] **Step 3: 执行 Debug CTest**

Run:

```powershell
ctest --test-dir "K:\ToolsBOX\out" -C Debug --output-on-failure
```

Expected: 100% tests passed；新增热键状态序列断言随 `timer_logic_tests` 通过。

- [ ] **Step 4: 执行 Release 全量构建**

Run:

```powershell
cmake --build "K:\ToolsBOX\out" --config Release
```

Expected: Release 全部目标构建成功；默认构建生成 payload、将其嵌入并链接 `PhasmophobiaInjector`。

- [ ] **Step 5: 执行 Release CTest**

Run:

```powershell
ctest --test-dir "K:\ToolsBOX\out" -C Release --output-on-failure
```

Expected: 100% tests passed，Release `timer_logic_tests` 与 `settings_store_tests` 均通过。

- [ ] **Step 6: 验证默认 Release EXE 产物**

Run:

```powershell
$artifact = "K:\ToolsBOX\out\bin\Release\PhasmophobiaInjector.exe"; if (-not (Test-Path $artifact -PathType Leaf)) { throw "Missing release injector: $artifact" }; $file = Get-Item $artifact; if ($file.Length -le 0) { throw "Empty release injector: $artifact" }; $file | Select-Object FullName,Length,LastWriteTime
```

Expected: 输出 `K:\ToolsBOX\out\bin\Release\PhasmophobiaInjector.exe`，文件长度大于 0，时间戳来自本次 Release 构建。

- [ ] **Step 7: 做最终静态审查**

逐项确认：

1. `HotkeyPressed` 仅在 `gameForeground && down && !previousDown` 时返回 true；测试明确覆盖前台、首次边沿、长按、松开重按、非前台及按住切回前台。
2. `crucifix_timer::UpdateHotkeys` 每帧读取 `'P'`，判定后无条件保存物理键状态，只在纯函数返回 true 时调用现有线程安全的 `ResetEvent()`。
3. `ResetEvent()` 仍在 `g_eventMutex` 下把 `g_event` 置空；没有修改 `CrucifixSettings` 的启用、固定、位置、字体或颜色。
4. `UpdateHotkeys` 不读取 `enabled`；Overlay 在 `smudge_timer::UpdateHotkeys(gameForeground)` 同处调用它。
5. `Shutdown()` 将 `g_pKeyDown` 重置为 false；未新增设置字段，未改变配置版本。
6. 按规格不启动注入器、不执行游戏注入；游戏内 P 键的最终交互确认明确留给实际运行验收，本计划的自动验证范围止于纯函数测试、Debug/Release 构建、CTest 和 EXE 产物检查。
