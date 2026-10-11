# 圣木计时器与游戏时间 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在现有游戏内 Overlay 中加入可独立移动、固定并跨运行保存设置的圣木点燃计时器和本局游戏时间，同时落实“功能独立 + 公共层”的最终文件边界。

**Architecture:** `overlay.cpp/.h` 只管理 ImGui 生命周期、菜单总入口和模块调度；圣木计时器与游戏时间分别由独立模块完整实现，互不包含、互不调用，只依赖 IL2CPP 运行时、设置存储和纯逻辑公共层。圣木记录时间通过顶层注入的只读时间提供器取得，避免 `smudge_timer` 对 `game_time` 的源代码依赖。

**Tech Stack:** C++17、Windows x64、Unity IL2CPP、Dear ImGui、D3D11、CMake/CTest、MSVC

**已批准 UI 变更（2026-07-20）：** 功能列表项改为 36 高并以分隔线相隔；两个功能的字号改为 16、20、24、26、28、32、36、42、48、56 px 十档 Combo（默认 26），旧配置按最近档吸附；新增公共 `font_store` 预加载各档中文字体，功能窗口使用 `PushFont/PopFont`，不再缩放字体纹理。

---

## 最终文件结构

- Create: `payload/timer_logic.h` — 纯时间、位置、字号、颜色和配置头校验接口。
- Create: `payload/timer_logic.cpp` — 仅依赖 C++ 标准库的纯逻辑实现。
- Create: `payload/timer_logic_tests.cpp` — 纯逻辑红—绿单元测试。
- Create: `payload/il2cpp_runtime.h` — IL2CPP API、类/方法解析及安全读取公共接口。
- Create: `payload/il2cpp_runtime.cpp` — 公共运行时解析、模块范围和内存权限校验实现。
- Create: `payload/settings_store.h` — 两个功能的持久化 DTO 与加载、脏标记、延迟保存接口。
- Create: `payload/settings_store.cpp` — 本地配置路径、格式校验、延迟保存和原子写入实现。
- Create: `payload/game_time.h` — 游戏时间生命周期、只读快照、窗口和专属设置入口。
- Create: `payload/game_time.cpp` — `LevelStats` 查询、本局时间窗口和专属设置实现。
- Create: `payload/smudge_timer.h` — 圣木生命周期、时间提供器契约、窗口和专属设置入口。
- Create: `payload/smudge_timer.cpp` — WhiteSage Hook、玩家名、事件快照、圣木窗口和专属设置实现。
- Modify: `payload/overlay.cpp` — 移出功能与公共层实现，仅保留 ImGui 生命周期、菜单总入口和模块调度。
- Keep public surface: `payload/overlay.h` — 保持 `Initialize()`、`Shutdown()`、`Render()` 生命周期入口；如需字体选择等调度接口，只能增加与 ImGui 生命周期直接相关的接口。
- Modify: `payload/CMakeLists.txt` — 编译全部新模块并注册 CTest。
- Keep: `payload/payload.cpp` — 继续负责 DLL 与 D3D11 Hook 顶层入口，不加入功能实现。

## 强制依赖规则

- `smudge_timer.cpp/.h` 不包含 `game_time.h`，不调用 `game_time::*`。
- `game_time.cpp/.h` 不包含 `smudge_timer.h`，不调用 `smudge_timer::*`。
- 两个功能只能依赖 `il2cpp_runtime`、`timer_logic` 和 `settings_store` 的公共契约。
- `overlay.cpp` 可以同时包含两个功能头文件以完成顶层装配，但不得包含 WhiteSage Hook、LevelStats、安全内存读取、事件状态、功能窗口细节、功能专属控件、配置序列化或磁盘 I/O。
- `LevelStats` 的类、方法、实例和字段偏移只出现在 `game_time.cpp/.h`。
- `WhiteSage`、玩家名路径、Hook 状态和事件快照只出现在 `smudge_timer.cpp/.h`。
- 所有 IL2CPP 导出指针、类/方法通用解析、安全读取和可执行地址校验只出现在 `il2cpp_runtime.cpp/.h`。
- 所有配置文件加载、脏标记、延迟保存、原子写入只出现在 `settings_store.cpp/.h`。

---

### Task 1: 用 TDD 建立纯计时、位置与配置校验逻辑

