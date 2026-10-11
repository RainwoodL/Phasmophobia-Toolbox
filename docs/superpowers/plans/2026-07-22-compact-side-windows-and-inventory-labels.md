# 紧凑侧窗与物品栏非装备显示 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 将按键说明/支持作者侧窗改为 253px/344px 宽的无标准标题栏双行标题窗口，并让物品栏以采集期证据严格区分 `Empty`、`Equipment`、`NonEquipment`、`Unknown`，在非装备槽显示两行文字且在空槽被选中时仍高光。

**Architecture:** `timer_logic` 增加无 ImGui 依赖的侧窗尺寸契约，`feature_logic` 增加槽位分类、非装备文案选择、有效索引高光三个纯逻辑入口，全部先由 `timer_logic_tests` 建立 RED。`inventory_bar` 的游戏线程快照保留分类状态和既有 `InventoryItemVisual`，渲染线程只按状态选择 63 张既有纹理、两行固定文字或空白；当前槽索引直接读取 `PCPropGrab + 0x58` 的现有 `int` 字段，不再由可能为空的 `get_HeldObject()` 反推。`overlay` 继续使用主菜单本帧 Bounds、10px 间隔、现有主题和进程内开关，只替换侧窗尺寸、标题 chrome 与显式换行正文。

**Tech Stack:** Windows x64、C++17、CMake/CTest、MSVC、Unity IL2CPP、Dear ImGui、D3D11、VS Code diagnostics

---

## 文件结构与职责

- Modify: `payload/timer_logic.h` — 声明紧凑侧窗尺寸/标题/二维码的纯布局契约。
- Modify: `payload/timer_logic.cpp` — 实现侧窗布局常量，并把新标题副文案及“可互动物”纳入实际固定字体语料。
- Modify: `payload/feature_logic.h` — 声明四状态枚举、采集证据、渲染文案和 active index 纯逻辑接口。
- Modify: `payload/feature_logic.cpp` — 实现严格四状态分类、仅非装备返回两行文案、有效索引高光。
- Modify: `payload/timer_logic_tests.cpp` — 先建立侧窗、四状态、纹理独立性、文案、空槽高光和固定字形的失败测试。
- Modify: `payload/inventory_bar.cpp` — 采集并发布状态；保留 Prop/Equipment/StoreItem 读取结果；直接读取当前槽索引；按状态绘制贴图、两行文字或空白。
- Modify: `payload/overlay.cpp` — 使用 253/344×菜单实际高度，绘制无标准标题栏的双行标题/关闭按钮，显式换行正文并保留 252×120 二维码。

保持不变：`payload/inventory_resources.h/.cpp` 的 `InventoryItemVisual`、`ResourceIndex`、`GetEmbeddedPng` 和 63 项顺序；`payload/inventory_textures.h/.cpp` 的纹理生命周期；`payload/settings_store.h/.cpp`、磁盘 V7 格式和迁移；`injector` 图标资源与 Console 入口。不得新增设置，不执行游戏注入；本计划没有任何版本控制操作。

---

### Task 1: RED — 锁定紧凑侧窗布局契约

**Files:**
- Test: `payload/timer_logic_tests.cpp:146-173`
- Modify: `payload/timer_logic.h:26-34,94-96`

- [ ] **Step 1: 在头文件声明尚未实现的侧窗布局结果**

在 `SideWindowPositionResult` 后加入：

```cpp
struct CompactSideWindowLayout {
    Size keyHelpSize;
    Size supportSize;
    float titleHeight;
    Size sponsorImageSize;
};
```

在 `SideWindowPositions` 后声明：

```cpp
CompactSideWindowLayout GetCompactSideWindowLayout(float menuHeight);
```

该接口只描述固定 UI 契约，不接收 viewport，不执行 Clamp，也不包含 ImGui 类型。

- [ ] **Step 2: 将侧窗测试改为批准尺寸并增加标题/二维码断言**

用以下内容替换现有 380/430 侧窗段：

```cpp
constexpr Size menuSize{645.0f, 560.0f};
const Bounds menuBounds{{400.0f, 100.0f}, menuSize};
const auto compact = GetCompactSideWindowLayout(menuBounds.size.height);
assert(compact.keyHelpSize.width == 253.0f && compact.keyHelpSize.height == 560.0f);
assert(compact.supportSize.width == 344.0f && compact.supportSize.height == 560.0f);
assert(compact.titleHeight == 54.0f);
assert(compact.sponsorImageSize.width == 252.0f && compact.sponsorImageSize.height == 120.0f);

const auto sidePositions = SideWindowPositions(
    menuBounds, compact.keyHelpSize, compact.supportSize, 10.0f);
assert((sidePositions.left == Position{137.0f, 100.0f}));
assert((sidePositions.right == Position{1055.0f, 100.0f}));
assert(sidePositions.left.y + compact.keyHelpSize.height == 660.0f);
assert(sidePositions.right.y + compact.supportSize.height == 660.0f);

const Bounds negativeMenu{{0.0f, -40.0f}, menuSize};
const auto negative = SideWindowPositions(
    negativeMenu, compact.keyHelpSize, compact.supportSize, 10.0f);
assert((negative.left == Position{-263.0f, -40.0f}));
assert((negative.right == Position{655.0f, -40.0f}));

const Bounds rightEdgeMenu{{1275.0f, 100.0f}, menuSize};
const auto rightOverflow = SideWindowPositions(
    rightEdgeMenu, compact.keyHelpSize, compact.supportSize, 10.0f);
assert((rightOverflow.right == Position{1930.0f, 100.0f}));
```

