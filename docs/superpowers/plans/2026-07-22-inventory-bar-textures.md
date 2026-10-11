# 屏幕物品栏贴图化 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 将现有四槽屏幕物品栏从中文名称改为 63 张内嵌 PNG 贴图，并让游戏线程发布 `equipmentType + tier` 纯值快照、渲染线程通过 WIC/D3D11 管理和绘制纹理，同时维持单 EXE 发布、键位、高光、布局和配置行为。

**Architecture:** CMake 用显式有序清单驱动一个 C++17 生成器，把 `resource` 下 21 类 × 3 级 PNG 生成字节数组头文件并编入 payload，现有 injector 再把整个 payload DLL 内嵌进最终 EXE。`inventory_bar` 仍由唯一的 `PCPropGrab.Update` dispatcher 回调在游戏线程解析 Unity 对象，但快照改为四组整数值；独立的 `inventory_textures` 仅在 Overlay 渲染线程使用 WIC 解码内嵌 PNG、创建/释放 D3D11 SRV，绘制层按贴图、键位、边框的顺序调用 ImGui DrawList。

**Tech Stack:** Windows x64、C++17、CMake/CTest、MSVC、Unity IL2CPP、Dear ImGui、D3D11、Windows Imaging Component (`windowscodecs`)

---

## 文件结构与职责

- Create: `payload/gen_inventory_resources.cpp` — 构建期资源生成器；按命令行顺序读取 63 个 PNG，生成稳定的 C++ 字节数组和资源表。
- Create: `payload/inventory_resources.h` — 声明 `InventoryItemVisual`、资源数量常量、合法性/索引函数和内嵌 PNG 只读视图。
- Create: `payload/inventory_resources.cpp` — 校验 `(equipmentType, tier)`、索引生成文件中的 63 项资源，不依赖运行时文件名拼接。
- Create: `payload/inventory_textures.h` — 声明渲染线程拥有的 D3D11 纹理视图与 `Initialize/Get/Shutdown` 生命周期接口。
- Create: `payload/inventory_textures.cpp` — WIC 内存解码、D3D11 texture/SRV 创建、单资源失败日志和完整释放。
- Create (generated under build tree): `out/payload/generated/inventory_png_data.h` — 由 CMake 生成，不手工编辑、不纳入源码树；包含 63 个 PNG 数组及源文件名表。
- Modify: `payload/CMakeLists.txt` — 定义显式 63 文件资源表、自定义生成流程、generated include、测试依赖、`windowscodecs` 链接及新增源码。
- Modify: `payload/timer_logic_tests.cpp` — 在现有测试程序内新增资源边界、63 项映射、PNG 签名、快照值合法性测试。
- Modify: `payload/inventory_bar.h` — `Initialize` 接收 `ID3D11Device*`，使纹理创建入口明确位于渲染线程。
- Modify: `payload/inventory_bar.cpp` — 动态读取 `StoreItem.tier`，快照改为纯整数，按 SRV 等比例绘图并移除物品名称绘制。
- Modify: `payload/overlay.cpp` — 把已取得的 D3D11 device 传给物品栏，并在退出路径确保纹理先于 ImGui/D3D 设施释放。

保持不变：`payload/game_update_dispatcher.h/.cpp` 已把物品栏作为第三回调接入唯一 `PCPropGrab.Update` Hook；`payload/settings_store.h/.cpp` 已持久化启用、固定、槽位大小、字体大小、高光颜色、位置和布局，本次不增加字段、不升级配置版本；`injector/CMakeLists.txt` 已在 payload 构建后把 DLL 转成 `payload_dll.h`，因此 PNG 随 payload 自动进入单 EXE。

---

### Task 1: 建立显式资源生成流程

**Files:**
- Create: `payload/gen_inventory_resources.cpp`
- Modify: `payload/CMakeLists.txt`
- Generated: `out/payload/generated/inventory_png_data.h`

- [ ] **Step 1: 写资源生成器的命令行契约与输入校验**

创建 `payload/gen_inventory_resources.cpp`，参数固定为 `output-header` 后跟 63 个 PNG；任何文件无法读取、为空或不是 PNG 签名时返回非零，避免悄悄生成残缺 payload：

```cpp
#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

namespace {
constexpr int kExpectedResourceCount = 63;
constexpr std::array<std::uint8_t, 8> kPngSignature{
    0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A
};

bool ReadPng(const std::filesystem::path& path, std::vector<std::uint8_t>& bytes) {
    std::ifstream input(path, std::ios::binary);
    bytes.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    return input && bytes.size() >= kPngSignature.size() &&
           std::equal(kPngSignature.begin(), kPngSignature.end(), bytes.begin());
}
}

int main(int argc, char** argv) {
    if (argc != kExpectedResourceCount + 2) {
        std::cerr << "usage: gen_inventory_resources <output-header> <63 png files>\n";
        return 1;
    }
    std::vector<std::vector<std::uint8_t>> resources(kExpectedResourceCount);
    for (int i = 0; i < kExpectedResourceCount; ++i) {
        if (!ReadPng(argv[i + 2], resources[static_cast<std::size_t>(i)])) {
            std::cerr << "invalid PNG: " << argv[i + 2] << '\n';
            return 2;
        }
    }
    return 0; // 下一步将此行替换为完整的原子输出逻辑。
}
```

- [ ] **Step 2: 写出确定性的生成头文件**

在同一 `main` 中创建输出父目录并写临时文件，成功后替换目标文件；每项保留 `std::filesystem::path(argv[i + 2]).filename().string()`，供测试验证显式映射。生成内容的形状必须与 `inventory_resources.cpp` 使用方式一致：