**Files:**
- Create: `payload/timer_logic.h`
- Create: `payload/timer_logic.cpp`
- Create: `payload/timer_logic_tests.cpp`
- Modify: `payload/CMakeLists.txt`

- [ ] **Step 1: 先创建失败测试目标**

在 `payload/CMakeLists.txt` 中启用 CTest，新增仅编译 `timer_logic_tests.cpp` 和 `timer_logic.cpp` 的 `timer_logic_tests` 目标，设置 C++17 与当前 payload 一致的 MSVC 运行库和 `/utf-8`。将 `timer_logic.cpp` 同时加入 `payload` 源文件。

- [ ] **Step 2: 先写时间与位置失败测试**

测试覆盖：

```cpp
assert(FormatMmSs(0) == "00:00");
assert(FormatMmSs(59) == "00:59");
assert(FormatMmSs(60) == "01:00");
assert(FormatMmSs(7507) == "125:07");
assert(NormalizeSeconds(-1.0f) == 0);
assert(NormalizeSeconds(std::numeric_limits<float>::quiet_NaN()) == 0);
assert(NormalizeSeconds(59.9f) == 59);
assert(ElapsedSeconds(1000, 999) == 0);
assert(ElapsedSeconds(1000, 2501) == 1);
assert(ClampPosition({-20, 900}, {200, 80}, {800, 600}) == Position{0, 520});
assert(ClampPosition({50, 50}, {900, 700}, {800, 600}) == Position{0, 0});
```

配置并构建测试目标，确认因接口尚未实现而失败：

```powershell
cmake -S "K:\ToolsBOX" -B "K:\ToolsBOX\out" -G "Visual Studio 18 2026" -A x64
cmake --build "K:\ToolsBOX\out" --config Debug --target timer_logic_tests --parallel
```

- [ ] **Step 3: 实现最小时间与位置接口**

在 `timer_logic.h/.cpp` 实现 `Position`、`Size`、`NormalizeSeconds`、`ElapsedSeconds`、`FormatMmSs` 和 `ClampPosition`。分钟至少两位且超过一小时继续累计分钟；非法秒数归零；时钟回退归零；位置范围为 `[0, viewport-window]`。

- [ ] **Step 4: 先写字号、颜色和配置头失败测试**

增加 `Color`、`ClampFontSize`、`ClampColor` 和 `IsConfigHeaderValid` 的测试，覆盖字号 16–56、颜色 0–1、NaN 使用默认分量、错误魔数、错误版本、错误 payload 长度和正确头。先构建并确认新符号未定义。

- [ ] **Step 5: 实现最小值域与配置头校验逻辑**

实现新接口；纯逻辑文件不得包含 Windows、ImGui、D3D11 或 IL2CPP 头文件。

- [ ] **Step 6: 运行 Debug 单测**

```powershell
cmake --build "K:\ToolsBOX\out" --config Debug --target timer_logic_tests --parallel
ctest --test-dir "K:\ToolsBOX\out" -C Debug -R "^timer_logic_tests$" --output-on-failure
```

Expected: `100% tests passed`。

### Task 2: 抽取 IL2CPP 公共运行时层

**Files:**
- Create: `payload/il2cpp_runtime.h`
- Create: `payload/il2cpp_runtime.cpp`
- Modify: `payload/overlay.cpp`
- Modify: `payload/CMakeLists.txt`

- [ ] **Step 1: 定义与功能无关的公共接口**

在 `il2cpp_runtime.h` 声明：初始化/关闭、`FindClass(namespaceName, className)`、按名称和显式参数数查找方法、取得方法入口、模块内地址校验、可执行地址校验、范围可读校验、安全读取指针/整数/浮点/字节，以及受长度限制的 IL2CPP UTF-16 字符串复制接口。接口不得出现 `WhiteSage`、`LevelStats`、玩家、圣木或 UI 类型。

- [ ] **Step 2: 从 Overlay 移出 IL2CPP 导出解析**

将 `il2cpp_domain_get`、`il2cpp_domain_get_assemblies`、`il2cpp_assembly_get_image`、`il2cpp_class_from_name`、`il2cpp_class_get_method_from_name` 等函数指针和 `GameAssembly.dll` 模块信息移动到 `il2cpp_runtime.cpp`。保留现有动态解析方式，不硬编码方法 RVA。

- [ ] **Step 3: 从 Overlay 移出安全读取和地址权限校验**

