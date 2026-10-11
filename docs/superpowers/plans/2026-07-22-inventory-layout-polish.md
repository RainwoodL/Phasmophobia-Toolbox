# 物品栏布局与视觉优化 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 让物品栏按当前中心稳定缩放和切换横纵布局，使用 50–96px 槽位驱动键位字号与直角分层视觉，并让五个透明文字窗口的可见内容可以精确贴到屏幕四边。

**Architecture:** 在现有 `timer_logic` 中加入不依赖 ImGui 的尺寸、中心重定位、底部默认位置与线性字号纯函数，由 `inventory_bar` 同时复用于槽位缩放、横纵切换和重置，位置结果统一经过现有 `ClampPosition`。配置结构和版本保持 v6 不变，仅改变 `slotSize` 默认值/归一化范围并停止 UI 与绘制读取兼容字段 `fontSize`；五个文字窗口仅在各自 `Begin` 周围局部压入零 `WindowPadding`，主菜单及非窗口覆盖层不受影响。

**Tech Stack:** C++17、Dear ImGui、D3D11、CMake/CTest、MSVC、Windows x64

---

## 文件结构与边界

- Modify: `payload/timer_logic.h` — 声明通用物品栏布局尺寸、中心重定位、底部居中和线性字号纯函数。
- Modify: `payload/timer_logic.cpp` — 实现纯函数，并继续以 `ClampPosition` 作为唯一屏幕约束入口。
- Modify: `payload/timer_logic_tests.cpp` — 先覆盖 50/96 边界、横纵尺寸、中心保持、靠边约束、线性字号、重置位置及四边精确贴边。
- Modify: `payload/settings_store.h` — 将 `InventoryBarSettings::slotSize` 默认值改为 50，原位保留 `fontSize` 字段以维持 v6 二进制布局。
- Modify: `payload/settings_store.cpp` — 加载归一化范围改为 50–96；不升级配置版本，不删除或重排字段。
- Modify: `payload/inventory_bar.cpp` — 设置范围、中心缩放、重置顺序、字号选择和槽位六层绘制。
- Modify: `payload/smudge_timer.cpp` — 圣木计时器窗口局部零内边距。
- Modify: `payload/game_time.cpp` — 游戏时间窗口局部零内边距。
- Modify: `payload/ghost_status.cpp` — 鬼魂状态窗口局部零内边距。
- Modify: `payload/crucifix_timer.cpp` — 十字架计时器窗口局部零内边距。
- Modify: `payload/crosshair_info.cpp` — 准星信息窗口局部零内边距。

不修改 `payload/overlay.cpp` 的主设置菜单、`payload/screen_crosshair.cpp` 的屏幕中心定位或 `payload/player_nicknames.cpp` 的世界坐标投影；不新增颜色设置，不修改 `kVersion = 6`，不改变 `Settings`/`InventoryBarSettings` 字段顺序。

---

### Task 1: 用失败测试锁定布局、缩放、字号与精确贴边行为

**Files:**
- Modify: `payload/timer_logic_tests.cpp:100-177`
- Modify: `payload/timer_logic.h:38-49`

- [ ] **Step 1: 在头文件声明将由测试驱动的纯函数**

在 `ClampPosition` 后加入以下声明；`InventoryLayoutSize` 接收 `vertical`，避免 `timer_logic` 反向依赖 `feature_logic::InventoryLayout`：

```cpp
Size InventoryLayoutSize(float slotSize, bool vertical);
Position CenterPositionForResize(Position position, Size previousWindow,
                                 Size nextWindow, Size viewport);
Position DefaultBottomCenter(Size viewport, Size window, float bottomMargin = 20.0f);
float InventoryKeyFontSize(float slotSize);
```

- [ ] **Step 2: 添加 50/96、横纵尺寸和线性字号断言**

在 `timer_logic_tests.cpp` 已有 `ClampPosition` 测试附近加入：

```cpp
const Size horizontal50 = InventoryLayoutSize(50.0f, false);
const Size vertical50 = InventoryLayoutSize(50.0f, true);
const Size horizontal96 = InventoryLayoutSize(96.0f, false);
const Size vertical96 = InventoryLayoutSize(96.0f, true);
assert(horizontal50.width == 212.0f && horizontal50.height == 50.0f);
assert(vertical50.width == 50.0f && vertical50.height == 212.0f);
assert(horizontal96.width == 396.0f && horizontal96.height == 96.0f);
assert(vertical96.width == 96.0f && vertical96.height == 396.0f);
assert(InventoryKeyFontSize(50.0f) == 16.0f);
assert(InventoryKeyFontSize(73.0f) == 25.0f);
assert(InventoryKeyFontSize(96.0f) == 34.0f);
assert(InventoryKeyFontSize(20.0f) == 16.0f);
assert(InventoryKeyFontSize(120.0f) == 34.0f);
```

