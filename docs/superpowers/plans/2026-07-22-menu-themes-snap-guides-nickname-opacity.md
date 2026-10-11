# 菜单主题、四宫格吸附与昵称不透明 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 为主菜单增加可关闭且可由 U 键重开的独立设置与三种局部主题，为六个可移动功能窗口增加带二维优先、轴独立和 10/16px 滞回的四宫格/四边吸附及参考线，并彻底移除昵称遮挡透明逻辑。

**Architecture:** 在 `timer_logic` 中实现不依赖 ImGui 的 `SnapState`/`SnapResult` 纯逻辑，六个功能模块通过统一 `MovableWindowResult` 返回稳定 ID、实际 Bounds、可移动/实际拖动状态和位置提交回调，`overlay` 每帧只协调当前实际拖动窗口、保存滞回状态并在功能窗口之后绘制参考线。配置以精确旧布局 `SettingsV6` 迁移到 V7；功能浮层每帧恢复通用暗色，主菜单绘制期间临时应用主题并立即恢复，避免污染用户颜色与功能浮层。

**Tech Stack:** C++17、Dear ImGui（含 `imgui_internal.h` 仅用于准确识别 `MovingWindow`）、D3D11、Win32、CMake/CTest、MSVC x64

---

## 文件结构与边界

- Modify: `payload/timer_logic.h` — 定义纯逻辑吸附锚点、轴状态、结果和六窗口统一返回结构。
- Modify: `payload/timer_logic.cpp` — 实现中心/象限/四边候选、二维优先、轴独立、10/16px 滞回、禁用和最终 Clamp。
- Modify: `payload/timer_logic_tests.cpp` — TDD 覆盖所有吸附规则，并删除 `OcclusionAlpha` 旧断言、补充固定字形断言。
- Modify: `payload/settings_store.h` — 新增 `MenuTheme`、`MenuSettings`、V7 `Settings`，并保留字段顺序完全等于当前源码的 `SettingsV6`。
- Modify: `payload/settings_store.cpp` — 升级到 V7，提供可测试的规范化/V6 迁移纯函数，保持 V1–V5 迁移路径。
- Create: `payload/settings_store_tests.cpp` — 独立验证 V7 默认值、无效主题/布尔规范化和 V6 全字段迁移。
- Modify: `payload/CMakeLists.txt` — 注册配置测试，并让 CTest 默认覆盖吸附与配置测试。
- Modify: `payload/smudge_timer.h/.cpp`、`payload/game_time.h/.cpp`、`payload/ghost_status.h/.cpp`、`payload/crucifix_timer.h/.cpp`、`payload/crosshair_info.h/.cpp`、`payload/inventory_bar.h/.cpp` — 六个窗口统一返回实际拖动信息并实现位置提交回调。
- Modify: `payload/overlay.cpp` — 协调单一拖动窗口的吸附状态、参考线层级、菜单关闭/设置/主题以及每帧样式隔离。
- Modify: `payload/player_nicknames.cpp` — 删除 Physics、射线目标解析、遮挡层数及 alpha 映射，直接使用设置颜色 alpha。
- Modify: `payload/feature_logic.h/.cpp` — 删除仅供昵称遮挡使用的 `OcclusionAlpha`。
- Modify: `payload/font_store.cpp`、`payload/timer_logic.cpp` — 将菜单设置、参考线、主题颜色、暗色、亮色、深蓝加入固定字形文本。

不修改 `payload/screen_crosshair.cpp`、主菜单位置持久化规则、功能浮层的用户颜色/背景/高光或准星信息与物品栏的 `GetComponentInParent`。主菜单、屏幕准星和玩家昵称不进入 `MovableWindowResult` 集合。环境没有 Git，因此所有任务均不包含提交步骤。

---

### Task 1: 用失败测试锁定纯逻辑吸附契约

**Files:**
- Modify: `payload/timer_logic.h:20-37,38-77`
- Modify: `payload/timer_logic_tests.cpp:93-145`

- [ ] **Step 1: 在 `timer_logic.h` 的 `Bounds` 后声明吸附数据结构**

```cpp
enum class SnapAnchor {
    None,
    NearEdge,
    Quarter,
    Center,
    ThreeQuarter,
    FarEdge,
};

struct AxisSnapState {
    SnapAnchor anchor = SnapAnchor::None;
};

struct SnapState {
    AxisSnapState x;
    AxisSnapState y;
};

struct SnapResult {
    Position position{};
    SnapState state{};
    bool xSnapped = false;
    bool ySnapped = false;
    bool pointSnapped = false;
    Position point{};
};
```

`SnapState` 只保存锚点种类，不保存像素坐标，因此分辨率变化后释放距离仍基于当前 viewport 重新计算。

- [ ] **Step 2: 声明唯一纯逻辑入口和相等运算**

在 `ClampPosition` 声明后加入：

```cpp
bool operator==(const AxisSnapState& lhs, const AxisSnapState& rhs);
bool operator==(const SnapState& lhs, const SnapState& rhs);
SnapResult ComputeSnap(Bounds window, Size viewport, SnapState previous,
                       bool enabled, float enterDistance = 10.0f,
                       float releaseDistance = 16.0f);
```

- [ ] **Step 3: 添加四象限中心和屏幕中心的双轴断言**

在现有 `ClampPosition` 测试后加入；窗口为 `100x40`，所以目标中心转回左上角时分别减去 `(50,20)`：

```cpp
const Size snapViewport{800.0f, 600.0f};
const Size snapWindow{100.0f, 40.0f};
const SnapState noSnap{};
for (const Position expected : {
         Position{150.0f, 130.0f}, Position{550.0f, 130.0f},
         Position{150.0f, 430.0f}, Position{550.0f, 430.0f},
         Position{350.0f, 280.0f}}) {
    const auto result = ComputeSnap({{expected.x + 6.0f, expected.y - 8.0f}, snapWindow},
                                    snapViewport, noSnap, true);
    assert(result.position == expected);
    assert(result.xSnapped && result.ySnapped && result.pointSnapped);
}
```

- [ ] **Step 4: 添加二维候选优先和欧氏最小位移断言**

```cpp
const auto pointPriority = ComputeSnap({{344.0f, 274.0f}, snapWindow}, snapViewport, noSnap, true);
assert((pointPriority.position == Position{350.0f, 280.0f}));
assert(pointPriority.pointSnapped);
const auto nearestPoint = ComputeSnap({{347.0f, 278.0f}, snapWindow}, snapViewport, noSnap, true);
assert((nearestPoint.point == Position{400.0f, 300.0f}));
assert((nearestPoint.position == Position{350.0f, 280.0f}));
```

第一组同时存在单轴可用候选时仍必须采用屏幕中心二维候选；第二组明确检查报告的吸附点是窗口中心目标，而不是左上角。