```cpp
const std::filesystem::path outputPath = argv[1];
std::filesystem::create_directories(outputPath.parent_path());
const std::filesystem::path temporaryPath = outputPath.string() + ".tmp";
std::ofstream output(temporaryPath, std::ios::trunc);
if (!output) return 3;
output << "#pragma once\n#include <array>\n#include <cstddef>\n#include <cstdint>\n\n";
output << "namespace inventory_resources::generated {\n";
for (int i = 0; i < kExpectedResourceCount; ++i) {
    output << "inline constexpr std::uint8_t kPng" << i << "[] = {\n";
    for (std::size_t byte = 0; byte < resources[static_cast<std::size_t>(i)].size(); ++byte) {
        if (byte % 16 == 0) output << "    ";
        output << "0x" << std::hex << std::uppercase << std::setw(2) << std::setfill('0')
               << static_cast<unsigned>(resources[static_cast<std::size_t>(i)][byte]) << ",";
        if (byte % 16 == 15) output << '\n';
    }
    output << std::dec << "\n};\n";
}
output << "struct GeneratedPng { const std::uint8_t* data; std::size_t size; const char* sourceName; };\n";
output << "inline constexpr std::array<GeneratedPng, 63> kInventoryPngs{{\n";
for (int i = 0; i < kExpectedResourceCount; ++i) {
    const std::string name = std::filesystem::path(argv[i + 2]).filename().string();
    output << "    {kPng" << i << ", sizeof(kPng" << i << "), \"" << name << "\"},\n";
}
output << "}};\n} // namespace inventory_resources::generated\n";
output.close();
if (!output) return 4;
std::error_code error;
std::filesystem::remove(outputPath, error);
error.clear();
std::filesystem::rename(temporaryPath, outputPath, error);
if (error) {
    std::cerr << "cannot publish generated header: " << error.message() << '\n';
    return 5;
}
return 0;
```

生成器不得转义运行时路径，因为清单中的 63 个文件名只含 ASCII、空格和连字符；源文件大小写按磁盘真实名称写入。

- [ ] **Step 3: 在 CMake 中声明 21×3 的显式有序映射**

在 `payload/CMakeLists.txt` 的测试/库目标之前加入生成器 target 和完整资源列表。列表每连续三项依次是 tier `0,1,2`，组顺序严格对应 `equipmentType 0..20`：

```cmake
add_executable(gen_inventory_resources gen_inventory_resources.cpp)
target_compile_options(gen_inventory_resources PRIVATE /utf-8)

set(INVENTORY_PNG_FILES
    "${CMAKE_SOURCE_DIR}/resource/Crucifix 1 Square-no-bg.png"
    "${CMAKE_SOURCE_DIR}/resource/Crucifix 2 square-no-bg.png"
    "${CMAKE_SOURCE_DIR}/resource/Crucifix 3 square-no-bg.png"
    "${CMAKE_SOURCE_DIR}/resource/DOTS 1 square-no-bg.png"
    "${CMAKE_SOURCE_DIR}/resource/DOTS 2 square-no-bg.png"
    "${CMAKE_SOURCE_DIR}/resource/DOTS 3 square-no-bg.png"
    "${CMAKE_SOURCE_DIR}/resource/EMF 1 square-no-bg.png"
    "${CMAKE_SOURCE_DIR}/resource/EMF 2 square-no-bg.png"
    "${CMAKE_SOURCE_DIR}/resource/EMF 3 square-no-bg.png"
    "${CMAKE_SOURCE_DIR}/resource/Firelight 1 Square-no-bg.png"
    "${CMAKE_SOURCE_DIR}/resource/Firelight 2 square-no-bg.png"
    "${CMAKE_SOURCE_DIR}/resource/Firelight 3 square-no-bg.png"
    "${CMAKE_SOURCE_DIR}/resource/Flashlight 1 square-no-bg.png"
    "${CMAKE_SOURCE_DIR}/resource/Flashlight 2 square-no-bg.png"
    "${CMAKE_SOURCE_DIR}/resource/Flashlight 3 square-no-bg.png"
    "${CMAKE_SOURCE_DIR}/resource/Ghost w 1 square-no-bg.png"
    "${CMAKE_SOURCE_DIR}/resource/Ghost w 2 square-no-bg.png"
    "${CMAKE_SOURCE_DIR}/resource/Ghost w 3 square-no-bg.png"
    "${CMAKE_SOURCE_DIR}/resource/Head 1 square-no-bg.png"
    "${CMAKE_SOURCE_DIR}/resource/Head 2 square-no-bg.png"
    "${CMAKE_SOURCE_DIR}/resource/Head 3 square-no-bg.png"
    "${CMAKE_SOURCE_DIR}/resource/Igniter 1 square-no-bg.png"
    "${CMAKE_SOURCE_DIR}/resource/Igniter 2 square-no-bg.png"
    "${CMAKE_SOURCE_DIR}/resource/Igniter 3 square-no-bg.png"
    "${CMAKE_SOURCE_DIR}/resource/Repellent 1 square-no-bg.png"
    "${CMAKE_SOURCE_DIR}/resource/Repellent 2 square-no-bg.png"
    "${CMAKE_SOURCE_DIR}/resource/Repellent 3 square-no-bg.png"
    "${CMAKE_SOURCE_DIR}/resource/Motion 1 square-no-bg.png"
    "${CMAKE_SOURCE_DIR}/resource/Motion 2 square-no-bg.png"
    "${CMAKE_SOURCE_DIR}/resource/Motion 3 square-no-bg.png"
    "${CMAKE_SOURCE_DIR}/resource/Para 1 square-no-bg.png"
    "${CMAKE_SOURCE_DIR}/resource/Para 2 square-no-bg.png"
    "${CMAKE_SOURCE_DIR}/resource/Para 3 square-no-bg.png"
    "${CMAKE_SOURCE_DIR}/resource/Photo 1 square-no-bg.png"
    "${CMAKE_SOURCE_DIR}/resource/Photo 2 square-no-bg.png"
    "${CMAKE_SOURCE_DIR}/resource/Photo 3 square-no-bg.png"
    "${CMAKE_SOURCE_DIR}/resource/Salt 1 square-no-bg.png"
    "${CMAKE_SOURCE_DIR}/resource/Salt 2 square-no-bg.png"
    "${CMAKE_SOURCE_DIR}/resource/Salt 3 square-no-bg.png"
    "${CMAKE_SOURCE_DIR}/resource/Sanity 1 square-no-bg.png"
    "${CMAKE_SOURCE_DIR}/resource/Sanity 2 square-no-bg.png"
    "${CMAKE_SOURCE_DIR}/resource/Sanity 3 square-no-bg.png"
    "${CMAKE_SOURCE_DIR}/resource/Sound Recorder 1 square-no-bg.png"
    "${CMAKE_SOURCE_DIR}/resource/Sound Recorder 2 square-no-bg.png"
    "${CMAKE_SOURCE_DIR}/resource/Sound Recorder 3 square-no-bg.png"
    "${CMAKE_SOURCE_DIR}/resource/Sound 1 square-no-bg.png"
    "${CMAKE_SOURCE_DIR}/resource/Sound 2 square-no-bg.png"
    "${CMAKE_SOURCE_DIR}/resource/Sound 3 square-no-bg.png"
    "${CMAKE_SOURCE_DIR}/resource/Spirit box 1 square-no-bg.png"
    "${CMAKE_SOURCE_DIR}/resource/Spirit box 2 square-no-bg.png"
    "${CMAKE_SOURCE_DIR}/resource/Spirit box 3 square-no-bg.png"
    "${CMAKE_SOURCE_DIR}/resource/Thermo 1 square-no-bg.png"
    "${CMAKE_SOURCE_DIR}/resource/Thermo 2 square-no-bg.png"
    "${CMAKE_SOURCE_DIR}/resource/Thermo 3 square-no-bg.png"
    "${CMAKE_SOURCE_DIR}/resource/Tripod 1 square-no-bg.png"
    "${CMAKE_SOURCE_DIR}/resource/Tripod 2 square-no-bg.png"
    "${CMAKE_SOURCE_DIR}/resource/Tripod 3 square-no-bg.png"
    "${CMAKE_SOURCE_DIR}/resource/UV 1 square-no-bg.png"
    "${CMAKE_SOURCE_DIR}/resource/UV 2 square-no-bg.png"
    "${CMAKE_SOURCE_DIR}/resource/UV 3 square-no-bg.png"
    "${CMAKE_SOURCE_DIR}/resource/Video 1 square-no-bg.png"
    "${CMAKE_SOURCE_DIR}/resource/Video 2 square-no-bg.png"
    "${CMAKE_SOURCE_DIR}/resource/Video 3 square-no-bg.png"
)
list(LENGTH INVENTORY_PNG_FILES INVENTORY_PNG_COUNT)
if(NOT INVENTORY_PNG_COUNT EQUAL 63)
    message(FATAL_ERROR "Inventory PNG manifest must contain exactly 63 files")
endif()

set(INVENTORY_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(INVENTORY_GENERATED_HEADER "${INVENTORY_GENERATED_DIR}/inventory_png_data.h")
add_custom_command(
    OUTPUT "${INVENTORY_GENERATED_HEADER}"
    COMMAND $<TARGET_FILE:gen_inventory_resources>
            "${INVENTORY_GENERATED_HEADER}" ${INVENTORY_PNG_FILES}
    DEPENDS gen_inventory_resources ${INVENTORY_PNG_FILES}
    COMMENT "Embedding 63 inventory PNG resources"
    COMMAND_EXPAND_LISTS
    VERBATIM
)
add_custom_target(inventory_png_header DEPENDS "${INVENTORY_GENERATED_HEADER}")
```