这里的中点断言精确验证公式 `16 + (slotSize - 50) * 18 / 46`，而非仅验证单调性。

- [ ] **Step 3: 添加中心缩放、横纵切换、边缘约束和重置位置断言**

继续加入：

```cpp
assert((CenterPositionForResize(
    {100.0f, 200.0f}, {212.0f, 50.0f}, {396.0f, 96.0f}, {1920.0f, 1080.0f}) ==
    Position{8.0f, 177.0f}));
assert((CenterPositionForResize(
    {100.0f, 200.0f}, {212.0f, 50.0f}, {50.0f, 212.0f}, {1920.0f, 1080.0f}) ==
    Position{181.0f, 119.0f}));
assert((CenterPositionForResize(
    {0.0f, 0.0f}, {212.0f, 50.0f}, {396.0f, 96.0f}, {800.0f, 600.0f}) ==
    Position{0.0f, 0.0f}));
assert((CenterPositionForResize(
    {588.0f, 550.0f}, {212.0f, 50.0f}, {396.0f, 96.0f}, {800.0f, 600.0f}) ==
    Position{404.0f, 504.0f}));
assert((DefaultBottomCenter({1920.0f, 1080.0f}, InventoryLayoutSize(50.0f, false)) ==
    Position{854.0f, 1010.0f}));
assert((DefaultBottomCenter({200.0f, 40.0f}, InventoryLayoutSize(50.0f, false)) ==
    Position{0.0f, 0.0f}));
```

第一个缩放断言验证中心 `(206,225)` 在无约束时不变；第二个断言验证横转纵复用同一计算；第三、四个断言验证先计算中心、再约束到左上和右下。

- [ ] **Step 4: 将现有位置约束测试扩展为四边精确贴边**

加入显式等于边界的四组断言，防止引入 1px 安全间距：

```cpp
const Size edgeViewport{800.0f, 600.0f};
const Size edgeWindow{200.0f, 80.0f};
assert((ClampPosition({0.0f, 123.0f}, edgeWindow, edgeViewport) == Position{0.0f, 123.0f}));
assert((ClampPosition({321.0f, 0.0f}, edgeWindow, edgeViewport) == Position{321.0f, 0.0f}));
assert((ClampPosition({600.0f, 123.0f}, edgeWindow, edgeViewport) == Position{600.0f, 123.0f}));
assert((ClampPosition({321.0f, 520.0f}, edgeWindow, edgeViewport) == Position{321.0f, 520.0f}));
```

- [ ] **Step 5: 构建测试目标确认 RED**

Run:

```powershell
cmake --build "K:\ToolsBOX\out" --config Debug --target timer_logic_tests
```

Expected: 链接失败，错误指向 `InventoryLayoutSize`、`CenterPositionForResize`、`DefaultBottomCenter` 和 `InventoryKeyFontSize` 尚未定义；不是测试源码编译错误。

---

### Task 2: 实现纯布局逻辑并让测试转绿

**Files:**
- Modify: `payload/timer_logic.cpp:50-79`
- Test: `payload/timer_logic_tests.cpp`

- [ ] **Step 1: 实现横纵布局尺寸**

在 `ClampPosition` 后加入：

```cpp
Size InventoryLayoutSize(float slotSize, bool vertical) {
    constexpr float gap = 4.0f;
    constexpr float slotCount = 4.0f;
    const float longSide = slotSize * slotCount + gap * (slotCount - 1.0f);
    return vertical ? Size{slotSize, longSide} : Size{longSide, slotSize};
}
```

- [ ] **Step 2: 实现以旧窗口中心为锚点、最后才 Clamp 的位置计算**

```cpp
Position CenterPositionForResize(Position position, Size previousWindow,
                                 Size nextWindow, Size viewport) {
    return ClampPosition(
        {position.x + (previousWindow.width - nextWindow.width) * 0.5f,
         position.y + (previousWindow.height - nextWindow.height) * 0.5f},
        nextWindow, viewport);
}
```

