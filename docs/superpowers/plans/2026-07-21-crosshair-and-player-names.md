# 准星信息、屏幕准星与玩家昵称 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在现有 D3D11/ImGui Overlay 中加入彼此独立的准星目标信息、纯屏幕准星和远端存活玩家昵称，并将设置格式从 v4 无损迁移到 v5。

**Architecture:** `game_update_dispatcher` 独占 `PCPropGrab.Update` 的游戏线程 Hook，在原方法返回后调用两个无捕获回调；`crosshair_info` 与 `player_nicknames` 仅在游戏线程解析 Unity/IL2CPP 对象并发布纯值快照，渲染线程不保存或访问游戏对象指针。`screen_crosshair` 完全是 ImGui 前景绘制；三个功能模块互不包含，统一由 `overlay` 进行生命周期、菜单和绘制装配。

**Tech Stack:** C++17、Windows x64、Unity IL2CPP、Dear ImGui、D3D11、CMake/CTest、MSVC

---

## 文件结构与边界

- Create: `payload/feature_logic.h/.cpp` — 三个功能共享但不含平台依赖的纯计算与装备文本格式化。
- Create: `payload/game_update_dispatcher.h/.cpp` — 唯一的 `PCPropGrab.Update` Hook、回调调度与 Shutdown 恢复。
- Create: `payload/crosshair_info.h/.cpp` — 目标采样、纯值快照、透明可移动窗口和专属设置。
- Create: `payload/screen_crosshair.h/.cpp` — 屏幕中心三种准星前景绘制和专属设置。
- Create: `payload/player_nicknames.h/.cpp` — 玩家采样、投影、最多两层遮挡、纯值快照、前景昵称和专属设置。
- Modify: `payload/il2cpp_runtime.h/.cpp` — 增加按 owner/target 查询字段 offset、类型对象、运行时调用所需的通用导出包装和安全 List/Array 读取。
- Modify: `payload/settings_store.h/.cpp` — 新增三个 DTO、精准冻结 v4 布局、v1-v4 到 v5 加载和 Normalize。
- Modify: `payload/timer_logic.cpp` — 固定字形加入菜单、标签、装备名和交互物文本。
- Modify: `payload/timer_logic_tests.cpp` — 先 RED 后 GREEN 的纯函数测试及固定字形覆盖。
- Modify: `payload/overlay.cpp` — 顶层模块装配、菜单三项、绘制顺序、Alt 拖动和 Shutdown 顺序。
- Modify: `payload/CMakeLists.txt` — 编译新模块并让纯逻辑进入测试目标。

依赖约束：`crosshair_info`、`screen_crosshair`、`player_nicknames` 不互相 include；dispatcher 不包含功能头，只接收两个函数指针；所有 Unity API 只从 dispatcher 的游戏线程回调调用；跨线程仅传递字符串、数值和小型结构体快照。

---

### Task 1: TDD 建立纯功能逻辑

**Files:**
- Create: `payload/feature_logic.h`
- Create: `payload/feature_logic.cpp`
- Modify: `payload/timer_logic_tests.cpp`
- Modify: `payload/CMakeLists.txt`

- [ ] **Step 1: 写失败测试**

在 `timer_logic_tests.cpp` 加入以下断言；函数声明先放入 `feature_logic.h`，实现文件暂为空，使链接阶段因未定义符号失败：

```cpp
assert(ClampCrosshairRadius(-1) == 1);
assert(ClampCrosshairRadius(2) == 2);
assert(ClampCrosshairRadius(31) == 30);
assert(NicknameFontSize(1.0f, 26.0f) == 26.0f);
assert(NicknameFontSize(20.0f, 26.0f) == 14.0f);
assert(NicknameFontSize(30.0f, 26.0f) == 14.0f);
assert(NicknameHeight(20.0f, 14.0f, 26.0f) == 20.0f * 14.0f / 26.0f);
assert(OcclusionAlpha(0) == 1.0f);
assert(OcclusionAlpha(1) == 0.55f);
assert(OcclusionAlpha(2) == 0.25f);
assert((NormalizeScreenPoint(960, 540, 1920, 1080) == NormalizedPoint{0.5f, 0.5f}));
assert(FormatEquipmentLabel(0, 1, true) == u8"十字架 · 一级");
assert(FormatEquipmentLabel(20, 3, false) == u8"摄像机");
assert(FormatEquipmentLabel(21, 1, true).empty());
```