- [ ] **Step 4: 生成头文件并确认构建期转换成功**

Run:

```powershell
cmake -S "K:\ToolsBOX" -B "K:\ToolsBOX\out" -A x64
cmake --build "K:\ToolsBOX\out" --config Debug --target inventory_png_header
```

Expected: 配置成功，构建日志出现 `Embedding 63 inventory PNG resources`，`K:\ToolsBOX\out\payload\generated\inventory_png_data.h` 存在且生成器退出码为 0；源码树 `resource` 之外没有复制 PNG。

---

### Task 2: TDD 固化资源索引、63 项映射和纯值类型

**Files:**
- Modify: `payload/timer_logic_tests.cpp`
- Create: `payload/inventory_resources.h`
- Create: `payload/inventory_resources.cpp`
- Modify: `payload/CMakeLists.txt`

- [ ] **Step 1: 先在现有测试程序中写资源边界与完整映射测试**

在 `payload/timer_logic_tests.cpp` 加入 `#include "inventory_resources.h"`、`#include <cstring>`，并在 `main()` 的物品栏逻辑断言后加入以下断言。该表同时锁定 `equipmentType` 顺序和磁盘文件名大小写：

```cpp
using inventory_resources::InventoryItemVisual;
assert(!inventory_resources::IsValidVisual({-1, 0}));
assert(!inventory_resources::IsValidVisual({0, -1}));
assert(!inventory_resources::IsValidVisual({21, 0}));
assert(!inventory_resources::IsValidVisual({0, 3}));
assert(inventory_resources::ResourceIndex({0, 0}) == 0);
assert(inventory_resources::ResourceIndex({20, 2}) == 62);
assert(inventory_resources::ResourceIndex({21, 2}) == inventory_resources::kInvalidResourceIndex);

constexpr std::array<const char*, 21> stems{
    "Crucifix", "DOTS", "EMF", "Firelight", "Flashlight", "Ghost w", "Head",
    "Igniter", "Repellent", "Motion", "Para", "Photo", "Salt", "Sanity",
    "Sound Recorder", "Sound", "Spirit box", "Thermo", "Tripod", "UV", "Video"
};
constexpr std::array<std::uint8_t, 8> pngSignature{0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
for (int equipmentType = 0; equipmentType < 21; ++equipmentType) {
    for (int tier = 0; tier < 3; ++tier) {
        const InventoryItemVisual visual{equipmentType, tier};
        const auto* png = inventory_resources::GetEmbeddedPng(visual);
        assert(png != nullptr && png->data != nullptr && png->size > pngSignature.size());
        assert(std::equal(pngSignature.begin(), pngSignature.end(), png->data));
        const std::string sourceName = png->sourceName;
        assert(sourceName.rfind(stems[static_cast<std::size_t>(equipmentType)], 0) == 0);
        assert(sourceName.find(std::to_string(tier + 1)) != std::string::npos);
        assert(inventory_resources::ResourceIndex(visual) == equipmentType * 3 + tier);
    }
}
assert(inventory_resources::GetEmbeddedPng({-1, 0}) == nullptr);
assert(inventory_resources::GetEmbeddedPng({0, 3}) == nullptr);
```