不要在计算中心前 Clamp 旧位置，也不要分别为缩放和横纵切换复制公式。

- [ ] **Step 3: 实现底部居中默认位置与线性字号**

```cpp
Position DefaultBottomCenter(Size viewport, Size window, float bottomMargin) {
    return ClampPosition(
        {(viewport.width - window.width) * 0.5f,
         viewport.height - window.height - bottomMargin},
        window, viewport);
}

float InventoryKeyFontSize(float slotSize) {
    const float clamped = std::isfinite(slotSize) ? std::clamp(slotSize, 50.0f, 96.0f) : 50.0f;
    return 16.0f + (clamped - 50.0f) * (34.0f - 16.0f) / (96.0f - 50.0f);
}
```

- [ ] **Step 4: 构建并运行目标测试确认 GREEN**

Run:

```powershell
cmake --build "K:\ToolsBOX\out" --config Debug --target timer_logic_tests; ctest --test-dir "K:\ToolsBOX\out" -C Debug -R "^timer_logic_tests$" --output-on-failure
```

Expected: 构建成功，CTest 报告 `timer_logic_tests` 通过。

---

### Task 3: 更新配置默认值与旧配置归一化，同时保留字段兼容

**Files:**
- Modify: `payload/settings_store.h:71-80`
- Modify: `payload/settings_store.cpp:104-143`
- Modify: `payload/timer_logic_tests.cpp:171-178`

- [ ] **Step 1: 先修改默认设置测试为 50，并显式确认兼容字段仍存在**

将已有断言改为：

```cpp
const settings_store::InventoryBarSettings inventoryDefaults{};
assert(!inventoryDefaults.enabled && !inventoryDefaults.fixed);
assert(inventoryDefaults.slotSize == 50.0f && inventoryDefaults.fontSize == 26.0f);
assert(inventoryDefaults.highlightColor.r == 1.0f && inventoryDefaults.highlightColor.g == 1.0f &&
       inventoryDefaults.highlightColor.b == 1.0f && inventoryDefaults.highlightColor.a == 1.0f);
assert(!inventoryDefaults.positionInitialized);
assert(inventoryDefaults.layout == 0);
```

- [ ] **Step 2: 构建确认默认值测试 RED**

Run:

```powershell
cmake --build "K:\ToolsBOX\out" --config Debug --target timer_logic_tests; ctest --test-dir "K:\ToolsBOX\out" -C Debug -R "^timer_logic_tests$" --output-on-failure
```

Expected: 测试进程因 `inventoryDefaults.slotSize == 50.0f` 断言失败。

- [ ] **Step 3: 修改默认值和加载约束范围**

在 `settings_store.h` 仅改变默认值，不删除 `fontSize`：

```cpp
struct InventoryBarSettings {
    bool enabled = false;
    bool fixed = false;
    float slotSize = 50.0f;
    float fontSize = 26.0f;
    timer_logic::Color highlightColor{1.0f, 1.0f, 1.0f, 1.0f};
    timer_logic::Position position{};
    bool positionInitialized = false;
    int layout = 0;
};
```

在 `Normalize()` 中改为：

```cpp
g_settings.inventoryBar.slotSize = std::isfinite(g_settings.inventoryBar.slotSize)
    ? std::clamp(g_settings.inventoryBar.slotSize, 50.0f, 96.0f) : inventoryDefaults.slotSize;
g_settings.inventoryBar.fontSize = timer_logic::SnapFontSize(g_settings.inventoryBar.fontSize);
```

保留第二行是为了让旧二进制字段继续得到合法化；后续绘制不得读取该字段决定键位字号。

- [ ] **Step 4: 审查配置布局和版本未变化**

确认 `settings_store.cpp` 仍为：

```cpp
constexpr std::uint32_t kVersion = 6;
```

并确认 `InventoryBarSettings` 字段顺序、`Settings` 字段顺序、`StoredSettingsV6` 和 v1–v5 迁移分支均未调整。

- [ ] **Step 5: 运行测试确认 GREEN**

Run:

```powershell
cmake --build "K:\ToolsBOX\out" --config Debug --target timer_logic_tests; ctest --test-dir "K:\ToolsBOX\out" -C Debug -R "^timer_logic_tests$" --output-on-failure
```

Expected: `timer_logic_tests` 通过。

---

