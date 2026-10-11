# 准星信息位置与侧窗高度修复 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 修正准星信息的三项物品名称和跨注入持久化偏移，并让按键说明、支持作者侧窗始终与主菜单等高且上下自然对齐。

**Architecture:** 继续由 `feature_logic::FormatEquipmentLabel` 负责运行时物品名称，由 `timer_logic` 保存固定字体语料和无 ImGui 依赖的窗口纯逻辑。新增一个仅接收 `appearing/hasBaseline/previous/current` 的自动尺寸判定函数；`crosshair_info` 只执行判定结果，出现帧不建立基准，首个稳定帧只初始化基准，后续尺寸变化才复用 `CenterPositionForResize`。侧窗沿用不接收 viewport、不会 Clamp 的 `SideWindowPositions`，仅把传入高度改为每帧主菜单实际高度。

**Tech Stack:** Windows x64、C++17、CMake、CTest、MSVC、Dear ImGui、VS Code diagnostics

---

## 文件结构与职责

- Modify: `payload/feature_logic.cpp` — 将 `FormatEquipmentLabel` 的索引 2、5、10 映射改为批准名称。
- Modify: `payload/timer_logic.h` — 声明自动尺寸稳定判定的最小结果类型与纯逻辑入口。
- Modify: `payload/timer_logic.cpp` — 实现自动尺寸稳定判定，并同步固定字体语料中的三项名称。
- Modify: `payload/timer_logic_tests.cpp` — 先锁定名称/等级、固定字形、自动尺寸生命周期和 380×560/430×560 侧窗位置，再驱动实现。
- Modify: `payload/crosshair_info.cpp` — 使用纯逻辑结果控制尺寸基准初始化和后续居中，保持现有拖动、吸附、默认重置和 Clamp 路径。
- Modify: `payload/overlay.cpp` — 侧窗宽度保持 380/430，每帧把高度设为 `menuBounds.size.height`。

保持不变：`payload/settings_store.h`、`payload/settings_store.cpp`、磁盘配置版本 V7、配置字段布局、`CenterPositionForResize` 与 `ClampPosition` 的既有公式、侧窗内容和赞助图片尺寸。所有步骤均无 Git 操作，不执行游戏注入。

---

### Task 1: RED — 锁定三个准星物品名称、等级后缀与固定字形

**Files:**
- Test: `payload/timer_logic_tests.cpp:61-67,359-375`

- [ ] **Step 1: 用批准名称增加会失败的映射断言**

在现有 `FormatEquipmentLabel` 断言中，保留一级/二级/三级通用规则和无效输入断言，并加入索引 2、5、10 的精确期望：

```cpp
assert(FormatEquipmentLabel(0, 0, true) == u8"十字架 · 一级");
assert(FormatEquipmentLabel(0, 1, true) == u8"十字架 · 二级");
assert(FormatEquipmentLabel(0, 2, true) == u8"十字架 · 三级");
assert(FormatEquipmentLabel(2, 0, false) == u8"EMF读取仪");
assert(FormatEquipmentLabel(2, 2, true) == u8"EMF读取仪 · 三级");
assert(FormatEquipmentLabel(5, 1, true) == u8"鬼魂笔记 · 二级");
assert(FormatEquipmentLabel(10, 0, true) == u8"收音器 · 一级");
assert(FormatEquipmentLabel(20, 2, false) == u8"摄像机");
assert(FormatEquipmentLabel(0, -1, true) == u8"十字架");
assert(FormatEquipmentLabel(0, 3, true) == u8"十字架");
assert(FormatEquipmentLabel(21, 1, true).empty());
```

- [ ] **Step 2: 更新字形期望语料为新名称**

在传给 `ExtractSupportedGlyphs` 的完整固定 UI 期望文本中，将两行物品名称改为：

```cpp
u8"激光投影仪EMF读取仪火光手电筒鬼魂笔记头戴设备点火器熏香运动传感器"
u8"收音器照相机盐理智药物录音机声音传感器通灵盒温度计三脚架紫外线灯摄像机"
```