- [ ] **Step 2: 接线测试 target 并确认 RED**

先仅把尚未实现的 `inventory_resources.cpp` 路径和生成头依赖写入测试 target：

```cmake
target_sources(timer_logic_tests PRIVATE inventory_resources.cpp "${INVENTORY_GENERATED_HEADER}")
target_include_directories(timer_logic_tests PRIVATE "${INVENTORY_GENERATED_DIR}")
add_dependencies(timer_logic_tests inventory_png_header)
```

Run:

```powershell
cmake --build "K:\ToolsBOX\out" --config Debug --target timer_logic_tests
```

Expected: 构建失败，错误指向缺少 `inventory_resources.h` 或新增资源函数尚未定义；失败原因必须来自刚加入的接口，而不是资源生成器失败。

- [ ] **Step 3: 定义稳定的纯值与资源视图 API**

创建 `payload/inventory_resources.h`：

```cpp
#pragma once

#include <cstddef>
#include <cstdint>

namespace inventory_resources {

inline constexpr int kEquipmentTypeCount = 21;
inline constexpr int kTierCount = 3;
inline constexpr int kInventoryPngCount = kEquipmentTypeCount * kTierCount;
inline constexpr int kInvalidResourceIndex = -1;

struct InventoryItemVisual {
    int equipmentType = -1;
    int tier = -1;
};

struct EmbeddedPng {
    const std::uint8_t* data = nullptr;
    std::size_t size = 0;
    const char* sourceName = nullptr;
};

bool IsValidVisual(InventoryItemVisual visual);
int ResourceIndex(InventoryItemVisual visual);
const EmbeddedPng* GetEmbeddedPng(InventoryItemVisual visual);

} // namespace inventory_resources
```

- [ ] **Step 4: 写最小资源索引实现**

创建 `payload/inventory_resources.cpp`；使用生成表，不进行字符串拼接和文件 I/O：

```cpp
#include "inventory_resources.h"
#include "inventory_png_data.h"

#include <array>

namespace inventory_resources {
namespace {

const std::array<EmbeddedPng, kInventoryPngCount>& Resources() {
    static const std::array<EmbeddedPng, kInventoryPngCount> resources = [] {
        std::array<EmbeddedPng, kInventoryPngCount> result{};
        for (std::size_t i = 0; i < result.size(); ++i) {
            const auto& source = generated::kInventoryPngs[i];
            result[i] = {source.data, source.size, source.sourceName};
        }
        return result;
    }();
    return resources;
}

} // namespace

bool IsValidVisual(InventoryItemVisual visual) {
    return visual.equipmentType >= 0 && visual.equipmentType < kEquipmentTypeCount &&
           visual.tier >= 0 && visual.tier < kTierCount;
}

int ResourceIndex(InventoryItemVisual visual) {
    return IsValidVisual(visual) ? visual.equipmentType * kTierCount + visual.tier
                                 : kInvalidResourceIndex;
}

const EmbeddedPng* GetEmbeddedPng(InventoryItemVisual visual) {
    const int index = ResourceIndex(visual);
    return index == kInvalidResourceIndex ? nullptr : &Resources()[static_cast<std::size_t>(index)];
}

} // namespace inventory_resources
```

- [ ] **Step 5: 运行 GREEN，确认 63 项均被测试消费**

Run:

```powershell
cmake --build "K:\ToolsBOX\out" --config Debug --target timer_logic_tests
ctest --test-dir "K:\ToolsBOX\out" -C Debug -R "^timer_logic_tests$" --output-on-failure
```

Expected: `timer_logic_tests` 构建成功且 `1/1 Test #... timer_logic_tests ... Passed`；任一映射顺序、文件名 tier 或 PNG 签名错误都会触发断言失败。

---

### Task 3: TDD 将物品栏快照改为 `equipmentType + tier`

**Files:**
- Modify: `payload/timer_logic_tests.cpp`
- Modify: `payload/inventory_bar.cpp`

- [ ] **Step 1: 增加快照值的合法/非法边界断言**

继续复用 `InventoryItemVisual`，在现有 `MatchInventorySlot` 测试旁加入：

```cpp
assert(inventory_resources::IsValidVisual({0, 0}));
assert(inventory_resources::IsValidVisual({20, 2}));
assert(!inventory_resources::IsValidVisual({-1, -1}));
assert(!inventory_resources::IsValidVisual({7, 3}));
```

- [ ] **Step 2: 先改 Snapshot 形状并确认 RED**

在 `payload/inventory_bar.cpp` 用以下结构替换字符串快照，但暂不改采集和绘制引用：

```cpp
struct Snapshot {
    std::array<inventory_resources::InventoryItemVisual, 4> items{};
    int activeIndex = -1;
};
```

同时加入 `#include "inventory_resources.h"`，删除不再需要的 `<string>`。

Run:

```powershell
cmake --build "K:\ToolsBOX\out" --config Debug --target payload
```

Expected: 编译失败，旧 `labels` / `ResolveLabel` / 文本绘制访问不再匹配新快照；这证明渲染端不能继续依赖名称。

- [ ] **Step 3: 动态解析 `StoreItem.tier` 字段**