- [ ] **Step 5: 添加四边和 X/Y 轴独立吸附断言**

```cpp
assert((ComputeSnap({{7.0f, 211.0f}, snapWindow}, snapViewport, noSnap, true).position ==
        Position{0.0f, 211.0f}));
assert((ComputeSnap({{694.0f, 211.0f}, snapWindow}, snapViewport, noSnap, true).position ==
        Position{700.0f, 211.0f}));
assert((ComputeSnap({{311.0f, 9.0f}, snapWindow}, snapViewport, noSnap, true).position ==
        Position{311.0f, 0.0f}));
assert((ComputeSnap({{311.0f, 557.0f}, snapWindow}, snapViewport, noSnap, true).position ==
        Position{311.0f, 560.0f}));
const auto xOnly = ComputeSnap({{145.0f, 211.0f}, snapWindow}, snapViewport, noSnap, true);
assert((xOnly.position == Position{150.0f, 211.0f}));
assert(xOnly.xSnapped && !xOnly.ySnapped && !xOnly.pointSnapped);
const auto yOnly = ComputeSnap({{311.0f, 125.0f}, snapWindow}, snapViewport, noSnap, true);
assert((yOnly.position == Position{311.0f, 130.0f}));
assert(!yOnly.xSnapped && yOnly.ySnapped && !yOnly.pointSnapped);
```

- [ ] **Step 6: 添加单轴最小绝对位移和 10px 包含边界断言**

```cpp
const auto minimumAxis = ComputeSnap({{341.0f, 211.0f}, snapWindow}, snapViewport, noSnap, true);
assert((minimumAxis.position == Position{350.0f, 211.0f}));
assert(minimumAxis.xSnapped && minimumAxis.state.x.anchor == SnapAnchor::Center);
assert((ComputeSnap({{140.0f, 211.0f}, snapWindow}, snapViewport, noSnap, true).position ==
        Position{150.0f, 211.0f}));
const auto outsideEnter = ComputeSnap({{139.0f, 211.0f}, snapWindow}, snapViewport, noSnap, true);
assert((outsideEnter.position == Position{139.0f, 211.0f}));
assert(!outsideEnter.xSnapped);
```

- [ ] **Step 7: 添加 16px 释放滞回和轴状态独立释放断言**

```cpp
SnapState latched{};
latched.x.anchor = SnapAnchor::Quarter;
latched.y.anchor = SnapAnchor::Center;
const auto retained = ComputeSnap({{165.0f, 295.0f}, snapWindow}, snapViewport, latched, true);
assert((retained.position == Position{150.0f, 280.0f}));
assert(retained.xSnapped && retained.ySnapped);
const auto releasedX = ComputeSnap({{167.0f, 295.0f}, snapWindow}, snapViewport, latched, true);
assert((releasedX.position == Position{167.0f, 280.0f}));
assert(!releasedX.xSnapped && releasedX.ySnapped);
const auto releaseBoundary = ComputeSnap({{166.0f, 280.0f}, snapWindow}, snapViewport, latched, true);
assert((releaseBoundary.position == Position{150.0f, 280.0f}));
assert(releaseBoundary.xSnapped);
```

- [ ] **Step 8: 添加禁用与最终 Clamp 断言**

```cpp
const auto disabled = ComputeSnap({{-5.0f, 570.0f}, snapWindow}, snapViewport, latched, false);
assert((disabled.position == Position{0.0f, 560.0f}));
assert((disabled.state == SnapState{}));
assert(!disabled.xSnapped && !disabled.ySnapped && !disabled.pointSnapped);
const auto clampedSnap = ComputeSnap({{706.0f, 566.0f}, snapWindow}, snapViewport, noSnap, true);
assert((clampedSnap.position == Position{700.0f, 560.0f}));
```

- [ ] **Step 9: 构建测试以确认新入口尚未实现**

Run:

```powershell
cmake --build out --config Debug --target timer_logic_tests
```

Expected: FAIL，链接器报告 `timer_logic::ComputeSnap` 和新增相等运算未定义。

---

### Task 2: 最小实现二维优先、轴独立与滞回

**Files:**
- Modify: `payload/timer_logic.cpp:23-58`

- [ ] **Step 1: 在匿名命名空间加入锚点坐标和轴位移纯辅助函数**

```cpp
float AnchorCoordinate(SnapAnchor anchor, float viewportExtent, float windowExtent) {
    switch (anchor) {
    case SnapAnchor::NearEdge: return 0.0f;
    case SnapAnchor::Quarter: return viewportExtent * 0.25f - windowExtent * 0.5f;
    case SnapAnchor::Center: return viewportExtent * 0.5f - windowExtent * 0.5f;
    case SnapAnchor::ThreeQuarter: return viewportExtent * 0.75f - windowExtent * 0.5f;
    case SnapAnchor::FarEdge: return viewportExtent - windowExtent;
    default: return 0.0f;
    }
}

struct AxisCandidate { SnapAnchor anchor; float position; float distance; };

AxisCandidate BestAxis(float position, float viewportExtent, float windowExtent, float enterDistance) {
    AxisCandidate best{SnapAnchor::None, position, enterDistance + 1.0f};
    for (const SnapAnchor anchor : {SnapAnchor::NearEdge, SnapAnchor::Quarter, SnapAnchor::Center,
                                    SnapAnchor::ThreeQuarter, SnapAnchor::FarEdge}) {
        const float target = AnchorCoordinate(anchor, viewportExtent, windowExtent);
        const float distance = std::fabs(target - position);
        if (distance <= enterDistance && distance < best.distance) best = {anchor, target, distance};
    }
    return best;
}
```

- [ ] **Step 2: 实现状态相等运算**

```cpp
bool operator==(const AxisSnapState& lhs, const AxisSnapState& rhs) { return lhs.anchor == rhs.anchor; }
bool operator==(const SnapState& lhs, const SnapState& rhs) { return lhs.x == rhs.x && lhs.y == rhs.y; }
```

- [ ] **Step 3: 实现禁用、已有轴滞回与五个二维候选优先**