54px 标题区容纳 20px 主标题、16px 弱色副标题和行间距；侧窗与菜单等高，因此上下边严格对齐。右侧 X 仍只由菜单宽度和 10px gap 决定。

- [ ] **Step 3: 构建确认 RED**

Run:

```powershell
cmake -S "K:\ToolsBOX" -B "K:\ToolsBOX\out" -A x64
cmake --build "K:\ToolsBOX\out" --config Debug --target timer_logic_tests
```

Expected: FAIL，链接器报告 `timer_logic::GetCompactSideWindowLayout(float)` 未定义；失败必须来自新增接口，不能先修改 `overlay.cpp`。

---

### Task 2: GREEN — 实现紧凑侧窗布局并保持无 Clamp 公式

**Files:**
- Modify: `payload/timer_logic.cpp:88-96`
- Test: `payload/timer_logic_tests.cpp`

- [ ] **Step 1: 在 `SideWindowPositions` 后写最小实现**

```cpp
CompactSideWindowLayout GetCompactSideWindowLayout(float menuHeight) {
    return {
        {253.0f, menuHeight},
        {344.0f, menuHeight},
        54.0f,
        {252.0f, 120.0f},
    };
}
```

不要清理或 Clamp `menuHeight`；调用方已只在有效菜单 Bounds 时绘制，且此函数应逐帧原样跟随主菜单实际高度。

- [ ] **Step 2: 运行 GREEN**

Run:

```powershell
cmake --build "K:\ToolsBOX\out" --config Debug --target timer_logic_tests
ctest --test-dir "K:\ToolsBOX\out" -C Debug -R "^timer_logic_tests$" --output-on-failure
```

Expected: 构建成功，紧凑尺寸、上下对齐、10px gap、负坐标和右侧溢出断言全部通过。

---

### Task 3: RED — 用纯逻辑定义严格四状态、文案和高光

**Files:**
- Modify: `payload/feature_logic.h:15-20`
- Test: `payload/timer_logic_tests.cpp:75-95`

- [ ] **Step 1: 声明槽位证据、四状态和纯逻辑接口**

在 `InventoryLayout` 后加入：

```cpp
enum class InventorySlotState { Empty, Equipment, NonEquipment, Unknown };

struct InventorySlotEvidence {
    bool slotPresent = false;
    bool propValid = false;
    bool equipmentFound = false;
    bool storeItemValid = false;
    int equipmentType = -1;
    int tier = -1;
};

struct InventorySlotText {
    const char* firstLine = nullptr;
    const char* secondLine = nullptr;
};

InventorySlotState ClassifyInventorySlot(InventorySlotEvidence evidence);
InventorySlotText InventorySlotTextForState(InventorySlotState state);
bool ShouldHighlightInventorySlot(int activeIndex, int slotIndex);
int NormalizeInventoryActiveIndex(int rawIndex);
```

`equipmentFound == false` 只有在 `slotPresent && propValid` 时表示“已确认不是 Equipment”；Prop、StoreItem 或数值失败必须保留为 `Unknown`。

- [ ] **Step 2: 写分类矩阵失败测试**

在现有物品栏断言旁加入：

```cpp
using feature_logic::InventorySlotEvidence;
using feature_logic::InventorySlotState;
assert(ClassifyInventorySlot({}) == InventorySlotState::Empty);
assert(ClassifyInventorySlot({true, true, true, true, 0, 0}) == InventorySlotState::Equipment);
assert(ClassifyInventorySlot({true, true, true, true, 20, 2}) == InventorySlotState::Equipment);
assert(ClassifyInventorySlot({true, true, false, false, -1, -1}) == InventorySlotState::NonEquipment);
assert(ClassifyInventorySlot({true, false, false, false, -1, -1}) == InventorySlotState::Unknown);
assert(ClassifyInventorySlot({true, true, true, false, -1, -1}) == InventorySlotState::Unknown);
assert(ClassifyInventorySlot({true, true, true, true, -1, 0}) == InventorySlotState::Unknown);
assert(ClassifyInventorySlot({true, true, true, true, 21, 0}) == InventorySlotState::Unknown);
assert(ClassifyInventorySlot({true, true, true, true, 0, -1}) == InventorySlotState::Unknown);
assert(ClassifyInventorySlot({true, true, true, true, 0, 3}) == InventorySlotState::Unknown);
```