在 `Metadata` 中紧邻 equipmentType offset 增加 tier offset：

```cpp
std::uintptr_t storeItemEquipmentTypeOffset = 0;
std::uintptr_t storeItemTierOffset = 0;
```

在 `ResolveMetadata()` 的 `fieldsResolved` 条件中复用 `crosshair_info.cpp` 已验证的字段名：

```cpp
il2cpp_runtime::FindInstanceFieldByName(
    storeItemClass, "equipmentType", metadata.storeItemEquipmentTypeOffset) &&
il2cpp_runtime::FindInstanceFieldByName(
    storeItemClass, "tier", metadata.storeItemTierOffset);
```

更新一次性成功日志，使其同时输出 `equipmentType` 和 `tier` offset；失败仍沿用 2 秒 metadata 重试节流，不添加逐帧字段日志。

- [ ] **Step 4: 用纯值解析替换中文名称解析**

把 `ResolveLabel` 替换为返回非法默认值的 `ResolveVisual`：

```cpp
inventory_resources::InventoryItemVisual ResolveVisual(std::uintptr_t photonObject) {
    if (!photonObject) return {};
    std::uintptr_t prop = 0;
    if (!il2cpp_runtime::ReadPointer(photonObject + g_metadata.photonPropOffset, prop) || !prop) return {};
    void* equipment = g_metadata.getInParent(reinterpret_cast<void*>(prop), g_metadata.equipmentType,
                                              false, g_metadata.getInParentMethod);
    if (!equipment) return {};
    std::uintptr_t storeItem = 0;
    inventory_resources::InventoryItemVisual visual{};
    if (!il2cpp_runtime::ReadPointer(
            reinterpret_cast<std::uintptr_t>(equipment) + g_metadata.equipmentStoreItemOffset, storeItem) ||
        !storeItem ||
        !il2cpp_runtime::ReadInt32(storeItem + g_metadata.storeItemEquipmentTypeOffset, visual.equipmentType) ||
        !il2cpp_runtime::ReadInt32(storeItem + g_metadata.storeItemTierOffset, visual.tier) ||
        !inventory_resources::IsValidVisual(visual)) return {};
    return visual;
}
```

采集循环改为：

```cpp
for (std::size_t i = 0; i < slots.size(); ++i)
    snapshot.items[i] = ResolveVisual(slots[i]);
```

`activeIndex` 仍只由原始四槽对象数组与 `get_HeldObject()` 的返回值调用 `MatchInventorySlot` 得出；空手、无法匹配、异常列表仍为 `-1`。快照只含 8 个 `int` 和一个 active index，不持有 `PhotonObjectInteract`、`Prop`、`Equipment`、`StoreItem` 或 SRV 指针。

- [ ] **Step 5: 临时移除旧名称渲染并让 payload 恢复 GREEN**

删除 `font_store::RequestText(label)`、`CalcTextSizeA`、`FitInventoryFontSize` 和居中文字代码；保留四框、键位数字、拖动/方向/位置/高光逻辑。贴图绘制在 Task 5 接入。

Run:

```powershell
cmake --build "K:\ToolsBOX\out" --config Debug --target payload timer_logic_tests
ctest --test-dir "K:\ToolsBOX\out" -C Debug -R "^timer_logic_tests$" --output-on-failure
```

Expected: payload 与现有测试成功；物品栏代码中不再读取 `Snapshot::labels`，并且 `FormatEquipmentLabel` 只保留给准星信息等既有功能。

---

### Task 4: 实现渲染线程 WIC/D3D11 纹理生命周期

**Files:**
- Create: `payload/inventory_textures.h`
- Create: `payload/inventory_textures.cpp`
- Modify: `payload/CMakeLists.txt`

- [ ] **Step 1: 定义不泄漏 COM 所有权的纹理接口**

创建 `payload/inventory_textures.h`：

```cpp
#pragma once

#include "inventory_resources.h"

#include <d3d11.h>

namespace inventory_textures {

struct TextureView {
    ID3D11ShaderResourceView* shaderResourceView = nullptr;
    unsigned width = 0;
    unsigned height = 0;
};

bool Initialize(ID3D11Device* device);
const TextureView* Get(inventory_resources::InventoryItemVisual visual);
void Shutdown();

} // namespace inventory_textures
```

约束：调用方只借用 `TextureView`；不得 `AddRef/Release`。`Initialize/Get/Shutdown` 只由 Overlay 的 Present/渲染线程调用。

- [ ] **Step 2: 实现 WIC 内存解码辅助函数**

创建 `payload/inventory_textures.cpp`，包含 `<wincodec.h>`、`<array>`、`<vector>`、`inventory_resources.h` 和 `log.h`。用 WIC stream 直接读取内嵌数组并统一转换到 32-bit RGBA：

```cpp
namespace inventory_textures {
namespace {
std::array<TextureView, inventory_resources::kInventoryPngCount> g_textures{};

void Release(TextureView& texture) {
    if (texture.shaderResourceView) texture.shaderResourceView->Release();
    texture = {};
}

bool DecodeRgba(IWICImagingFactory* factory, const inventory_resources::EmbeddedPng& png,
                std::vector<std::uint8_t>& pixels, UINT& width, UINT& height) {
    IWICStream* stream = nullptr;
    IWICBitmapDecoder* decoder = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter* converter = nullptr;
    bool success = false;
    if (SUCCEEDED(factory->CreateStream(&stream)) &&
        SUCCEEDED(stream->InitializeFromMemory(
            const_cast<BYTE*>(reinterpret_cast<const BYTE*>(png.data)), static_cast<DWORD>(png.size))) &&
        SUCCEEDED(factory->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnLoad, &decoder)) &&
        SUCCEEDED(decoder->GetFrame(0, &frame)) &&
        SUCCEEDED(factory->CreateFormatConverter(&converter)) &&
        SUCCEEDED(converter->Initialize(frame, GUID_WICPixelFormat32bppRGBA,
                                        WICBitmapDitherTypeNone, nullptr, 0.0,
                                        WICBitmapPaletteTypeCustom)) &&
        SUCCEEDED(converter->GetSize(&width, &height)) && width > 0 && height > 0 &&
        width <= 4096 && height <= 4096) {
        const UINT stride = width * 4;
        pixels.resize(static_cast<std::size_t>(stride) * height);
        success = SUCCEEDED(converter->CopyPixels(nullptr, stride,
                                                  static_cast<UINT>(pixels.size()), pixels.data()));
    }
    if (converter) converter->Release();
    if (frame) frame->Release();
    if (decoder) decoder->Release();
    if (stream) stream->Release();
    return success;
}
}
```