### Task 4: 将物品栏缩放、横纵切换和重置统一到纯尺寸/位置逻辑

**Files:**
- Modify: `payload/inventory_bar.cpp:144-149, 222-349`
- Test: `payload/timer_logic_tests.cpp`

- [ ] **Step 1: 让 ImGui 尺寸适配器复用纯函数**

将本地 `LayoutSize` 改为仅做类型转换：

```cpp
ImVec2 LayoutSize(float slotSize, feature_logic::InventoryLayout layout) {
    const auto size = timer_logic::InventoryLayoutSize(
        slotSize, layout == feature_logic::InventoryLayout::Vertical);
    return {size.width, size.height};
}
```

- [ ] **Step 2: 记录上一帧已经应用的内容尺寸**

在匿名命名空间状态中加入：

```cpp
timer_logic::Size g_appliedContentSize{};
```

在 `Initialize` 根据加载后的槽位大小和方向初始化，在 `Shutdown` 清零：

```cpp
const ImVec2 initialSize = LayoutSize(settings.slotSize, g_layout);
g_appliedContentSize = {initialSize.x, initialSize.y};
```

```cpp
g_appliedContentSize = {};
```

该状态明确表示屏幕上当前位置所对应的旧尺寸，避免依赖 ImGui `AlwaysAutoResize` 在本帧何时更新 `GetWindowSize()`。

- [ ] **Step 3: 槽位大小改变时保持当前窗口中心**

在 `Begin` 前计算目标尺寸；若它与已应用尺寸不同，则先中心重定位、保存位置，再把位置交给 ImGui：

```cpp
const ImVec2 display = ImGui::GetIO().DisplaySize;
const ImVec2 contentSize = LayoutSize(settings.slotSize, g_layout);
if (!g_resetPosition && settings.positionInitialized &&
    (g_appliedContentSize.width != contentSize.x || g_appliedContentSize.height != contentSize.y)) {
    settings.position = timer_logic::CenterPositionForResize(
        settings.position, g_appliedContentSize, {contentSize.x, contentSize.y},
        {display.x, display.y});
    ImGui::SetNextWindowPos({settings.position.x, settings.position.y}, ImGuiCond_Always);
    g_appliedContentSize = {contentSize.x, contentSize.y};
    g_positionApplied = true;
    settings_store::MarkDirty();
}
```

`Begin` 后令 `windowPosition = ImGui::GetWindowPos()`。更新位置后立即同步已应用尺寸，保证同帧若还发生横纵切换，下一次中心计算从当前尺寸继续，而不会重复使用旧尺寸。

- [ ] **Step 4: 横纵切换复用同一中心重定位函数**

将 `desiredLayout != g_layout` 分支里的手写中心公式替换为：

```cpp
const ImVec2 nextSize = LayoutSize(settings.slotSize, desiredLayout);
const auto centered = timer_logic::CenterPositionForResize(
    {windowPosition.x, windowPosition.y}, g_appliedContentSize,
    {nextSize.x, nextSize.y}, {display.x, display.y});
ImGui::SetWindowPos({centered.x, centered.y});
windowPosition = {centered.x, centered.y};
g_layout = desiredLayout;
g_appliedContentSize = {nextSize.x, nextSize.y};
settings.layout = g_layout == feature_logic::InventoryLayout::Vertical ? 1 : 0;
settings_store::MarkDirty();
```

方向判定继续使用窗口到四边距离；横向和纵向不得出现两套重定位实现。

- [ ] **Step 5: 重置先恢复尺寸/方向，再按新尺寸定位**

重置按钮保留 `enabled` 后先赋默认设置并显式恢复横向状态：

```cpp
const bool enabled = settings.enabled;
settings = settings_store::InventoryBarSettings{};
settings.enabled = enabled;
g_layout = feature_logic::InventoryLayout::Horizontal;
g_hasLayout = false;
g_positionApplied = false;
RequestResetPosition();
changed = true;
```

在绘制中的重置分支用默认 50px 横向尺寸计算位置：

```cpp
if (g_resetPosition || !settings.positionInitialized) {
    g_layout = feature_logic::InventoryLayout::Horizontal;
    settings.layout = 0;
    const ImVec2 resetSize = LayoutSize(settings.slotSize, g_layout);
    const auto initial = timer_logic::DefaultBottomCenter(
        {display.x, display.y}, {resetSize.x, resetSize.y});
    g_appliedContentSize = {resetSize.x, resetSize.y};
    ImGui::SetWindowPos({initial.x, initial.y});
    windowPosition = {initial.x, initial.y};
    settings.position = initial;
    settings.positionInitialized = true;
    g_positionApplied = true;
    g_resetPosition = false;
    settings_store::MarkDirty();
}
```

