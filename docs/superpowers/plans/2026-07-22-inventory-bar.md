# 屏幕物品栏 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在现有 D3D11/ImGui Overlay 中加入默认关闭、可拖动并自动切换横纵方向的四槽屏幕物品栏，安全显示装备名称并高光当前手持槽位。

**Architecture:** `game_update_dispatcher` 继续独占 `PCPropGrab.Update` Hook，新增第三个函数指针回调把实例交给 `inventory_bar`；该模块只在游戏线程解析 IL2CPP 对象，使用互斥锁发布 `array<string,4> + activeIndex` 纯值快照，渲染线程只消费副本。布局方向、对象匹配和字号 fit 放入不依赖 ImGui 的 `feature_logic` 纯函数；设置存储精确从 v5 升至 v6并冻结旧结构以无损迁移。

**Tech Stack:** C++17、Windows x64、Unity IL2CPP、Dear ImGui、D3D11、CMake/CTest、MSVC

---

## 文件结构与边界

- Create: `payload/inventory_bar.h/.cpp` — 元数据解析、游戏线程采样、纯值快照、透明窗口绘制、位置/方向状态和设置 UI。
- Modify: `payload/feature_logic.h/.cpp` — 增加布局方向判定、四槽 `uintptr_t` 匹配、字号 fit 纯函数。
- Modify: `payload/timer_logic_tests.cpp` — 为新增纯逻辑和 v6 默认设置先写失败断言。
- Modify: `payload/game_update_dispatcher.h/.cpp` — 安全保存并调度 inventory 第三回调，不新增 Hook。
- Modify: `payload/settings_store.h/.cpp` — 新增 `InventoryBarSettings`，冻结 v5 DTO，精确 v5→v6 迁移、Normalize 与初始布局标志。
- Modify: `payload/overlay.cpp` — 接入生命周期、菜单 section、设置同步、绘制、Alt 可移动判断和第三回调。
- Modify: `payload/CMakeLists.txt` — 将 `inventory_bar.cpp` 加入 payload。

约束：不修改现有 `FormatEquipmentLabel` 映射；快照不包含 Unity 指针；游戏线程不调用 ImGui；列表读取验证 List `items +0x10`、`size +0x18`、Array 数据 `+0x20`，size 限制 `0..16` 且只消费前四项；解析失败仍绘制空槽框，不逐帧日志。

---

### Task 1: TDD 建立物品栏纯逻辑

**Files:**
- Modify: `payload/timer_logic_tests.cpp`
- Modify: `payload/feature_logic.h`
- Modify: `payload/feature_logic.cpp`

- [ ] **Step 1: 写失败测试**

在测试中加入布局、角落优先、中心保持/默认、对象指针匹配和字号缩小断言；先只声明以下接口，不提供定义：

```cpp
enum class InventoryLayout { Horizontal, Vertical };
InventoryLayout ResolveInventoryLayout(float left, float top, float right, float bottom,
                                       InventoryLayout previous, bool hasPrevious,
                                       float edgeThreshold = 24.0f);
int MatchInventorySlot(std::uintptr_t heldObject,
                       const std::array<std::uintptr_t, 4>& slots);
float FitInventoryFontSize(float configuredSize, float minimumSize,
                           float measuredWidth, float availableWidth);
```

测试覆盖：左右距离 `<=24` 为纵向，上下距离 `<=24` 为横向，角落横向优先，四边均远时保持先前方向且无先前时默认横向；用显式 `uintptr_t` 值匹配四槽，空手或不匹配返回 `-1`；fit 结果不超过 26、不低于最小可读值 12，短文本保持 26，长文本按宽度比例缩小。

- [ ] **Step 2: 构建确认 RED**

Run: `cmake --build "K:\ToolsBOX\out" --config Debug --target timer_logic_tests`
Expected: 链接失败，报告新增纯函数未定义。

- [ ] **Step 3: 写最小实现**

方向函数先判断 top/bottom，再判断 left/right；对象匹配拒绝零 held pointer，再逐项比较；字号函数清理非有限/非法输入，将 `configured * available / measured` 限制到 `[minimum, configured]`。

- [ ] **Step 4: 运行 GREEN**

Run: `cmake --build "K:\ToolsBOX\out" --config Debug --target timer_logic_tests; ctest --test-dir "K:\ToolsBOX\out" -C Debug -R "^timer_logic_tests$" --output-on-failure`
Expected: 构建成功且测试通过。