在调用 `InitializeFromMemory` 前显式拒绝 `png.size > MAXDWORD`；宽高上限和乘法采用 `std::size_t` 检查后再分配，避免损坏资源导致溢出。

- [ ] **Step 3: 从 RGBA 创建 D3D11 texture 和 SRV**

加入最小创建函数，texture 仅作为创建 SRV 的临时对象：

```cpp
bool CreateTexture(ID3D11Device* device, IWICImagingFactory* factory,
                   const inventory_resources::EmbeddedPng& png, TextureView& result) {
    std::vector<std::uint8_t> pixels;
    UINT width = 0;
    UINT height = 0;
    if (!DecodeRgba(factory, png, pixels, width, height)) return false;

    D3D11_TEXTURE2D_DESC description{};
    description.Width = width;
    description.Height = height;
    description.MipLevels = 1;
    description.ArraySize = 1;
    description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    description.SampleDesc.Count = 1;
    description.Usage = D3D11_USAGE_IMMUTABLE;
    description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA initial{};
    initial.pSysMem = pixels.data();
    initial.SysMemPitch = width * 4;

    ID3D11Texture2D* texture = nullptr;
    const HRESULT textureResult = device->CreateTexture2D(&description, &initial, &texture);
    if (FAILED(textureResult) || !texture) return false;
    const HRESULT viewResult = device->CreateShaderResourceView(texture, nullptr, &result.shaderResourceView);
    texture->Release();
    if (FAILED(viewResult) || !result.shaderResourceView) return false;
    result.width = width;
    result.height = height;
    return true;
}
```

- [ ] **Step 4: 实现批量初始化、部分失败继续和关闭释放**

`Initialize` 先调用 `Shutdown()` 清理旧状态，拒绝空 device；在当前线程调用 `CoInitializeEx(nullptr, COINIT_MULTITHREADED)`，接受 `S_OK`、`S_FALSE` 和 `RPC_E_CHANGED_MODE`，随后 `CoCreateInstance(CLSID_WICImagingFactory, ..., IID_PPV_ARGS(&factory))`。遍历索引 `0..62`：

```cpp
for (int index = 0; index < inventory_resources::kInventoryPngCount; ++index) {
    const inventory_resources::InventoryItemVisual visual{
        index / inventory_resources::kTierCount,
        index % inventory_resources::kTierCount
    };
    const auto* png = inventory_resources::GetEmbeddedPng(visual);
    if (!png || !CreateTexture(device, factory, *png, g_textures[static_cast<std::size_t>(index)])) {
        LOG("InventoryTextures: failed to create %s", png && png->sourceName ? png->sourceName : "unknown resource");
    }
}
```

每项只尝试一次，因此单张失败只记一次且其余继续。释放 factory；仅当 `CoInitializeEx` 返回 `S_OK` 或 `S_FALSE` 时平衡调用 `CoUninitialize()`。若 WIC factory 创建失败，记录一次并返回 false。只要 factory 创建成功就返回 true，即便个别纹理失败。

`Get` 用 `ResourceIndex` 拒绝越界，并在 SRV 为空时返回 `nullptr`；`Shutdown` 遍历 63 项调用 `Release`，可重复调用：

```cpp
const TextureView* Get(inventory_resources::InventoryItemVisual visual) {
    const int index = inventory_resources::ResourceIndex(visual);
    if (index == inventory_resources::kInvalidResourceIndex) return nullptr;
    const auto& texture = g_textures[static_cast<std::size_t>(index)];
    return texture.shaderResourceView ? &texture : nullptr;
}

void Shutdown() {
    for (auto& texture : g_textures) Release(texture);
}
```

- [ ] **Step 5: 接入 payload target 与 Windows Imaging Component**

在 `add_library(payload SHARED ...)` 中加入：

```cmake
inventory_resources.cpp
inventory_textures.cpp
"${INVENTORY_GENERATED_HEADER}"
```

对 payload 增加生成 include 与依赖：

```cmake
target_include_directories(payload PRIVATE "${INVENTORY_GENERATED_DIR}")
add_dependencies(payload inventory_png_header)
```

在 `target_link_libraries(payload PRIVATE ...)` 追加：

```cmake
windowscodecs
ole32
```

`windowscodecs` 满足 WIC；`ole32` 满足 COM 初始化/实例创建。不得引入 stb_image、DirectXTK 或其他图片库。

- [ ] **Step 6: 构建验证 WIC 和 D3D11 类型/链接**

Run:

```powershell
cmake --build "K:\ToolsBOX\out" --config Debug --target payload
```

Expected: `inventory_textures.cpp` 编译成功，payload 链接不出现 `CoInitializeEx`、`CoCreateInstance` 或 WIC IID 的 unresolved external；生成的 payload DLL 体积相对贴图化前明显增加。

---

### Task 5: 在 ImGui 中按层绘制贴图、键位和高光

**Files:**
- Modify: `payload/inventory_bar.h`
- Modify: `payload/inventory_bar.cpp`
- Modify: `payload/overlay.cpp`

- [ ] **Step 1: 把 D3D11 device 明确传入物品栏初始化**

将 `payload/inventory_bar.h` 改为前置声明并更新签名：

```cpp
struct ID3D11Device;

namespace inventory_bar {
void Initialize(ID3D11Device* device);
// 其余现有接口不变。
}
```

