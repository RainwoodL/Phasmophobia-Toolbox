# 菜单侧窗、资源图片与程序图标 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 将主菜单缩窄到 645×560，为其增加同帧跟随的按键说明/支持作者侧窗，内嵌并显示赞助 PNG，并只给 `PhasmophobiaInjector.exe` 嵌入现有图标。

**Architecture:** `timer_logic` 提供不依赖 ImGui、且明确不做 Clamp 的 `SideWindowPositions`；`overlay` 仍负责主菜单 Clamp、主题局部应用、侧窗进程内开关与赞助纹理生命周期，并严格按“浮层→参考线→主菜单→侧窗”绘制。现有资源生成器在 63 项物品表之外单独生成赞助图数据，`inventory_textures` 将 WIC/D3D11 内存 PNG 创建/释放泛化为公开 `TextureView` API；injector 通过 RC 资源嵌入 ICO，保持 Console 子系统和现有入口不变。

**Tech Stack:** Windows x64、C++17、CMake/CTest、MSVC、Dear ImGui、D3D11、WIC、Win32 RC

---

## 文件结构与职责

- Modify: `payload/timer_logic.h` — 声明侧窗双位置结果及无 Clamp 的 `SideWindowPositions` 纯逻辑接口。
- Modify: `payload/timer_logic.cpp` — 实现左右固定间隔、相对主菜单垂直居中的位置公式，并补齐侧窗固定文本字形。
- Modify: `payload/timer_logic_tests.cpp` — 锁定 645×560、左右位置、允许越界、赞助 PNG 签名/315×150、63 项索引不变及固定字形。
- Modify: `payload/gen_inventory_resources.cpp` — 在 63 张物品图之外读取 `wx_zfb.png`，生成独立 `kSponsorPng`，不改变 `kInventoryPngs[0..62]`。
- Modify: `payload/inventory_resources.h/.cpp` — 通过现有 `EmbeddedPng` 增加 `GetSponsorPng()`，保留 `ResourceIndex` 与 `GetEmbeddedPng` 契约。
- Modify: `payload/inventory_textures.h/.cpp` — 公开任意内存 PNG 到 `TextureView` 的 `CreateFromMemory`/`Release`，物品栏 63 张图改用该接口。
- Modify: `payload/screen_crosshair.cpp` — 在形状 Combo 前设置 100px 宽度，不改变选项与配置。
- Modify: `payload/overlay.cpp` — 菜单尺寸/列宽调整；管理两个进程内 bool 和赞助纹理；主菜单结束后同帧按实际 Bounds 绘制固定侧窗；每次局部应用/恢复主题。
- Modify: `payload/CMakeLists.txt` — 把赞助图加入生成命令依赖，并让现有测试链接资源实现。
- Create: `injector/resource.h` — 定义注入器图标资源 ID。
- Create: `injector/resource.rc` — 将 `resource/favicon.ico` 声明为注入器 ICON 资源。
- Modify: `injector/CMakeLists.txt` — 启用 RC 语言并仅把 RC 加入 `PhasmophobiaInjector`。

保持不变：`settings_store::Settings`、磁盘格式版本 V7、主菜单位置/主题/参考线字段、`inventory_resources::kInventoryPngCount == 63`、Console 子系统、injector `main.cpp`、payload DLL 目标。所有步骤均无 Git 操作。

---

### Task 1: TDD 锁定菜单尺寸与无 Clamp 侧窗公式

**Files:**
- Modify: `payload/timer_logic.h:21-24,79-100`
- Modify: `payload/timer_logic_tests.cpp:114-126`
- Modify: `payload/timer_logic.cpp:76-82`

- [ ] **Step 1: 先声明待实现的纯逻辑结果与入口**

在 `Bounds` 后加入：

```cpp
struct SideWindowPositionResult {
    Position left;
    Position right;
};
```

在 `ClampPosition` 声明后加入：

```cpp
SideWindowPositionResult SideWindowPositions(Bounds menu, Size leftWindow,
                                             Size rightWindow, float gap = 10.0f);
```

- [ ] **Step 2: 写失败测试，锁定 645×560 与正常位置公式**

在 `timer_logic_tests.cpp` 的 `ClampPosition` 断言后加入：

```cpp
constexpr Size menuSize{645.0f, 560.0f};
static_assert(menuSize.width == 645.0f && menuSize.height == 560.0f);
const auto sidePositions = SideWindowPositions(
    {{400.0f, 100.0f}, menuSize}, {240.0f, 220.0f}, {280.0f, 300.0f});
assert((sidePositions.left == Position{150.0f, 270.0f}));
assert((sidePositions.right == Position{1055.0f, 230.0f}));
```