此期望会同时要求默认拉丁字形 `E/M/F` 以及“收音器”“读取仪”“鬼魂笔记”所需中文字符存在。

- [ ] **Step 3: 配置并构建 Debug 测试，确认 RED**

Run:

```powershell
cmake -S "K:\ToolsBOX" -B "K:\ToolsBOX\out" -A x64
cmake --build "K:\ToolsBOX\out" --config Debug --target timer_logic_tests
& "K:\ToolsBOX\out\payload\Debug\timer_logic_tests.exe"
```

Expected: 构建成功，但测试进程因索引 2 的实际值仍为 `电磁场读取器`（索引 5、10 也仍是旧值）而触发 assertion，退出码非 0。不得在看到此预期失败之前修改生产映射或固定字形。

---

### Task 2: GREEN — 修改名称映射并同步固定字体语料

**Files:**
- Modify: `payload/feature_logic.cpp:62-67`
- Modify: `payload/timer_logic.cpp:38-51`
- Test: `payload/timer_logic_tests.cpp`

- [ ] **Step 1: 只替换 `FormatEquipmentLabel` 的三个索引值**

将名称数组保持 21 项和原顺序不变，仅把索引 2、5、10 改为：

```cpp
static constexpr std::array<const char*, 21> names = {
    u8"十字架", u8"激光投影仪", u8"EMF读取仪", u8"火光", u8"手电筒",
    u8"鬼魂笔记", u8"头戴设备", u8"点火器", u8"熏香", u8"运动传感器",
    u8"收音器", u8"照相机", u8"盐", u8"理智药物", u8"录音机",
    u8"声音传感器", u8"通灵盒", u8"温度计", u8"三脚架", u8"紫外线灯", u8"摄像机"
};
```

不要改变 tier 0/1/2 到“一级/二级/三级”的后缀规则、分隔符 ` · ` 或无效 tier 行为。

- [ ] **Step 2: 同步 `timer_logic` 的固定字体文本**

在 `kFixedUiText` 中只替换对应物品名称片段：

```cpp
u8"激光投影仪EMF读取仪火光手电筒鬼魂笔记头戴设备点火器熏香运动传感器"
u8"收音器照相机盐理智药物录音机声音传感器通灵盒温度计三脚架紫外线灯摄像机"
```

不要修改 `font_store.cpp` 中未被运行时使用的局部 `kFixedUiText`；当前字体构建实际调用的是 `timer_logic::GetFixedUiText()`。

- [ ] **Step 3: 重建并运行定向 Debug 测试，确认 GREEN**

Run:

```powershell
cmake --build "K:\ToolsBOX\out" --config Debug --target timer_logic_tests
ctest --test-dir "K:\ToolsBOX\out" -C Debug -R "^timer_logic_tests$" --output-on-failure
```

Expected: 构建成功，`timer_logic_tests` 通过（`100% tests passed`）。

---

### Task 3: RED — 为自动尺寸稳定判定定义最小纯逻辑契约

**Files:**
- Modify: `payload/timer_logic.h:16-30,89-100`
- Test: `payload/timer_logic_tests.cpp:234-245`

- [ ] **Step 1: 在 `timer_logic.h` 声明结果类型**

紧接 `Size` 后加入：

```cpp
struct AutoResizeDecision {
    bool initializeBaseline = false;
    bool recenter = false;
};
```

结果只表达调用方应否初始化尺寸基准、应否执行居中；不携带位置、不写配置、不调用 ImGui。

- [ ] **Step 2: 在 `timer_logic.h` 声明纯逻辑入口**

在 `CenterPositionForResize` 声明前加入：

```cpp
AutoResizeDecision DecideAutoResize(bool appearing, bool hasBaseline,
                                    Size previous, Size current);
```

四个输入必须保持为批准契约：`appearing/hasBaseline/previous/current`；不要传入 viewport、配置对象或 ImGui 窗口。

- [ ] **Step 3: 写出现帧与首个稳定帧测试**

在 `CenterPositionForResize` 测试之前加入：