在 `payload/inventory_bar.cpp` 包含 `inventory_textures.h`，初始化时调用：

```cpp
void Initialize(ID3D11Device* device) {
    // 保留当前布局、metadata、快照和设置初始化。
    inventory_textures::Initialize(device);
    SyncSettings();
}
```

在 `payload/overlay.cpp::InitializeImpl()` 中，把已经验证非空的 `g_device` 传入：

```cpp
inventory_bar::Initialize(g_device);
```

这条调用发生在 Present 回调的 Overlay 初始化路径，满足纹理只在渲染线程创建。

- [ ] **Step 2: 先写贴图矩形计算的局部规则**

在 `inventory_bar.cpp` 的匿名 namespace 增加等比例 contain 计算；保留 3px 内边距，宽图或高图均居中且不拉伸：

```cpp
struct ImageRect { ImVec2 min; ImVec2 max; };

ImageRect FitImageRect(ImVec2 slotMin, float slotSize, unsigned imageWidth, unsigned imageHeight) {
    constexpr float padding = 3.0f;
    const float available = (std::max)(1.0f, slotSize - padding * 2.0f);
    const float scale = (std::min)(available / static_cast<float>(imageWidth),
                                   available / static_cast<float>(imageHeight));
    const ImVec2 size{imageWidth * scale, imageHeight * scale};
    const ImVec2 min{slotMin.x + (slotSize - size.x) * 0.5f,
                     slotMin.y + (slotSize - size.y) * 0.5f};
    return {min, {min.x + size.x, min.y + size.y}};
}
```

该函数只在 `TextureView` 宽高均大于零时调用。

- [ ] **Step 3: 按正确图层顺序绘制每个槽位**

重写现有四槽循环中的内容，顺序固定为贴图、键位、边框：

```cpp
const auto& visual = snapshot.items[static_cast<std::size_t>(i)];
if (const auto* texture = inventory_textures::Get(visual)) {
    const ImageRect image = FitImageRect(min, settings.slotSize, texture->width, texture->height);
    draw->AddImage(reinterpret_cast<ImTextureID>(texture->shaderResourceView),
                   image.min, image.max, {0.0f, 0.0f}, {1.0f, 1.0f}, IM_COL32_WHITE);
}

const char key[] = {static_cast<char>('1' + i), '\0'};
draw->AddText(font, settings.fontSize, {min.x + 3.0f, min.y + 2.0f}, IM_COL32_WHITE, key);

const bool active = snapshot.activeIndex == i;
const ImU32 border = active
    ? ImGui::ColorConvertFloat4ToU32({settings.highlightColor.r, settings.highlightColor.g,
                                      settings.highlightColor.b, settings.highlightColor.a})
    : IM_COL32(255, 255, 255, 115);
draw->AddRect(min, max, border, 0.0f, 0, active ? 2.0f : 1.0f);
```

删除循环开头旧的边框绘制，避免边框被图片覆盖。空槽、越界值或纹理失败时 `Get` 返回 null，因此只留下键位与边框；不得回退中文物品名。字体大小继续只控制键位，以保持现有配置兼容。

- [ ] **Step 4: 保留现有交互与方向逻辑不变**

逐项核对并保留：窗口默认关闭；透明无装饰；槽位默认 36×36；键位为 1–4；普通边框半透明 1px；active 边框用保存的高光色 2px；游戏前台且未固定时 Alt 拖动；左右 24px 切纵向；上下 24px 切横向；角落横向优先；切换方向保持中心并 Clamp；位置、布局、启用、固定、槽位大小、字体大小和高光颜色继续写入 v6 现有设置。不要修改 `InventoryBarSettings` 或配置版本。

- [ ] **Step 5: 在关闭顺序中释放全部 SRV**

在 `inventory_bar::Shutdown()` 的开头停止可见状态、清快照，然后调用：

```cpp
inventory_textures::Shutdown();
```

保持 `overlay.cpp::ShutdownImpl()` 当前顺序：先 `game_update_dispatcher::Shutdown()`，再 `inventory_bar::Shutdown()`，之后才执行 `ImGui_ImplDX11_Shutdown()`、`ImGui::DestroyContext()` 和清空 `g_device`。这样游戏线程不再发布快照，且 63 个 SRV 在 D3D11 device 借用期结束前全部 `Release`；重复 Shutdown 安全。

- [ ] **Step 6: 构建并运行现有逻辑测试**

Run:

```powershell
cmake --build "K:\ToolsBOX\out" --config Debug --target payload timer_logic_tests
ctest --test-dir "K:\ToolsBOX\out" -C Debug --output-on-failure
```

Expected: payload 与测试全部成功；编译器没有 `InventoryItemVisual` / `TextureView` 类型不一致，CTest 报告 100% tests passed。

---

### Task 6: 验证生成链、单 EXE、Debug/Release 与运行时场景

**Files:**
- Review: `payload/gen_inventory_resources.cpp`
- Review: `payload/inventory_resources.h`
- Review: `payload/inventory_resources.cpp`
- Review: `payload/inventory_textures.h`
- Review: `payload/inventory_textures.cpp`
- Review: `payload/inventory_bar.h`
- Review: `payload/inventory_bar.cpp`
- Review: `payload/CMakeLists.txt`
- Review: `payload/timer_logic_tests.cpp`
- Review: `payload/overlay.cpp`
- Verify generated: `out/payload/generated/inventory_png_data.h`
- Verify final artifact: `out/bin/Debug/PhasmophobiaInjector.exe` and `out/bin/Release/PhasmophobiaInjector.exe`

- [ ] **Step 1: 从干净构建目录验证 Debug 全生成链**

使用新的验证目录，避免旧 generated header 掩盖依赖问题：

```powershell
cmake -S "K:\ToolsBOX" -B "K:\ToolsBOX\out-inventory-verify" -A x64
cmake --build "K:\ToolsBOX\out-inventory-verify" --config Debug
ctest --test-dir "K:\ToolsBOX\out-inventory-verify" -C Debug --output-on-failure
```