公式必须是：左侧 `menu.x - gap - left.width`，右侧 `menu.x + menu.width + gap`；两侧 Y 分别是 `menu.y + (menu.height - side.height) / 2`。

- [ ] **Step 3: 写失败测试，明确不接收 viewport、不执行 Clamp**

```cpp
const auto overflow = SideWindowPositions(
    {{0.0f, 20.0f}, menuSize}, {252.0f, 700.0f}, {300.0f, 700.0f});
assert((overflow.left == Position{-262.0f, -50.0f}));
assert((overflow.right == Position{655.0f, -50.0f}));
const auto rightOverflow = SideWindowPositions(
    {{1275.0f, 100.0f}, menuSize}, {200.0f, 200.0f}, {252.0f, 200.0f});
assert((rightOverflow.right == Position{1930.0f, 280.0f}));
```

这里故意不传屏幕尺寸；负坐标和超过 1920 的 X 必须原样返回。

- [ ] **Step 4: 构建以确认测试先失败**

Run:

```powershell
cmake -S "K:\ToolsBOX" -B "K:\ToolsBOX\out" -A x64
cmake --build "K:\ToolsBOX\out" --config Debug --target timer_logic_tests
```

Expected: FAIL，链接器报告 `timer_logic::SideWindowPositions` 未定义。

- [ ] **Step 5: 写最小实现，不调用 `ClampPosition`**

在 `ClampPosition` 实现之后加入：

```cpp
SideWindowPositionResult SideWindowPositions(Bounds menu, Size leftWindow,
                                             Size rightWindow, float gap) {
    return {
        {menu.position.x - gap - leftWindow.width,
         menu.position.y + (menu.size.height - leftWindow.height) * 0.5f},
        {menu.position.x + menu.size.width + gap,
         menu.position.y + (menu.size.height - rightWindow.height) * 0.5f},
    };
}
```

- [ ] **Step 6: 重建并运行测试**

Run:

```powershell
cmake --build "K:\ToolsBOX\out" --config Debug --target timer_logic_tests
& "K:\ToolsBOX\out\payload\Debug\timer_logic_tests.exe"
```

Expected: 构建成功，测试进程退出码为 0。

---

### Task 2: TDD 扩展生成器并保持 63 项索引稳定

**Files:**
- Modify: `payload/gen_inventory_resources.cpp:12-63`
- Modify: `payload/CMakeLists.txt:88-109`
- Modify: `payload/inventory_resources.h:18-26`
- Modify: `payload/inventory_resources.cpp:23-36`
- Modify: `payload/timer_logic_tests.cpp:80-113`
- Generated: `out/payload/generated/inventory_png_data.h`

- [ ] **Step 1: 先扩展资源只读接口**

在 `inventory_resources.h` 的 `GetEmbeddedPng` 后声明：

```cpp
const EmbeddedPng* GetSponsorPng();
```

- [ ] **Step 2: 写失败测试，验证赞助图独立存在、PNG 签名和 IHDR 尺寸**

在现有 63 项 PNG 循环及无效资源断言后加入：

```cpp
const auto* sponsor = inventory_resources::GetSponsorPng();
assert(sponsor != nullptr && sponsor->data != nullptr && sponsor->size >= 24);
assert(std::equal(pngSignature.begin(), pngSignature.end(), sponsor->data));
assert(std::string(sponsor->sourceName) == "wx_zfb.png");
const auto readBigEndian32 = [](const std::uint8_t* bytes) {
    return (static_cast<std::uint32_t>(bytes[0]) << 24) |
           (static_cast<std::uint32_t>(bytes[1]) << 16) |
           (static_cast<std::uint32_t>(bytes[2]) << 8) |
           static_cast<std::uint32_t>(bytes[3]);
};
assert(readBigEndian32(sponsor->data + 16) == 315);
assert(readBigEndian32(sponsor->data + 20) == 150);
assert(inventory_resources::kInventoryPngCount == 63);
assert(inventory_resources::ResourceIndex({0, 0}) == 0);
assert(inventory_resources::ResourceIndex({20, 2}) == 62);
```

PNG 的 IHDR 宽高固定处于字节偏移 16/20；运行时显示尺寸在 Task 5 固定为 252×120。

- [ ] **Step 3: 构建以确认新资源接口尚未实现**

Run:

```powershell
cmake --build "K:\ToolsBOX\out" --config Debug --target timer_logic_tests
```

Expected: FAIL，链接器报告 `inventory_resources::GetSponsorPng` 未定义。

- [ ] **Step 4: 调整生成器参数契约，但将赞助图存放在 63 项数组之外**

将生成器参数改为 `output-header sponsor-png 63-png-files`：