- [ ] **Step 2: 配置并验证 RED**

```powershell
cmake -S "K:\ToolsBOX" -B "K:\ToolsBOX\out" -A x64
cmake --build "K:\ToolsBOX\out" --config Debug --target timer_logic_tests
```

预期：链接失败，缺少上述纯函数定义；保留输出作为 RED 证据。

- [ ] **Step 3: 实现最小纯逻辑**

实现规则：半径 clamp 到 1..30；昵称字号在 `<=2m` 为 nearSize，`2..20m` 线性插值到 `nearSize*14/26`（最低 12），`>=20m` 固定远字号；高度按当前字号/nearSize 同比；遮挡 alpha 为 1/0.55/0.25；屏幕点保存 `{x/width, 1-y/height}`，非法尺寸返回无效点；装备枚举 0..20 映射为十字架、激光投影仪、电磁场读取器、火光、手电筒、幽灵笔记、头戴设备、点火器、熏香、运动传感器、抛物线麦克风、照相机、盐、理智药物、录音机、声音传感器、通灵盒、温度计、三脚架、紫外线灯、摄像机，tier 1..3 映射一级至三级。

- [ ] **Step 4: 验证 GREEN**

```powershell
cmake --build "K:\ToolsBOX\out" --config Debug --target timer_logic_tests
ctest --test-dir "K:\ToolsBOX\out" -C Debug -R "^timer_logic_tests$" --output-on-failure
```

预期：构建和测试通过。

---

### Task 2: 扩展通用 IL2CPP runtime

**Files:**
- Modify: `payload/il2cpp_runtime.h`
- Modify: `payload/il2cpp_runtime.cpp`

- [ ] **Step 1: 解析新增导出**

在 `Initialize` 解析并在 `Shutdown` 清空 `il2cpp_class_get_type`、`il2cpp_type_get_object`、`il2cpp_runtime_invoke`；公开 `GetTypeObject(Class*)`。不得扫描模块或使用 dump RVA。

- [ ] **Step 2: 增加字段和托管集合安全接口**

保留现有 `FindInstanceFieldByType`，增加可返回所有匹配 offset 的 `FindInstanceFieldsByType`，用于识别 PCPropGrab 两个 Collider 候选；增加 `ReadManagedList(object, maxCount)`，验证 List `+0x10 items/+0x18 size`、size `0..maxCount`、Array vector `+0x20` 每个元素均可读。

- [ ] **Step 3: 增加方法调用辅助**

公开只返回已验证方法入口的解析接口；Unity Injected 调用仍由功能模块声明精确 ABI。为 `Component.GetComponentInParent(Type,bool)` 提供类型对象，但实际调用只发生于游戏线程。

---

### Task 3: 游戏线程 dispatcher

**Files:**
- Create: `payload/game_update_dispatcher.h`
- Create: `payload/game_update_dispatcher.cpp`

- [ ] **Step 1: 定义窄接口**

```cpp
using CrosshairCallback = void (*)(void* pcPropGrab);
using NicknamesCallback = void (*)();
bool Initialize(CrosshairCallback, NicknamesCallback);
void Shutdown();
```

- [ ] **Step 2: 安装唯一 Update Hook**

动态查找 `PCPropGrab.Update` 零参数方法；按现有项目模式保存并验证入口前 14 字节，写绝对跳转。Detour 以递归 mutex 串行：临时恢复原字节、调用原 Update、调用两个非空回调、若未 stopping 则重新安装跳转。回调异常不得越过 Hook 边界。

- [ ] **Step 3: 保证安全拆卸**

安装线程可重试，Shutdown 先置 stopping、唤醒并 join，再持有 hook mutex 恢复原字节并清空回调和状态，确保不在卸载后回调模块。

