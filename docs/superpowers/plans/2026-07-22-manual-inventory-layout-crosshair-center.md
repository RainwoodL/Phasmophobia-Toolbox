# 手动物品栏布局与准星信息居中 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 移除物品栏自动横竖切换并改为设置中手动选择，同时让准星信息默认中心位于屏幕中心上方 10px，且内容尺寸变化始终围绕当前用户中心伸缩。

**Architecture:** 继续使用 `InventoryBarSettings::layout`（0 横版、1 竖版）作为物品栏布局的唯一持久化来源，由 `inventory_bar` 在布局或槽位尺寸变化时统一调用现有中心换算与屏幕约束纯逻辑。准星信息复用中心换算函数，并新增屏幕中心偏移纯函数；`crosshair_info` 记录上次实际窗口尺寸，在 `Begin` 得到新尺寸后围绕当前左上角换算并约束，不改变配置结构、版本、固定行为或位置持久化语义。

**Tech Stack:** C++17、Dear ImGui、D3D11、CMake/CTest、MSVC、Windows x64

---

## 文件结构与边界

- Modify: `payload/feature_logic.h` — 删除不再使用的自动物品栏方向判断声明，保留布局枚举及其他纯逻辑。
- Modify: `payload/feature_logic.cpp` — 删除基于四边距离和阈值的自动方向实现。
- Modify: `payload/timer_logic.h` — 声明按窗口实际尺寸计算屏幕中心偏移位置的纯函数；继续复用已有 `CenterPositionForResize`。
- Modify: `payload/timer_logic.cpp` — 实现准星默认中心偏移，并把布局选项中文字加入固定字形文本。
- Modify: `payload/timer_logic_tests.cpp` — 删除自动方向阈值测试，保留物品栏尺寸/中心/约束测试，并以失败测试覆盖准星默认中心、宽高变化、拖动后中心及靠边约束。
- Modify: `payload/inventory_bar.cpp` — 恢复保存布局、移除自动切换调用和死状态、增加横版/竖版 Combo，并让手动切换与槽位缩放共用中心保持逻辑。
- Modify: `payload/crosshair_info.cpp` — 延迟到首次实际显示完成默认定位，记录已应用实际尺寸，并在文字或字号改变后保持当前中心。

不修改 `payload/settings_store.h`、`payload/settings_store.cpp` 或其配置版本：现有 `InventoryBarSettings::layout`、`position`、`positionInitialized` 足够表达需求。准星继续使用现有位置字段和 `NeedsInitialCrosshairInfoLayout()` 初始化请求，不向配置结构增加窗口尺寸字段；实际尺寸仅是进程内绘制状态。

当前工作目录没有 Git。本计划不包含提交步骤，所有实施结果保留在工作目录中。

---

### Task 1: 用失败测试锁定准星中心位置和尺寸变化行为

**Files:**
- Modify: `payload/timer_logic.h:43-53`
- Modify: `payload/timer_logic_tests.cpp:109-151,222-239`

- [ ] **Step 1: 声明准星默认中心偏移纯函数**

在 `CenterPositionForResize` 后声明新函数；偏移量表示相对于屏幕中心的 Y 方向偏移，`-10.0f` 即中心上移 10px：

```cpp
Position DefaultCenterOffset(Size viewport, Size window, float centerOffsetY = -10.0f);
```

- [ ] **Step 2: 添加默认中心与屏幕约束断言**

在现有 `CenterPositionForResize` 断言后加入：

```cpp
assert((DefaultCenterOffset({1920.0f, 1080.0f}, {300.0f, 40.0f}) ==
        Position{810.0f, 510.0f}));
assert((DefaultCenterOffset({800.0f, 600.0f}, {200.0f, 80.0f}, -10.0f) ==
        Position{300.0f, 250.0f}));
assert((DefaultCenterOffset({100.0f, 40.0f}, {200.0f, 80.0f}) ==
        Position{0.0f, 0.0f}));
```

第一条验证窗口内容中心为 `(960, 530)`，即屏幕中心 `(960, 540)` 上方 10px；第三条验证窗口大于屏幕时仍完整走 `ClampPosition` 的既有约束语义。

- [ ] **Step 3: 添加文字变宽、变窄和高度变化断言**