这些输入分别覆盖空槽、边界装备、确认非装备、Prop 失败、StoreItem 失败、类型/等级上下界越界。

- [ ] **Step 3: 写“纹理不参与分类”和仅非装备文案测试**

```cpp
const auto equipmentWithoutTextureEvidence = InventorySlotEvidence{true, true, true, true, 7, 1};
assert(ClassifyInventorySlot(equipmentWithoutTextureEvidence) == InventorySlotState::Equipment);

const auto emptyText = InventorySlotTextForState(InventorySlotState::Empty);
const auto equipmentText = InventorySlotTextForState(InventorySlotState::Equipment);
const auto nonEquipmentText = InventorySlotTextForState(InventorySlotState::NonEquipment);
const auto unknownText = InventorySlotTextForState(InventorySlotState::Unknown);
assert(emptyText.firstLine == nullptr && emptyText.secondLine == nullptr);
assert(equipmentText.firstLine == nullptr && equipmentText.secondLine == nullptr);
assert(std::string(nonEquipmentText.firstLine) == u8"可互");
assert(std::string(nonEquipmentText.secondLine) == u8"动物");
assert(unknownText.firstLine == nullptr && unknownText.secondLine == nullptr);
```

纯分类入口不接收 texture/SRV 参数；这从接口层锁定纹理解码失败不会把有效装备改判成非装备。

- [ ] **Step 4: 写 active index 与空槽内容解耦测试**

```cpp
assert(NormalizeInventoryActiveIndex(0) == 0);
assert(NormalizeInventoryActiveIndex(3) == 3);
assert(NormalizeInventoryActiveIndex(-1) == -1);
assert(NormalizeInventoryActiveIndex(4) == -1);
assert(ShouldHighlightInventorySlot(2, 2));
assert(!ShouldHighlightInventorySlot(2, 1));
assert(!ShouldHighlightInventorySlot(-1, 0));
assert(!ShouldHighlightInventorySlot(4, 0));

const auto emptySelectedState = ClassifyInventorySlot({});
assert(emptySelectedState == InventorySlotState::Empty);
assert(ShouldHighlightInventorySlot(2, 2));
```

最后两行明确证明高光只看有效索引，不依赖槽位状态、对象或纹理；不允许最后持有槽或第一槽回退。

- [ ] **Step 5: 构建确认 RED**

Run:

```powershell
cmake --build "K:\ToolsBOX\out" --config Debug --target timer_logic_tests
```

Expected: FAIL，链接器报告新增 `feature_logic` 函数未定义；测试编译阶段不得引用 ImGui、Unity 对象或 D3D11。

---

### Task 4: GREEN — 实现四状态、文案和有效索引规则

**Files:**
- Modify: `payload/feature_logic.cpp:9`
- Test: `payload/timer_logic_tests.cpp`

- [ ] **Step 1: 写严格分类最小实现**

```cpp
InventorySlotState ClassifyInventorySlot(InventorySlotEvidence evidence) {
    if (!evidence.slotPresent) return InventorySlotState::Empty;
    if (!evidence.propValid) return InventorySlotState::Unknown;
    if (!evidence.equipmentFound) return InventorySlotState::NonEquipment;
    if (!evidence.storeItemValid || evidence.equipmentType < 0 || evidence.equipmentType >= 21 ||
        evidence.tier < 0 || evidence.tier >= 3)
        return InventorySlotState::Unknown;
    return InventorySlotState::Equipment;
}
```

不得调用 `inventory_textures::Get` 或以 visual 默认值直接代表非装备；有效范围与既有 `inventory_resources` 的 21×3 契约一致。

- [ ] **Step 2: 写最小文案和高光实现**

```cpp
InventorySlotText InventorySlotTextForState(InventorySlotState state) {
    return state == InventorySlotState::NonEquipment
        ? InventorySlotText{u8"可互", u8"动物"}
        : InventorySlotText{};
}

int NormalizeInventoryActiveIndex(int rawIndex) {
    return rawIndex >= 0 && rawIndex < 4 ? rawIndex : -1;
}

bool ShouldHighlightInventorySlot(int activeIndex, int slotIndex) {
    return NormalizeInventoryActiveIndex(activeIndex) == slotIndex;
}
```

- [ ] **Step 3: 运行 GREEN**

Run:

```powershell
cmake --build "K:\ToolsBOX\out" --config Debug --target timer_logic_tests
ctest --test-dir "K:\ToolsBOX\out" -C Debug -R "^timer_logic_tests$" --output-on-failure
```