```cpp
SnapResult ComputeSnap(Bounds window, Size viewport, SnapState previous,
                       bool enabled, float enterDistance, float releaseDistance) {
    SnapResult result{};
    result.position = window.position;
    if (!enabled) {
        result.position = ClampPosition(result.position, window.size, viewport);
        return result;
    }

    const auto retain = [&](AxisSnapState previousAxis, float position,
                            float viewportExtent, float windowExtent,
                            AxisSnapState& nextAxis, bool& snapped, float& output) {
        if (previousAxis.anchor == SnapAnchor::None) return;
        const float target = AnchorCoordinate(previousAxis.anchor, viewportExtent, windowExtent);
        if (std::fabs(position - target) <= releaseDistance) {
            nextAxis = previousAxis;
            snapped = true;
            output = target;
        }
    };
    retain(previous.x, window.position.x, viewport.width, window.size.width,
           result.state.x, result.xSnapped, result.position.x);
    retain(previous.y, window.position.y, viewport.height, window.size.height,
           result.state.y, result.ySnapped, result.position.y);

    struct PointCandidate { SnapAnchor x; SnapAnchor y; float distanceSquared; };
    PointCandidate best{SnapAnchor::None, SnapAnchor::None, 0.0f};
    if (!result.xSnapped && !result.ySnapped) {
        for (const auto point : {std::pair{SnapAnchor::Quarter, SnapAnchor::Quarter},
                                 std::pair{SnapAnchor::ThreeQuarter, SnapAnchor::Quarter},
                                 std::pair{SnapAnchor::Quarter, SnapAnchor::ThreeQuarter},
                                 std::pair{SnapAnchor::ThreeQuarter, SnapAnchor::ThreeQuarter},
                                 std::pair{SnapAnchor::Center, SnapAnchor::Center}}) {
            const float x = AnchorCoordinate(point.first, viewport.width, window.size.width);
            const float y = AnchorCoordinate(point.second, viewport.height, window.size.height);
            const float dx = x - window.position.x;
            const float dy = y - window.position.y;
            const float distanceSquared = dx * dx + dy * dy;
            if (std::fabs(dx) <= enterDistance && std::fabs(dy) <= enterDistance &&
                (best.x == SnapAnchor::None || distanceSquared < best.distanceSquared))
                best = {point.first, point.second, distanceSquared};
        }
    }
    if (best.x != SnapAnchor::None) {
        result.state = {{best.x}, {best.y}};
        result.xSnapped = result.ySnapped = result.pointSnapped = true;
        result.position.x = AnchorCoordinate(best.x, viewport.width, window.size.width);
        result.position.y = AnchorCoordinate(best.y, viewport.height, window.size.height);
        result.point = {result.position.x + window.size.width * 0.5f,
                        result.position.y + window.size.height * 0.5f};
    }
```

在 `timer_logic.cpp` 头部同时加入 `#include <utility>`，供 `std::pair` 使用。

- [ ] **Step 4: 在同一函数末尾实现未锁定轴的最小位移和最终 Clamp**

```cpp
    if (!result.xSnapped) {
        const auto candidate = BestAxis(window.position.x, viewport.width, window.size.width, enterDistance);
        if (candidate.anchor != SnapAnchor::None) {
            result.state.x.anchor = candidate.anchor;
            result.position.x = candidate.position;
            result.xSnapped = true;
        }
    }
    if (!result.ySnapped) {
        const auto candidate = BestAxis(window.position.y, viewport.height, window.size.height, enterDistance);
        if (candidate.anchor != SnapAnchor::None) {
            result.state.y.anchor = candidate.anchor;
            result.position.y = candidate.position;
            result.ySnapped = true;
        }
    }
    result.position = ClampPosition(result.position, window.size, viewport);
    return result;
}
```

- [ ] **Step 5: 运行纯逻辑测试**

Run:

```powershell
cmake --build out --config Debug --target timer_logic_tests
ctest --test-dir out -C Debug --output-on-failure -R timer_logic_tests
```

Expected: build PASS，CTest 报告 `timer_logic_tests` PASS。

---

### Task 3: 用独立测试锁定 V7 默认值、规范化和精确 V6 迁移

**Files:**
- Modify: `payload/settings_store.h:5-103`
- Create: `payload/settings_store_tests.cpp`
- Modify: `payload/CMakeLists.txt:101-111`

- [ ] **Step 1: 在 `settings_store.h` 增加菜单设置类型**

```cpp
enum class MenuTheme : int { Dark = 0, Light = 1, DeepBlue = 2 };

struct MenuSettings {
    bool snapGuides = true;
    MenuTheme theme = MenuTheme::DeepBlue;
};
```

- [ ] **Step 2: 将当前 V6 字段原样冻结为 `SettingsV6`，再定义 V7 `Settings`**

用以下完整声明替换当前 `Settings`；不得调整 `SettingsV6` 的顺序或字段类型：

```cpp
struct SettingsV6 {
    SmudgeSettings smudge;
    GameTimeSettings gameTime;
    GhostStatusSettings ghostStatus;
    CrucifixSettings crucifix;
    CrosshairInfoSettings crosshairInfo;
    ScreenCrosshairSettings screenCrosshair;
    PlayerNicknamesSettings playerNicknames;
    InventoryBarSettings inventoryBar;
    timer_logic::Position menuPosition{};
    bool menuPositionInitialized = false;
};

struct Settings {
    SmudgeSettings smudge;
    GameTimeSettings gameTime;
    GhostStatusSettings ghostStatus;
    CrucifixSettings crucifix;
    CrosshairInfoSettings crosshairInfo;
    ScreenCrosshairSettings screenCrosshair;
    PlayerNicknamesSettings playerNicknames;
    InventoryBarSettings inventoryBar;
    timer_logic::Position menuPosition{};
    bool menuPositionInitialized = false;
    MenuSettings menu;
};

Settings NormalizeSettings(Settings settings);
Settings MigrateSettingsV6(const SettingsV6& settings);
```

V7 只在旧布局末尾追加 `menu`；`SettingsV6` 精确镜像当前源码的 V6 payload。

- [ ] **Step 3: 新建配置纯逻辑测试，验证默认值和规范化**

创建 `payload/settings_store_tests.cpp`：

```cpp
#include "settings_store.h"

#include <cassert>
#include <cstddef>
#include <cstring>
#include <type_traits>

int main() {
    using namespace settings_store;
    static_assert(std::is_standard_layout_v<SettingsV6>);
    static_assert(offsetof(SettingsV6, smudge) == 0);
    static_assert(offsetof(SettingsV6, gameTime) > offsetof(SettingsV6, smudge));
    static_assert(offsetof(SettingsV6, ghostStatus) > offsetof(SettingsV6, gameTime));
    static_assert(offsetof(SettingsV6, crucifix) > offsetof(SettingsV6, ghostStatus));
    static_assert(offsetof(SettingsV6, crosshairInfo) > offsetof(SettingsV6, crucifix));
    static_assert(offsetof(SettingsV6, screenCrosshair) > offsetof(SettingsV6, crosshairInfo));
    static_assert(offsetof(SettingsV6, playerNicknames) > offsetof(SettingsV6, screenCrosshair));
    static_assert(offsetof(SettingsV6, inventoryBar) > offsetof(SettingsV6, playerNicknames));
    static_assert(offsetof(SettingsV6, menuPosition) > offsetof(SettingsV6, inventoryBar));
    static_assert(offsetof(SettingsV6, menuPositionInitialized) > offsetof(SettingsV6, menuPosition));
    const Settings defaults{};
    assert(defaults.menu.snapGuides);
    assert(defaults.menu.theme == MenuTheme::DeepBlue);

    Settings invalid{};
    invalid.menu.snapGuides = false;
    invalid.menu.theme = static_cast<MenuTheme>(99);
    const Settings normalized = NormalizeSettings(invalid);
    assert(!normalized.menu.snapGuides);
    assert(normalized.menu.theme == MenuTheme::DeepBlue);
    invalid.menu.snapGuides = true;
    assert(NormalizeSettings(invalid).menu.snapGuides);
```