```cpp
if (argc != kExpectedResourceCount + 3) {
    std::cerr << "usage: gen_inventory_resources <output-header> <sponsor png> <63 png files>\n";
    return 1;
}
std::vector<std::uint8_t> sponsor;
if (!ReadPng(argv[2], sponsor)) {
    std::cerr << "invalid PNG: " << argv[2] << '\n';
    return 2;
}
std::vector<std::vector<std::uint8_t>> resources(kExpectedResourceCount);
for (int i = 0; i < kExpectedResourceCount; ++i) {
    if (!ReadPng(argv[i + 3], resources[static_cast<std::size_t>(i)])) {
        std::cerr << "invalid PNG: " << argv[i + 3] << '\n';
        return 2;
    }
}
```

抽出复用的字节数组输出 lambda：

```cpp
const auto writeBytes = [&output](const char* symbol, const std::vector<std::uint8_t>& bytes) {
    output << "inline constexpr std::uint8_t " << symbol << "[] = {\n";
    for (std::size_t byte = 0; byte < bytes.size(); ++byte) {
        if (byte % 16 == 0) output << "    ";
        output << "0x" << std::hex << std::uppercase << std::setw(2) << std::setfill('0')
               << static_cast<unsigned>(bytes[byte]) << ",";
        if (byte % 16 == 15) output << '\n';
    }
    output << std::dec << "\n};\n";
};
writeBytes("kSponsorPngData", sponsor);
for (int i = 0; i < kExpectedResourceCount; ++i) {
    const std::string symbol = "kPng" + std::to_string(i);
    writeBytes(symbol.c_str(), resources[static_cast<std::size_t>(i)]);
}
```

在 `GeneratedPng` 声明后、`kInventoryPngs` 前单独输出：

```cpp
output << "inline constexpr GeneratedPng kSponsorPng{kSponsorPngData, sizeof(kSponsorPngData), \"wx_zfb.png\"};\n";
output << "inline constexpr std::array<GeneratedPng, 63> kInventoryPngs{{\n";
for (int i = 0; i < kExpectedResourceCount; ++i) {
    const std::string name = std::filesystem::path(argv[i + 3]).filename().string();
    output << "    {kPng" << i << ", sizeof(kPng" << i << "), \"" << name << "\"},\n";
}
```

不得把赞助图插入 `kInventoryPngs`，不得改变 63 项原顺序。

- [ ] **Step 5: 在 CMake 生成命令中显式加入赞助图**

在生成目录定义前加入，并替换命令/依赖：

```cmake
set(SPONSOR_PNG_FILE "${CMAKE_SOURCE_DIR}/resource/wx_zfb.png")
add_custom_command(
    OUTPUT "${INVENTORY_GENERATED_HEADER}"
    COMMAND $<TARGET_FILE:gen_inventory_resources>
            "${INVENTORY_GENERATED_HEADER}" "${SPONSOR_PNG_FILE}" ${INVENTORY_PNG_FILES}
    DEPENDS gen_inventory_resources "${SPONSOR_PNG_FILE}" ${INVENTORY_PNG_FILES}
    COMMENT "Embedding 63 inventory PNG resources and sponsor PNG"
    COMMAND_EXPAND_LISTS
    VERBATIM
)
```

保留现有 `list(LENGTH INVENTORY_PNG_FILES ...)` 和 `EQUAL 63` 配置期检查。

- [ ] **Step 6: 实现赞助数据适配，不改物品索引函数**

在 `inventory_resources.cpp` 的 `GetEmbeddedPng` 后加入：

```cpp
const EmbeddedPng* GetSponsorPng() {
    static const EmbeddedPng sponsor{
        generated::kSponsorPng.data,
        generated::kSponsorPng.size,
        generated::kSponsorPng.sourceName,
    };
    return &sponsor;
}
```

`IsValidVisual`、`ResourceIndex`、`Resources()` 与 `GetEmbeddedPng` 不做任何修改。

- [ ] **Step 7: 重建生成头并运行资源测试**

Run:

```powershell
cmake --build "K:\ToolsBOX\out" --config Debug --target inventory_png_header
cmake --build "K:\ToolsBOX\out" --config Debug --target timer_logic_tests
& "K:\ToolsBOX\out\payload\Debug\timer_logic_tests.exe"
```

Expected: 日志出现新 embedding 文本；生成头同时含 `kSponsorPng` 和 `std::array<GeneratedPng, 63> kInventoryPngs`；测试退出码为 0。

---

### Task 3: TDD 泛化任意内存 PNG 的 `TextureView` API

**Files:**
- Create: `payload/inventory_textures_tests.cpp`
- Modify: `payload/inventory_textures.h:1-18`
- Modify: `payload/inventory_textures.cpp:13-137`
- Modify: `payload/CMakeLists.txt:101-120`

- [ ] **Step 1: 先在头文件声明通用内存接口**

加入标准类型头并在 `TextureView` 后声明：