---

### Task 4: crosshair_info 独立模块

**Files:**
- Create: `payload/crosshair_info.h`
- Create: `payload/crosshair_info.cpp`

- [ ] **Step 1: 初始化反射元数据**

解析 `PCPropGrab` 的 `PCCanvas` 字段（类型反射，验证应为 `0x28`）、PCCanvas state（当前 dump `+0x88`）；收集 PCPropGrab 两个 Collider 类型字段，只有结果无法按类型区分时才选择经可读性和对象链验证的 `+0x78/+0xD0`。解析 `Component.GetComponentInParent(Type,bool)` 及 PhotonObjectInteract、Equipment、LightSwitch、Door、Drawer、Player 类型对象。

- [ ] **Step 2: 游戏线程采样目标**

仅在设置 enabled 时运行。PCCanvas state 非 active/locked/light 时发布空快照；按状态从两个 Collider 候选选择并验证。对 Collider 动态调用 `GetComponentInParent`：优先 PhotonObjectInteract，再分别检测 LightSwitch/Drawer/Door；无专属类型但存在有效交互组件时显示“可交互物”。所有对象指针仅在本次回调栈内使用。

- [ ] **Step 3: 解析装备链并格式化**

PhotonObjectInteract `Prop +0x108` 先经类型/可读性验证；若其父链是 Equipment，则读取 `StoreItem +0x140`、enum `+0x18`、tier `+0x1C`，用纯函数输出 21 类中文名及可选 tier。元数据字段优先于偏移常量；常量只用于当前 dump 无法唯一反射的字段并逐级验证。

- [ ] **Step 4: 发布与绘制**

mutex 下仅发布 `{visible,text}`。透明自动大小窗口无目标不创建；默认 enabled=false/fixed=false/font=26/white/showTier=true。第一次或重置时在获得 own size 后设置 `(displayWidth-ownWidth)/2, (displayHeight-ownHeight)/2-10` 并 clamp；只在 Alt 拖动生效时允许移动，保存位置。

- [ ] **Step 5: 设置 UI**

使用 `LabelControlRow` 提供启用、固定、显示等级、字体大小、颜色和“重置默认设置”；重置保留 enabled 并请求重新计算默认位置。

---

### Task 5: screen_crosshair 独立模块

**Files:**
- Create: `payload/screen_crosshair.h`
- Create: `payload/screen_crosshair.cpp`

- [ ] **Step 1: 前景绘制**

默认 disabled、shape=`FilledCircle`、radius=2、white。在 `ImGui::GetForegroundDrawList()` 的 `DisplaySize/2` 绘制：实心 `AddCircleFilled`；空心 `AddCircle(..., thickness=1)`；十字以 radius 为水平/垂直半长，各一条 `AddLine(..., thickness=1)`。模块不可移动且没有 fixed。

- [ ] **Step 2: 设置 UI**

提供启用、形状 Combo（实心圆/空心圆/十字）、`SliderInt` 半径 1..30、颜色和重置；重置保留 enabled。

---

### Task 6: player_nicknames 独立模块

**Files:**
- Create: `payload/player_nicknames.h`
- Create: `payload/player_nicknames.cpp`

- [ ] **Step 1: 动态解析安全 Unity API**

动态解析 `Network.get_Instance`、`Transform.get_position_Injected`、`Camera.WorldToScreenPoint_Injected`、`Camera.get_scaledPixelWidth/Height`、`Animator.GetBoneTransform`、`Component.get_transform`、`Physics.get_defaultPhysicsScene_Injected`、`PhysicsScene.Internal_Raycast_Injected`、`RaycastHit.get_collider` 和 `Component.GetComponentInParent(Type,bool)`；不使用任何 RVA。

- [ ] **Step 2: 遍历并过滤玩家**