该测试锁定合法 `false`/`true` 均被保留，并将越界主题恢复为深蓝；加载 V7 后实现仍通过 `settings.menu.snapGuides = settings.menu.snapGuides == true` 做显式布尔规范化。

- [ ] **Step 4: 在同一测试中逐字段填充 V6 并验证全部复制及菜单默认值**

```cpp
    SettingsV6 old{};
    old.smudge.enabled = false; old.smudge.position = {11.0f, 12.0f}; old.smudge.fontSize = 32.0f;
    old.gameTime.enabled = false; old.gameTime.position = {21.0f, 22.0f};
    old.ghostStatus.enabled = false; old.ghostStatus.flashing = false; old.ghostStatus.position = {31.0f, 32.0f};
    old.crucifix.enabled = false; old.crucifix.position = {41.0f, 42.0f};
    old.crosshairInfo.enabled = true; old.crosshairInfo.showTier = false; old.crosshairInfo.position = {51.0f, 52.0f};
    old.screenCrosshair.enabled = true; old.screenCrosshair.radius = 9;
    old.playerNicknames.enabled = true; old.playerNicknames.height = 33.0f;
    old.inventoryBar.enabled = true; old.inventoryBar.fixed = true; old.inventoryBar.position = {61.0f, 62.0f};
    old.inventoryBar.positionInitialized = true; old.inventoryBar.layout = 1;
    old.menuPosition = {71.0f, 72.0f}; old.menuPositionInitialized = true;

    const Settings migrated = MigrateSettingsV6(old);
    assert(std::memcmp(&migrated.smudge, &old.smudge, sizeof(old.smudge)) == 0);
    assert(std::memcmp(&migrated.gameTime, &old.gameTime, sizeof(old.gameTime)) == 0);
    assert(std::memcmp(&migrated.ghostStatus, &old.ghostStatus, sizeof(old.ghostStatus)) == 0);
    assert(std::memcmp(&migrated.crucifix, &old.crucifix, sizeof(old.crucifix)) == 0);
    assert(std::memcmp(&migrated.crosshairInfo, &old.crosshairInfo, sizeof(old.crosshairInfo)) == 0);
    assert(std::memcmp(&migrated.screenCrosshair, &old.screenCrosshair, sizeof(old.screenCrosshair)) == 0);
    assert(std::memcmp(&migrated.playerNicknames, &old.playerNicknames, sizeof(old.playerNicknames)) == 0);
    assert(std::memcmp(&migrated.inventoryBar, &old.inventoryBar, sizeof(old.inventoryBar)) == 0);
    assert(migrated.menuPosition == old.menuPosition && migrated.menuPositionInitialized);
    assert(migrated.menu.snapGuides);
    assert(migrated.menu.theme == MenuTheme::DeepBlue);
    return 0;
}
```

- [ ] **Step 5: 在 CMake 注册配置测试**

紧跟 `timer_logic_tests` 后加入：

```cmake
add_executable(settings_store_tests
    settings_store_tests.cpp
    settings_store.cpp
    timer_logic.cpp
    feature_logic.cpp
)
target_compile_options(settings_store_tests PRIVATE /utf-8)
target_link_libraries(settings_store_tests PRIVATE shell32)
add_test(NAME settings_store_tests COMMAND settings_store_tests)
```

- [ ] **Step 6: 重新配置并确认测试先失败**

Run:

```powershell
cmake -S . -B out
cmake --build out --config Debug --target settings_store_tests
```

Expected: FAIL，报告 `NormalizeSettings`/`MigrateSettingsV6` 未定义，且 `settings_store.cpp` 仍按 V6 结构编译。

---

### Task 4: 实现 V7 保存、规范化和 V1–V6 迁移

**Files:**
- Modify: `payload/settings_store.cpp:16-90,104-172,176-276`

- [ ] **Step 1: 升级版本常量并定义 V6 存储包装**

```cpp
constexpr std::uint32_t kVersion = 7;
constexpr std::uint32_t kVersion6 = 6;
constexpr std::uint32_t kVersion5 = 5;
```

将当前 `StoredSettingsV6` 替换为：

```cpp
struct StoredSettingsV7 {
    timer_logic::ConfigHeader header;
    Settings settings;
};
```

加载 V6 时直接读取公开的 `SettingsV6`，不再错误地用已经扩展的 `Settings` 推断旧 payload。

- [ ] **Step 2: 把现有 `Normalize()` 移出匿名命名空间并改为可测试的按值函数**

从当前匿名命名空间内删除 `Normalize()`，将完整函数放到匿名命名空间结束之后、公开 `Load()` 之前，签名改为：

```cpp
Settings NormalizeSettings(Settings settings) {
```

这样定义与 `settings_store.h` 的外部链接声明一致；`Save()` 仍留在匿名命名空间。函数体中所有 `g_settings.` 精确替换为 `settings.`，保留现有字体、颜色、位置、布局和半径归一化；在返回前追加：

```cpp
    settings.menu.snapGuides = settings.menu.snapGuides == true;
    const int theme = static_cast<int>(settings.menu.theme);
    if (theme < static_cast<int>(MenuTheme::Dark) || theme > static_cast<int>(MenuTheme::DeepBlue))
        settings.menu.theme = MenuSettings{}.theme;
    return settings;
}
```

- [ ] **Step 3: 实现精确 V6 到 V7 的全字段迁移**

在公开的 `NormalizeSettings` 后加入同样具有外部链接的定义：

```cpp
Settings MigrateSettingsV6(const SettingsV6& old) {
    Settings settings{};
    settings.smudge = old.smudge;
    settings.gameTime = old.gameTime;
    settings.ghostStatus = old.ghostStatus;
    settings.crucifix = old.crucifix;
    settings.crosshairInfo = old.crosshairInfo;
    settings.screenCrosshair = old.screenCrosshair;
    settings.playerNicknames = old.playerNicknames;
    settings.inventoryBar = old.inventoryBar;
    settings.menuPosition = old.menuPosition;
    settings.menuPositionInitialized = old.menuPositionInitialized;
    return settings;
}
```

默认构造确保菜单设置为“参考线开启 + 深蓝”。

- [ ] **Step 4: 将保存格式改为 V7**

```cpp
StoredSettingsV7 stored{{kMagic, kVersion, static_cast<std::uint32_t>(sizeof(Settings))}, g_settings};
```