复用现有 `CenterPositionForResize`，用准星信息窗口尺寸场景加入：

```cpp
assert((CenterPositionForResize(
    {800.0f, 510.0f}, {320.0f, 40.0f}, {480.0f, 40.0f}, {1920.0f, 1080.0f}) ==
    Position{720.0f, 510.0f}));
assert((CenterPositionForResize(
    {720.0f, 510.0f}, {480.0f, 40.0f}, {200.0f, 40.0f}, {1920.0f, 1080.0f}) ==
    Position{860.0f, 510.0f}));
assert((CenterPositionForResize(
    {860.0f, 510.0f}, {200.0f, 40.0f}, {200.0f, 60.0f}, {1920.0f, 1080.0f}) ==
    Position{860.0f, 500.0f}));
```

这些断言分别证明变宽向左右各扩展 80px、变窄向左右各收缩 140px、高度增加时上下各扩展 10px。

- [ ] **Step 4: 添加拖动后保持用户中心和靠边约束断言**

```cpp
assert((CenterPositionForResize(
    {300.0f, 200.0f}, {200.0f, 40.0f}, {400.0f, 60.0f}, {1920.0f, 1080.0f}) ==
    Position{200.0f, 190.0f}));
assert((CenterPositionForResize(
    {10.0f, 100.0f}, {200.0f, 40.0f}, {300.0f, 40.0f}, {800.0f, 600.0f}) ==
    Position{0.0f, 100.0f}));
assert((CenterPositionForResize(
    {590.0f, 520.0f}, {200.0f, 80.0f}, {300.0f, 120.0f}, {800.0f, 600.0f}) ==
    Position{500.0f, 480.0f}));
```

第一条把 `{300,200}` 当作 Alt 拖动后保存的新左上角，证明后续变化围绕用户新中心；后两条证明先按中心公式换算，再约束到左边和右下边界。

- [ ] **Step 5: 把新 UI 文字加入字形覆盖测试**

将固定字形验证输入中的物品栏片段从：

```cpp
u8"物品栏槽位大小高光颜色显示等级形状半径高度颜色"
```

改为：

```cpp
u8"物品栏布局横版竖版槽位大小高光颜色显示等级形状半径高度颜色"
```

- [ ] **Step 6: 运行测试确认 RED**

Run:

```powershell
cmake --build "K:\ToolsBOX\out" --config Debug --target timer_logic_tests
```

Expected: 编译或链接失败并明确指出 `DefaultCenterOffset` 尚未定义；失败来自新增行为而非已有测试。

---

### Task 2: 实现准星默认中心偏移纯逻辑并补齐固定字形

**Files:**
- Modify: `payload/timer_logic.cpp:12-21,65-78`
- Test: `payload/timer_logic_tests.cpp`

- [ ] **Step 1: 实现默认中心偏移函数**

紧跟 `CenterPositionForResize` 实现加入：

```cpp
Position DefaultCenterOffset(Size viewport, Size window, float centerOffsetY) {
    return ClampPosition(
        {(viewport.width - window.width) * 0.5f,
         (viewport.height - window.height) * 0.5f + centerOffsetY},
        window, viewport);
}
```

该函数以窗口内容尺寸计算左上角，不把屏幕中心误当作左上角；约束只通过已有 `ClampPosition` 完成。

- [ ] **Step 2: 将布局选项加入固定 UI 文本**

把 `kFixedUiText` 的物品栏片段改为：

```cpp
u8"圣木计时器游戏时间物品栏布局横版竖版槽位大小高光颜色启用:固定:字体大小:字体颜色:无记录颜色:有记录颜色:"
```

这样动态字体图集始终包含 Combo 标签需要的“布局”“横版”“竖版”字形。

- [ ] **Step 3: 运行测试确认 GREEN**

Run:

```powershell
cmake --build "K:\ToolsBOX\out" --config Debug --target timer_logic_tests; ctest --test-dir "K:\ToolsBOX\out" -C Debug -R "^timer_logic_tests$" --output-on-failure
```

Expected: `timer_logic_tests` 构建成功，CTest 报告该测试通过；新增默认中心、尺寸变化和字形断言全部为绿色。

---

### Task 3: 删除物品栏自动方向判断及边缘阈值测试