---

### Task 2: 实现安全采样与纯快照

**Files:**
- Create: `payload/inventory_bar.h`
- Create: `payload/inventory_bar.cpp`
- Modify: `payload/game_update_dispatcher.h`
- Modify: `payload/game_update_dispatcher.cpp`

- [ ] **Step 1: 扩展唯一 Hook 的 API**

新增 `InventoryCallback = void (*)(void*)` 和三参数 `Initialize`；在安装前保存三个回调，在 `InvokeCallbacks` 中顺序调用，在 Shutdown 持锁恢复入口后清空三个指针。不得创建第二个 `PCPropGrab.Update` Hook。

- [ ] **Step 2: 定义模块接口和快照**

公开 `Initialize/Shutdown/SyncSettings/ClearSnapshot/OnGameThreadUpdate/DrawWindow/DrawSettings/RequestResetPosition`。内部快照仅为：

```cpp
struct Snapshot {
    std::array<std::string, 4> labels{};
    int activeIndex = -1;
};
```

使用互斥锁整体发布和复制；disabled 或空实例立即发布空快照。

- [ ] **Step 3: 动态解析元数据**

解析 `PCPropGrab` 中唯一 `NetworkedPropGrab` 类型字段（当前 `0x48`）；尝试按 `List<PhotonObjectInteract>` 字段类型解析，若泛型类身份无法稳定取得则使用受控 `0x50` 并仅初始化时记录 fallback；解析 `NetworkedPropGrab.get_HeldObject()` 零参入口；复用 crosshair 路径所验证的 `PhotonObjectInteract→Prop`、`Component.GetComponentInParent(Equipment)`、`Equipment→StoreItem` 和 `StoreItem.equipmentType` 类型/字段查询方式。日志一次输出关键 offset 和方法入口。

- [ ] **Step 4: 安全读取四槽**

从 PCPropGrab 字段读取 List 指针，分别验证 items、size 和数组元素地址，size 仅接受 `0..16`，只复制前四个 `uintptr_t`；调用 `get_HeldObject(networked, methodInfo)` 后用 `MatchInventorySlot` 得 active index。每个非空槽沿 Prop component 调用 `GetComponentInParent(Equipment,false)`，读取 StoreItem 和 equipmentType，调用现有 `feature_logic::FormatEquipmentLabel(type, 0, false)`；任一环节失败则对应名称为空。异常 size 仅在状态变化时记录，不逐帧输出。

---

### Task 3: 绘制、方向和位置行为

**Files:**
- Modify: `payload/inventory_bar.cpp`

- [ ] **Step 1: 绘制透明单窗口**

使用 `NoBackground|NoDecoration|AlwaysAutoResize|NoSavedSettings|NoFocusOnAppearing|NoNav`；仅 `feature_logic::CanMoveOverlay(fixed, altDragActive)` 为真时允许移动/输入，否则追加 `NoMove|NoInputs`。窗口内以 DrawList 绘制四个 `slotSize × slotSize` 固定正方形和 4px gap：普通边框半透明白色 1px，active 使用 `highlightColor` 2px；键位位于左上角，空槽不画名称。

- [ ] **Step 2: 名称居中和动态字体**

渲染端复制快照后对每个名称调用 `font_store::RequestText(configuredFontSize, label)`；用所选 ImFont 的 `CalcTextSizeA` 测量，再调用纯函数按槽内可用宽度求 fit size，并用 `AddText(font, fittedSize, centeredPosition, ...)` 居中绘制。不得从游戏线程请求字体或调用 ImGui。

- [ ] **Step 3: 默认位置、方向切换和保存**

首次/重置时待窗口实际尺寸可知后设置到 `{(display.x-width)/2, display.y-height-20}`，Clamp 后保存并置 `positionInitialized=true`。有效保存位置只在首次帧用 crosshair 风格的状态变量应用，避免 `ImGuiCond_FirstUseEver` 覆盖。每帧根据窗口到四边距离调用 `ResolveInventoryLayout`；方向变化时以旧窗口中心计算新左上角并 Clamp，保存方向与新位置。拖动或 Clamp 后更新设置并 MarkDirty。

- [ ] **Step 4: 设置 UI**