Expected: 四状态矩阵、仅非装备文案、纹理独立性和 active index 断言全部通过。

---

### Task 5: RED→GREEN — 固定字体覆盖新标题与“可互动物”

**Files:**
- Test: `payload/timer_logic_tests.cpp:441-453`
- Modify: `payload/timer_logic.cpp:38-51`

- [ ] **Step 1: 先扩展固定字形期望并确认 RED**

在 `ExtractSupportedGlyphs(...)` 的完整期望语料中，把侧窗部分改为包含：

```cpp
u8"按键说明关闭后本次注入不再显示显示隐藏工具箱菜单Alt鼠标左键移动未固定的功能浮窗"
u8"手动开始圣木计时清除圣木计时记录清除十字架燃烧记录支持作者感谢支持你的支持就是我最大的动力"
u8"可互动物自愿赞助辅助永久免费如果无法赞助也可以去抖音bilibili给作者支持点赞关注"
```

Run:

```powershell
cmake --build "K:\ToolsBOX\out" --config Debug --target timer_logic_tests
& "K:\ToolsBOX\out\payload\Debug\timer_logic_tests.exe"
```

Expected: FAIL，固定字形循环在“关闭后本次注入不再显示”或新增字形处触发 assertion；此时不要通过运行时 `RequestText` 绕过固定字形要求。

- [ ] **Step 2: 更新实际 `kFixedUiText`**

将当前包含前台提示的尾部片段替换为实际会显示的固定文本：

```cpp
u8"按键说明关闭后本次注入不再显示U显示隐藏工具箱菜单Alt鼠标左键移动未固定的功能浮窗"
u8"I手动开始圣木计时O清除圣木计时记录P清除十字架燃烧记录支持作者感谢支持你的支持就是我最大的动力"
u8"可互动物自愿赞助辅助永久免费如果无法赞助也可以去抖音bilibili给作者支持点赞关注QQ群304393689不定时更新"
u8"作者霖木B站摆烂的霖木抖音号26953143462或搜索";
```

删除 `仅游戏窗口位于前台时生效`；`font_store.cpp` 构建 atlas 时实际调用 `timer_logic::GetFixedUiText()`，不要修改其未使用的局部 `kFixedUiText`。

- [ ] **Step 3: 运行 GREEN**

Run:

```powershell
cmake --build "K:\ToolsBOX\out" --config Debug --target timer_logic_tests
ctest --test-dir "K:\ToolsBOX\out" -C Debug -R "^timer_logic_tests$" --output-on-failure
```

Expected: 固定字形测试通过，且“可互动物”和双行标题副文案均由初始 atlas 覆盖。

---

### Task 6: 接入游戏线程四状态与真实 active slot

**Files:**
- Modify: `payload/inventory_bar.cpp:28-46,71-149,206-227`
- Test: `payload/timer_logic_tests.cpp`
- Reference only: `tools/Il2CppDumper/dump.cs:209925-209943`

- [ ] **Step 1: 扩展纯值快照与元数据**

将快照项改为同时携带状态和既有 visual：

```cpp
struct SnapshotItem {
    feature_logic::InventorySlotState state = feature_logic::InventorySlotState::Empty;
    inventory_resources::InventoryItemVisual visual{};
};

struct Snapshot {
    std::array<SnapshotItem, 4> items{};
    int activeIndex = -1;
};
```

在 `Metadata` 中加入当前 PCPropGrab 槽索引字段：

```cpp
std::uintptr_t activeSlotOffset = 0x58;
```

当前 `dump.cs` 的 `PCPropGrab` 布局明确为 NetworkedPropGrab `0x48`、List `0x50`、`int` `0x58`；字段名已混淆且同类中存在其他 `int` 使用场景，因此沿用现有 List `0x50(fallback)` 的受控偏移策略，不按模糊字段名猜测，也不新增 Hook。

- [ ] **Step 2: 将 `ResolveVisual` 改为保留采集证据的 `ResolveItem`**

```cpp
SnapshotItem ResolveItem(std::uintptr_t photonObject) {
    feature_logic::InventorySlotEvidence evidence{};
    evidence.slotPresent = photonObject != 0;
    if (!evidence.slotPresent) return {};

    std::uintptr_t prop = 0;
    evidence.propValid = il2cpp_runtime::ReadPointer(
        photonObject + g_metadata.photonPropOffset, prop) && prop;
    if (!evidence.propValid)
        return {feature_logic::ClassifyInventorySlot(evidence), {}};

    void* equipment = g_metadata.getInParent(
        reinterpret_cast<void*>(prop), g_metadata.equipmentType,
        false, g_metadata.getInParentMethod);
    evidence.equipmentFound = equipment != nullptr;
    if (!evidence.equipmentFound)
        return {feature_logic::ClassifyInventorySlot(evidence), {}};

    std::uintptr_t storeItem = 0;
    inventory_resources::InventoryItemVisual visual{};
    evidence.storeItemValid =
        il2cpp_runtime::ReadPointer(
            reinterpret_cast<std::uintptr_t>(equipment) + g_metadata.equipmentStoreItemOffset,
            storeItem) && storeItem &&
        il2cpp_runtime::ReadInt32(
            storeItem + g_metadata.storeItemEquipmentTypeOffset, visual.equipmentType) &&
        il2cpp_runtime::ReadInt32(
            storeItem + g_metadata.storeItemTierOffset, visual.tier);
    evidence.equipmentType = visual.equipmentType;
    evidence.tier = visual.tier;
    const auto state = feature_logic::ClassifyInventorySlot(evidence);
    return {state, state == feature_logic::InventorySlotState::Equipment
                       ? visual : inventory_resources::InventoryItemVisual{}};
}
```