该顺序保证不使用重置前的槽位大小或纵向窗口尺寸，同时只保留功能当前 `enabled` 状态。

- [ ] **Step 6: 设置面板范围改为 50–96，并移除字体控件**

将滑块改为：

```cpp
changed |= ImGui::SliderFloat("##inventory_slot_size", &settings.slotSize, 50.0f, 96.0f, "%.0f px");
```

完整删除 `fontIndex`、`字体大小:` 行、`##inventory_font` Combo 及对 `settings.fontSize` 的赋值；不得删除设置结构中的 `fontSize` 字段。

- [ ] **Step 7: 构建 payload 与测试目标**

Run:

```powershell
cmake --build "K:\ToolsBOX\out" --config Debug --target timer_logic_tests payload; ctest --test-dir "K:\ToolsBOX\out" -C Debug -R "^timer_logic_tests$" --output-on-failure
```

Expected: 两个目标构建成功，纯逻辑测试通过。

---

### Task 5: 使用线性目标字号并按字体仓库最近可用字号绘制键位

**Files:**
- Modify: `payload/inventory_bar.cpp:245-274`
- Test: `payload/timer_logic_tests.cpp`

- [ ] **Step 1: 在槽位循环前只计算一次可用字号**

替换 `font_store::Get(settings.fontSize)`：

```cpp
const float requestedKeyFontSize = timer_logic::InventoryKeyFontSize(settings.slotSize);
const int keyFontIndex = font_store::IndexOf(requestedKeyFontSize);
const float keyFontSize = font_store::Sizes()[keyFontIndex];
ImFont* keyFont = font_store::Get(keyFontSize);
```

`IndexOf` 通过现有 `SnapFontSize` 在 `{16,20,24,26,28,32,36,42,48,56}` 中选择最近项；平局遵循现有较小字号行为。不得读取 `settings.fontSize`。

- [ ] **Step 2: 使用选中的实际字号绘制键位数字**

将键位绘制改为：

```cpp
const char key[] = {static_cast<char>('1' + i), '\0'};
draw->AddText(keyFont, keyFontSize, {min.x + 4.0f, min.y + 3.0f}, IM_COL32_WHITE, key);
```

该调用保持键位位于贴图上层；字体仓库已有数字字形，不需要动态请求或新增字体。

- [ ] **Step 3: 验证旧字段不再参与物品栏 UI/绘制**

Run:

```powershell
Select-String -Path "K:\ToolsBOX\payload\inventory_bar.cpp" -Pattern "fontSize|inventory_font"
```

Expected: 无匹配；`settings_store.h/.cpp` 中仍存在兼容字段及其归一化。

- [ ] **Step 4: 构建 Debug payload**

Run:

```powershell
cmake --build "K:\ToolsBOX\out" --config Debug --target payload timer_logic_tests
```

Expected: 构建成功。

---

### Task 6: 按固定六层顺序绘制正方形直角槽位

**Files:**
- Modify: `payload/inventory_bar.cpp:251-274`

- [ ] **Step 1: 在槽位循环内先绘制固定深色背景**

紧接 `min/max` 计算后加入，无圆角参数：

```cpp
constexpr ImU32 slotBackground = IM_COL32(16, 18, 24, 210);
draw->AddRectFilled(min, max, slotBackground, 0.0f);
```

- [ ] **Step 2: 当前槽在深色背景上叠加轻微高光底色**

将 `active` 判断提前到背景之后，并加入：

```cpp
const bool active = snapshot.activeIndex == i;
if (active) {
    const ImVec4 highlightFill{
        settings.highlightColor.r, settings.highlightColor.g,
        settings.highlightColor.b, settings.highlightColor.a * 0.18f};
    draw->AddRectFilled(min, max, ImGui::ColorConvertFloat4ToU32(highlightFill), 0.0f);
}
```

不新增背景或高光底色配置字段。

- [ ] **Step 3: 保持贴图等比例居中，再绘制键位**

保留现有 `FitImageRect` 与 `AddImage`，并确保代码顺序严格为高光底色之后、`AddText` 之前：