**Files:**
- Modify: `payload/feature_logic.h:15-21`
- Modify: `payload/feature_logic.cpp:7-18`
- Modify: `payload/timer_logic_tests.cpp:50-56`

- [ ] **Step 1: 删除自动方向阈值测试**

完整删除以下七条 `ResolveInventoryLayout(...)` 断言：左边、右边、上边、下边、角落、保持旧方向和无旧方向测试。保留 `InventoryLayoutSize` 的横版/竖版尺寸断言、`CenterPositionForResize` 的横纵换算断言、`ClampPosition` 的四边约束断言以及 `InventoryBarSettings{}.layout == 0` 默认值断言。

- [ ] **Step 2: 删除不再使用的函数声明**

从 `feature_logic.h` 删除：

```cpp
InventoryLayout ResolveInventoryLayout(float left, float top, float right, float bottom,
                                       InventoryLayout previous, bool hasPrevious,
                                       float edgeThreshold = 24.0f);
```

保留：

```cpp
enum class InventoryLayout { Horizontal, Vertical };
```

该枚举仍用于物品栏绘制和布局尺寸适配。

- [ ] **Step 3: 删除自动方向实现**

从 `feature_logic.cpp` 完整删除：

```cpp
InventoryLayout ResolveInventoryLayout(float left, float top, float right, float bottom,
                                       InventoryLayout previous, bool hasPrevious,
                                       float edgeThreshold) {
    const bool nearHorizontalEdge = top <= edgeThreshold || bottom <= edgeThreshold;
    if (nearHorizontalEdge) return InventoryLayout::Horizontal;
    if (left <= edgeThreshold || right <= edgeThreshold) return InventoryLayout::Vertical;
    return hasPrevious ? previous : InventoryLayout::Horizontal;
}
```

- [ ] **Step 4: 运行纯逻辑测试确认清理未破坏保留行为**

Run:

```powershell
cmake --build "K:\ToolsBOX\out" --config Debug --target timer_logic_tests; ctest --test-dir "K:\ToolsBOX\out" -C Debug -R "^timer_logic_tests$" --output-on-failure
```

Expected: 测试构建并通过；横版/竖版尺寸、中心换算、默认横版和屏幕约束测试仍在执行。

---

### Task 4: 将物品栏改为保存布局驱动的手动切换

**Files:**
- Modify: `payload/inventory_bar.cpp:47-58,166-195,225-347,349-369`
- Test: `payload/timer_logic_tests.cpp`

- [ ] **Step 1: 删除自动布局遗留状态**

删除全局变量：

```cpp
bool g_hasLayout = false;
```

同时删除 `Initialize()` 中的：

```cpp
g_hasLayout = settings.positionInitialized;
```

删除 `Shutdown()` 中的：

```cpp
g_hasLayout = false;
```

重置按钮中也不再写 `g_hasLayout`。`g_layout` 和 `g_appliedContentSize` 保留，分别表示已应用布局和已应用窗口尺寸。

- [ ] **Step 2: 让绘制循环只响应保存布局变化**

在 `DrawWindow()` 取得 `display` 后，从设置字段计算本帧目标布局，并用目标布局计算内容尺寸：

```cpp
const auto desiredLayout = settings.layout == 1
    ? feature_logic::InventoryLayout::Vertical
    : feature_logic::InventoryLayout::Horizontal;
const ImVec2 display = ImGui::GetIO().DisplaySize;
const ImVec2 contentSize = LayoutSize(settings.slotSize, desiredLayout);
```

把现有尺寸变化分支替换为同时处理手动布局和槽位尺寸的统一分支：

```cpp
if (!g_resetPosition && settings.positionInitialized &&
    (desiredLayout != g_layout ||
     g_appliedContentSize.width != contentSize.x ||
     g_appliedContentSize.height != contentSize.y)) {
    settings.position = timer_logic::CenterPositionForResize(
        settings.position, g_appliedContentSize, {contentSize.x, contentSize.y},
        {display.x, display.y});
    ImGui::SetNextWindowPos({settings.position.x, settings.position.y}, ImGuiCond_Always);
    g_layout = desiredLayout;
    g_appliedContentSize = {contentSize.x, contentSize.y};
    g_positionApplied = true;
    settings_store::MarkDirty();
} else if (settings.positionInitialized && !g_positionApplied) {
    ImGui::SetNextWindowPos({settings.position.x, settings.position.y}, ImGuiCond_Always);
    g_positionApplied = true;
}
```