```cpp
const Size measuringSize{32.0f, 16.0f};
const Size stableSize{180.0f, 32.0f};
const auto appearingWithoutBaseline = DecideAutoResize(
    true, false, {}, measuringSize);
assert(!appearingWithoutBaseline.initializeBaseline);
assert(!appearingWithoutBaseline.recenter);

const auto firstStable = DecideAutoResize(
    false, false, {}, stableSize);
assert(firstStable.initializeBaseline);
assert(!firstStable.recenter);
```

- [ ] **Step 4: 写相同尺寸与后续变化测试**

继续加入：

```cpp
const auto unchangedStable = DecideAutoResize(
    false, true, stableSize, stableSize);
assert(!unchangedStable.initializeBaseline);
assert(!unchangedStable.recenter);

const Size widerSize{260.0f, 32.0f};
const auto changedStable = DecideAutoResize(
    false, true, stableSize, widerSize);
assert(!changedStable.initializeBaseline);
assert(changedStable.recenter);
```

- [ ] **Step 5: 写已有基准但再次 appearing 时不居中的测试**

继续加入：

```cpp
const auto appearingWithBaseline = DecideAutoResize(
    true, true, stableSize, measuringSize);
assert(!appearingWithBaseline.initializeBaseline);
assert(!appearingWithBaseline.recenter);
```

这锁定窗口出现/测量阶段的最高优先级：无论是否已有基准，都不得初始化或居中。

- [ ] **Step 6: 写重新开始生命周期的持久化位置模拟**

用局部状态模拟一次新 ImGui Context，明确首帧测量不会改变保存位置：

```cpp
Position persistedPosition{700.0f, 500.0f};
bool hasBaseline = false;
Size baseline{};
const auto newLifecycleAppearing = DecideAutoResize(
    true, hasBaseline, baseline, measuringSize);
if (newLifecycleAppearing.initializeBaseline) {
    baseline = measuringSize;
    hasBaseline = true;
}
if (newLifecycleAppearing.recenter) {
    persistedPosition = CenterPositionForResize(
        persistedPosition, baseline, measuringSize, {1920.0f, 1080.0f});
}
assert((persistedPosition == Position{700.0f, 500.0f}));
assert(!hasBaseline);

const auto newLifecycleStable = DecideAutoResize(
    false, hasBaseline, baseline, stableSize);
if (newLifecycleStable.initializeBaseline) {
    baseline = stableSize;
    hasBaseline = true;
}
if (newLifecycleStable.recenter) {
    persistedPosition = CenterPositionForResize(
        persistedPosition, baseline, stableSize, {1920.0f, 1080.0f});
}
assert((persistedPosition == Position{700.0f, 500.0f}));
assert(hasBaseline);
assert(baseline.width == stableSize.width && baseline.height == stableSize.height);
```

- [ ] **Step 7: 构建以确认纯逻辑尚未实现**

Run:

```powershell
cmake --build "K:\ToolsBOX\out" --config Debug --target timer_logic_tests
```

Expected: FAIL，链接器报告 `timer_logic::DecideAutoResize` 未定义。不得用测试内假实现绕过 RED。

---

### Task 4: GREEN — 实现自动尺寸稳定判定

**Files:**
- Modify: `payload/timer_logic.cpp:96-98`
- Test: `payload/timer_logic_tests.cpp`

- [ ] **Step 1: 在 `SideWindowPositions` 后加入最小实现**

```cpp
AutoResizeDecision DecideAutoResize(bool appearing, bool hasBaseline,
                                    Size previous, Size current) {
    if (appearing) return {};
    if (!hasBaseline) return {true, false};
    const bool changed = previous.width != current.width ||
                         previous.height != current.height;
    return {false, changed};
}
```

不要在此函数中执行 Clamp、中心计算、基准状态修改或持久化；这些仍由调用方按结果执行。

- [ ] **Step 2: 重建并运行定向 Debug 测试**

Run:

```powershell
cmake --build "K:\ToolsBOX\out" --config Debug --target timer_logic_tests
ctest --test-dir "K:\ToolsBOX\out" -C Debug -R "^timer_logic_tests$" --output-on-failure
```