关键边界：空对象立即 `Empty`；非空对象但 Prop 读取失败为 `Unknown`；Prop 有效且 `GetComponentInParent(Equipment)` 明确返回空才是 `NonEquipment`；Equipment 后 StoreItem/字段失败或值越界为 `Unknown`。快照仍不持有 Unity 指针或纹理指针。

- [ ] **Step 3: 用 PCPropGrab 当前索引替换 held object 反推**

把采集循环和 active index 读取改为：

```cpp
for (std::size_t i = 0; i < slots.size(); ++i)
    snapshot.items[i] = ResolveItem(slots[i]);

std::int32_t rawActiveIndex = -1;
if (il2cpp_runtime::ReadInt32(grab + g_metadata.activeSlotOffset, rawActiveIndex))
    snapshot.activeIndex = feature_logic::NormalizeInventoryActiveIndex(rawActiveIndex);
```

删除该路径中 `get_HeldObject()` 调用和 `MatchInventorySlot()`；这样槽对象为空时仍保留游戏的有效 `0..3` 索引，读取失败或越界为 `-1`。从 `Metadata` 删除 `GetHeldObjectFn`、`getHeldObject`、`getHeldObjectMethod`，从 `ResolveMetadata()` 的有效条件和日志删除 held method，日志改为明确输出 `activeSlot=0x58(fallback)`。NetworkedPropGrab offset 仍可由其他既有逻辑保留时才保留；若物品栏已无消费者，则仅删除该模块内无用解析，不改 dispatcher 或其他功能。

- [ ] **Step 4: 构建生产采集代码并回归纯逻辑测试**

Run:

```powershell
cmake --build "K:\ToolsBOX\out" --config Debug --target payload timer_logic_tests
ctest --test-dir "K:\ToolsBOX\out" -C Debug -R "^timer_logic_tests$" --output-on-failure
```

Expected: payload 和测试成功；编译器没有旧 `ResolveVisual`、`GetHeldObjectFn` 或 `Snapshot::items` 类型引用。63 项资源代码未改变。

---

### Task 7: 按状态绘制 63 图标、非装备双行文字和空槽高光

**Files:**
- Modify: `payload/inventory_bar.cpp:270-309`
- Test: `payload/timer_logic_tests.cpp`

- [ ] **Step 1: 仅对 Equipment 查询既有纹理**

将现有无条件 `inventory_textures::Get(visual)` 改为状态门控：

```cpp
const auto& item = snapshot.items[static_cast<std::size_t>(i)];
if (item.state == feature_logic::InventorySlotState::Equipment) {
    if (const auto* texture = inventory_textures::Get(item.visual)) {
        const ImageRect image = FitImageRect(
            min, settings.slotSize, texture->width, texture->height);
        draw->AddImage(reinterpret_cast<ImTextureID>(texture->shaderResourceView),
                       image.min, image.max, {0.0f, 0.0f}, {1.0f, 1.0f}, IM_COL32_WHITE);
    }
}
```

不要修改 `InventoryItemVisual`、`ResourceIndex`、63 张 PNG 清单或纹理 API。有效装备纹理缺失时此槽只是不画图，状态仍为 Equipment，绝不回退非装备文字。

- [ ] **Step 2: 以固定 16px 分别测量并居中两行非装备文字**

紧接图标层加入：

```cpp
const auto text = feature_logic::InventorySlotTextForState(item.state);
if (text.firstLine && text.secondLine) {
    constexpr float labelFontSize = 16.0f;
    constexpr float lineGap = 2.0f;
    ImFont* labelFont = font_store::Get(labelFontSize);
    const ImVec2 firstSize = labelFont->CalcTextSizeA(
        labelFontSize, FLT_MAX, 0.0f, text.firstLine);
    const ImVec2 secondSize = labelFont->CalcTextSizeA(
        labelFontSize, FLT_MAX, 0.0f, text.secondLine);
    const float totalHeight = firstSize.y + lineGap + secondSize.y;
    const float firstY = min.y + (settings.slotSize - totalHeight) * 0.5f;
    draw->AddText(labelFont, labelFontSize,
                  {min.x + (settings.slotSize - firstSize.x) * 0.5f, firstY},
                  IM_COL32_WHITE, text.firstLine);
    draw->AddText(labelFont, labelFontSize,
                  {min.x + (settings.slotSize - secondSize.x) * 0.5f,
                   firstY + firstSize.y + lineGap},
                  IM_COL32_WHITE, text.secondLine);
}
```