这样 Combo 切换和槽位大小变化都只在保存值真正变化时换算一次，并且先保持中心、再由纯函数内部约束到屏幕。

- [ ] **Step 3: 删除窗口末尾自动方向调用和死逻辑**

完整删除 `ResolveInventoryLayout(...)` 调用、`desiredLayout != g_layout` 自动切换块以及 `g_hasLayout = true`。窗口绘制完成后仅保留重置处理、按当前 `g_layout` 计算最终尺寸、约束当前位置和持久化实际位置的逻辑：

```cpp
const ImVec2 finalSize = LayoutSize(settings.slotSize, g_layout);
const auto clamped = timer_logic::ClampPosition(
    {windowPosition.x, windowPosition.y}, {finalSize.x, finalSize.y}, {display.x, display.y});
if (clamped.x != windowPosition.x || clamped.y != windowPosition.y)
    ImGui::SetWindowPos({clamped.x, clamped.y});
ImGui::End();
if (clamped.x != settings.position.x || clamped.y != settings.position.y) {
    settings.position = clamped;
    settings_store::MarkDirty();
}
```

拖动物品栏到任何角落不再有修改 `settings.layout` 的路径。

- [ ] **Step 4: 在设置面板新增横版/竖版 Combo**

在“固定”与“槽位大小”之间加入：

```cpp
settings_ui::LabelControlRow(u8"布局:");
ImGui::SetNextItemWidth(100.0f);
constexpr const char* layouts[] = {u8"横版", u8"竖版"};
changed |= ImGui::Combo("##inventory_layout", &settings.layout, layouts, 2);
```

Combo 直接写现有 `layout` 字段，不增加并行 UI 状态；`DrawWindow()` 在下一帧检测字段变化并围绕当前中心应用一次。

- [ ] **Step 5: 保持初始化恢复和重置顺序明确**

保留 `Initialize()` 从 `settings.layout` 恢复 `g_layout` 的现有代码。重置按钮保持当前 `enabled` 后恢复默认结构，并显式准备默认横版：

```cpp
const bool enabled = settings.enabled;
settings = settings_store::InventoryBarSettings{};
settings.enabled = enabled;
g_layout = feature_logic::InventoryLayout::Horizontal;
g_appliedContentSize = timer_logic::InventoryLayoutSize(settings.slotSize, false);
g_positionApplied = false;
RequestResetPosition();
changed = true;
```

绘制中的重置分支继续先设置：

```cpp
g_layout = feature_logic::InventoryLayout::Horizontal;
settings.layout = 0;
```

再用默认 50px 横版尺寸调用 `DefaultBottomCenter`，得到 1920×1080 下 `{854,1010}`，即底部中央且距底 20px。重启时不触发自动方向逻辑，直接恢复保存的 0/1 布局。

- [ ] **Step 6: 构建物品栏相关目标并运行测试**

Run:

```powershell
cmake --build "K:\ToolsBOX\out" --config Debug --target timer_logic_tests payload; ctest --test-dir "K:\ToolsBOX\out" -C Debug -R "^timer_logic_tests$" --output-on-failure
```

Expected: `timer_logic_tests` 与 `payload` 均构建成功，纯逻辑测试通过，且编译阶段没有任何 `ResolveInventoryLayout` 或 `g_hasLayout` 引用错误。

---

### Task 5: 让准星信息围绕首次、拖动后和当前中心伸缩

**Files:**
- Modify: `payload/crosshair_info.cpp:42-50,103-118,158-206`
- Test: `payload/timer_logic_tests.cpp`

- [ ] **Step 1: 增加进程内窗口尺寸状态并正确清理**

在现有匿名命名空间状态中加入：

```cpp
timer_logic::Size g_appliedWindowSize{};
bool g_hasAppliedWindowSize = false;
```

将初始化和关闭改为重置尺寸状态，但不改变保存设置：