- [ ] **Step 5: 在 `Load()` 的 V7 分支后增加精确 V6 分支**

```cpp
} else if (header.version == kVersion6 && header.payloadLength == sizeof(SettingsV6)) {
    SettingsV6 settings{};
    if (std::fread(&settings, sizeof(settings), 1, file) == 1 && std::fgetc(file) == EOF) {
        g_settings = MigrateSettingsV6(settings);
        g_needsInitialNewFeatureLayout = false;
        g_needsInitialCrosshairInfoLayout = false;
        g_needsInitialInventoryLayout = !settings.inventoryBar.positionInitialized;
        loadedFromDisk = true;
    }
```

- [ ] **Step 6: 保持 V1–V5 分支并统一使用菜单默认值**

保留当前 V1–V5 每个逐字段迁移分支不变；这些分支都从 `g_settings = {}` 开始且不写 `g_settings.menu`，因此自然得到同一 `MenuSettings{}` 默认值。不要用 V6 的 `SettingsV6` 覆盖这些旧分支各自已有的布局标志逻辑。

- [ ] **Step 7: 用按值规范化替换加载末尾调用**

```cpp
g_settings = NormalizeSettings(g_settings);
```

- [ ] **Step 8: 运行配置与原有测试**

Run:

```powershell
cmake --build out --config Debug --target settings_store_tests timer_logic_tests
ctest --test-dir out -C Debug --output-on-failure
```

Expected: 两个测试均 PASS；`settings_store_tests` 证明 V6 所有旧字段原样迁移，菜单设置使用默认值。

---

### Task 5: 统一六个可移动窗口的返回与位置提交接口

**Files:**
- Modify: `payload/timer_logic.h:20-77`
- Modify: `payload/smudge_timer.h/.cpp`
- Modify: `payload/game_time.h/.cpp`
- Modify: `payload/ghost_status.h/.cpp`
- Modify: `payload/crucifix_timer.h/.cpp`
- Modify: `payload/crosshair_info.h/.cpp`
- Modify: `payload/inventory_bar.h/.cpp`

- [ ] **Step 1: 在 `timer_logic.h` 定义统一窗口结果**

在 `SnapResult` 后加入：

```cpp
using PositionSubmitter = void (*)(Position);

struct MovableWindowResult {
    const char* id = nullptr;
    Bounds bounds{};
    bool movable = false;
    bool dragging = false;
    PositionSubmitter submitPosition = nullptr;
};
```

- [ ] **Step 2: 修改六个头文件的 `DrawWindow` 返回类型**

六个头文件统一声明：

```cpp
timer_logic::MovableWindowResult DrawWindow(bool menuVisible, bool altDragActive);
```

`crosshair_info.h` 和 `inventory_bar.h` 当前为 `void`，另外四个当前为 `timer_logic::Bounds`；全部改成相同签名，不增加主菜单、屏幕准星或昵称接口。

- [ ] **Step 3: 在六个 `.cpp` 引入准确拖动检测所需内部头**

每个文件在 `#include "imgui.h"` 后加入：

```cpp
#include "imgui_internal.h"
```

- [ ] **Step 4: 为圣木计时器增加提交回调并返回实际状态**

在匿名命名空间加入：

```cpp
constexpr const char* kWindowId = "##smudge_timer";
void SubmitPosition(timer_logic::Position position) {
    auto& settings = settings_store::Get().smudge;
    ImGui::SetWindowPos(kWindowId, {position.x, position.y}, ImGuiCond_Always);
    if (!(settings.position == position)) { settings.position = position; settings_store::MarkDirty(); }
}
```

将 `ImGui::Begin("##smudge_timer"` 改为 `ImGui::Begin(kWindowId`；`Begin` 后、`End` 前计算：

```cpp
const bool dragging = canMove && GImGui->MovingWindow == ImGui::GetCurrentWindow();
```

禁用早退改为 `return {};`，函数末尾改为：

```cpp
return {kWindowId, {clamped, {windowSize.x, windowSize.y}}, canMove, dragging, &SubmitPosition};
```

- [ ] **Step 5: 让游戏时间窗口返回可提交的拖动结果**

在匿名命名空间加入 `constexpr const char* kWindowId = "##game_time";` 和 `void SubmitPosition(timer_logic::Position position)`；回调调用 `ImGui::SetWindowPos(kWindowId, {position.x, position.y}, ImGuiCond_Always)`，将变更写入 `settings_store::Get().gameTime.position` 并调用 `settings_store::MarkDirty()`。`DrawPositionedWindow` 与公开 `DrawWindow` 都返回 `timer_logic::MovableWindowResult`，调用 `ImGui::Begin(kWindowId, ...)`，并在当前窗口上下文内用 `canMove && GImGui->MovingWindow == ImGui::GetCurrentWindow()` 计算 `dragging`。末尾返回：

```cpp
return {kWindowId, {clamped, {windowSize.x, windowSize.y}}, canMove, dragging, &SubmitPosition};
```

- [ ] **Step 6: 对鬼魂状态窗口应用相同契约**

使用稳定 ID `"##ghost_status"`、写入 `settings_store::Get().ghostStatus.position` 的 `SubmitPosition`，并返回：

```cpp
return {kWindowId, {clamped, {size.x, size.y}}, canMove, dragging, &SubmitPosition};
```

- [ ] **Step 7: 对十字架计时器窗口应用相同契约**

使用稳定 ID `"##crucifix_timer"`、写入 `settings_store::Get().crucifix.position` 的 `SubmitPosition`，并返回：

```cpp
return {kWindowId, {clamped, {size.x, size.y}}, canMove, dragging, &SubmitPosition};
```

- [ ] **Step 8: 对准星信息窗口应用相同契约**

使用稳定 ID `"##crosshair_info"`、写入 `settings_store::Get().crosshairInfo.position` 的 `SubmitPosition`。保留“无新鲜目标但可移动时显示占位文本”的现有规则；只有真正早退时返回 `{}`。窗口末尾返回：

```cpp
return {kWindowId, {clamped, {size.x, size.y}}, canMove, dragging, &SubmitPosition};
```

- [ ] **Step 9: 对物品栏窗口应用相同契约**

使用稳定 ID `"##inventory_bar"`、写入 `settings_store::Get().inventoryBar.position` 且保持 `positionInitialized = true` 的 `SubmitPosition`。实际 Bounds 使用最终 `LayoutSize`，末尾返回：

```cpp
return {kWindowId, {clamped, {finalSize.x, finalSize.y}}, canMove, dragging, &SubmitPosition};
```

- [ ] **Step 10: 构建 payload，确认六个声明与定义完全一致**

Run:

```powershell
cmake --build out --config Debug --target payload
```

Expected: PASS；不存在 `DrawWindow` 返回类型、`SubmitPosition` 或 ImGui 内部类型签名错误。

---