```cpp
#include <cstddef>
#include <cstdint>

bool CreateFromMemory(ID3D11Device* device, const std::uint8_t* data,
                      std::size_t size, TextureView& result);
void Release(TextureView& texture);
```

保留 `Initialize(ID3D11Device*)`、`Get(InventoryItemVisual)`、`Shutdown()`，避免改变物品栏调用方。

- [ ] **Step 2: 写失败测试锁定空输入和释放契约**

创建 `payload/inventory_textures_tests.cpp`：

```cpp
#include "inventory_textures.h"

#include <cstdint>

#define CHECK(condition) do { if (!(condition)) return 1; } while (false)

int main() {
    inventory_textures::TextureView texture{};
    constexpr std::uint8_t invalidPng[]{0x89, 0x50, 0x4E, 0x47};
    CHECK(!inventory_textures::CreateFromMemory(nullptr, invalidPng, sizeof(invalidPng), texture));
    CHECK(!texture.shaderResourceView && texture.width == 0 && texture.height == 0);
    CHECK(!inventory_textures::CreateFromMemory(nullptr, nullptr, 0, texture));
    inventory_textures::Release(texture);
    CHECK(!texture.shaderResourceView && texture.width == 0 && texture.height == 0);
    return 0;
}
```

在 `payload/CMakeLists.txt` 的现有测试目标后注册：

```cmake
add_executable(inventory_textures_tests
    inventory_textures_tests.cpp
    inventory_textures.cpp
    inventory_resources.cpp
    "${INVENTORY_GENERATED_HEADER}"
)
target_include_directories(inventory_textures_tests PRIVATE "${INVENTORY_GENERATED_DIR}")
add_dependencies(inventory_textures_tests inventory_png_header)
target_compile_options(inventory_textures_tests PRIVATE /utf-8)
target_link_libraries(inventory_textures_tests PRIVATE d3d11 windowscodecs ole32)
add_test(NAME inventory_textures_tests COMMAND inventory_textures_tests)
```

- [ ] **Step 3: 构建以确认公开函数尚未定义**

Run:

```powershell
cmake --build "K:\ToolsBOX\out" --config Debug --target inventory_textures_tests
```

Expected: FAIL，编译器/链接器报告公开 `CreateFromMemory` 或 `Release` 与当前匿名实现不匹配。

- [ ] **Step 4: 让内部解码只依赖任意内存视图**

将 `DecodeRgba` 的签名和起始校验替换为：

```cpp
bool DecodeRgba(IWICImagingFactory* factory, const std::uint8_t* data, std::size_t size,
                std::vector<std::uint8_t>& pixels, UINT& width, UINT& height) {
    if (!data || size == 0 || size > MAXDWORD) return false;
```

函数其余 WIC 创建链保持逐项 `SUCCEEDED` 判定，并把 `InitializeFromMemory` 参数精确替换为：

```cpp
const_cast<BYTE*>(reinterpret_cast<const BYTE*>(data)), static_cast<DWORD>(size)
```

将内部 `CreateTexture` 签名替换为：

```cpp
bool CreateTexture(ID3D11Device* device, IWICImagingFactory* factory,
                   const std::uint8_t* data, std::size_t size, TextureView& result)
```

并将其解码调用替换为：

```cpp
if (!DecodeRgba(factory, data, size, pixels, width, height)) return false;
```

- [ ] **Step 5: 实现公开创建/释放，并保证失败结果为空**

把原匿名 `Release` 删除，在匿名命名空间结束后加入公开实现：

```cpp
void Release(TextureView& texture) {
    if (texture.shaderResourceView) texture.shaderResourceView->Release();
    texture = {};
}

bool CreateFromMemory(ID3D11Device* device, const std::uint8_t* data,
                      std::size_t size, TextureView& result) {
    Release(result);
    if (!device || !data || size == 0) return false;
    const HRESULT initializeResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool uninitialize = initializeResult == S_OK || initializeResult == S_FALSE;
    if (FAILED(initializeResult) && initializeResult != RPC_E_CHANGED_MODE) return false;
    IWICImagingFactory* factory = nullptr;
    const HRESULT factoryResult = CoCreateInstance(
        CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    const bool success = SUCCEEDED(factoryResult) && factory &&
                         CreateTexture(device, factory, data, size, result);
    if (factory) factory->Release();
    if (uninitialize) CoUninitialize();
    if (!success) Release(result);
    return success;
}
```

- [ ] **Step 6: 让 63 张物品纹理复用通用 API**

简化 `Initialize`：保留 `Shutdown()` 和空 device 检查，循环中改为：