加入启用、固定、槽位大小（24..96）、现有 font_store Combo、高光颜色和重置按钮。重置时保留 enabled，恢复 fixed=false、slotSize=36、fontSize=26、white，并请求默认位置。

---

### Task 4: 配置 v6 精确迁移

**Files:**
- Modify: `payload/settings_store.h`
- Modify: `payload/settings_store.cpp`
- Modify: `payload/timer_logic_tests.cpp`

- [ ] **Step 1: 定义新设置与冻结 v5**

新增 `InventoryBarSettings`，默认 `enabled=false`、`fixed=false`、`slotSize=36`、`fontSize=26`、`highlightColor={1,1,1,1}`、空位置、`positionInitialized=false`，并加入当前 `Settings`。在 cpp 定义与旧二进制布局完全一致的 `SettingsV5`（原 Settings 字段顺序）和对应存储头。

- [ ] **Step 2: 升级读写版本**

将当前版本设为 6；v6 严格校验 payloadLength=`sizeof(Settings)`；v5 严格校验 `sizeof(SettingsV5)` 并逐字段复制原有七个功能、菜单位置和初始化状态，inventory 保持默认关闭且位置待初始化。保留 v1-v4 分支原行为；保存写 v6 header。

- [ ] **Step 3: Normalize 与初始布局**

slotSize 对有限值 clamp 到 24..96，否则 36；fontSize 用现有 SnapFontSize；颜色用 ClampColor；position 用 ClampPosition。增加 `NeedsInitialInventoryLayout()`，新配置及 v1-v5 为 true，成功读取 v6 时依据持久化 positionInitialized 返回 false/相应状态。

- [ ] **Step 4: 补充默认/字形测试**

断言 `InventoryBarSettings{}` 的关闭、36、26、白色、未初始化默认；固定字形覆盖加入“物品栏、槽位大小、高光颜色”等 UI 文本，但不得改装备名称映射。

---

### Task 5: Overlay 和构建接入

**Files:**
- Modify: `payload/overlay.cpp`
- Modify: `payload/CMakeLists.txt`

- [ ] **Step 1: 生命周期与 dispatcher 接入**

Initialize 中初始化 inventory，并以第三参数传入 `OnGameThreadUpdate`；Shutdown 先停 dispatcher，再 shutdown inventory；Render 每帧 `SyncSettings`。模块顺序保证 dispatcher 运行期间 metadata/snapshot 存活。

- [ ] **Step 2: 菜单与输入接入**

MenuSection 新增“物品栏”，列表、标题和 DrawSettings 分支完整接入；`hasMovableFeature` 加入 enabled 且 !fixed 的 inventory；Draw 顺序放在屏幕准星/准星信息之后、昵称和计时窗口之前。

- [ ] **Step 3: 首次布局和 CMake**

`NeedsInitialInventoryLayout()` 时请求重置；新增 cpp 编入 payload target。确认 fixed UI glyph 字符串可覆盖新增设置文本。

---

### Task 6: 验证与审查

**Files:**
- Review: all modified files

- [ ] **Step 1: Debug 全量构建与 CTest**

Run: `cmake -S "K:\ToolsBOX" -B "K:\ToolsBOX\out" -A x64; cmake --build "K:\ToolsBOX\out" --config Debug; ctest --test-dir "K:\ToolsBOX\out" -C Debug --output-on-failure`
Expected: 所有目标和测试通过。

- [ ] **Step 2: Release 全量构建与 CTest**

Run: `cmake --build "K:\ToolsBOX\out" --config Release; ctest --test-dir "K:\ToolsBOX\out" -C Release --output-on-failure`
Expected: 所有目标和测试通过。

- [ ] **Step 3: 诊断与静态审查**

检查 IDE diagnostics，并审查：dispatcher 仅一个 Hook；回调读写生命周期无竞争；快照无 Unity ptr；窗口方向切换保持中心且 Clamp；有效位置不被首次条件覆盖；v5 所有字段/菜单位置均迁移；默认关闭/36/26/white；metadata/异常列表不逐帧刷日志。

- [ ] **Step 4: 修复后复验**

针对审查发现做最小修复并重跑受影响的 Debug/Release 构建与 CTest。记录仍需游戏内验证：拾取顺序、数字键切换、丢弃中间槽、空手、相机专槽排除、左右贴边纵向及角落横向优先。

> 本计划按任务要求禁止任何 commit；所有变更保留在工作树中。