### Task 6: 在 Overlay 协调实际拖动、吸附提交和参考线层级

**Files:**
- Modify: `payload/overlay.cpp:28-43,95-153,251-293`

- [ ] **Step 1: 增加单一拖动状态和菜单 Bounds 状态**

在 Overlay 匿名命名空间全局状态中加入：

```cpp
const char* g_draggingWindowId = nullptr;
timer_logic::SnapState g_snapState{};
timer_logic::Bounds g_menuBounds{};
```

- [ ] **Step 2: 增加窗口结果选择和吸附提交函数**

```cpp
const timer_logic::MovableWindowResult* FindDraggedWindow(
    const std::array<timer_logic::MovableWindowResult, 6>& windows) {
    for (const auto& window : windows)
        if (window.id && window.movable && window.dragging && window.submitPosition) return &window;
    return nullptr;
}

timer_logic::SnapResult UpdateSnap(
    const std::array<timer_logic::MovableWindowResult, 6>& windows,
    timer_logic::Size viewport, bool enabled) {
    const auto* dragged = FindDraggedWindow(windows);
    if (!enabled || !dragged) {
        g_draggingWindowId = nullptr;
        g_snapState = {};
        return {};
    }
    if (!g_draggingWindowId || std::strcmp(g_draggingWindowId, dragged->id) != 0) g_snapState = {};
    g_draggingWindowId = dragged->id;
    auto result = timer_logic::ComputeSnap(dragged->bounds, viewport, g_snapState, true);
    g_snapState = result.state;
    dragged->submitPosition(result.position);
    return result;
}
```

在头部增加 `#include <array>` 和 `#include <cstring>`。

- [ ] **Step 3: 实现固定青蓝参考线绘制**

```cpp
void DrawSnapGuides(const timer_logic::SnapResult& snap, timer_logic::Size viewport, bool active) {
    if (!active) return;
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    constexpr ImU32 normal = IM_COL32(38, 198, 218, 72);
    constexpr ImU32 strong = IM_COL32(38, 198, 218, 210);
    const float centerX = viewport.width * 0.5f;
    const float centerY = viewport.height * 0.5f;
    draw->AddLine({centerX, 0.0f}, {centerX, viewport.height}, snap.xSnapped ? strong : normal,
                  snap.xSnapped ? 2.0f : 1.0f);
    draw->AddLine({0.0f, centerY}, {viewport.width, centerY}, snap.ySnapped ? strong : normal,
                  snap.ySnapped ? 2.0f : 1.0f);
    if (snap.pointSnapped)
        draw->AddCircleFilled({snap.point.x, snap.point.y}, 5.0f, strong);
}
```

颜色不读取 `MenuTheme`，因此不会随主题变化。

- [ ] **Step 4: 让菜单函数返回实际 Bounds**

将 `DrawMenuWindow()` 改为 `timer_logic::Bounds DrawMenuWindow()`；隐藏时 `return {};`，在 `ImGui::End()` 前保存实际 size/position，函数末尾：

```cpp
g_menuBounds = {clamped, {actualSize.x, actualSize.y}};
return g_menuBounds;
```

该 Bounds 只用于验证/维护菜单在参考线之上的交互层，不参与吸附。

- [ ] **Step 5: 每帧收集六个窗口结果并仅对实际拖动窗口吸附**

把当前六次 `DrawWindow` 调用替换为：

```cpp
const std::array<timer_logic::MovableWindowResult, 6> movableWindows{
    smudge_timer::DrawWindow(g_menuVisible, altDragActive),
    game_time::DrawWindow(g_menuVisible, altDragActive),
    ghost_status::DrawWindow(g_menuVisible, altDragActive),
    crucifix_timer::DrawWindow(g_menuVisible, altDragActive),
    crosshair_info::DrawWindow(g_menuVisible, altDragActive),
    inventory_bar::DrawWindow(g_menuVisible, altDragActive),
};
const timer_logic::Size viewport{ImGui::GetIO().DisplaySize.x, ImGui::GetIO().DisplaySize.y};
const bool guidesEnabled = settings.menu.snapGuides;
const auto snap = UpdateSnap(movableWindows, viewport, guidesEnabled);
const bool actualDragActive = guidesEnabled && FindDraggedWindow(movableWindows) != nullptr;
DrawSnapGuides(snap, viewport, actualDragActive);
g_menuBounds = DrawMenuWindow();
```

保留 `screen_crosshair::Draw()`、`player_nicknames::Draw()` 和 `ghost_status::Update()`；只调整六个功能窗口的收集位置。参考线调用位于全部功能浮层之后、`DrawMenuWindow()` 之前，菜单继续最后创建并接收输入；若 ImGui 前景列表在后端排序上覆盖普通窗口，则在下一步采用裁剪保证功能上菜单下。

- [ ] **Step 6: 对菜单矩形裁掉前景参考线，确保功能上菜单下**

给 `DrawSnapGuides` 增加 `timer_logic::Bounds menuBounds, bool menuVisible` 参数；用四个互不重叠 clip rect（菜单上、下、左、右）分别调用绘线逻辑，菜单隐藏时只绘一次全屏。抽出局部 lambda：

```cpp
const auto drawLines = [&] {
    draw->AddLine({centerX, 0.0f}, {centerX, viewport.height}, snap.xSnapped ? strong : normal,
                  snap.xSnapped ? 2.0f : 1.0f);
    draw->AddLine({0.0f, centerY}, {viewport.width, centerY}, snap.ySnapped ? strong : normal,
                  snap.ySnapped ? 2.0f : 1.0f);
    if (snap.pointSnapped) draw->AddCircleFilled({snap.point.x, snap.point.y}, 5.0f, strong);
};
```

菜单可见且 `g_menuBounds.size` 有效时依次 `PushClipRect`/`drawLines`/`PopClipRect` 绘制菜单矩形外四区；这样仍使用前景绘制列表、覆盖功能浮层，但菜单区域没有参考线，功能上等价位于菜单下。调用使用上一帧稳定的 `g_menuBounds`，随后 `DrawMenuWindow` 更新下一帧 Bounds。

- [ ] **Step 7: 构建并运行纯逻辑回归**

Run:

```powershell
cmake --build out --config Debug --target payload timer_logic_tests
ctest --test-dir out -C Debug --output-on-failure -R timer_logic_tests
```

Expected: payload build PASS，吸附纯逻辑测试 PASS。

---

### Task 7: 增加菜单首项、X 关闭与三种局部主题

**Files:**
- Modify: `payload/overlay.cpp:31-153,161-189,251-293`
- Modify: `payload/font_store.cpp:19-22`
- Modify: `payload/timer_logic.cpp:12-21`
- Modify: `payload/timer_logic_tests.cpp:243-256`

- [ ] **Step 1: 将菜单设置设为枚举首项和首次默认项**