```cpp
const auto* png = inventory_resources::GetEmbeddedPng(visual);
if (!png || !CreateFromMemory(device, png->data, png->size,
                              g_textures[static_cast<std::size_t>(index)])) {
    LOG("InventoryTextures: failed to create %s",
        png && png->sourceName ? png->sourceName : "unknown resource");
}
```

删除 `Initialize` 外层重复的 COM/WIC factory 创建和释放；`Shutdown()` 继续逐项调用公开 `Release`。

- [ ] **Step 7: 构建并运行纹理接口测试，再构建 payload**

Run:

```powershell
cmake --build "K:\ToolsBOX\out" --config Debug --target inventory_textures_tests payload
& "K:\ToolsBOX\out\payload\Debug\inventory_textures_tests.exe"
```

Expected: 两个目标构建成功，测试退出码为 0；`windowscodecs` 与 `ole32` 链接成功。

---

### Task 4: 固定准星 Combo、菜单尺寸和侧窗所需字形

**Files:**
- Modify: `payload/screen_crosshair.cpp:34-37`
- Modify: `payload/overlay.cpp:85-86,155-174`
- Modify: `payload/timer_logic.cpp:38-47`
- Modify: `payload/timer_logic_tests.cpp:330-343`

- [ ] **Step 1: 写源码契约检查并确认当前值失败**

Run:

```powershell
$crosshair = Get-Content "K:\ToolsBOX\payload\screen_crosshair.cpp" -Raw
$overlay = Get-Content "K:\ToolsBOX\payload\overlay.cpp" -Raw
if ($crosshair -notmatch 'SetNextItemWidth\(100\.0f\);\s*changed \|= ImGui::Combo\("##screen_crosshair_shape"') { throw 'shape Combo is not 100px' }
if ($overlay -notmatch 'menuSize\{645\.0f, 560\.0f\}') { throw 'menu size is not 645x560' }
if ($overlay -notmatch 'SetColumnWidth\(0, 180\.0f\)') { throw 'menu column is not 180px' }
if ($overlay -notmatch 'BeginChild\("##feature_list", ImVec2\(162\.0f, 0\.0f\)') { throw 'feature child is not 162px' }
```

Expected: FAIL，至少报告 shape Combo 或菜单尺寸仍为旧值。

- [ ] **Step 2: 固定形状 Combo 为 100px**

在 `screen_crosshair.cpp` 的形状 `Combo` 前加入：

```cpp
ImGui::SetNextItemWidth(100.0f);
changed |= ImGui::Combo("##screen_crosshair_shape", &shape, u8"实心圆\0空心圆\0十字\0");
```

不得改 `CrosshairShape`、选项顺序、`settings.shape` 或 Settings 格式。

- [ ] **Step 3: 调整主菜单与左列尺寸**

在 `overlay.cpp` 中只替换以下三个数值：

```cpp
ImGui::BeginChild("##feature_list", ImVec2(162.0f, 0.0f), true);
constexpr timer_logic::Size menuSize{645.0f, 560.0f};
ImGui::SetColumnWidth(0, 180.0f);
```

右侧仍是 `BeginChild("##feature_settings", {0, 0}, true)`，不得缩窄现有标签/滑块/颜色控件；超出内容由该 Child 滚动。保留 `ClampPosition` 主菜单逻辑。

- [ ] **Step 4: 先扩展固定字形测试**

把固定文本覆盖断言的输入追加完整侧窗文案：

```cpp
u8"按键说明显示隐藏工具箱菜单Alt鼠标左键移动未固定的功能浮窗手动开始圣木计时清除圣木计时记录"
u8"清除十字架燃烧记录仅游戏窗口位于前台时生效支持作者感谢支持你的支持就是我最大的动力"
u8"自愿赞助辅助永久免费如果无法赞助也可以去抖音bilibili给作者支持点赞关注QQ群不定时更新作者霖木"
u8"B站摆烂的霖木抖音号或搜索"
```

- [ ] **Step 5: 运行测试确认缺失字形先失败**

Run:

```powershell
cmake --build "K:\ToolsBOX\out" --config Debug --target timer_logic_tests
& "K:\ToolsBOX\out\payload\Debug\timer_logic_tests.exe"
```

Expected: FAIL，固定字形集合不包含至少一个新增汉字/字母。

- [ ] **Step 6: 将完整侧窗文本加入唯一生效的固定字形源**

在 `timer_logic.cpp` 的 `kFixedUiText` 追加：

```cpp
u8"按键说明U显示隐藏工具箱菜单Alt鼠标左键移动未固定的功能浮窗I手动开始圣木计时O清除圣木计时记录"
u8"P清除十字架燃烧记录仅游戏窗口位于前台时生效支持作者感谢支持你的支持就是我最大的动力"
u8"自愿赞助辅助永久免费如果无法赞助也可以去抖音bilibili给作者支持点赞关注QQ群304393689不定时更新"
u8"作者霖木B站摆烂的霖木抖音号26953143462或搜索";
```

