# 物品栏非装备文字描边 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 仅为物品栏 `NonEquipment` 的“可互 / 动物”两行白字增加四方向 1px 不透明黑色描边，保持现有布局、状态、高光和 V7 配置不变。

**Architecture:** 在 `feature_logic` 暴露不依赖 ImGui 的固定描边偏移契约，由现有 `timer_logic_tests` 先锁定左、右、上、下四个 1px 偏移。`inventory_bar` 只在既有 `NonEquipment` 两行文字分支复用该契约：每行先画四次黑字，再在原坐标画一次白字，不改变测量和定位公式。

**Tech Stack:** Windows x64、C++17、CMake/CTest、MSVC、Dear ImGui、VS Code diagnostics

---

## 文件结构与边界

- Modify: `payload/feature_logic.h` — 声明纯值偏移类型和固定四方向描边接口。
- Modify: `payload/feature_logic.cpp` — 返回左、右、上、下四个 1px 偏移。
- Modify: `payload/timer_logic_tests.cpp` — 先写失败断言，再验证偏移数量、值和顺序。
- Modify: `payload/inventory_bar.cpp` — 每一行先绘制四次不透明黑色描边，再绘制原位置白色正文。

不修改 `payload/settings_store.h/.cpp`，不升级或重排 V7；不修改槽位状态分类、装备图片、空槽、`Unknown`、槽位序号、高光颜色/透明度、16px 字号、文字测量、水平/垂直居中或 2px 行距。不得启动注入器或执行游戏注入。本计划不含 Git 操作。

---

### Task 1: 用失败测试锁定四方向 1px 描边契约

**Files:**
- Modify: `payload/feature_logic.h:28-40`
- Modify: `payload/timer_logic_tests.cpp:90-120`

- [ ] **Step 1: 声明可独立测试的纯逻辑接口**

在 `InventorySlotText` 后加入类型，并在 `InventorySlotTextForState` 后加入函数声明：

```cpp
struct InventoryTextOffset {
    float x = 0.0f;
    float y = 0.0f;
};

bool operator==(const InventoryTextOffset& lhs, const InventoryTextOffset& rhs);
std::array<InventoryTextOffset, 4> InventoryTextOutlineOffsets();
```

接口只描述相对原文字坐标的偏移，不依赖 `ImVec2`、颜色或绘制 API。

- [ ] **Step 2: 在现有物品栏文字测试后加入精确断言**

```cpp
const auto outlineOffsets = InventoryTextOutlineOffsets();
assert(outlineOffsets.size() == 4);
assert((outlineOffsets[0] == InventoryTextOffset{-1.0f, 0.0f}));
assert((outlineOffsets[1] == InventoryTextOffset{1.0f, 0.0f}));
assert((outlineOffsets[2] == InventoryTextOffset{0.0f, -1.0f}));
assert((outlineOffsets[3] == InventoryTextOffset{0.0f, 1.0f}));
```

这些断言固定恰好四次、无对角线、每轴恰好 1px，并把顺序明确为左、右、上、下。

- [ ] **Step 3: 构建目标并确认 RED**

Run:

```powershell
cmake --build "K:\ToolsBOX\out" --config Debug --target timer_logic_tests
```

Expected: 编译通过但链接失败，未解析符号指向 `feature_logic::InventoryTextOutlineOffsets`（以及测试比较所需的 `InventoryTextOffset::operator==`）；失败原因不是测试语法、头文件或既有断言。

---

### Task 2: 最小实现纯逻辑接口并确认 GREEN

**Files:**
- Modify: `payload/feature_logic.cpp:19-25`
- Test: `payload/timer_logic_tests.cpp`

- [ ] **Step 1: 实现偏移比较和固定偏移数组**

在 `InventorySlotTextForState` 后加入：

```cpp
bool operator==(const InventoryTextOffset& lhs, const InventoryTextOffset& rhs) {
    return lhs.x == rhs.x && lhs.y == rhs.y;
}

std::array<InventoryTextOffset, 4> InventoryTextOutlineOffsets() {
    return {{{-1.0f, 0.0f}, {1.0f, 0.0f}, {0.0f, -1.0f}, {0.0f, 1.0f}}};
}
```

不要增加可配置宽度、对角线、颜色参数或 ImGui 依赖。

- [ ] **Step 2: 重建并运行单项测试确认 GREEN**

Run:

```powershell
cmake --build "K:\ToolsBOX\out" --config Debug --target timer_logic_tests; if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }; ctest --test-dir "K:\ToolsBOX\out" -C Debug -R "^timer_logic_tests$" --output-on-failure
```

Expected: 目标构建成功，CTest 报告 `timer_logic_tests` 通过。

---

### Task 3: 按行绘制四次黑色描边和一次白色正文

**Files:**
- Modify: `payload/inventory_bar.cpp:314-332`

- [ ] **Step 1: 保留现有字号、测量、居中和行距计算**

原样保留以下契约：

```cpp
constexpr float labelFontSize = 16.0f;
constexpr float lineGap = 2.0f;
```

并保留两次 `CalcTextSizeA`、`totalHeight`、`firstY` 及两行 X/Y 坐标公式。不得改用描边后的包围盒参与测量或居中。

- [ ] **Step 2: 在既有两行文字分支中加入按行绘制助手**

在 `firstY` 计算后，以纯逻辑偏移为唯一描边位置来源：