```cpp
enum class MenuSection {
    MenuSettings,
    SmudgeTimer,
    GameTime,
    GhostStatus,
    CrucifixTimer,
    CrosshairInfo,
    InventoryBar,
    ScreenCrosshair,
    PlayerNicknames,
};
MenuSection g_selectedSection = MenuSection::MenuSettings;
```

在 `DrawSectionList()` 第一个 selectable 加入：

```cpp
if (ImGui::Selectable(u8"菜单设置", g_selectedSection == MenuSection::MenuSettings, 0, {0, 26}))
    g_selectedSection = MenuSection::MenuSettings;
ImGui::Separator();
```

- [ ] **Step 2: 让 Overlay 可复用现有设置行布局**

在 `overlay.cpp` 增加：

```cpp
#include "settings_ui.h"
```

`settings_ui.h` 本身无需改动；继续复用现有 `settings_ui::LabelControlRow(const char*)`，不新建菜单专用布局抽象。

- [ ] **Step 3: 增加菜单设置 UI 并即时标脏**

```cpp
void DrawMenuSettings() {
    auto& menu = settings_store::Get().menu;
    bool changed = false;
    settings_ui::LabelControlRow(u8"参考线:");
    changed |= ImGui::Checkbox("##menu_snap_guides", &menu.snapGuides);
    int theme = static_cast<int>(menu.theme);
    constexpr const char* themes[] = {u8"暗色", u8"亮色", u8"深蓝"};
    settings_ui::LabelControlRow(u8"主题颜色:");
    ImGui::SetNextItemWidth(120.0f);
    if (ImGui::Combo("##menu_theme", &theme, themes, 3)) {
        menu.theme = static_cast<settings_store::MenuTheme>(theme);
        changed = true;
    }
    if (changed) settings_store::MarkDirty();
}
```

`sectionTitle` 默认改为 `u8"菜单设置"`，并在设置内容 dispatch 的第一分支调用 `DrawMenuSettings()`。

- [ ] **Step 4: 让标题栏 X 只隐藏菜单**

将：

```cpp
ImGui::Begin(u8"抓鬼工具箱(U键 显示/隐藏 | 按住Alt拖动功能位置)", nullptr,
             ImGuiWindowFlags_NoCollapse)
```

改为：

```cpp
ImGui::Begin(u8"抓鬼工具箱(U键 显示/隐藏 | 按住Alt拖动功能位置)", &g_menuVisible,
             ImGuiWindowFlags_NoCollapse)
```

不调用任何模块的 `Shutdown()`，不保存 `g_menuVisible`。现有 `UpdateMenuToggle()` 保持不变，因此 X 后按 U 会把 `false` 切回 `true`，进程启动仍由 `bool g_menuVisible = true` 保证默认显示。

- [ ] **Step 5: 抽出通用暗色几何样式**

```cpp
void ApplyCommonDarkStyle() {
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 12.0f;
    style.FrameRounding = 7.0f;
    style.ChildRounding = 10.0f;
    style.WindowPadding = {14.0f, 14.0f};
    style.ItemSpacing = {10.0f, 10.0f};
}
```

`InitializeImpl()` 用 `ApplyCommonDarkStyle()` 替换当前内联样式初始化；每次 `ImGui::NewFrame()` 后也调用一次，确保所有功能浮层先恢复通用暗色基础。

- [ ] **Step 6: 实现三种菜单主题，不触碰功能设置颜色**

```cpp
void ApplyMenuTheme(settings_store::MenuTheme theme) {
    ImGuiStyle& style = ImGui::GetStyle();
    if (theme == settings_store::MenuTheme::Light) ImGui::StyleColorsLight();
    else ImGui::StyleColorsDark();
    style.WindowRounding = 12.0f;
    style.FrameRounding = 7.0f;
    style.ChildRounding = 10.0f;
    style.WindowPadding = {14.0f, 14.0f};
    style.ItemSpacing = {10.0f, 10.0f};
    if (theme == settings_store::MenuTheme::DeepBlue) {
        style.Colors[ImGuiCol_WindowBg] = {0.035f, 0.075f, 0.13f, 0.98f};
        style.Colors[ImGuiCol_ChildBg] = {0.045f, 0.09f, 0.15f, 1.0f};
        style.Colors[ImGuiCol_Header] = {0.05f, 0.32f, 0.58f, 0.75f};
        style.Colors[ImGuiCol_HeaderHovered] = {0.08f, 0.43f, 0.75f, 0.9f};
        style.Colors[ImGuiCol_HeaderActive] = {0.08f, 0.5f, 0.85f, 1.0f};
        style.Colors[ImGuiCol_CheckMark] = {0.2f, 0.72f, 1.0f, 1.0f};
        style.Colors[ImGuiCol_FrameBgActive] = {0.05f, 0.38f, 0.68f, 1.0f};
        style.Colors[ImGuiCol_Button] = {0.05f, 0.28f, 0.5f, 0.8f};
        style.Colors[ImGuiCol_ButtonHovered] = {0.08f, 0.42f, 0.72f, 1.0f};
    }
}
```

- [ ] **Step 7: 只在菜单绘制调用期间应用并恢复主题**

在 `DrawMenuWindow()` 可见性早退后加入：

```cpp
const ImGuiStyle previousStyle = ImGui::GetStyle();
ApplyMenuTheme(settings_store::Get().menu.theme);
```

函数退出前、返回 Bounds 前加入：

```cpp
ImGui::GetStyle() = previousStyle;
```

设置 Combo 改值发生在当前菜单帧中；下一帧立刻使用新主题。功能窗口已在菜单之前按通用暗色绘制，且保存/恢复阻止主题泄漏到后续帧。

- [ ] **Step 8: 补充固定字形文本和对应测试**

将 `font_store.cpp` 和 `timer_logic.cpp` 的固定 UI 文本补入：

```cpp
u8"菜单设置参考线主题颜色暗色亮色深蓝"
```

将 `timer_logic_tests.cpp` 的固定字形验证字符串同步补入相同文本，确保首次打开菜单无需等待动态字体 atlas 重建。

- [ ] **Step 9: 构建并运行字形回归**

Run:

```powershell
cmake --build out --config Debug --target timer_logic_tests payload
ctest --test-dir out -C Debug --output-on-failure -R timer_logic_tests
```

Expected: build PASS，固定字形断言 PASS。

---

### Task 8: 删除昵称 Physics、遮挡层数和 `OcclusionAlpha`

**Files:**
- Modify: `payload/player_nicknames.cpp:20-121,129-207`
- Modify: `payload/feature_logic.h:21-29`
- Modify: `payload/feature_logic.cpp:35-53`
- Modify: `payload/timer_logic_tests.cpp:31-38`

- [ ] **Step 1: 先删除 `OcclusionAlpha` 测试并确认其余测试仍可表达目标**

从 `timer_logic_tests.cpp` 删除：