`font_store::BuildAtlas` 实际调用 `timer_logic::GetFixedUiText()`；不要扩展 `font_store.cpp` 中未被 `BuildAtlas` 使用的同名匿名常量。

- [ ] **Step 7: 运行测试和源码契约检查**

Run: 重复 Step 1 的 PowerShell 检查，再运行 `timer_logic_tests.exe`。

Expected: 所有检查通过，测试退出码为 0。

---

### Task 5: Overlay 管理赞助纹理与两个进程内侧窗

**Files:**
- Modify: `payload/overlay.cpp:3-52,133-220,307-385,391-439`

- [ ] **Step 1: 增加依赖与仅进程内状态**

在 include 区加入：

```cpp
#include "inventory_resources.h"
#include "inventory_textures.h"
```

在现有匿名全局状态旁加入：

```cpp
bool g_keyHelpOpen = true;
bool g_supportOpen = true;
inventory_textures::TextureView g_sponsorTexture{};
```

两个 bool 不得加入 `settings_store.h/.cpp`，不调用 `MarkDirty()`，因此每次 DLL 重新加载会由静态初始化恢复为 `true`。

- [ ] **Step 2: 在 Overlay 初始化/关闭中拥有赞助纹理**

在 `inventory_bar::Initialize(g_device);` 之后加入：

```cpp
const auto* sponsor = inventory_resources::GetSponsorPng();
if (sponsor && !inventory_textures::CreateFromMemory(
        g_device, sponsor->data, sponsor->size, g_sponsorTexture)) {
    LOG("Overlay: failed to create sponsor texture");
}
```

在 `ShutdownImpl` 中、`inventory_bar::Shutdown()` 之前加入：

```cpp
inventory_textures::Release(g_sponsorTexture);
```

创建失败时 `g_sponsorTexture` 保持空，后续只跳过 `ImGui::Image`，文字仍绘制。

- [ ] **Step 3: 添加按键说明窗口绘制函数**

```cpp
void DrawKeyHelpWindow(timer_logic::Position position) {
    ImGui::SetNextWindowPos({position.x, position.y}, ImGuiCond_Always);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                                   ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize;
    if (ImGui::Begin(u8"按键说明", &g_keyHelpOpen, flags)) {
        ImGui::TextUnformatted(u8"U：显示/隐藏工具箱菜单");
        ImGui::TextUnformatted(u8"Alt + 鼠标左键：移动未固定的功能浮窗");
        ImGui::TextUnformatted(u8"I：手动开始圣木计时");
        ImGui::TextUnformatted(u8"O：清除圣木计时记录");
        ImGui::TextUnformatted(u8"P：清除十字架燃烧记录");
        ImGui::Separator();
        ImGui::TextUnformatted(u8"仅游戏窗口位于前台时生效");
    }
    ImGui::End();
}
```

不使用 `NoTitleBar` 或 `NoInputs`，确保标准标题栏 X 可点击；不接入 `MovableWindowResult`、Alt 拖动或吸附。

- [ ] **Step 4: 添加支持作者窗口及精确内容/图片尺寸**

```cpp
void DrawSupportWindow(timer_logic::Position position) {
    ImGui::SetNextWindowPos({position.x, position.y}, ImGuiCond_Always);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                                   ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize;
    if (ImGui::Begin(u8"支持作者", &g_supportOpen, flags)) {
        ImGui::TextUnformatted(u8"感谢支持！你的支持就是我最大的动力！！");
        if (g_sponsorTexture.shaderResourceView) {
            ImGui::Image(g_sponsorTexture.shaderResourceView, {252.0f, 120.0f});
        }
        ImGui::TextUnformatted(u8"自愿赞助！辅助永久免费！！");
        ImGui::TextUnformatted(u8"如果无法赞助也可以去抖音、bilibili给作者支持！");
        ImGui::TextUnformatted(u8"点赞+关注！");
        ImGui::TextUnformatted(u8"QQ群:304393689 不定时更新！");
        ImGui::TextUnformatted(u8"作者:霖木");
        ImGui::TextUnformatted(u8"B站:摆烂的霖木");
        ImGui::TextUnformatted(u8"抖音号:26953143462或搜索:霖木");
    }
    ImGui::End();
}
```

不得在运行时读取 `resource/wx_zfb.png` 或任何相对路径。

- [ ] **Step 5: 用本帧主菜单 Bounds 测量并定位侧窗**

增加协调函数；先用 `CalcTextSize`/当前样式确定窗口固定内容所需尺寸，或在首次提交时用固定尺寸，关键是传给纯逻辑的 size 与 `SetNextWindowSize` 一致。采用以下稳定尺寸，支持窗宽覆盖 252px 图片及左右 padding：