移动 `IsPointerReadable`、指针/整数读取等通用实现，并补齐浮点读取、范围溢出保护、模块映像范围检查、页面可执行检查和 UTF-16 到拥有型 UTF-8 字符串复制。字符串长度限定为 1–128 个 UTF-16 code unit；失败返回调用方指定的默认值。

- [ ] **Step 4: 迁移现有 GhostAI 使用点**

现有 Overlay 中仍需保留的旧功能通过 `il2cpp_runtime` 公共接口完成类/方法解析和安全读取，确保抽取不改变旧功能行为。任何具体旧功能状态应留在其现有职责位置，本任务只移动通用运行时能力。

- [ ] **Step 5: 检查公共层边界并构建**

确认 `il2cpp_runtime.cpp/.h` 不包含 ImGui 和两个新功能头文件，`overlay.cpp` 不再定义 IL2CPP 导出函数指针或通用安全读取函数。

```powershell
cmake --build "K:\ToolsBOX\out" --config Debug --target payload --parallel
```

Expected: `payload.dll` 构建成功，无本次抽取新增错误。

### Task 3: 建立设置存储公共层

**Files:**
- Create: `payload/settings_store.h`
- Create: `payload/settings_store.cpp`
- Modify: `payload/CMakeLists.txt`
- Verify: `payload/timer_logic_tests.cpp`

- [ ] **Step 1: 定义版本化设置 DTO**

在 `settings_store.h` 定义不依赖 ImGui 的 `SmudgeSettings`、`GameTimeSettings` 与总设置快照。字段包含各自的 `enabled`、`fixed`、`Position`、`fontSize` 和颜色；默认值严格遵循设计文档。公开 `Load()`、获取可修改设置、`MarkDirty()`、`Tick()` 和 `Flush()`。

- [ ] **Step 2: 实现加载与规范化**

在 `settings_store.cpp` 使用魔数、版本和 payload 长度校验二进制结构。缺失或无效文件整体恢复默认；有效文件加载后调用 `timer_logic` 规范化字号、颜色和位置值。该文件不得包含 ImGui、WhiteSage 或 LevelStats。

- [ ] **Step 3: 实现本地路径和原子写入**

将配置放在当前用户本地应用数据目录的独立应用子目录下，正式文件名为 `settings.bin`。保存时先写同目录 `settings.tmp`，确认写入完成后使用 Windows 原子替换更新正式文件；不写游戏安装目录，不记录或保存敏感数据。

- [ ] **Step 4: 实现脏标记与延迟保存**

`MarkDirty()` 记录最后变化时间；`Tick()` 仅在脏状态静止满一秒后保存一次；`Flush()` 在关闭时无条件保存尚未落盘的变化。连续拖动不得逐帧写文件。

- [ ] **Step 5: 构建并检查职责**

```powershell
cmake --build "K:\ToolsBOX\out" --config Debug --target timer_logic_tests payload --parallel
ctest --test-dir "K:\ToolsBOX\out" -C Debug -R "^timer_logic_tests$" --output-on-failure
```

Expected: 单测与 payload 均成功；配置格式的纯校验仍由 `timer_logic_tests` 覆盖，文件 I/O 只存在于 `settings_store.cpp`。

### Task 4: 独立实现游戏时间模块

**Files:**
- Create: `payload/game_time.h`
- Create: `payload/game_time.cpp`
- Modify: `payload/CMakeLists.txt`

- [ ] **Step 1: 定义独立模块接口**

在 `game_time.h` 声明 `GameTimeSnapshot { bool valid; float seconds; }`，以及 `Initialize()`、`Shutdown()`、`ReadSnapshot()`、`DrawWindow()` 和 `DrawSettings()`。头文件不得包含 `smudge_timer.h`。

- [ ] **Step 2: 解析并验证 LevelStats**

在 `game_time.cpp` 通过 `il2cpp_runtime::FindClass("", "LevelStats")` 查找类型，并按当前游戏元数据解析单例 getter。调用入口前验证地址属于 `GameAssembly.dll` 且页面可执行。字段偏移使用当前 `dump.cs` 已确认的 `LevelStats.timeSpentInvestigating` 偏移并仅在本文件集中定义，不使用方法 RVA。

- [ ] **Step 3: 实现本局时间查询**