```cpp
const auto& visual = snapshot.items[static_cast<std::size_t>(i)];
if (const auto* texture = inventory_textures::Get(visual)) {
    const ImageRect image = FitImageRect(min, settings.slotSize, texture->width, texture->height);
    draw->AddImage(reinterpret_cast<ImTextureID>(texture->shaderResourceView),
                   image.min, image.max, {0.0f, 0.0f}, {1.0f, 1.0f}, IM_COL32_WHITE);
}
const char key[] = {static_cast<char>('1' + i), '\0'};
draw->AddText(keyFont, keyFontSize, {min.x + 4.0f, min.y + 3.0f}, IM_COL32_WHITE, key);
```

- [ ] **Step 4: 最后绘制内亮边与外框**

键位之后加入内框，再绘制最外框：

```cpp
const ImVec2 innerMin{min.x + 1.0f, min.y + 1.0f};
const ImVec2 innerMax{max.x - 1.0f, max.y - 1.0f};
draw->AddRect(innerMin, innerMax, IM_COL32(255, 255, 255, 42), 0.0f, 0, 1.0f);
const ImU32 outerBorder = active
    ? ImGui::ColorConvertFloat4ToU32({settings.highlightColor.r, settings.highlightColor.g,
                                      settings.highlightColor.b, settings.highlightColor.a})
    : IM_COL32(255, 255, 255, 115);
draw->AddRect(min, max, outerBorder, 0.0f, 0, active ? 2.0f : 1.0f);
```

最终循环中的调用顺序必须可直接读出为：`AddRectFilled` 深色背景、可选 `AddRectFilled` 高光底色、`AddImage`、`AddText`、内层 `AddRect`、外层 `AddRect`。所有圆角参数保持 `0.0f`，槽位尺寸始终为 `slotSize × slotSize`。

- [ ] **Step 5: 构建 Debug payload**

Run:

```powershell
cmake --build "K:\ToolsBOX\out" --config Debug --target payload
```

Expected: `payload.dll` 构建成功，无 ImGui 参数或颜色类型错误。

---

### Task 7: 为五个透明文字窗口局部设置零 WindowPadding

**Files:**
- Modify: `payload/smudge_timer.cpp:199-207`
- Modify: `payload/game_time.cpp:46-54`
- Modify: `payload/ghost_status.cpp:249-258`
- Modify: `payload/crucifix_timer.cpp:151-159`
- Modify: `payload/crosshair_info.cpp:177-183`

- [ ] **Step 1: 修改圣木计时器窗口**

在 `Begin` 紧邻位置局部压入并恢复样式：

```cpp
ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.0f, 0.0f});
ImGui::Begin("##smudge_timer", nullptr, flags);
ImGui::PopStyleVar();
```

- [ ] **Step 2: 修改游戏时间窗口**

```cpp
ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.0f, 0.0f});
ImGui::Begin(name, nullptr, flags);
ImGui::PopStyleVar();
```

- [ ] **Step 3: 修改鬼魂状态窗口**

```cpp
ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.0f, 0.0f});
ImGui::Begin("##ghost_status", nullptr, flags);
ImGui::PopStyleVar();
```

- [ ] **Step 4: 修改十字架计时器窗口**

```cpp
ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.0f, 0.0f});
ImGui::Begin("##crucifix_timer", nullptr, flags);
ImGui::PopStyleVar();
```

- [ ] **Step 5: 修改准星信息窗口**

```cpp
ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.0f, 0.0f});
ImGui::Begin("##crosshair_info", nullptr, flags);
ImGui::PopStyleVar();
```

每处都在 `Begin` 后立即 `PopStyleVar`：窗口创建时读取零内边距，随后恢复全局样式，确保 `DrawMenuWindow()` 仍使用现有内边距。物品栏已有同样的局部零内边距，保持不变；不触碰屏幕准星和玩家昵称。

- [ ] **Step 6: 检查五处成对使用并构建 payload**

Run:

```powershell
Select-String -Path "K:\ToolsBOX\payload\smudge_timer.cpp","K:\ToolsBOX\payload\game_time.cpp","K:\ToolsBOX\payload\ghost_status.cpp","K:\ToolsBOX\payload\crucifix_timer.cpp","K:\ToolsBOX\payload\crosshair_info.cpp" -Pattern "ImGuiStyleVar_WindowPadding|PopStyleVar"; cmake --build "K:\ToolsBOX\out" --config Debug --target payload
```