```cpp
void Initialize() {
    g_appliedWindowSize = {};
    g_hasAppliedWindowSize = false;
    SyncSettings();
    ResolveMetadata();
}

void Shutdown() {
    ClearSnapshot();
    g_resetPosition = false;
    g_appliedWindowSize = {};
    g_hasAppliedWindowSize = false;
    g_metadata = {};
}
```

尺寸只描述本次进程中上一次实际 `ImGui::GetWindowSize()`，不得加入 `CrosshairInfoSettings`，从而保持配置结构和版本不变。

- [ ] **Step 2: 保持无目标且不可移动时延迟定位**

保留现有提前返回：

```cpp
if (!freshTarget && !canMove) return;
```

`g_resetPosition` 在该返回路径上不得清除。这样首次启动或重置后如果没有有效目标且 Alt 拖动未启用，窗口继续不绘制；直到首次实际 `Begin` 后获得真实文字与字号对应尺寸，才计算默认中心。

- [ ] **Step 3: 用实际尺寸完成首次和重置定位**

在 `Begin`、文字绘制和 `GetWindowSize()` 后，以当前实际尺寸执行：

```cpp
const ImVec2 size = ImGui::GetWindowSize();
const ImVec2 display = ImGui::GetIO().DisplaySize;
ImVec2 position = ImGui::GetWindowPos();
if (g_resetPosition) {
    const auto initial = timer_logic::DefaultCenterOffset(
        {display.x, display.y}, {size.x, size.y});
    ImGui::SetWindowPos({initial.x, initial.y});
    position = {initial.x, initial.y};
    settings.position = initial;
    g_appliedWindowSize = {size.x, size.y};
    g_hasAppliedWindowSize = true;
    g_resetPosition = false;
    settings_store::MarkDirty();
}
```

默认偏移由窗口内容中心计算，结果是屏幕中心上方 10px，而不是把左上角置于中心附近；同时登记本帧尺寸，避免重置后又按重置前尺寸二次换算。

- [ ] **Step 4: 尺寸变化时围绕当前中心重定位**

紧接重置分支加入；首次非重置显示只登记尺寸，后续变化才重定位：

```cpp
if (!g_hasAppliedWindowSize) {
    g_appliedWindowSize = {size.x, size.y};
    g_hasAppliedWindowSize = true;
} else if (g_appliedWindowSize.width != size.x || g_appliedWindowSize.height != size.y) {
    const auto centered = timer_logic::CenterPositionForResize(
        {position.x, position.y}, g_appliedWindowSize, {size.x, size.y},
        {display.x, display.y});
    ImGui::SetWindowPos({centered.x, centered.y});
    position = {centered.x, centered.y};
    settings.position = centered;
    g_appliedWindowSize = {size.x, size.y};
    settings_store::MarkDirty();
}
```

这里以 `ImGui::GetWindowPos()` 得到的当前位置为旧左上角：Alt 拖动发生后，该位置就是用户的新位置，因此下一次文字或字号变化自然围绕用户拖动后的中心伸缩。`CenterPositionForResize` 内部先做中心差值，再执行屏幕约束。

- [ ] **Step 5: 统一最终约束和位置持久化**

用可能已被重置或中心换算更新的 `position` 做最终约束：

```cpp
const auto clamped = timer_logic::ClampPosition(
    {position.x, position.y}, {size.x, size.y}, {display.x, display.y});
if (clamped.x != position.x || clamped.y != position.y)
    ImGui::SetWindowPos({clamped.x, clamped.y});
ImGui::End();
if (!(clamped == settings.position) &&
    (canMove || clamped.x != position.x || clamped.y != position.y)) {
    settings.position = clamped;
    settings_store::MarkDirty();
}
```

保留现有 `settings.fixed ? ImGuiCond_Always : ImGuiCond_FirstUseEver`、`CanMoveOverlay` 和 `NoMove/NoInputs` 行为；固定窗口继续使用持久化位置，Alt 拖动继续保存新位置。

- [ ] **Step 6: 构建 payload 并回归纯逻辑测试**

Run:

```powershell
cmake --build "K:\ToolsBOX\out" --config Debug --target timer_logic_tests payload; ctest --test-dir "K:\ToolsBOX\out" -C Debug -R "^timer_logic_tests$" --output-on-failure
```

Expected: `payload` 编译成功；默认中心、变宽、变窄、高度变化、拖动后中心和靠边约束断言全部通过。