获取并缓存实例；每次读取均通过公共安全读取接口验证实例与字段。只接受有限、非负且不超过定义合理上限的值。实例或读数无效时清除缓存并返回 `{false, 0}`，后续调用重新解析；不得使用进程累计时间回退。

- [ ] **Step 4: 实现游戏时间窗口**

在 `game_time.cpp` 绘制无标题、无背景、自动尺寸窗口，文本为 `游戏时间: MM:SS`。从 `settings_store` 取得游戏时间专属设置，使用 `timer_logic` 格式化和限制位置；读取失败显示 `00:00`。未固定时回写位置并标脏，固定时应用 `ImGuiWindowFlags_NoMove` 和保存位置。

- [ ] **Step 5: 实现游戏时间专属设置**

`DrawSettings()` 只绘制启用、固定、字号和字体颜色控件；任何变化调用 `settings_store::MarkDirty()`。不在 Overlay 中复制这些控件。

- [ ] **Step 6: 构建与运行时烟雾验证**

```powershell
cmake --build "K:\ToolsBOX\out" --config Debug --target payload --parallel
```

运行时确认：大厅中数据不可用不崩溃且显示 `00:00`；进入调查后数值递增；进入新调查不会沿用上一局实例或数值。

### Task 5: 验证 ABI 后独立实现圣木模块

**Files:**
- Create: `payload/smudge_timer.h`
- Create: `payload/smudge_timer.cpp`
- Modify: `payload/CMakeLists.txt`
- Reference: `dump.cs:153028-153030,1270121-1270126,1287273-1287323`

- [ ] **Step 1: 定义无跨功能依赖的模块接口**

在 `smudge_timer.h` 定义本局时间提供器契约和 `Initialize(provider)`、`Shutdown()`、`DrawWindow()`、`DrawSettings()`。提供器只返回通用的有效标志与秒数，头文件和实现均不得包含或引用 `game_time.h`。

- [ ] **Step 2: 建立 ABI 运行时验证门槛**

在 `smudge_timer.cpp` 按类 `WhiteSage`、方法 `UseNetworked`、显式参数数 `2` 解析入口，并通过 `il2cpp_runtime` 验证：

1. 地址属于 `GameAssembly.dll` 映像。
2. 页面可执行。
3. 14 字节覆盖范围可读。
4. 当前运行版本方法入口和调用点能证明 24 字节 `PhotonMessageInfo` 的传递方式。
5. 当前运行版本能证明是否存在尾部 `MethodInfo*`。

预期托管语义固定为：

```text
WhiteSage.UseNetworked(bool, Photon.Pun.PhotonMessageInfo)
PhotonMessageInfo.Sender @ +0x08
Photon.Realtime.Player.nickName @ +0x20
```

这些语义不足以单独确定原生 ABI。只有证据完整时才声明原生 typedef 并进入安装步骤；任一验证失败都必须保持 Hook 未安装，不做猜测性调用。

- [ ] **Step 3: 建立模块私有 Hook 和事件状态**

在 `smudge_timer.cpp` 私有保存目标地址、原入口信息、原字节、installed 状态、停止原子标志、工作线程句柄与互斥锁。定义拥有型 `SmudgeEvent`：有效标志、`steady_clock` 点燃时刻、玩家名字符串副本和点燃时本局时间。读取事件必须按值返回完整副本。

- [ ] **Step 4: 实现 Hook 回调**

按已验证 ABI 从 `PhotonMessageInfo` 提取 Sender，经 `il2cpp_runtime` 安全读取并复制玩家名；失败使用 `--`。调用初始化时注入的提供器取得本局时间快照，不调用 `game_time::*`。写入最新事件后保持原游戏方法正常执行。每次新事件替换旧事件并重置单调计时起点。

- [ ] **Step 5: 实现圣木窗口**

在 `smudge_timer.cpp` 绘制：

```text
圣木被点燃: MM:SS
玩家:NAME  记录时间: MM:SS
```

无记录显示 `00:00/--/00:00` 和白色主行；有记录时主行黄色，玩家淡红色，记录时间白色。主计时使用 `steady_clock` 与 `timer_logic::ElapsedSeconds`。模块独立处理启用、拖动、固定、位置限制和脏标记。

- [ ] **Step 6: 实现圣木专属设置**

`DrawSettings()` 只绘制启用、固定、字号、无记录颜色、有记录颜色、玩家颜色和记录时间颜色；变化后调用 `settings_store::MarkDirty()`。不在 Overlay 中复制这些控件。