Expected: 构建成功，自动尺寸判定的六类断言和既有中心保持断言全部通过。

---

### Task 5: GREEN — 在准星信息窗口接入稳定判定并保持既有行为

**Files:**
- Modify: `payload/crosshair_info.cpp:49-51,114-125,209-246`
- Test: `payload/timer_logic_tests.cpp`

- [ ] **Step 1: 读取 ImGui 的出现状态并构造当前尺寸**

在 `ImGui::Begin` 后的现有窗口信息读取处保留 `size/display/position`，加入：

```cpp
const bool appearing = ImGui::IsWindowAppearing();
const timer_logic::Size currentSize{size.x, size.y};
```

- [ ] **Step 2: 保留默认位置重置语义并建立其明确基准**

将重置分支中的尺寸赋值统一使用 `currentSize`：

```cpp
if (g_resetPosition) {
    const auto initial = timer_logic::DefaultCenterOffset(
        {display.x, display.y}, currentSize);
    ImGui::SetWindowPos({initial.x, initial.y});
    position = {initial.x, initial.y};
    settings.position = initial;
    g_appliedWindowSize = currentSize;
    g_hasAppliedWindowSize = true;
    g_resetPosition = false;
    settings_store::MarkDirty();
}
```

重置是显式用户操作，仍应立即计算默认位置、写入配置并建立当前尺寸基准；不要让稳定判定取消该行为。

- [ ] **Step 3: 用纯逻辑结果替换无条件首帧基准记录**

删除当前 `if (!g_hasAppliedWindowSize) ... else if (...)` 块，改为仅在没有处理重置时执行：

```cpp
else {
    const auto resizeDecision = timer_logic::DecideAutoResize(
        appearing, g_hasAppliedWindowSize, g_appliedWindowSize, currentSize);
    if (resizeDecision.initializeBaseline) {
        g_appliedWindowSize = currentSize;
        g_hasAppliedWindowSize = true;
    } else if (resizeDecision.recenter) {
        const auto centered = timer_logic::CenterPositionForResize(
            {position.x, position.y}, g_appliedWindowSize, currentSize,
            {display.x, display.y});
        ImGui::SetWindowPos({centered.x, centered.y});
        position = {centered.x, centered.y};
        settings.position = centered;
        g_appliedWindowSize = currentSize;
        settings_store::MarkDirty();
    }
}
```

首个稳定尺寸分支只更新内存基准，不调用 `SetWindowPos`、不修改 `settings.position`、不调用 `MarkDirty()`；后续名称长度、等级或字体导致的尺寸变化仍走现有中心保持和必要持久化。

- [ ] **Step 4: 保留后置 Clamp 与拖动保存条件不变**

确认上述替换之后仍原样保留：

```cpp
const auto clamped = timer_logic::ClampPosition(
    {position.x, position.y}, currentSize, {display.x, display.y});
if (clamped.x != position.x || clamped.y != position.y)
    ImGui::SetWindowPos({clamped.x, clamped.y});
ImGui::End();
if (!(clamped == settings.position) &&
    (canMove || clamped.x != position.x || clamped.y != position.y)) {
    settings.position = clamped;
    settings_store::MarkDirty();
}
```

返回的 `MovableWindowResult` 也使用 `currentSize`。这确保超出当前 viewport 的已保存位置仍会 Clamp 并保存必要修正，Alt 拖动、智能吸附和固定窗口行为不变。

- [ ] **Step 5: 核对生命周期重置点仍完整**

确认 `Initialize()` 和 `Shutdown()` 均继续执行：

```cpp
g_appliedWindowSize = {};
g_hasAppliedWindowSize = false;
```

`Shutdown()` 还应继续清除 `g_resetPosition`。不要新增配置字段，也不要把尺寸基准写进 `settings_store`。

- [ ] **Step 6: 构建准星生产代码并回归定向测试**

Run:

```powershell
cmake --build "K:\ToolsBOX\out" --config Debug --target payload timer_logic_tests
ctest --test-dir "K:\ToolsBOX\out" -C Debug -R "^timer_logic_tests$" --output-on-failure
```

Expected: `payload.dll` 与 `timer_logic_tests` 构建成功，定向测试通过。编译器不得报告 `else` 配对、未使用变量或类型不匹配问题。

---

### Task 6: 测试先行 — 让 380/430 侧窗使用主菜单实际高度并自然上下对齐

**Files:**
- Test: `payload/timer_logic_tests.cpp:142-154`
- Modify: `payload/overlay.cpp:266-275`

- [ ] **Step 1: 先把正常位置测试改为两个 560px 高侧窗**

替换当前不同高度的正常位置断言：

```cpp
constexpr Size menuSize{645.0f, 560.0f};
static_assert(menuSize.width == 645.0f && menuSize.height == 560.0f);
const Bounds menuBounds{{400.0f, 100.0f}, menuSize};
const Size keyHelpSize{380.0f, menuBounds.size.height};
const Size supportSize{430.0f, menuBounds.size.height};
const auto sidePositions = SideWindowPositions(
    menuBounds, keyHelpSize, supportSize, 10.0f);
assert((sidePositions.left == Position{10.0f, 100.0f}));
assert((sidePositions.right == Position{1055.0f, 100.0f}));
assert(sidePositions.left.y == menuBounds.position.y);
assert(sidePositions.right.y == menuBounds.position.y);
assert(sidePositions.left.y + keyHelpSize.height ==
       menuBounds.position.y + menuBounds.size.height);
assert(sidePositions.right.y + supportSize.height ==
       menuBounds.position.y + menuBounds.size.height);
```

- [ ] **Step 2: 保留并更新无 Clamp 的负坐标/右侧溢出测试**

紧接正常位置断言保留：

```cpp
const Bounds negativeMenu{{0.0f, -40.0f}, menuSize};
const auto negative = SideWindowPositions(
    negativeMenu, {380.0f, negativeMenu.size.height},
    {430.0f, negativeMenu.size.height}, 10.0f);
assert((negative.left == Position{-390.0f, -40.0f}));
assert((negative.right == Position{655.0f, -40.0f}));

const Bounds rightEdgeMenu{{1275.0f, 100.0f}, menuSize};
const auto rightOverflow = SideWindowPositions(
    rightEdgeMenu, {380.0f, rightEdgeMenu.size.height},
    {430.0f, rightEdgeMenu.size.height}, 10.0f);
assert((rightOverflow.right == Position{1930.0f, 100.0f}));
```

测试不传 viewport，明确位置函数仍自然返回负坐标和超屏坐标。

- [ ] **Step 3: 运行测试确认测试本身通过、生产 UI 尚待接线**

Run:

```powershell
cmake --build "K:\ToolsBOX\out" --config Debug --target timer_logic_tests
ctest --test-dir "K:\ToolsBOX\out" -C Debug -R "^timer_logic_tests$" --output-on-failure
```

Expected: PASS；`SideWindowPositions` 的既有通用公式已能满足等高输入。这组测试先锁定侧窗尺寸与自然对齐契约，实际 UI 高度仍由下一步修改。

- [ ] **Step 4: 将侧窗高度改为每帧菜单实际高度**

把 `DrawSideWindows` 中两个固定尺寸改为：

```cpp
const timer_logic::Size keyHelpSize{380.0f, menuBounds.size.height};
const timer_logic::Size supportSize{430.0f, menuBounds.size.height};
const auto positions = timer_logic::SideWindowPositions(
    menuBounds, keyHelpSize, supportSize, 10.0f);
```

保留前置有效性判断、主题应用/恢复、`g_keyHelpOpen`/`g_supportOpen` 和两个绘制函数不变。不要调用 `ClampPosition`，不要向侧窗位置函数增加 viewport，也不要更改支持图片的 252×120 尺寸或添加填充内容。

- [ ] **Step 5: 构建 payload 并再次运行定向测试**

Run:

```powershell
cmake --build "K:\ToolsBOX\out" --config Debug --target payload timer_logic_tests
ctest --test-dir "K:\ToolsBOX\out" -C Debug -R "^timer_logic_tests$" --output-on-failure
```

Expected: 构建成功，侧窗顶部/底部对齐及无 Clamp 断言全部通过。

---

### Task 7: 全量验证、诊断与默认 Release 产物

**Files:**
- Verify: `payload/feature_logic.cpp`
- Verify: `payload/timer_logic.h`
- Verify: `payload/timer_logic.cpp`
- Verify: `payload/timer_logic_tests.cpp`
- Verify: `payload/crosshair_info.cpp`
- Verify: `payload/overlay.cpp`
- Verify unchanged: `payload/settings_store.cpp:17`
- Output: `out/bin/Release/PhasmophobiaInjector.exe`

- [ ] **Step 1: 运行 VS Code 诊断**

对本计划修改的六个源文件运行 GetDiagnostics：

```text
K:\ToolsBOX\payload\feature_logic.cpp
K:\ToolsBOX\payload\timer_logic.h
K:\ToolsBOX\payload\timer_logic.cpp
K:\ToolsBOX\payload\timer_logic_tests.cpp
K:\ToolsBOX\payload\crosshair_info.cpp
K:\ToolsBOX\payload\overlay.cpp
```

Expected: 六个文件均无 error；若存在由当前改动引入的 warning，也应先修复再继续。

- [ ] **Step 2: 执行 Debug 全量构建**

Run:

```powershell
cmake --build "K:\ToolsBOX\out" --config Debug
```

Expected: 所有目标构建成功，包括 `payload`、三个测试可执行文件和 `PhasmophobiaInjector`。

- [ ] **Step 3: 执行 Debug 的全部 3 个 CTest**

Run:

```powershell
ctest --test-dir "K:\ToolsBOX\out" -C Debug --output-on-failure
```

Expected: 精确运行并通过 3 个测试：`timer_logic_tests`、`inventory_textures_tests`、`settings_store_tests`；结尾为 `100% tests passed, 0 tests failed out of 3`。

- [ ] **Step 4: 执行 Release 全量构建**

Run:

```powershell
cmake --build "K:\ToolsBOX\out" --config Release
```

Expected: 所有 Release 目标构建成功。该命令是本任务的默认最终构建，不以 Debug 产物作为交付产物。

- [ ] **Step 5: 执行 Release 的全部 3 个 CTest**

Run:

```powershell
ctest --test-dir "K:\ToolsBOX\out" -C Release --output-on-failure
```

Expected: `timer_logic_tests`、`inventory_textures_tests`、`settings_store_tests` 全部通过；结尾为 `100% tests passed, 0 tests failed out of 3`。

- [ ] **Step 6: 验证最终 Release 注入器存在**

Run:

```powershell
Get-Item "K:\ToolsBOX\out\bin\Release\PhasmophobiaInjector.exe" | Select-Object FullName, Length, LastWriteTime
```

Expected: 输出完整路径 `K:\ToolsBOX\out\bin\Release\PhasmophobiaInjector.exe`，文件长度大于 0，时间戳来自本次 Release 构建。

- [ ] **Step 7: 确认配置仍为 V7 且无结构改动**

检查 `payload/settings_store.cpp` 仍包含：

```cpp
constexpr std::uint32_t kVersion = 7;
```

Expected: `payload/settings_store.h/.cpp` 均未因本任务产生修改；没有新配置字段、迁移结构或版本升级。

- [ ] **Step 8: 记录仅能在游戏内确认的验收项，但不执行注入**

本次自动验证完成后，将以下项目留给运行确认：实际游戏内 `EMF读取仪`/`鬼魂笔记`/`收音器` 显示完整；重新注入后准星信息保存位置不再逐次偏移；物品名、等级或字体变化时中心稳定；主菜单移动和显隐时三窗同帧跟随且上下对齐；侧窗关闭后本次注入不再显示。不要启动游戏，不要运行注入器执行注入。