补充 `<cfloat>`。两行独立测量并水平居中，整体垂直居中；横版/竖版仍只改变 `min` 的槽位偏移。`Empty` 和 `Unknown` 文案为空，因此槽内保持空白。

- [ ] **Step 3: 用纯逻辑高光判断替换直接比较**

```cpp
const bool active = feature_logic::ShouldHighlightInventorySlot(snapshot.activeIndex, i);
```

保留现有深色槽背景、active 半透明填充、键位数字、内边框和 active 2px 外边框层序；高光判断必须在读取 item state 之前成立，不能增加 `item.state != Empty` 条件。

- [ ] **Step 4: 构建并运行全部 Debug 测试**

Run:

```powershell
cmake --build "K:\ToolsBOX\out" --config Debug --target payload timer_logic_tests
ctest --test-dir "K:\ToolsBOX\out" -C Debug --output-on-failure
```

Expected: payload 构建成功，3 个 CTest 全部通过；物品栏绘制仍使用 63 项既有 texture index，且纯逻辑测试证明 Empty/Unknown 无文案、仅 NonEquipment 有两行文案、空槽有效索引仍高光。

---

### Task 8: 绘制无标准标题栏双行侧窗并显式换行正文

**Files:**
- Modify: `payload/overlay.cpp:227-276`
- Contract: `payload/timer_logic.h/.cpp`

- [ ] **Step 1: 添加共用自定义标题 chrome**

在侧窗函数前加入：

```cpp
void DrawSideWindowTitle(const char* title, bool& open, float titleHeight) {
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 window = ImGui::GetWindowPos();
    ImFont* titleFont = font_store::Get(20.0f);
    ImFont* subtitleFont = font_store::Get(16.0f);
    const ImU32 titleColor = ImGui::GetColorU32(ImGuiCol_Text);
    const ImU32 weakColor = ImGui::GetColorU32(ImGuiCol_TextDisabled);
    draw->AddText(titleFont, 20.0f, {window.x + 14.0f, window.y + 8.0f}, titleColor, title);
    draw->AddText(subtitleFont, 16.0f, {window.x + 14.0f, window.y + 30.0f},
                  weakColor, u8"（关闭后本次注入不再显示）");

    ImGui::SetCursorPos({ImGui::GetWindowWidth() - 34.0f, 8.0f});
    if (ImGui::InvisibleButton("##side_close", {24.0f, 24.0f})) open = false;
    const ImVec2 closeMin = ImGui::GetItemRectMin();
    const ImVec2 closeMax = ImGui::GetItemRectMax();
    const ImU32 closeColor = ImGui::GetColorU32(
        ImGui::IsItemHovered() ? ImGuiCol_Text : ImGuiCol_TextDisabled);
    draw->AddLine({closeMin.x + 6.0f, closeMin.y + 6.0f},
                  {closeMax.x - 6.0f, closeMax.y - 6.0f}, closeColor, 2.0f);
    draw->AddLine({closeMax.x - 6.0f, closeMin.y + 6.0f},
                  {closeMin.x + 6.0f, closeMax.y - 6.0f}, closeColor, 2.0f);
    ImGui::SetCursorPos({14.0f, titleHeight + 10.0f});
}
```

两个窗口分别用唯一按钮 ID，调用前后用 `PushID("key_help")` / `PushID("support")`。标题文字左侧区域与右上 24×24 关闭按钮分离，正文从 54px 标题区下方开始。

- [ ] **Step 2: 将两个窗口改为无标准标题栏且不把 bool 交给 Begin**

共同 flags 改为：

```cpp
const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar |
    ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
    ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings;
```

调用形状改为：

```cpp
if (ImGui::Begin("##key_help_side_window", nullptr, flags)) {
    ImGui::PushID("key_help");
    DrawSideWindowTitle(u8"按键说明", g_keyHelpOpen, layout.titleHeight);
    ImGui::TextUnformatted(u8"U：显示/隐藏工具箱菜单");
    ImGui::TextUnformatted(u8"Alt + 鼠标左键：\n移动未固定的功能浮窗");
    ImGui::TextUnformatted(u8"I：手动开始圣木计时");
    ImGui::TextUnformatted(u8"O：清除圣木计时记录");
    ImGui::TextUnformatted(u8"P：清除十字架燃烧记录");
    ImGui::PopID();
}
```