---

### Task 6: 完成 Debug、Release、CTest 和默认 Release EXE 重建

**Files:**
- Verify: `payload/feature_logic.h`
- Verify: `payload/feature_logic.cpp`
- Verify: `payload/timer_logic.h`
- Verify: `payload/timer_logic.cpp`
- Verify: `payload/timer_logic_tests.cpp`
- Verify: `payload/inventory_bar.cpp`
- Verify: `payload/crosshair_info.cpp`
- Verify artifact: `out/bin/Release/PhasmophobiaInjector.exe`

- [ ] **Step 1: 重新生成默认 out 构建目录**

Run:

```powershell
cmake -S "K:\ToolsBOX" -B "K:\ToolsBOX\out" -A x64
```

Expected: 配置和生成成功，继续使用默认 `K:\ToolsBOX\out`，测试目标 `timer_logic_tests` 与最终目标 `PhasmophobiaInjector` 均存在。

- [ ] **Step 2: Debug 全量构建**

Run:

```powershell
cmake --build "K:\ToolsBOX\out" --config Debug
```

Expected: 所有 Debug 目标成功，包括 `timer_logic_tests`、`payload` 和 `PhasmophobiaInjector`。

- [ ] **Step 3: Debug CTest**

Run:

```powershell
ctest --test-dir "K:\ToolsBOX\out" -C Debug --output-on-failure
```

Expected: `100% tests passed`，`timer_logic_tests` 通过。

- [ ] **Step 4: Release 全量构建并重建默认 EXE**

Run:

```powershell
cmake --build "K:\ToolsBOX\out" --config Release --target PhasmophobiaInjector
```

Expected: Release `payload.dll` 先重建并重新生成 `payload_dll.h`，随后链接新的 `PhasmophobiaInjector.exe`。

- [ ] **Step 5: Release CTest**

Run:

```powershell
ctest --test-dir "K:\ToolsBOX\out" -C Release --output-on-failure
```

Expected: `100% tests passed`，Release `timer_logic_tests` 通过。

- [ ] **Step 6: 精确验证最终默认产物**

Run:

```powershell
$artifact = "K:\ToolsBOX\out\bin\Release\PhasmophobiaInjector.exe"; if (-not (Test-Path $artifact -PathType Leaf)) { throw "Missing release injector: $artifact" }; $item = Get-Item $artifact; if ($item.Length -le 0) { throw "Release injector is empty: $artifact" }; $item | Select-Object FullName,Length,LastWriteTime
```

Expected: 输出 `K:\ToolsBOX\out\bin\Release\PhasmophobiaInjector.exe`，长度大于 0，时间戳来自本次 Release 构建。

- [ ] **Step 7: 做最终静态自检**

Run:

```powershell
$removed = Get-ChildItem "K:\ToolsBOX\payload" -File -Include *.h,*.cpp | Select-String -Pattern 'ResolveInventoryLayout|g_hasLayout'; if ($removed) { $removed; throw "Automatic inventory layout logic remains" }; $plan = Get-Content "K:\ToolsBOX\docs\superpowers\plans\2026-07-22-manual-inventory-layout-crosshair-center.md" -Raw; $forbidden = @((('T' + 'BD')), (('TO' + 'DO'))) -join '|'; if ($plan -match "\b($forbidden)\b") { throw "Plan contains a placeholder" }
```

Expected: 命令无错误退出；业务代码中没有自动方向函数或遗留状态，计划中没有占位符。

- [ ] **Step 8: 完成游戏内验收**

使用本次生成的 `K:\ToolsBOX\out\bin\Release\PhasmophobiaInjector.exe` 验证：物品栏移到四个角落时布局不变化；设置 Combo 在横版/竖版间每次只切换一次且中心稳定；调整槽位大小仍保持当前中心；重启恢复保存布局；重置恢复 50px 横版并位于底部中央、距底 20px。准星信息无有效目标且不可移动时不绘制；首次显示和重置后内容中心位于屏幕中心上方 10px；长短文字与字号切换围绕当前中心伸缩；Alt 拖动后再次切换文字仍围绕拖动后的中心；靠边时窗口完整可见；固定和位置持久化行为保持不变。