- [ ] **Step 7: 完善关闭顺序并构建**

`Shutdown()` 先设置停止标志、等待工作线程退出，再恢复 WhiteSage Hook 并清理模块状态。不得在线程仍可能写 Hook 时卸载。

```powershell
cmake --build "K:\ToolsBOX\out" --config Debug --target payload --parallel
```

- [ ] **Step 8: 运行时验证 ABI 与 Hook**

本地和远端玩家各点燃一次：原游戏行为不变；事件仅在调用发生时更新；名称失败仍产生 `--` 记录；记录时间来自注入的提供器；卸载后再次点燃不崩溃。另验证 ABI 门槛人为不满足时不安装 Hook 且 Overlay 保持稳定。

### Task 6: 将 Overlay 收敛为 ImGui 生命周期、菜单入口与模块调度

**Files:**
- Modify: `payload/overlay.cpp`
- Verify/Modify only if lifecycle surface requires: `payload/overlay.h`
- Verify: `payload/payload.cpp`

- [ ] **Step 1: 初始化公共层与模块**

在现有 ImGui/D3D 生命周期就绪后加载设置并初始化 `il2cpp_runtime`、`game_time` 和 `smudge_timer`。由 `overlay.cpp` 创建适配函数，将 `game_time::ReadSnapshot()` 转换为 `smudge_timer` 的通用提供器返回值并注入；`smudge_timer` 不包含 `game_time.h`。

- [ ] **Step 2: 只保留模块渲染调度**

在 `ImGui::NewFrame()` 后调用：

```text
smudge_timer::DrawWindow()
game_time::DrawWindow()
DrawMenuWindow()
settings_store::Tick()
```

随后提交 ImGui DrawData。U 键关闭菜单时仍调用两个 `DrawWindow()`。

- [ ] **Step 3: 将菜单收敛为总入口**

菜单只保留“圣木计时器”和“游戏时间”页签/列表选择，并分别调用 `smudge_timer::DrawSettings()` 与 `game_time::DrawSettings()`。移除“十字架计时器”入口。Overlay 不定义两个功能的设置结构或控件。

- [ ] **Step 4: 保持字体与 ImGui 资源所有权**

中文字体加载、ImGui 上下文、D3D11 设备与 RenderTarget 生命周期保留在 Overlay。为功能模块提供选择已加载字号字体所需的最小 ImGui 生命周期接口；不得把功能文本或窗口实现放回 Overlay。

- [ ] **Step 5: 实现确定的关闭顺序**

`overlay::Shutdown()` 依次：停止并关闭 `smudge_timer`、关闭 `game_time`、关闭 `il2cpp_runtime`、调用 `settings_store::Flush()`，最后释放 ImGui/D3D 资源。若现有 GhostAI Hook 仍由 Overlay 管理，必须在其线程停止并恢复 Hook 后再释放公共运行时。

- [ ] **Step 6: 执行边界静态检查**

确认 `overlay.cpp` 中不存在以下实现或状态：

- `WhiteSage`、`UseNetworked`、`PhotonMessageInfo`、玩家名路径、圣木事件互斥锁。
- `GhostAI` 探测、旧入口覆盖、实例扫描或相关 Hook 状态。
- `LevelStats`、本局时间字段偏移或实例缓存。
- IL2CPP 导出函数指针、通用安全读取函数或可执行地址校验实现。
- 配置魔数、配置路径、文件句柄、临时文件、原子替换、脏状态计时。
- `DrawSmudgeTimer`、`DrawGameTime` 或两个功能的专属设置控件实现。

- [ ] **Step 7: 构建并手工验证调度**

```powershell
cmake --build "K:\ToolsBOX\out" --config Debug --target payload --parallel
```

验证两个窗口分别启用、移动和固定；关闭菜单后继续显示；菜单页只调度模块设置；两个功能任一初始化失败不阻断另一个窗口和 Overlay。

### Task 7: CMake、诊断、全量构建与最终验收

**Files:**
- Verify: `payload/overlay.cpp`
- Verify: `payload/overlay.h`
- Verify: `payload/smudge_timer.cpp`
- Verify: `payload/smudge_timer.h`
- Verify: `payload/game_time.cpp`
- Verify: `payload/game_time.h`
- Verify: `payload/il2cpp_runtime.cpp`
- Verify: `payload/il2cpp_runtime.h`
- Verify: `payload/settings_store.cpp`
- Verify: `payload/settings_store.h`
- Verify: `payload/timer_logic.cpp`
- Verify: `payload/timer_logic.h`
- Verify: `payload/timer_logic_tests.cpp`
- Verify: `payload/CMakeLists.txt`