仅 enabled 时读取 Network `List<PlayerSpot> +0x38`，集合上限 16。每项读取 Photon Player `+0x18`（actor `+0x18`、local `+0x1C`、nick `+0x20`）和 game Player `+0x38`；跳过 local、空昵称、无 gamePlayer、`Player +0xD8 DeadPlayer` 非空。当前 dump 已确认死亡字段为 `+0xD8`。

- [ ] **Step 3: 取得头部与相机**

优先 Player `+0x78 NetworkedHeadMountedSocket -> +0x30 modelHeadSpot Transform`；其次 Player `+0x130 Animator` 调 `GetBoneTransform(Head=10)`；最后调 Player 根 `get_transform` 后位置 Y 加保守头高。相机从 Network `+0x30 LocalPlayer -> +0x30 Camera`，逐级验证。

- [ ] **Step 4: 两层遮挡采样**

构造 camera 到 head 的 Ray，使用 `PhysicsScene.Internal_Raycast_Injected`，layerMask=-1、QueryTriggerInteraction.Ignore。读取 `RaycastHit` 当前 dump 布局（distance `+0x1C`），通过 `get_collider` 获取 Collider；若 Collider 父链 Player 是目标或本地玩家则停止且不计层，否则层数加一，将起点沿方向推进 `distance+0.02m` 并缩短剩余距离，最多重复两次。这样快照明确区分 0、1、>=2 层。

- [ ] **Step 5: 投影并发布纯值快照**

`WorldToScreenPoint_Injected` 后跳过 z<=0；调用 camera scaled 尺寸并保存 normalized `{x/width,1-y/height}`。快照每项仅含 actorId、UTF-8 name、normalized x/y、distance、alpha 和 fontSize；不跨线程保存 IL2CPP 指针。

- [ ] **Step 6: 前景昵称绘制和动态字形**

渲染线程复制快照，`font_store::RequestText(26,name)`；使用 26 档 font，通过 `ImDrawList::AddText(font, scaledPx, centeredPosition, colorWithAlpha, name)` 连续缩放 14..near font。设置 height 作为名字相对头顶的像素偏移，并按字号/nearSize 同比；normalized 坐标乘当前 ImGui DisplaySize。

- [ ] **Step 7: 设置 UI**

默认 disabled、height=20、font=26、white；提供启用、高度 `SliderFloat 0..100`、字体、颜色和重置，重置保留 enabled。无 fixed、无移动。

---

### Task 7: 配置 v5 与字体

**Files:**
- Modify: `payload/settings_store.h`
- Modify: `payload/settings_store.cpp`
- Modify: `payload/timer_logic.cpp`
- Modify: `payload/timer_logic_tests.cpp`

- [ ] **Step 1: 新增 DTO**

`CrosshairInfoSettings` 含 enabled/fixed/position/fontSize/color/showTier；`ScreenCrosshairSettings` 含 enabled/shape/radius/color；`PlayerNicknamesSettings` 含 enabled/height/fontSize/color。默认值严格使用需求值。

- [ ] **Step 2: 精准冻结 v4**

在 `settings_store.cpp` 定义独立 `SettingsV4`，字段顺序和类型精确复制当前 `Settings`：Smudge、GameTime、当前 GhostStatus（不含 v3 removedColor）、当前 Crucifix、menuPosition、menuPositionInitialized。`StoredSettingsV4` 必须包含该冻结类型，不能再别名到新 Settings。

- [ ] **Step 3: v5 保存和迁移**

版本改 5；v5 保存完整新 Settings。v4 加载逐字段复制全部旧设置，三个新设置保持默认；保留现有 v1-v3 分支及其语义。成功加载 v4 后新功能布局标志触发 crosshair_info 的中心默认位置。

- [ ] **Step 4: Normalize**

三个颜色 Clamp；两个字号 Snap；radius 调纯函数 clamp；shape 限定 0..2；height 对 NaN 使用 20 并 clamp 0..100；crosshair_info position clamp 到 16384 虚拟边界。

- [ ] **Step 5: 固定字形**

将三个菜单名、所有设置标签、21 个装备中文名、“一级/二级/三级”“门”“抽屉”“灯光开关”“可交互物”“实心圆/空心圆/十字”加入 `GetFixedUiText()`，不加入完整 CJK。扩展测试逐字确认固定集合包含这些文本。