Expected: 首次构建依次生成 `inventory_png_data.h`、构建 payload、由既有 `gen_payload` 生成 `payload_dll.h`、链接 `PhasmophobiaInjector.exe`；CTest 100% 通过。

- [ ] **Step 2: 验证单个 PNG 改动会触发重新生成**

不修改资源内容，使用 PowerShell 更新时间戳并重建：

```powershell
(Get-Item "K:\ToolsBOX\resource\UV 2 square-no-bg.png").LastWriteTime = Get-Date
cmake --build "K:\ToolsBOX\out-inventory-verify" --config Debug --target PhasmophobiaInjector
```

Expected: 日志再次出现 `Embedding 63 inventory PNG resources`，随后重新构建 payload 和最终 injector；证明每张 PNG 都是生成头的精确依赖。

- [ ] **Step 3: 验证 Release 和 CTest**

Run:

```powershell
cmake --build "K:\ToolsBOX\out-inventory-verify" --config Release
ctest --test-dir "K:\ToolsBOX\out-inventory-verify" -C Release --output-on-failure
```

Expected: Release payload、最终 injector 和 `timer_logic_tests` 全部成功，CTest 100% 通过；不需要把 `resource` 目录复制到 `out-inventory-verify/bin/Release`。

- [ ] **Step 4: 验证最终发布物只需要注入器 EXE**

把 Release injector 单独复制到不含资源目录的临时位置：

```powershell
New-Item -ItemType Directory -Force "K:\ToolsBOX\out-inventory-verify\standalone" | Out-Null
Copy-Item "K:\ToolsBOX\out-inventory-verify\bin\Release\PhasmophobiaInjector.exe" `
          "K:\ToolsBOX\out-inventory-verify\standalone\PhasmophobiaInjector.exe"
Get-ChildItem "K:\ToolsBOX\out-inventory-verify\standalone"
```

Expected: standalone 目录只有 `PhasmophobiaInjector.exe`；启动时不访问 `resource/*.png`。实际注入验证在 Step 6 执行。

- [ ] **Step 5: 做静态生命周期与线程边界审查**

逐条确认：

1. `game_update_dispatcher` 仍只有一个 `PCPropGrab.Update` detour，未新增 Hook。
2. 游戏线程只写 `Snapshot::items` 的 `InventoryItemVisual` 与 `activeIndex`，不触碰 WIC、D3D11、ImGui 或 Unity 指针快照。
3. `StoreItem.equipmentType` 与 `StoreItem.tier` 都通过动态字段名解析；有效范围分别是 `0..20`、`0..2`。
4. 列表缺失、size 异常、对象链失败或值越界时，对应槽为非法默认值，不猜图、不显示名称。
5. `Initialize/Get/Shutdown` 只由渲染线程调用；每个成功创建的 SRV 在 Shutdown 恰好释放一次，局部 WIC/texture COM 对象在失败路径也释放。
6. 单资源失败只在初始化遍历中记录一次，且不会中止剩余 62 项。
7. 绘制顺序是 `AddImage` → 键位 `AddText` → 普通/高光 `AddRect`。
8. CMake 显式列出 63 个真实文件名，未通过运行时大小写拼接定位资源。
9. `InventoryBarSettings` 和配置版本没有变化；默认关闭、36px、白色高光和既有位置/布局保持。
10. 生成头仅在 build tree；最终 payload 仍由 injector 的既有 XOR 嵌入链进入单 EXE。

- [ ] **Step 6: 完成游戏内验收矩阵**

仅携带 standalone injector 运行，逐项记录实际结果：

- 依次拾取四件不同类型装备，确认槽位顺序与 1–4 对应。
- 对至少一类装备分别携带 tier 0、1、2，确认三张图不同且映射正确；再抽查其余 20 类，最终覆盖全部 63 张。
- 使用数字键切换当前手持，确认只有匹配槽位显示白色默认高光，键位始终位于贴图上层。
- 丢弃中间槽位，确认空槽只有键位和普通边框，后续槽位不显示猜测贴图。
- 空手时确认无槽高光；`get_HeldObject()` 返回无法匹配对象时也无高光。
- 携带/切换相机专用槽，确认它不进入四个普通槽。
- 将栏拖到左/右 24px 内确认纵向，拖到上/下 24px 内确认横向，角落确认横向优先且窗口中心/屏幕 Clamp 正确。
- 固定后 Alt 不可拖，取消固定且游戏前台按 Alt 可拖；重启后位置、布局、槽位大小、字体大小和高光色恢复。
- 模拟一张纹理不可用的 Debug 构建时，确认该槽仅缺图、四框和键位仍绘制、其他贴图仍可用，日志对该资源仅一条。
- 退出/卸载 payload，使用 D3D11 debug layer 或 Visual Studio Graphics Diagnostics 确认没有物品栏 SRV live-object 报告。

- [ ] **Step 7: 最终一致性扫描**

Run:

```powershell
Select-String -Path "K:\ToolsBOX\payload\inventory_bar.cpp" -Pattern "labels|ResolveLabel|FormatEquipmentLabel"
Select-String -Path "K:\ToolsBOX\payload\inventory_textures.cpp" -Pattern "stb_image|CreateFile|resource\\"
```

Expected: 两条命令均无匹配；物品栏不再携带/绘制名称，纹理加载不依赖第三方解码器或运行时文件。

---

## 分阶段版本记录说明

本机环境没有可用 git 命令，因此执行本计划时不要求、也不要尝试提交。若用户在另一个具备 git 的环境执行，可在 Task 2（资源生成与映射）、Task 4（WIC/D3D11 纹理管理）、Task 5（快照与绘制接入）、Task 6（验证完成）之后分别创建小提交；提交前只暂存该阶段列出的精确文件，并保持生成头位于 build tree、不纳入版本控制。