- [ ] **Step 1: 核对 CMake 目标**

确认 `payload` 编译 `overlay.cpp`、`smudge_timer.cpp`、`game_time.cpp`、`il2cpp_runtime.cpp`、`settings_store.cpp` 和 `timer_logic.cpp`；`timer_logic_tests` 只编译纯逻辑与测试，不链接 ImGui、D3D11 或 IL2CPP 集成。

- [ ] **Step 2: 运行 IDE 诊断**

检查全部修改文件，无新增 error；处理本次改动引入的 warning，不重构无关旧代码。

- [ ] **Step 3: Debug 全量构建与测试**

```powershell
cmake --build "K:\ToolsBOX\out" --config Debug --parallel
ctest --test-dir "K:\ToolsBOX\out" -C Debug --output-on-failure
```

Expected: 注入器、payload 和测试均成功，CTest 100% 通过。

- [ ] **Step 4: Release 全量构建与测试**

```powershell
cmake --build "K:\ToolsBOX\out" --config Release --parallel
ctest --test-dir "K:\ToolsBOX\out" -C Release --output-on-failure
```

Expected: Release 全量成功，CTest 100% 通过。

- [ ] **Step 5: 验证模块依赖边界**

通过包含关系和链接符号检查确认：`smudge_timer` 与 `game_time` 互不依赖；两个功能只调用公共层；`overlay` 只做生命周期、菜单总入口和调度；文件 I/O 只在 `settings_store.cpp`；通用 IL2CPP 与安全读取只在 `il2cpp_runtime.cpp`。

- [ ] **Step 6: 按设计文档完成运行时验收**

逐项验证默认文本、字号与颜色；本地/远端玩家名；记录时间；新事件重置；当前调查时间；独立移动/固定；菜单隐藏；跨运行恢复；损坏配置恢复默认；ABI 门槛失败安全禁用；退出时停止线程并恢复 Hook。

## 实施约束

- 仅使用本项目代码、当前 `dump.cs` 和运行时证据；不读取或复制参考项目源码。
- 不硬编码方法 RVA；RVA 只可用于核对当前 dump，字段偏移必须留在所属功能模块并经安全读取。
- 不把 `PhotonMessageInfo` 未验证地当作普通指针参数；ABI 证据不足时禁止安装 Hook。
- 不让 `smudge_timer` 与 `game_time` 形成包含、调用或共享可变状态关系。
- 不把两个功能、公共 IL2CPP 层或设置持久化实现放入 `overlay.cpp`。
- 不实现“圣木实际影响鬼”、十字架、历史列表或 Hook 框架重写。
- 不因任一功能读数或初始化失败阻断 Overlay 或另一个功能。
- 实施必须保持 TDD 红—绿证据，并完成 Debug/Release 构建与 CTest 验证。
- 本计划不包含 Git 提交或推送步骤；实施期间除非用户另行明确要求，不执行 Git。

## 追加批准范围（固定字形、菜单持久位置与紧凑布局，2026-07-21）

- 先以纯逻辑测试覆盖完整固定 UI 文本字形，以及菜单右侧垂直居中的默认位置和小屏钳制，再实现功能。
- 固定字体语料集中维护并登记菜单标题、两个功能名、全部设置标签、重置按钮和半角/全角冒号；继续只对动态玩家名补字，不加入完整 CJK 范围。
- 设置格式升级为 v2，新增菜单位置及初始化标志；按头部版本和长度分别读取，v1 迁移时保留两个功能设置，仅将菜单位置标为待默认初始化。
- 菜单固定为 860×560，首次按实际窗口尺寸置于右侧 20 px 且垂直居中，之后持久化拖动位置并随分辨率钳制；列表项高 26 并保留分隔线。
- 两模块 Combo 宽 100，所有设置标签补半角冒号且保持唯一 ID；完成 Debug/Release 全量构建、CTest、诊断和静态核查。

## 追加批准范围（A 方案，2026-07-21）