```cpp
assert(OcclusionAlpha(0) == 1.0f);
assert(OcclusionAlpha(1) == 0.55f);
assert(OcclusionAlpha(2) == 0.25f);
```

保留 `NicknameFontSize`、`NicknameAnchorY`、远近字号和投影归一化测试。

- [ ] **Step 2: 删除 feature_logic 的映射声明和实现**

从 `feature_logic.h` 删除：

```cpp
float OcclusionAlpha(int count);
```

从 `feature_logic.cpp` 删除完整 `OcclusionAlpha` 函数。

- [ ] **Step 3: 精简昵称条目和 API 类型**

将：

```cpp
struct Entry { std::int32_t actor; std::string name; float x, y, distance, alpha; };
```

改为：

```cpp
struct Entry { std::int32_t actor; std::string name; float x, y, distance; };
```

删除 `Ray`、`RaycastHit`、对应 `static_assert`、`GetPhysicsSceneFn`、`RaycastFn`、`HitColliderFn`、`GetComponentInParentFn`，并从 `Api` 删除 `getPhysicsScene/sceneMethod`、`raycast/raycastMethod`、`hitCollider/colliderMethod`、`getInParent/parentMethod`、`playerType`。

- [ ] **Step 4: 删除 Physics API 解析和整个遮挡函数**

从 `ResolveApi()` 删除对以下 Unity API/类型的解析：

```cpp
Physics.get_defaultPhysicsScene_Injected
PhysicsScene.Internal_Raycast_Injected
RaycastHit.get_collider
Component.GetComponentInParent
Player type object
```

完整删除 `Occlusions(...)`。不要修改 `crosshair_info.cpp` 或 `inventory_bar.cpp` 中独立使用的 `GetComponentInParent`。

- [ ] **Step 5: 删除仅为射线目标识别寻找本地 GamePlayer 的循环**

`OnGameThreadUpdate()` 仍从 `network + 0x30` 读取 local player 以取得 `camera`，但删除 `localGamePlayer` 变量和遍历 `spots` 查找本地 `spot + 0x38` 的整段循环。保留后续远端 `isLocal`、死亡指针、昵称、头部定位、投影和距离过滤。

- [ ] **Step 6: 直接发布无遮挡 alpha 的昵称条目**

将：

```cpp
const int occlusions = Occlusions(cameraPosition, head, player, localGamePlayer);
entries.push_back({actor, name, normalized.x, normalized.y, distance,
                   feature_logic::OcclusionAlpha(occlusions)});
```

改为：

```cpp
entries.push_back({actor, name, normalized.x, normalized.y, distance});
```

- [ ] **Step 7: 绘制时直接使用设置颜色 alpha**

将颜色构造改为：

```cpp
{settings.color.r, settings.color.g, settings.color.b, settings.color.a}
```

不再乘 `entry.alpha`。

- [ ] **Step 8: 构建并静态确认昵称模块无 Physics/Raycast 残留**

Run:

```powershell
cmake --build out --config Debug --target payload timer_logic_tests
ctest --test-dir out -C Debug --output-on-failure -R timer_logic_tests
```

Expected: build/test PASS。

再搜索 `payload/player_nicknames.cpp`，Expected: 以下模式均为 0 个匹配：

```text
Physics
Raycast
RaycastHit
Collider
Occlusion
GetComponentInParent
playerType
localGamePlayer
```

并搜索 `payload/crosshair_info.cpp` 与 `payload/inventory_bar.cpp`，Expected: 两者仍各自包含 `GetComponentInParent`，证明未误删其他功能依赖。

---

### Task 9: Debug、Release、默认 CTest 与最终产物验证

**Files:**
- Verify only: `payload/CMakeLists.txt`
- Verify only: `out/bin/Release/PhasmophobiaInjector.exe`

- [ ] **Step 1: 从仓库根目录重新生成默认 `out` 构建树**

Run:

```powershell
cmake -S . -B out
```

Expected: 配置成功，CTest 发现 `timer_logic_tests` 和 `settings_store_tests`；不使用旧 `build` 目录。

- [ ] **Step 2: 完整 Debug 构建**

Run:

```powershell
cmake --build out --config Debug
```

Expected: PASS，`payload`、注入器及两个测试目标全部构建成功。

- [ ] **Step 3: 运行 Debug CTest**

Run:

```powershell
ctest --test-dir out -C Debug --output-on-failure
```

Expected: 100% tests passed，至少包含 `timer_logic_tests` 和 `settings_store_tests`。

- [ ] **Step 4: 完整 Release 构建**

Run:

```powershell
cmake --build out --config Release
```

Expected: PASS。

- [ ] **Step 5: 运行 Release CTest**

Run:

```powershell
ctest --test-dir out -C Release --output-on-failure
```

Expected: 100% tests passed。

- [ ] **Step 6: 确认最终注入器产物**

Run:

```powershell
Test-Path "K:\ToolsBOX\out\bin\Release\PhasmophobiaInjector.exe"
```

Expected: `True`。

- [ ] **Step 7: 做最终源码一致性检查**

确认以下事实：

```text
SettingsV6 字段顺序与改动前 Settings 完全一致；kVersion 为 7，且存在独立 kVersion6 分支。
六个且仅六个功能模块返回 MovableWindowResult；主菜单、screen_crosshair、player_nicknames 不返回该结构。
六个 DrawWindow 声明与定义都为 timer_logic::MovableWindowResult DrawWindow(bool, bool)。
只有 GImGui->MovingWindow == ImGui::GetCurrentWindow() 时 dragging 为 true。
参考线关闭、Alt 松开、鼠标松开或没有实际拖动窗口时 UpdateSnap 清空状态且不提交位置。
ComputeSnap 的 enterDistance/releaseDistance 默认值分别为 10.0f/16.0f，返回前调用 ClampPosition。
菜单 `ImGui::Begin` 使用 `&g_menuVisible`；g_menuVisible 默认 true，UpdateMenuToggle 仍由 U 键切换。
主题样式只包围 DrawMenuWindow，功能浮层之前每帧 ApplyCommonDarkStyle。
昵称绘制 alpha 直接等于 settings.color.a，player_nicknames.cpp 无 Physics/Raycast/Occlusion 残留。
```

- [ ] **Step 8: 记录必须游戏内人工确认但不要执行注入**

仅记录后续人工验收项，不运行注入器：菜单 X 只隐藏且 U 可重开；暗色/亮色/深蓝即时切换且不改变功能浮层颜色/背景/高光；六个未固定窗口只有按 Alt 实际左键拖动时显示参考线并吸附；二维点优先、四边和单轴吸附、10/16px 滞回手感正确；菜单覆盖参考线；关闭参考线后绘制和吸附同时停止；昵称隔墙时仍保持设置 alpha。屏幕准星、主菜单和玩家昵称不得参与吸附。