```cpp
const auto outlineOffsets = feature_logic::InventoryTextOutlineOffsets();
const auto drawOutlinedText = [&](ImVec2 position, const char* line) {
    for (const auto offset : outlineOffsets) {
        draw->AddText(labelFont, labelFontSize,
                      {position.x + offset.x, position.y + offset.y},
                      IM_COL32(0, 0, 0, 255), line);
    }
    draw->AddText(labelFont, labelFontSize, position, IM_COL32_WHITE, line);
};
```

黑色必须使用 alpha 255；正文仍为 `IM_COL32_WHITE`。

- [ ] **Step 3: 以原坐标逐行调用，确保每行先描边后正文**

用以下两次调用替换原来的两次白字 `AddText`：

```cpp
drawOutlinedText(
    {min.x + (settings.slotSize - firstSize.x) * 0.5f, firstY},
    text.firstLine);
drawOutlinedText(
    {min.x + (settings.slotSize - secondSize.x) * 0.5f,
     firstY + firstSize.y + lineGap},
    text.secondLine);
```

因此单行调用顺序严格为四次黑字后一次白字；第一行完成后才处理第二行。该代码仍只在 `text.firstLine && text.secondLine` 成立时运行，而当前纯逻辑契约只为 `NonEquipment` 返回两行文本。

- [ ] **Step 4: 构建 Debug payload 和目标测试**

Run:

```powershell
cmake --build "K:\ToolsBOX\out" --config Debug --target payload timer_logic_tests
```

Expected: `payload.dll` 与 `timer_logic_tests.exe` 均构建成功，无 ImGui 参数、lambda 捕获或数组类型错误。

- [ ] **Step 5: 静态核对渲染范围和顺序**

检查 `payload/inventory_bar.cpp` 的该分支并确认：

1. `InventoryTextOutlineOffsets()` 只用于两行非装备文案。
2. lambda 循环内只有黑色 `AddText`，循环后紧接白色 `AddText`。
3. 两行都调用同一个 lambda，未给槽位序号增加描边。
4. 深色背景、可选高光填充、内容、内外边框的现有大层级顺序不变。
5. 装备贴图、Empty、Unknown、active 判定和高光颜色/透明度代码未修改。

---

### Task 4: 完成 diagnostics、Debug/Release、3 项 CTest、EXE 和 SHA 验证

**Files:**
- Review: `payload/feature_logic.h`
- Review: `payload/feature_logic.cpp`
- Review: `payload/timer_logic_tests.cpp`
- Review: `payload/inventory_bar.cpp`
- Review: `payload/settings_store.cpp`

- [ ] **Step 1: 运行修改文件 diagnostics**

分别获取以下四个文件的 VS Code diagnostics：

```text
K:\ToolsBOX\payload\feature_logic.h
K:\ToolsBOX\payload\feature_logic.cpp
K:\ToolsBOX\payload\timer_logic_tests.cpp
K:\ToolsBOX\payload\inventory_bar.cpp
```

Expected: 四个文件均无 error；若存在项目原有 warning，记录并确认不由本次修改引入。

- [ ] **Step 2: 重新配置默认 x64 构建目录**

Run:

```powershell
cmake -S "K:\ToolsBOX" -B "K:\ToolsBOX\out" -A x64
```

Expected: configure/generate 成功，CTest 注册 `timer_logic_tests`、`inventory_textures_tests`、`settings_store_tests` 三项。

- [ ] **Step 3: Debug 全量构建和全部 3 项 CTest**

Run:

```powershell
cmake --build "K:\ToolsBOX\out" --config Debug; if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }; ctest --test-dir "K:\ToolsBOX\out" -C Debug --output-on-failure
```

Expected: Debug 全量构建成功；CTest 显示 `3/3` 通过、`100% tests passed, 0 tests failed out of 3`。

- [ ] **Step 4: Release 全量构建和全部 3 项 CTest**

Run:

```powershell
cmake --build "K:\ToolsBOX\out" --config Release; if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }; ctest --test-dir "K:\ToolsBOX\out" -C Release --output-on-failure
```

Expected: Release 全量构建成功并重建内嵌 payload 的默认注入器；CTest 显示 `3/3` 通过、`100% tests passed, 0 tests failed out of 3`。

- [ ] **Step 5: 验证默认 Release EXE 并记录 SHA-256**

Run:

```powershell
$exe = "K:\ToolsBOX\out\bin\Release\PhasmophobiaInjector.exe"; if (-not (Test-Path $exe -PathType Leaf)) { throw "Missing Release EXE: $exe" }; $item = Get-Item $exe; if ($item.Length -le 0) { throw "Release EXE is empty: $exe" }; $hash = Get-FileHash $exe -Algorithm SHA256; [pscustomobject]@{ FullName = $item.FullName; Length = $item.Length; LastWriteTime = $item.LastWriteTime; SHA256 = $hash.Hash }
```

Expected: 输出绝对路径 `K:\ToolsBOX\out\bin\Release\PhasmophobiaInjector.exe`、大于 0 的长度、本次构建时间和 64 位十六进制 SHA-256。只检查产物，不运行 EXE。

- [ ] **Step 6: 最终范围审查**

Run:

```powershell
Select-String -Path "K:\ToolsBOX\payload\settings_store.cpp" -Pattern "constexpr std::uint32_t kVersion = 7"
```

Expected: 命中 V7。随后复核实际改动仅为 `feature_logic.h/.cpp`、`timer_logic_tests.cpp`、`inventory_bar.cpp`；没有设置字段、迁移代码、状态分类、高光、装备图、空槽、Unknown、槽位序号、字号/测量/居中/行距变更，也没有 Git 操作或游戏注入。