- 公共 `font_store` 新增线程安全的 `RequestText(fontSize, utf8Text)` 与帧安全点 `RebuildIfNeeded(io)`；按字号仅补已请求且受支持的 Unicode BMP 字符，保留固定紧凑 UI 字符，不预载完整语言或完整 CJK 范围。
- Unicode 提取先在 `timer_logic_tests.cpp` 做 RED/GREEN：保留中文、日文、韩文、西里尔和拉丁扩展；过滤非 BMP、代理项、variation selector、ZWJ、regional indicator、常见 emoji/pictograph BMP 区段和 combining keycap；非法 UTF-8 安全跳过并去重。
- Overlay 在 `ImGui_ImplDX11_NewFrame` 前处理字体重建，顺序为 `InvalidateDeviceObjects -> atlas rebuild -> CreateDeviceObjects`；失败或纹理超过 16384 时恢复上一可用字符集合并重建旧 atlas，刷新所有 `ImFont*`。
- `smudge_timer` 在捕获事件和绘制玩家名前请求当前字号字形；每帧独立维护 I/O 键边沿：I 创建玩家名“手动”的记录，O 清空记录，长按仅触发一次且菜单隐藏仍生效。
- `smudge_timer` 独立新增运行时解析的 `LevelStats.ResetStats(0)` Hook，与 `WhiteSage.UseNetworked` 分别维护 target、originalBytes、installed；任一安装失败仅重试未安装项。Reset detour 调用原方法后线程安全清空事件并在未停止时重装；Shutdown 等待安装线程并恢复两个 Hook。
- 保持模块边界：`smudge_timer` 不包含 `game_time`，Hook 公共写入继续经 `il2cpp_runtime`；动态字体请求仅本注入周期存在，不持久化。

## 追加批准范围（默认布局、独立重置与 Alt 拖动，2026-07-21）

- 先为左上与中上默认锚点补纯逻辑 RED 测试；首次无有效配置时请求模块复位，圣木定位 `(20,20)`，游戏时间待实际窗口尺寸可用后居中于 `y=20`，并保存结果。
- 两个功能设置页各自提供“重置默认设置”，以默认构造恢复本模块字体、颜色、位置与 `fixed=false`，保留 `enabled`；圣木事件与游戏时间运行数据不变。
- 功能窗口参数化接收菜单/Alt 拖动态：菜单打开且未固定时可直接移动；菜单隐藏时仅按住 Alt 才可移动并接管输入；未按 Alt 或已固定时不接管鼠标。
- Overlay 每帧以 `menuVisible || (AltDown && 任一启用且未固定)` 控制 ImGui 输入，并使用标题 `抓鬼工具箱(U键 显示/隐藏 | 按住Alt拖动功能位置)`。
- 修正游戏时间字号加载规范化为最近支持档，完成 Debug/Release 全量构建、CTest 与 IDE 诊断。

## 追加批准范围（鬼魂状态与十字架计时器，2026-07-21）

- 新增独立 `ghost_status` 与 `crucifix_timer` 模块，由 Overlay 注入通用值类型提供器；模块不得互相包含，也不得包含 `game_time.h`。
- 猎杀时长必须由 `std::chrono::steady_clock` 实时累加，不能通过游戏时间相减；游戏时间仅在猎杀开始时保存，用于计算十字架燃烧时的猎杀间隔。
- 当前 dump 无 `GhostAI` 单例 getter，因此动态解析并 Hook `GhostAI.Hunting(bool,int,PhotonMessageInfo)` 捕获实例；以 `GhostAI+0xF3` 为猎杀主信号并用 `GhostAI+0x30 == 2` 校验，不使用固定 RVA。
- 动态解析并 Hook `Crucifix.CrucifixUseNetworked(bool,PhotonMessageInfo)`；原方法调用前后读取 `instance+0x3D8` 的 `useCount`，仅在合法值发生变化时记录燃烧事件。
- 两模块都通过游戏时间显著回退检测新调查并清空运行状态，避免与现有 `LevelStats.ResetStats` Hook 冲突；中途附加时从首次观察到猎杀开始实时计时。
- 设置格式升级为 v3，精确迁移 v1 和 v2；新增两模块的启用、固定、字号、颜色、位置等设置，重置时保留启用状态。
- 先为 `HuntInterval`、`BlinkPhase`、`RightOf` 与 `Below` 添加失败测试并确认 RED，再实现 GREEN；固定字形只补实际新增 UI 文本，不载入完整 CJK。
- 鬼魂状态默认位于游戏时间右侧 20 px，十字架计时器默认位于圣木下方 20 px；Overlay 按绘制顺序传递只读窗口边界用于首次布局。
- 完成 Debug/Release 全量构建、CTest、IDE 诊断及无硬编码 RVA、模块依赖、迁移和固定字形静态检查；不提交 Git。