Expected: 每个文件各出现一组局部 `WindowPadding`/`PopStyleVar`，payload 构建成功。

---

### Task 8: Debug、Release、CTest 与默认 Release 注入器完整验证

**Files:**
- Review: `payload/timer_logic.h`
- Review: `payload/timer_logic.cpp`
- Review: `payload/timer_logic_tests.cpp`
- Review: `payload/settings_store.h`
- Review: `payload/settings_store.cpp`
- Review: `payload/inventory_bar.cpp`
- Review: `payload/smudge_timer.cpp`
- Review: `payload/game_time.cpp`
- Review: `payload/ghost_status.cpp`
- Review: `payload/crucifix_timer.cpp`
- Review: `payload/crosshair_info.cpp`

- [ ] **Step 1: 重新配置默认 `out` 构建目录**

Run:

```powershell
cmake -S "K:\ToolsBOX" -B "K:\ToolsBOX\out" -A x64
```

Expected: 配置与生成成功，测试目标 `timer_logic_tests` 和默认目标 `PhasmophobiaInjector` 均存在。

- [ ] **Step 2: Debug 全量构建**

Run:

```powershell
cmake --build "K:\ToolsBOX\out" --config Debug
```

Expected: 全部目标构建成功，包括 Debug payload、测试程序和注入器。

- [ ] **Step 3: Debug CTest**

Run:

```powershell
ctest --test-dir "K:\ToolsBOX\out" -C Debug --output-on-failure
```

Expected: 100% tests passed，`timer_logic_tests` 通过。

- [ ] **Step 4: Release 全量构建**

Run:

```powershell
cmake --build "K:\ToolsBOX\out" --config Release
```

Expected: 全部 Release 目标成功；自定义命令先生成内嵌 payload，再链接默认注入器。

- [ ] **Step 5: Release CTest**

Run:

```powershell
ctest --test-dir "K:\ToolsBOX\out" -C Release --output-on-failure
```

Expected: 100% tests passed，Release `timer_logic_tests` 通过。

- [ ] **Step 6: 精确验证最终默认产物**

Run:

```powershell
$artifact = "K:\ToolsBOX\out\bin\Release\PhasmophobiaInjector.exe"; if (-not (Test-Path $artifact -PathType Leaf)) { throw "Missing release injector: $artifact" }; Get-Item $artifact | Select-Object FullName,Length,LastWriteTime
```

Expected: 输出 `K:\ToolsBOX\out\bin\Release\PhasmophobiaInjector.exe`，文件长度大于 0，时间戳来自本次 Release 构建。

- [ ] **Step 7: 做最终静态审查**

逐项确认：

1. `InventoryKeyFontSize` 精确映射 50→16、96→34，物品栏通过 `font_store::IndexOf/Sizes/Get` 使用最近实际字号。
2. 缩放和横纵切换都调用 `CenterPositionForResize`，并且函数只在中心公式完成后调用 `ClampPosition`。
3. 重置保留 `enabled`，先恢复默认 50 与横向，再用 `DefaultBottomCenter` 和新尺寸定位到底部 20px。
4. `slotSize` 默认值、加载约束和 UI 范围全部为 50–96；`fontSize` 字段仍在原位置，版本仍为 6，但物品栏 UI/绘制不再读取它。
5. 五个透明文字窗口与物品栏局部使用零 `WindowPadding`；主菜单、屏幕准星和玩家昵称未改。
6. 每个槽为直角正方形，绘制顺序为背景、高光底色、贴图、键位、内框、外框，active 外框使用用户色和 2px 线宽。
7. 四边测试允许窗口位置精确等于 `0`、`viewport.width-window.width`、`viewport.height-window.height`，没有隐含边距。

- [ ] **Step 8: 记录游戏内验收结果**

启动本次生成的注入器后检查：连续拖动槽位大小 50→96→50 时中心不漂移；物品栏靠四边缩放后完整留在屏幕；横纵切换中心稳定；重置后为 50px 横向且底部居中、距底 20px；圣木计时器、游戏时间、鬼魂状态、十字架计时器、准星信息的可见文字可分别贴到四边；槽位为直角深色正方形，当前槽同时有轻微底色和用户色 2px 外框。

> 当前工作目录无 Git；本计划不包含提交步骤，实施结果保留在工作目录中。