```cpp
constexpr timer_logic::Size keyHelpSize{380.0f, 230.0f};
constexpr timer_logic::Size supportSize{430.0f, 390.0f};
```

把两个绘制函数改为先调用：

```cpp
ImGui::SetNextWindowSize({size.width, size.height}, ImGuiCond_Always);
```

并删除 `AlwaysAutoResize`，避免定位所用尺寸与本帧实际尺寸不一致。新增：

```cpp
void DrawSideWindows(timer_logic::Bounds menuBounds) {
    if (!g_menuVisible || menuBounds.size.width <= 0.0f || menuBounds.size.height <= 0.0f) return;
    constexpr timer_logic::Size keyHelpSize{380.0f, 230.0f};
    constexpr timer_logic::Size supportSize{430.0f, 390.0f};
    const auto positions = timer_logic::SideWindowPositions(
        menuBounds, keyHelpSize, supportSize, 10.0f);
    const ImGuiStyle previousStyle = ImGui::GetStyle();
    ApplyMenuTheme(settings_store::Get().menu.theme);
    if (g_keyHelpOpen) DrawKeyHelpWindow(positions.left, keyHelpSize);
    if (g_supportOpen) DrawSupportWindow(positions.right, supportSize);
    ImGui::GetStyle() = previousStyle;
}
```

同步把两个绘制函数签名改为 `(Position, Size)`。这里不得调用 `ClampPosition`、`ComputeSnap` 或读取 viewport。

- [ ] **Step 6: 严格调整每帧层级并消除一帧延迟**

在 `Render()` 中保持六个功能窗口先绘制，参考线随后绘制；将菜单与侧窗改为连续调用：

```cpp
g_previousMovableWindows = movableWindows;
DrawSnapGuides(g_currentSnap, viewport, g_menuBounds, g_menuVisible, actualDragActive);
g_menuBounds = DrawMenuWindow();
DrawSideWindows(g_menuBounds);
settings_store::Tick();
```

`DrawMenuWindow()` 返回的正是本帧 `GetWindowPos/GetWindowSize` 经主菜单 Clamp 后的 Bounds；侧窗因此同帧跟随。主菜单因 U 或 X 变成不可见时 `DrawSideWindows` 不绘制；再次显示时只绘制仍为 `true` 的侧窗。参考线仍在主菜单前绘制并只用主菜单区域裁剪，侧窗不参与参考线计算。

- [ ] **Step 7: 验证主题恢复与配置结构未变化**

在 `settings_store_tests.cpp` 保留并新增：

```cpp
static_assert(sizeof(Settings) == 384);
static_assert(offsetof(Settings, menu) == 376);
```

Run:

```powershell
cmake --build "K:\ToolsBOX\out" --config Debug --target settings_store_tests payload
& "K:\ToolsBOX\out\payload\Debug\settings_store_tests.exe"
```

Expected: PASS；证明侧窗 bool 未进入 V7 Settings。代码检查确认 `DrawSideWindows` 保存整个 `ImGuiStyle`、应用当前 `menu.theme`、最后恢复，功能浮层继续使用下一帧 `ApplyCommonDarkStyle()`，且侧窗之后没有功能浮层绘制。

---

### Task 6: 给注入器加入 Windows 图标资源

**Files:**
- Create: `injector/resource.h`
- Create: `injector/resource.rc`
- Modify: `injector/CMakeLists.txt:1-3,42-46`

- [ ] **Step 1: 先验证当前 Release EXE 没有组图标资源**

Run:

```powershell
cmake --build "K:\ToolsBOX\out" --config Release --target PhasmophobiaInjector
$exe = "K:\ToolsBOX\out\bin\Release\PhasmophobiaInjector.exe"
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class ResourceProbe {
  public delegate bool EnumResNameProc(IntPtr module, IntPtr type, IntPtr name, IntPtr param);
  [DllImport("kernel32", CharSet=CharSet.Unicode, SetLastError=true)] public static extern IntPtr LoadLibraryEx(string file, IntPtr h, uint flags);
  [DllImport("kernel32", SetLastError=true)] public static extern bool EnumResourceNames(IntPtr module, IntPtr type, EnumResNameProc callback, IntPtr param);
  [DllImport("kernel32")] public static extern bool FreeLibrary(IntPtr module);
}
'@
$module = [ResourceProbe]::LoadLibraryEx($exe, [IntPtr]::Zero, 2)
$count = 0
$callback = [ResourceProbe+EnumResNameProc]{ param($m,$t,$n,$p) $script:count++; return $true }
[void][ResourceProbe]::EnumResourceNames($module, [IntPtr]14, $callback, [IntPtr]::Zero)
[void][ResourceProbe]::FreeLibrary($module)
if ($count -ne 0) { throw "expected no RT_GROUP_ICON before implementation" }
```