## 追加批准范围（独立默认屏幕锚点修正，2026-07-21）

- 四个功能的位置与窗口尺寸完全解耦：任何功能都不得读取或跟随另一功能的当前位置或窗口尺寸，移动任意功能不影响其他功能。
- 默认位置分别基于独立屏幕锚点：圣木 `(20,20)`；十字架 `(20,100)`；游戏时间按自身实际窗口宽度顶部居中且 `y=20`；鬼魂以自身实际窗口尺寸钳制，左上角 `x=屏幕宽度/2+180`、`y=20`。
- 删除 Overlay 向鬼魂和十字架传递游戏时间/圣木边界的调用，并删除 `RightOf`、`Below` 相对布局逻辑；鬼魂与十字架复位只读取 `ImGuiIO::DisplaySize` 和自身实际窗口尺寸，计算一次后保存。
- `CrucifixSettings` 默认位置改为 `{20,100}`；v3 已保存位置原样保留。新配置与 v1/v2 迁移仅触发一次新模块初始布局，模块各自重置仍独立触发 pending。
- 先修改纯逻辑测试并确认 RED，再实现独立锚点并确认 GREEN；完成 Debug/Release 绝对路径构建、CTest、IDE 诊断和跨窗口 bounds/相对布局静态检查；不提交 Git。

## 追加批准范围（设置行布局、多人猎杀证据与主动实例发现，2026-07-21）

- 四个模块设置统一为约 140 px 标签列：先绘制带 ASCII 冒号的可见标签，再在固定 x 坐标绘制唯一隐藏 ID 控件；Combo 宽 100，ColorEdit4 使用 `NoInputs`。
- 为猎杀证据增加纯逻辑状态决策并先执行 RED：RPC 正反边沿立即生效；无新 RPC 时 F3 或 state==2 任一正向即维持/开始；双字段连续至少 3 帧为负且 RPC true 宽限期至少 750 ms 后才结束；字段读取失败保持当前状态。
- `il2cpp_runtime` 仅从 `GameAssembly.dll` 动态解析字段枚举、字段类型、静态字段值与偏移相关 exports；通过 `LevelController` 的同类型静态字段发现实例，再通过类型为 `GhostAI` 的实例字段发现 GhostAI，不使用字段名、全内存扫描、固定 RVA 或固定 0x40。
- `ghost_status` 保存 RPC 值与 `steady_clock` 时间戳，RPC detour 缓存实例并提交权威边沿；轮询低频主动重发现实例，读取失败进入 unknown 并重发现，新局清空 RPC、实例和负向证据。
- 串行完成 Debug/Release 全量构建与 CTest，执行 IDE 诊断和静态检查；不提交 Git。

## 追加批准范围（删除十字架猎杀间隔，2026-07-21）

- 十字架计时器删除猎杀快照提供器、燃烧事件猎杀间隔计算及对应 UI；第二行仅显示 `记录时间: 00:00`，设置页删除猎杀间隔颜色且保持左标签右控件布局。
- Overlay 不再适配鬼魂猎杀快照，十字架初始化仅注入游戏时间；若无其他消费者，删除 `ghost_status` 公共快照接口及仅供十字架使用的猎杀开始游戏时间和 epoch 状态，保留新局回退所需游戏时间提供器。
- 删除 `timer_logic::HuntInterval` 及其测试；固定字形语料删除猎杀间隔显示和设置标签，鬼魂状态所需“猎杀”字形继续保留。
- 设置格式升级为 v4，并以独立旧版 `CrucifixSettingsV3`、`SettingsV3` 精确定义 v3 布局（包括 intervalColor、menuPosition 和初始化标志）；v3 迁移逐字段保留全部现行字段，仅舍弃 intervalColor，v1/v2 迁移保持原语义。
- 全库 payload 源码静态检查 `intervalColor`、`HuntInterval`、`HuntSnapshot`、`猎杀间隔` 均为零，并串行完成绝对路径 Debug/Release 构建、CTest 与 IDE 诊断；不提交 Git。