支持作者使用 `"##support_side_window"`、`PushID("support")` 和标题 `u8"支持作者"`。不传 `&g_keyHelpOpen` / `&g_supportOpen`，避免标准标题栏；关闭按钮只把现有进程内 bool 设为 false，不调用 `settings_store`，不可移动、不可缩放、不可折叠。

- [ ] **Step 3: 显式换行按键说明并删除前台提示及其分隔线**

按键正文固定为：

```cpp
ImGui::TextUnformatted(u8"U：显示/隐藏工具箱菜单");
ImGui::TextUnformatted(u8"Alt + 鼠标左键：\n移动未固定的功能浮窗");
ImGui::TextUnformatted(u8"I：手动开始圣木计时");
ImGui::TextUnformatted(u8"O：清除圣木计时记录");
ImGui::TextUnformatted(u8"P：清除十字架燃烧记录");
```

删除 `ImGui::Separator()` 和 `仅游戏窗口位于前台时生效`。253px 窗口的左右 padding 为 14px，最长正文使用源码中的显式 `\n`，不依赖自动裁切或窗口扩宽。

- [ ] **Step 4: 显式换行支持作者正文并保留二维码精确尺寸**

支持正文固定为：

```cpp
ImGui::TextUnformatted(u8"感谢支持！你的支持就是\n我最大的动力！！");
if (g_sponsorTexture.shaderResourceView) {
    ImGui::Image(reinterpret_cast<ImTextureID>(g_sponsorTexture.shaderResourceView),
                 {layout.sponsorImageSize.width, layout.sponsorImageSize.height});
}
ImGui::TextUnformatted(u8"自愿赞助！辅助永久免费！！");
ImGui::TextUnformatted(u8"如果无法赞助也可以去\n抖音、bilibili给作者支持！");
ImGui::TextUnformatted(u8"点赞+关注！");
ImGui::TextUnformatted(u8"QQ群:304393689\n不定时更新！");
ImGui::TextUnformatted(u8"作者:霖木");
ImGui::TextUnformatted(u8"B站:摆烂的霖木");
ImGui::TextUnformatted(u8"抖音号:26953143462\n或搜索:霖木");
```

二维码继续为 252×120，比例等于 315:150，并小于 344px 窗口扣除 28px padding 后的 316px 内容宽度。

- [ ] **Step 5: 让 `DrawSideWindows` 使用纯布局契约和菜单实际高度**

```cpp
const auto layout = timer_logic::GetCompactSideWindowLayout(menuBounds.size.height);
const auto positions = timer_logic::SideWindowPositions(
    menuBounds, layout.keyHelpSize, layout.supportSize, 10.0f);
if (g_keyHelpOpen) DrawKeyHelpWindow(positions.left, layout);
if (g_supportOpen) DrawSupportWindow(positions.right, layout);
```

相应把两个绘制函数参数改为 `const timer_logic::CompactSideWindowLayout& layout`，各自使用对应 size。保留 `ApplyMenuTheme`/恢复、主菜单显隐判断和同帧 Bounds；不要调用 `ClampPosition`，允许侧窗越出屏幕。

- [ ] **Step 6: 构建侧窗生产代码并运行 Debug 测试**

Run:

```powershell
cmake --build "K:\ToolsBOX\out" --config Debug --target payload timer_logic_tests
ctest --test-dir "K:\ToolsBOX\out" -C Debug --output-on-failure
```

Expected: payload 与精确 3 个测试通过；编译器不报告 ImGui ID、函数签名或 Size 类型错误。

---

### Task 9: 静态契约、诊断、Debug/Release 与默认 EXE 验证

**Files:**
- Verify: `payload/feature_logic.h`
- Verify: `payload/feature_logic.cpp`
- Verify: `payload/timer_logic.h`
- Verify: `payload/timer_logic.cpp`
- Verify: `payload/timer_logic_tests.cpp`
- Verify: `payload/inventory_bar.cpp`
- Verify: `payload/overlay.cpp`
- Verify unchanged: `payload/inventory_resources.h/.cpp`
- Verify unchanged: `payload/settings_store.h/.cpp`
- Verify unchanged: `injector/resource.rc`
- Output: `out/bin/Release/PhasmophobiaInjector.exe`

- [ ] **Step 1: 执行源码契约扫描**

Run:

```powershell
Select-String -Path "K:\ToolsBOX\payload\overlay.cpp" -Pattern '253\.0f|344\.0f|仅游戏窗口位于前台时生效|ImGuiWindowFlags_NoTitleBar|关闭后本次注入不再显示|252\.0f, 120\.0f'
Select-String -Path "K:\ToolsBOX\payload\inventory_bar.cpp" -Pattern 'get_HeldObject|MatchInventorySlot|activeSlotOffset|InventorySlotState|InventorySlotTextForState'
Select-String -Path "K:\ToolsBOX\payload\inventory_resources.h","K:\ToolsBOX\payload\inventory_resources.cpp" -Pattern 'kInventoryPngCount|ResourceIndex|GetEmbeddedPng'
```