---

### Task 8: Overlay 与 CMake 集成

**Files:**
- Modify: `payload/overlay.cpp`
- Modify: `payload/CMakeLists.txt`

- [ ] **Step 1: 菜单接入**

MenuSection 增加三项；左侧各使用 26 高 Selectable 和分隔线；右侧标题及 `DrawSettings` 路由正确。保持现有 helper 的左标签右控件布局。

- [ ] **Step 2: 生命周期接入**

Load 后初始化 `crosshair_info`、`screen_crosshair`、`player_nicknames`，再 `game_update_dispatcher::Initialize(&crosshair_info::OnGameThreadUpdate,&player_nicknames::OnGameThreadUpdate)`。Shutdown 先 dispatcher，后三个模块，再既有模块和 runtime。

- [ ] **Step 3: 绘制和 Alt 输入**

NewFrame 后按 `screen_crosshair`、`crosshair_info`、`player_nicknames` 顺序绘制；crosshair_info 加入 hasMovableFeature，只有菜单关闭、Alt 按下且其 enabled&&!fixed 时开启输入。其余两模块不参与拖动。

- [ ] **Step 4: CMake**

把六个新 `.cpp` 加入 payload，把 `feature_logic.cpp` 加入 `timer_logic_tests`；不引入第三方 Hook 库。

---

### Task 9: 完整验证与静态审查

**Files:**
- Review: `payload/*.h`
- Review: `payload/*.cpp`
- Review: `payload/CMakeLists.txt`

- [ ] **Step 1: Debug 串行构建与测试**

```powershell
cmake --build "K:\ToolsBOX\out" --config Debug --target timer_logic_tests
ctest --test-dir "K:\ToolsBOX\out" -C Debug --output-on-failure
cmake --build "K:\ToolsBOX\out" --config Debug
```

- [ ] **Step 2: Release 串行构建与测试**

```powershell
cmake --build "K:\ToolsBOX\out" --config Release --target timer_logic_tests
ctest --test-dir "K:\ToolsBOX\out" -C Release --output-on-failure
cmake --build "K:\ToolsBOX\out" --config Release
```

- [ ] **Step 3: IDE 诊断**

检查全部新增/修改文件诊断，必须无编译错误；只报告项目原有且与本次无关的警告。

- [ ] **Step 4: 静态约束审查**

确认：无 `0x180...` RVA、无全内存扫描；三个功能模块不 include 彼此；Unity API 仅从 dispatcher 游戏线程路径调用；跨线程快照无目标指针；设置中存在独立冻结 `SettingsV4` 及 v4->v5 分支；dispatcher 是唯一 PCPropGrab.Update Hook；Shutdown 先停 dispatcher。

- [ ] **Step 5: 记录游戏内待验项**

由于本地构建无法注入目标游戏，最终明确标注需游戏内验证：PCPropGrab.Update 入口 14 字节临时拆装稳定性、IL2CPP Injected ABI、Collider 候选状态对应、GetComponentInParent Type 对象、RaycastHit collider 解析、Network/相机链、头部 fallback、遮挡 0/1/2 层、动态字体重建和 Alt 拖动默认位置。

---

## 实施自审结论

- 三个功能均有独立模块、默认值、设置、绘制和生命周期任务。
- 游戏线程生产/渲染线程消费边界明确，目标指针不跨线程。
- crosshair_info 覆盖 PCCanvas 状态、双 Collider、动态类型链、装备/交互物和 own-size 中心定位。
- player_nicknames 覆盖远端/存活过滤、三段字号、高度同比、0/1/2+ 遮挡、三重头部 fallback、normalized 投影和动态字形。
- 配置覆盖 v1-v3 保留、v4 精准冻结、v4->v5 默认迁移、v5 Normalize。
- 验证覆盖 RED/GREEN、Debug/Release 串行构建、CTest、诊断和静态约束；没有 commit 步骤，符合本任务禁止提交要求。