Expected: Release 构建成功，检查确认当前 `$count == 0`。

- [ ] **Step 2: 创建资源 ID 头和 RC**

`injector/resource.h`：

```cpp
#pragma once
#define IDI_PHASMOPHOBIA_INJECTOR 101
```

`injector/resource.rc`：

```rc
#include "resource.h"
IDI_PHASMOPHOBIA_INJECTOR ICON "../resource/favicon.ico"
```

- [ ] **Step 3: 只把 RC 加入 injector EXE**

将 injector project 语言与目标源改为：

```cmake
project(PhasmophobiaInjector LANGUAGES CXX RC)

add_executable(PhasmophobiaInjector
    main.cpp
    manual_pe.cpp
    resource.rc
    "${PAYLOAD_HEADER}"
)
```

保留 `LINK_FLAGS "/SUBSYSTEM:CONSOLE"`、`main.cpp`、输出名、启动参数和 payload target 不变；不要把 RC 加入 `payload`。

- [ ] **Step 4: 重建并验证组图标资源存在**

Run: 重复 Step 1 的构建与探测，将末尾断言改为：

```powershell
if ($count -lt 1) { throw "RT_GROUP_ICON not found in injector EXE" }
```

Expected: `$count >= 1`；`PhasmophobiaInjector.exe` 包含 RT_GROUP_ICON，payload DLL 未增加 RC 输入。

---

### Task 7: Debug、Release、CTest 与最终产物验收

**Files:**
- Verify: `K:\ToolsBOX\out\bin\Debug\PhasmophobiaInjector.exe`
- Verify: `K:\ToolsBOX\out\bin\Release\PhasmophobiaInjector.exe`

- [ ] **Step 1: 从默认 out 目录配置并构建完整 Debug**

Run:

```powershell
cmake -S "K:\ToolsBOX" -B "K:\ToolsBOX\out" -A x64
cmake --build "K:\ToolsBOX\out" --config Debug
```

Expected: 所有生成器、测试、payload、injector 成功；`K:\ToolsBOX\out\bin\Debug\PhasmophobiaInjector.exe` 存在。

- [ ] **Step 2: 运行 Debug CTest**

Run:

```powershell
ctest --test-dir "K:\ToolsBOX\out" -C Debug --output-on-failure
```

Expected: `timer_logic_tests`、`settings_store_tests` 与 `inventory_textures_tests` 全部通过，0 failed。

- [ ] **Step 3: 构建完整 Release**

Run:

```powershell
cmake --build "K:\ToolsBOX\out" --config Release
```

Expected: 完整 Release 构建成功；最终文件精确位于 `K:\ToolsBOX\out\bin\Release\PhasmophobiaInjector.exe`。

- [ ] **Step 4: 运行 Release CTest**

Run:

```powershell
ctest --test-dir "K:\ToolsBOX\out" -C Release --output-on-failure
```

Expected: 三个 CTest 测试全部通过，0 failed。

- [ ] **Step 5: 复核默认产物、PNG 数据与 EXE 图标**

Run:

```powershell
$exe = "K:\ToolsBOX\out\bin\Release\PhasmophobiaInjector.exe"
if (-not (Test-Path $exe)) { throw "missing default Release artifact: $exe" }
$header = Get-Content "K:\ToolsBOX\out\payload\generated\inventory_png_data.h" -Raw
if ($header -notmatch 'kSponsorPng' -or $header -notmatch 'array<GeneratedPng, 63> kInventoryPngs') { throw 'generated PNG resources invalid' }
```

随后重复 Task 6 Step 4 的 RT_GROUP_ICON 探测。

Expected: 默认 out Release EXE 存在；生成头包含独立赞助图和恰好 63 项物品表；EXE 至少有一个组图标资源。

- [ ] **Step 6: 做静态行为复核，不执行游戏注入**

逐项检查源码：形状 Combo 前紧邻 100px 宽度；菜单为 645×560、列 180、列表 Child 162；主菜单仍调用 `ClampPosition`，`SideWindowPositions` 和 `DrawSideWindows` 不 Clamp；左右窗口 bool 只在 `overlay.cpp`；侧窗绘制紧跟本帧 `DrawMenuWindow()`；支持图尺寸精确为 `{252.0f, 120.0f}`；支持窗口九项内容及按键窗口六项内容与规格完全一致；主题保存/恢复包围两个侧窗；RC 只属于 injector。

Expected: 全部满足。三窗实际跟随、主菜单 X/U 同步显隐、单独关闭后本次注入不恢复、三主题视觉、图片显示与侧窗允许越界标记为**游戏内人工验收项**；本计划不启动游戏、不执行注入。