Expected: `timer_logic.cpp` 命中 253/344/252×120 常量；`overlay.cpp` 命中无标题栏和副标题且不命中前台提示；`inventory_bar.cpp` 不命中 held-object 反推，命中 active slot 和状态路径；资源接口仍为 63 项既有契约。

- [ ] **Step 2: 运行修改文件 diagnostics**

对以下文件逐个运行 VS Code diagnostics：

```text
K:\ToolsBOX\payload\feature_logic.h
K:\ToolsBOX\payload\feature_logic.cpp
K:\ToolsBOX\payload\timer_logic.h
K:\ToolsBOX\payload\timer_logic.cpp
K:\ToolsBOX\payload\timer_logic_tests.cpp
K:\ToolsBOX\payload\inventory_bar.cpp
K:\ToolsBOX\payload\overlay.cpp
```

Expected: 全部无 error；由本次改动引入的 warning 也必须修复后再继续。

- [ ] **Step 3: Debug 全量构建与精确 3 个 CTest**

Run:

```powershell
cmake --build "K:\ToolsBOX\out" --config Debug
ctest --test-dir "K:\ToolsBOX\out" -C Debug --output-on-failure
```

Expected: 所有目标构建成功；`timer_logic_tests`、`inventory_textures_tests`、`settings_store_tests` 精确 3 个测试全部通过，结尾为 `100% tests passed, 0 tests failed out of 3`。

- [ ] **Step 4: Release 全量构建与精确 3 个 CTest**

Run:

```powershell
cmake --build "K:\ToolsBOX\out" --config Release
ctest --test-dir "K:\ToolsBOX\out" -C Release --output-on-failure
```

Expected: 所有 Release 目标构建成功，3 个测试全部通过；Release 是默认最终交付配置。

- [ ] **Step 5: 确认 V7、图标资源和默认 Release EXE**

Run:

```powershell
Select-String -Path "K:\ToolsBOX\payload\settings_store.cpp" -Pattern 'constexpr std::uint32_t kVersion = 7;'
Select-String -Path "K:\ToolsBOX\injector\resource.rc" -Pattern 'ICON'
Get-Item "K:\ToolsBOX\out\bin\Release\PhasmophobiaInjector.exe" | Select-Object FullName, Length, LastWriteTime
```

Expected: 配置版本仍精确为 7；注入器 RC 仍声明现有 ICON；默认最终产物为 `K:\ToolsBOX\out\bin\Release\PhasmophobiaInjector.exe`，长度大于 0 且时间戳来自本次 Release 构建。`settings_store.h/.cpp` 和 `injector` 源码无本任务改动。

- [ ] **Step 6: 做最终静态边界审查**

逐项确认：

1. 四状态只能是 `Empty/Equipment/NonEquipment/Unknown`；空对象、确认非装备、读取失败三者不会共用默认 visual 猜状态。
2. 分类发生在游戏线程采集期，渲染只消费状态；纹理成功与否不参与分类。
3. 只有 Equipment 查询 63 项贴图，只有 NonEquipment 取得“可互/动物”，Empty/Unknown 槽内空白。
4. active index 直接来自 `PCPropGrab + 0x58` 当前值，`0..3` 有效；空槽仍高光，失败/越界无高光，无历史值或第一槽回退。
5. 槽位序号、背景、active 填充、内边框、外边框、横竖排列、槽位大小和高光颜色保持现有实现。
6. 两个侧窗为无标准标题栏，双行标题、弱色副标题、右上关闭按钮和正文区域不重叠；关闭只改进程内 bool。
7. 按键窗口 253×菜单高度，支持窗口 344×菜单高度；10px gap、菜单同帧跟随、主题、允许越屏保持。
8. 正文长句均有源码显式换行；前台提示及专属分隔线已删除；二维码仍为 252×120。
9. 固定字体实际语料包含“可互动物”和标题副文案；不依赖首帧动态字体重建。
10. 未增加设置、未改变 V7、未改 63 资源索引、未改注入器图标/入口。

- [ ] **Step 7: 记录游戏内验收矩阵但不执行注入**

留给实际运行确认：主菜单移动/显隐/换主题时两侧窗同帧跟随；253px/344px 内标题、关闭按钮、正文和二维码无裁切重叠；关闭后仅本次注入不再显示；拾取普通可交互非装备时显示居中的 16px“可互/动物”；装备仍显示对应 63 图标；空槽及故障槽空白；横竖版文字一致；切到空槽时该槽仍高光；无效当前索引时无槽高光。本计划执行阶段不得启动注入器向游戏注入。
