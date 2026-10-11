#include "overlay.h"

#include "crucifix_timer.h"
#include "crosshair_info.h"
#include "d3d11_hook.h"
#include "font_store.h"
#include "game_time.h"
#include "game_update_dispatcher.h"
#include "ghost_status.h"
#include "ghost_runtime.h"
#include "ghost_distance.h"
#include "ghost_speed_display.h"
#include "ghost_speed_sync.h"
#include "hunt_timer.h"
#include "il2cpp_runtime.h"
#include "inventory_bar.h"
#include "inventory_resources.h"
#include "inventory_textures.h"
#include "log.h"
#include "settings_store.h"
#include "settings_ui.h"
#include "smudge_timer.h"
#include "stamina_display.h"
#include "screen_crosshair.h"
#include "sanity_display.h"
#include "ouija_translation.h"
#include "player_nicknames.h"
#include "player_sanity_lifecycle.h"
#include "timer_logic.h"

#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"

namespace overlay {
namespace {

enum class MenuSection { MenuSettings, SmudgeTimer, GameTime, HuntTimer, GhostStatus, GhostSpeed, GhostDistance, CrucifixTimer, OuijaTranslation, CrosshairInfo, InventoryBar, StaminaDisplay, SanityDisplay, ScreenCrosshair, PlayerNicknames };

bool g_initialized = false;
bool g_menuVisible = true;
bool g_menuPositionApplied = false;
bool g_supportOpen = true;
int g_capturingHotkey = -1;
bool g_hotkeyConflictPending = false;
std::array<bool, 256> g_captureKeyDown{};
std::array<bool, static_cast<std::size_t>(settings_store::HotkeyAction::Count)> g_hotkeyDown{};
bool g_showLaunchPrompt = false;
bool g_showSponsorChoice = false;
bool g_showSponsorThanks = false;
bool g_sponsorModalActive = false;
inventory_textures::TextureView g_sponsorTexture{};
MenuSection g_selectedSection = MenuSection::MenuSettings;
const char* g_draggingWindowId = nullptr;
timer_logic::SnapState g_snapState{};
timer_logic::SnapResult g_currentSnap{};
std::array<timer_logic::MovableWindowResult, 12> g_previousMovableWindows{};
timer_logic::Size g_previousOverlayViewport{};
timer_logic::Bounds g_menuBounds{};
timer_logic::Size g_lastMenuViewport{};
std::chrono::steady_clock::time_point g_lastFrameBuild{};
ID3D11Device* g_device = nullptr;
ID3D11DeviceContext* g_context = nullptr;
IDXGISwapChain* g_swapChain = nullptr;
HWND g_hwnd = nullptr;
ID3D11RenderTargetView* g_renderTarget = nullptr;

smudge_timer::GameTimeValue ProvideSmudgeGameTime() {
    const auto snapshot = game_time::ReadSnapshot();
    return {snapshot.valid, snapshot.seconds};
}

ghost_status::GameTimeValue ProvideGhostGameTime() {
    const auto snapshot = game_time::ReadSnapshot();
    return {snapshot.valid, snapshot.seconds};
}

ghost_runtime::GameTimeValue ProvideGhostRuntimeGameTime() {
    const auto snapshot = game_time::ReadSnapshot();
    return {snapshot.valid, snapshot.seconds};
}

crucifix_timer::GameTimeValue ProvideCrucifixGameTime() {
    const auto snapshot = game_time::ReadSnapshot();
    return {snapshot.valid, snapshot.seconds};
}

void ResetTimerEvents() {
    smudge_timer::ResetEvent();
    crucifix_timer::ResetEvent();
    ghost_runtime::ResetSession();
    ghost_status::ResetSession();
    crosshair_info::ClearSnapshot();
    inventory_bar::ClearSnapshot();
    player_nicknames::ClearSnapshot();
    ghost_speed_display::ClearSnapshot();
    ghost_distance::ClearSnapshot();
    sanity_display::ClearSnapshot();
    ouija_translation::ClearSnapshot();
}

void ReleaseBackBufferRTV() {
    if (g_renderTarget) {
        g_renderTarget->Release();
        g_renderTarget = nullptr;
    }
}

ID3D11RenderTargetView* GetBackBufferRTV() {
    if (g_renderTarget) return g_renderTarget;
    if (!g_swapChain || !g_device) return nullptr;
    ID3D11Texture2D* backBuffer = nullptr;
    const HRESULT getBufferResult = g_swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&backBuffer));
    if (FAILED(getBufferResult) || !backBuffer) return nullptr;
    const HRESULT result = g_device->CreateRenderTargetView(backBuffer, nullptr, &g_renderTarget);
    backBuffer->Release();
    if (FAILED(result)) g_renderTarget = nullptr;
    return g_renderTarget;
}

struct MenuItem {
    MenuSection section;
    settings_store::FeatureId feature;
    settings_store::HotkeyAction toggleAction;
    const char* label;
    bool hasFeature;
};

constexpr MenuItem kMenuItems[] = {
    {MenuSection::MenuSettings, {}, {}, u8"菜单设置", false},
    {MenuSection::SmudgeTimer, settings_store::FeatureId::Smudge, settings_store::HotkeyAction::ToggleSmudge, u8"圣木计时器", true},
    {MenuSection::GameTime, settings_store::FeatureId::GameTime, settings_store::HotkeyAction::ToggleGameTime, u8"游戏时间", true},
    {MenuSection::HuntTimer, settings_store::FeatureId::HuntTimer, settings_store::HotkeyAction::ToggleHuntTimer, u8"猎杀计时器", true},
    {MenuSection::GhostStatus, settings_store::FeatureId::GhostStatus, settings_store::HotkeyAction::ToggleGhostStatus, u8"鬼魂状态", true},
    {MenuSection::GhostSpeed, settings_store::FeatureId::GhostSpeed, settings_store::HotkeyAction::ToggleGhostSpeed, u8"鬼魂移速", true},
    {MenuSection::GhostDistance, settings_store::FeatureId::GhostDistance, settings_store::HotkeyAction::ToggleGhostDistance, u8"鬼魂距离", true},
    {MenuSection::CrucifixTimer, settings_store::FeatureId::Crucifix, settings_store::HotkeyAction::ToggleCrucifix, u8"十字架计时器", true},
    {MenuSection::OuijaTranslation, settings_store::FeatureId::OuijaTranslation, settings_store::HotkeyAction::ToggleOuijaTranslation, u8"通灵板翻译", true},
    {MenuSection::CrosshairInfo, settings_store::FeatureId::CrosshairInfo, settings_store::HotkeyAction::ToggleCrosshairInfo, u8"准星信息", true},
    {MenuSection::InventoryBar, settings_store::FeatureId::Inventory, settings_store::HotkeyAction::ToggleInventory, u8"物品栏", true},
    {MenuSection::StaminaDisplay, settings_store::FeatureId::Stamina, settings_store::HotkeyAction::ToggleStamina, u8"体力显示", true},
    {MenuSection::SanityDisplay, settings_store::FeatureId::Sanity, settings_store::HotkeyAction::ToggleSanity, u8"理智显示", true},
    {MenuSection::ScreenCrosshair, settings_store::FeatureId::ScreenCrosshair, settings_store::HotkeyAction::ToggleScreenCrosshair, u8"屏幕准星", true},
    {MenuSection::PlayerNicknames, settings_store::FeatureId::PlayerNicknames, settings_store::HotkeyAction::TogglePlayerNicknames, u8"玩家昵称", true},
};

std::size_t Index(settings_store::FeatureId id) { return static_cast<std::size_t>(id); }
constexpr bool MenuCoversAllFeatures() {
    std::array<bool, static_cast<std::size_t>(settings_store::FeatureId::Count)> seen{};
    for (const auto& item : kMenuItems) {
        if (!item.hasFeature) continue;
        const auto index = static_cast<std::size_t>(item.feature);
        if (index >= seen.size() || seen[index]) return false;
        seen[index] = true;
    }
    for (const bool present : seen) if (!present) return false;
    return true;
}
static_assert(MenuCoversAllFeatures(), "Every feature must have exactly one menu entry");
std::size_t Index(settings_store::HotkeyAction action) { return static_cast<std::size_t>(action); }

const char* KeyName(int key) {
    static char name[32];
    if (!key) return u8"未设置";
    if (key == VK_MENU) return "Alt";
    if (key == VK_CONTROL) return "Ctrl";
    if (key == VK_SHIFT) return "Shift";
    const UINT scan = MapVirtualKeyA(static_cast<UINT>(key), MAPVK_VK_TO_VSC) << 16;
    if (GetKeyNameTextA(static_cast<LONG>(scan), name, sizeof(name)) > 0) return name;
    std::snprintf(name, sizeof(name), "VK %d", key);
    return name;
}

bool SameHotkey(const settings_store::HotkeyBinding& a, const settings_store::HotkeyBinding& b) {
    return a.key && a.key == b.key && a.ctrl == b.ctrl && a.shift == b.shift;
}

bool HasHotkeyConflict(settings_store::HotkeyAction action) {
    const auto& keys = settings_store::Get().hotkeys;
    for (std::size_t i = 0; i < keys.size(); ++i) {
        if (i != Index(action) && SameHotkey(keys[Index(action)], keys[i])) return true;
    }
    return false;
}

bool HotkeyDown(const settings_store::HotkeyBinding& binding) {
    if (!binding.key) return false;
    const bool ctrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
    const bool shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
    return (GetAsyncKeyState(binding.key) & 0x8000) != 0 && ctrl == binding.ctrl && shift == binding.shift;
}

bool HotkeyPressed(settings_store::HotkeyAction action, bool enabled) {
    const bool down = HotkeyDown(settings_store::Get().hotkeys[Index(action)]);
    const bool pressed = enabled && down && !g_hotkeyDown[Index(action)];
    g_hotkeyDown[Index(action)] = down;
    return pressed;
}

void DrawHotkeyControl(const char* label, const char* id, settings_store::HotkeyAction action) {
    auto& binding = settings_store::Get().hotkeys[Index(action)];
    const auto previous = binding;
    bool conflictAttempt = false;
    const int captureId = static_cast<int>(Index(action));
    std::string button = g_capturingHotkey == captureId ? u8"请按键...##" : std::string(KeyName(binding.key)) + "##";
    button += id;
    settings_ui::LabelControlRow(label);
    if (ImGui::Button(button.c_str(), {110.0f, 0.0f})) {
        g_capturingHotkey = captureId;
        for (int key = 0; key < 256; ++key)
            g_captureKeyDown[static_cast<std::size_t>(key)] = (GetAsyncKeyState(key) & 0x8000) != 0;
    }
    ImGui::SameLine();
    bool changed = ImGui::Checkbox((std::string("Ctrl##") + id).c_str(), &binding.ctrl);
    ImGui::SameLine();
    changed |= ImGui::Checkbox((std::string("Shift##") + id).c_str(), &binding.shift);
    ImGui::SameLine();
    if (ImGui::Button((std::string(u8"清空##") + id).c_str(), {54.0f, 0.0f})) {
        binding = {};
        g_capturingHotkey = -1;
        changed = true;
    }
    if (g_capturingHotkey == captureId) {
        for (int key = 8; key <= 0xFE; ++key) {
            if (key >= VK_LBUTTON && key <= VK_XBUTTON2) continue;
            if (key == VK_CONTROL || key == VK_SHIFT || key == VK_LCONTROL ||
                key == VK_RCONTROL || key == VK_LSHIFT || key == VK_RSHIFT) continue;
            const bool down = (GetAsyncKeyState(key) & 0x8000) != 0;
            const bool pressed = down && !g_captureKeyDown[static_cast<std::size_t>(key)];
            g_captureKeyDown[static_cast<std::size_t>(key)] = down;
            if (!pressed) continue;
            auto candidate = binding;
            candidate.key = key;
            bool conflict = false;
            for (std::size_t i = 0; i < settings_store::Get().hotkeys.size(); ++i)
                if (i != Index(action) && SameHotkey(candidate, settings_store::Get().hotkeys[i])) conflict = true;
            if (!conflict) { binding = candidate; changed = true; }
            else conflictAttempt = true;
            g_capturingHotkey = -1;
            g_captureKeyDown = {};
            break;
        }
        for (int key = 0; key < 256; ++key)
            g_captureKeyDown[static_cast<std::size_t>(key)] = (GetAsyncKeyState(key) & 0x8000) != 0;
    }
    if (conflictAttempt) g_hotkeyConflictPending = true;
    if (changed && HasHotkeyConflict(action)) {
        binding = previous;
        changed = false;
        g_hotkeyConflictPending = true;
    }
    if (changed) settings_store::MarkDirty();
}

void DrawFeatureCommonSettings(settings_store::FeatureId feature, settings_store::HotkeyAction toggleAction) {
        auto& visual = settings_store::Get().featureVisuals[Index(feature)];
    settings_ui::LabelControlRow(u8"透明度:");
    ImGui::SetNextItemWidth(180.0f);
    float percent = visual.opacity * 100.0f;
    if (ImGui::SliderFloat("##feature_opacity", &percent, 0.0f, 100.0f, "%.0f%%", ImGuiSliderFlags_AlwaysClamp)) {
        visual.opacity = percent / 100.0f;
        settings_store::MarkDirty();
    }
    DrawHotkeyControl(u8"快捷键:", "feature_toggle", toggleAction);
    if (feature == settings_store::FeatureId::Smudge) {
        DrawHotkeyControl(u8"手动计时:", "smudge_start", settings_store::HotkeyAction::SmudgeStart);
        DrawHotkeyControl(u8"清除记录:", "smudge_clear", settings_store::HotkeyAction::SmudgeClear);
    } else if (feature == settings_store::FeatureId::Crucifix) {
        DrawHotkeyControl(u8"清除记录:", "crucifix_clear", settings_store::HotkeyAction::CrucifixClear);
    }
}

template <typename Draw>
auto WithOpacity(settings_store::FeatureId feature, Draw draw) -> decltype(draw()) {
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, settings_store::Get().featureVisuals[Index(feature)].opacity);
    auto result = draw();
    ImGui::PopStyleVar();
    return result;
}

void DrawSectionList() {
    ImGui::BeginChild("##feature_list", ImVec2(162.0f, 0.0f), true);
    bool drewItem = false;
    for (const auto& item : kMenuItems) {
        if (drewItem) ImGui::Separator();
        if (ImGui::Selectable(item.label, g_selectedSection == item.section, 0, {0, 26})) g_selectedSection = item.section;
        drewItem = true;
    }
    ImGui::EndChild();
}

void SyncFeatureSettings() {
    game_update_dispatcher::SyncSettings();
    crosshair_info::SyncSettings();
    inventory_bar::SyncSettings();
    player_nicknames::SyncSettings();
    ghost_speed_display::SyncSettings();
    ghost_distance::SyncSettings();
    sanity_display::SyncSettings();
    ouija_translation::SyncSettings();
}

void SetAllFeaturesEnabled(bool enabled) {
    auto& settings = settings_store::Get();
    settings_store::SetAllFeaturesEnabled(settings, enabled);
    SyncFeatureSettings();
    settings_store::MarkDirty();
}

void ResetAllFeatureSettings() {
    auto& settings = settings_store::Get();
    settings_store::ResetAllFeatureSettings(settings);

    smudge_timer::RequestResetPosition();
    game_time::RequestResetPosition();
    hunt_timer::RequestResetPosition();
    ghost_status::RequestResetPosition();
    ghost_speed_display::RequestResetPosition();
    ghost_distance::RequestResetPosition();
    crucifix_timer::RequestResetPosition();
    crosshair_info::RequestResetPosition();
    inventory_bar::RequestResetPosition();
    stamina_display::RequestResetPosition();
    sanity_display::RequestResetPosition();
    ouija_translation::RequestResetPosition();
    SyncFeatureSettings();
    settings_store::MarkDirty();
}

void ResetHotkeys() {
    auto& settings = settings_store::Get();
    settings_store::ResetAllHotkeys(settings);
    g_hotkeyDown = {};
    g_capturingHotkey = -1;
    settings_store::MarkDirty();
}

void DrawHotkeyConflictPopup() {
    if (g_hotkeyConflictPending) {
        ImGui::OpenPopup(u8"快捷键冲突##hotkey_conflict");
        g_hotkeyConflictPending = false;
    }
    if (ImGui::BeginPopupModal(u8"快捷键冲突##hotkey_conflict", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted(u8"快捷键冲突，请重新设置。");
        ImGui::TextUnformatted(u8"相同按键和组合键不能分配给多个功能。");
        if (ImGui::Button(u8"好", {160.0f, 0.0f})) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void DrawMenuSettings() {
    if (settings_store::LastSaveFailed()) {
        ImGui::TextColored({1.0f, 0.4f, 0.3f, 1.0f}, u8"设置未能保存，正在自动重试");
        ImGui::TextWrapped(u8"请检查磁盘空间和文件夹写入权限");
    }
    auto& menu = settings_store::Get().menu;
    bool changed = false;
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float buttonWidth = (ImGui::GetContentRegionAvail().x - spacing * 3.0f) / 4.0f;
    if (ImGui::Button(u8"一键启动", {buttonWidth, 32.0f})) SetAllFeaturesEnabled(true);
    ImGui::SameLine();
    if (ImGui::Button(u8"一键关闭", {buttonWidth, 32.0f})) SetAllFeaturesEnabled(false);
    ImGui::SameLine();
    if (ImGui::Button(u8"一键重置", {buttonWidth, 32.0f})) ResetAllFeatureSettings();
    ImGui::SameLine();
    if (ImGui::Button(u8"键位重置", {buttonWidth, 32.0f})) ResetHotkeys();
    ImGui::Separator();
    settings_ui::LabelControlRow(u8"参考线:");
    changed |= ImGui::Checkbox("##menu_snap_guides", &menu.snapGuides);
    settings_ui::LabelControlRow(u8"输入拦截:");
    changed |= ImGui::Checkbox("##menu_block_game_input", &menu.blockGameInput);
    int theme = static_cast<int>(menu.theme);
    constexpr const char* themes[] = {u8"暗色", u8"亮色", u8"深蓝"};
    settings_ui::LabelControlRow(u8"主题颜色:");
    ImGui::SetNextItemWidth(120.0f);
    if (ImGui::Combo("##menu_theme", &theme, themes, 3)) {
        menu.theme = static_cast<settings_store::MenuTheme>(theme);
        changed = true;
    }
    int frameRate = static_cast<int>(menu.frameRate);
    constexpr const char* frameRates[] = {u8"低 (100ms)", u8"中 (33ms)", u8"高 (16ms)", u8"超高 (1ms)"};
    settings_ui::LabelControlRow(u8"调整帧率:");
    ImGui::SetNextItemWidth(120.0f);
    if (ImGui::Combo("##overlay_frame_rate", &frameRate, frameRates, 4)) {
        menu.frameRate = static_cast<settings_store::OverlayFrameRate>(frameRate);
        changed = true;
    }
    DrawHotkeyControl(u8"移动布局:", "layout_modifier", settings_store::HotkeyAction::LayoutModifier);
    ImGui::TextDisabled(u8"按住移动布局键，在组件上滚轮调整大小");
    if (changed) settings_store::MarkDirty();
}

std::chrono::milliseconds FrameBuildInterval(settings_store::OverlayFrameRate frameRate) {
    switch (frameRate) {
    case settings_store::OverlayFrameRate::Low: return std::chrono::milliseconds(100);
    case settings_store::OverlayFrameRate::Medium: return std::chrono::milliseconds(33);
    case settings_store::OverlayFrameRate::Ultra: return std::chrono::milliseconds(1);
    default: return std::chrono::milliseconds(16);
    }
}

void SubmitDrawData() {
    ID3D11RenderTargetView* renderTarget = GetBackBufferRTV();
    ImDrawData* drawData = ImGui::GetDrawData();
    if (!renderTarget || !drawData || !drawData->Valid) return;
    ID3D11RenderTargetView* previousTarget = nullptr;
    ID3D11DepthStencilView* previousDepth = nullptr;
    g_context->OMGetRenderTargets(1, &previousTarget, &previousDepth);
    g_context->OMSetRenderTargets(1, &renderTarget, nullptr);
    ImGui_ImplDX11_RenderDrawData(drawData);
    g_context->OMSetRenderTargets(1, &previousTarget, previousDepth);
    if (previousDepth) previousDepth->Release();
    if (previousTarget) previousTarget->Release();
}

void ApplyCommonDarkStyle() {
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 12.0f;
    style.FrameRounding = 7.0f;
    style.ChildRounding = 10.0f;
    style.WindowPadding = {14.0f, 14.0f};
    style.ItemSpacing = {10.0f, 10.0f};
}

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

timer_logic::Bounds DrawMenuWindow() {
    if (!g_menuVisible) return {};
    auto& settings = settings_store::Get();
    const ImGuiStyle previousStyle = ImGui::GetStyle();
    ApplyMenuTheme(settings.menu.theme);
    const ImGuiIO& io = ImGui::GetIO();
    constexpr timer_logic::Size menuSize{685.0f, 560.0f};
    if (g_lastMenuViewport.width != io.DisplaySize.x || g_lastMenuViewport.height != io.DisplaySize.y) {
        g_lastMenuViewport = {io.DisplaySize.x, io.DisplaySize.y};
        g_menuPositionApplied = false;
    }
    ImGui::SetNextWindowSize({menuSize.width, menuSize.height}, ImGuiCond_Always);
    if (!g_menuPositionApplied && settings.menuPlacement.initialized && io.DisplaySize.x > 0.0f && io.DisplaySize.y > 0.0f) {
        const timer_logic::Position restored{
            settings.menuPlacement.normalizedCenter.x * io.DisplaySize.x - menuSize.width * 0.5f,
            settings.menuPlacement.normalizedCenter.y * io.DisplaySize.y - menuSize.height * 0.5f};
        ImGui::SetNextWindowPos({restored.x, restored.y}, ImGuiCond_Always);
        g_menuPositionApplied = true;
    } else if (settings.menuPositionInitialized && !g_menuPositionApplied) {
        ImGui::SetNextWindowPos({settings.menuPosition.x, settings.menuPosition.y}, ImGuiCond_Always);
        g_menuPositionApplied = true;
    }
    ImGui::SetNextWindowBgAlpha(0.98f);
    const bool visible = ImGui::Begin(u8"抓鬼工具箱(U键 显示/隐藏 | 按住Alt拖动功能位置)", &g_menuVisible, ImGuiWindowFlags_NoCollapse);
    if (visible) {
        ImGui::Columns(2, nullptr, false);
        ImGui::SetColumnWidth(0, 180.0f);
        DrawSectionList();
        ImGui::NextColumn();
        ImGui::BeginChild("##feature_settings", {0, 0}, true);
        const char* sectionTitle = u8"菜单设置";
        if (g_selectedSection == MenuSection::SmudgeTimer) sectionTitle = u8"圣木计时器";
        else if (g_selectedSection == MenuSection::GameTime) sectionTitle = u8"游戏时间";
        else if (g_selectedSection == MenuSection::HuntTimer) sectionTitle = u8"猎杀计时器";
        else if (g_selectedSection == MenuSection::GhostStatus) sectionTitle = u8"鬼魂状态";
        else if (g_selectedSection == MenuSection::GhostSpeed) sectionTitle = u8"鬼魂移速";
        else if (g_selectedSection == MenuSection::GhostDistance) sectionTitle = u8"鬼魂距离";
        else if (g_selectedSection == MenuSection::CrucifixTimer) sectionTitle = u8"十字架计时器";
        else if (g_selectedSection == MenuSection::OuijaTranslation) sectionTitle = u8"通灵板翻译";
        else if (g_selectedSection == MenuSection::CrosshairInfo) sectionTitle = u8"准星信息";
        else if (g_selectedSection == MenuSection::InventoryBar) sectionTitle = u8"物品栏";
        else if (g_selectedSection == MenuSection::StaminaDisplay) sectionTitle = u8"体力显示";
        else if (g_selectedSection == MenuSection::SanityDisplay) sectionTitle = u8"理智显示";
        else if (g_selectedSection == MenuSection::ScreenCrosshair) sectionTitle = u8"屏幕准星";
        else if (g_selectedSection == MenuSection::PlayerNicknames) sectionTitle = u8"玩家昵称";
        ImGui::TextUnformatted(sectionTitle);
        ImGui::Separator();
        if (g_selectedSection == MenuSection::MenuSettings) DrawMenuSettings();
        else {
            for (const auto& item : kMenuItems) {
                if (item.hasFeature && item.section == g_selectedSection) {
                    DrawFeatureCommonSettings(item.feature, item.toggleAction);
                    break;
                }
            }
        }
        if (g_selectedSection == MenuSection::SmudgeTimer) smudge_timer::DrawSettings();
        else if (g_selectedSection == MenuSection::GameTime) game_time::DrawSettings();
        else if (g_selectedSection == MenuSection::HuntTimer) hunt_timer::DrawSettings();
        else if (g_selectedSection == MenuSection::GhostStatus) ghost_status::DrawSettings();
        else if (g_selectedSection == MenuSection::GhostSpeed) ghost_speed_display::DrawSettings();
        else if (g_selectedSection == MenuSection::GhostDistance) ghost_distance::DrawSettings();
        else if (g_selectedSection == MenuSection::CrucifixTimer) crucifix_timer::DrawSettings();
        else if (g_selectedSection == MenuSection::OuijaTranslation) ouija_translation::DrawSettings();
        else if (g_selectedSection == MenuSection::CrosshairInfo) crosshair_info::DrawSettings();
        else if (g_selectedSection == MenuSection::InventoryBar) inventory_bar::DrawSettings();
        else if (g_selectedSection == MenuSection::StaminaDisplay) stamina_display::DrawSettings();
        else if (g_selectedSection == MenuSection::SanityDisplay) sanity_display::DrawSettings();
        else if (g_selectedSection == MenuSection::ScreenCrosshair) screen_crosshair::DrawSettings();
        else if (g_selectedSection == MenuSection::PlayerNicknames) player_nicknames::DrawSettings();
        ImGui::EndChild();
        ImGui::Columns(1);
    }
    const ImVec2 actualSize = ImGui::GetWindowSize();
    ImVec2 actualPosition = ImGui::GetWindowPos();
    if (!settings.menuPositionInitialized) {
        const auto initial = timer_logic::DefaultRightCenter(
            {io.DisplaySize.x, io.DisplaySize.y}, {actualSize.x, actualSize.y});
        ImGui::SetWindowPos({initial.x, initial.y});
        actualPosition = {initial.x, initial.y};
        settings.menuPositionInitialized = true;
        g_menuPositionApplied = true;
        settings_store::MarkDirty();
    }
    const auto clamped = timer_logic::ClampPosition(
        {actualPosition.x, actualPosition.y}, {actualSize.x, actualSize.y}, {io.DisplaySize.x, io.DisplaySize.y});
    if (clamped.x != actualPosition.x || clamped.y != actualPosition.y) ImGui::SetWindowPos({clamped.x, clamped.y});
    ImGui::End();
    if (clamped.x != settings.menuPosition.x || clamped.y != settings.menuPosition.y) {
        settings.menuPosition = clamped;
        settings_store::MarkDirty();
    }
    if (io.DisplaySize.x > 0.0f && io.DisplaySize.y > 0.0f) {
        const timer_logic::Position normalized{
            std::clamp((clamped.x + actualSize.x * 0.5f) / io.DisplaySize.x, 0.0f, 1.0f),
            std::clamp((clamped.y + actualSize.y * 0.5f) / io.DisplaySize.y, 0.0f, 1.0f)};
        if (!settings.menuPlacement.initialized || !(settings.menuPlacement.normalizedCenter == normalized)) {
            settings.menuPlacement = {normalized, true};
            settings_store::MarkDirty();
        }
    }
    g_menuBounds = {clamped, {actualSize.x, actualSize.y}};
    ImGui::GetStyle() = previousStyle;
    return g_menuBounds;
}

void DrawSideWindowTitle(const char* title, bool& open, float titleHeight) {
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 window = ImGui::GetWindowPos();
    ImFont* titleFont = font_store::Get(20.0f);
    ImFont* subtitleFont = font_store::Get(16.0f);
    const ImU32 titleColor = ImGui::GetColorU32(ImGuiCol_Text);
    const ImU32 weakColor = ImGui::GetColorU32(ImGuiCol_TextDisabled);
    draw->AddText(titleFont, 20.0f, {window.x + 14.0f, window.y + 8.0f}, titleColor, title);
    draw->AddText(subtitleFont, 16.0f, {window.x + 14.0f, window.y + 34.0f},
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

void DrawSupportWindow(timer_logic::Position position,
                       const timer_logic::CompactSideWindowLayout& layout) {
    ImGui::SetNextWindowPos({position.x, position.y}, ImGuiCond_Always);
    ImGui::SetNextWindowSize({layout.supportSize.width, layout.supportSize.height}, ImGuiCond_Always);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings;
    if (ImGui::Begin("##support_side_window", nullptr, flags)) {
        ImGui::PushID("support");
        DrawSideWindowTitle(u8"支持作者", g_supportOpen, layout.titleHeight);
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
        ImGui::PopID();
    }
    ImGui::End();
}

void DrawSideWindows(timer_logic::Bounds menuBounds) {
    if (!g_menuVisible || menuBounds.size.width <= 0.0f || menuBounds.size.height <= 0.0f) return;
    const auto layout = timer_logic::GetCompactSideWindowLayout(menuBounds.size.height);
    const timer_logic::Position supportPosition{
        menuBounds.position.x + menuBounds.size.width + 10.0f,
        menuBounds.position.y + (menuBounds.size.height - layout.supportSize.height) * 0.5f};
    const ImGuiStyle previousStyle = ImGui::GetStyle();
    ApplyMenuTheme(settings_store::Get().menu.theme);
    if (g_supportOpen) DrawSupportWindow(supportPosition, layout);
    ImGui::GetStyle() = previousStyle;
}

void DrawSponsorModals() {
    if (g_showLaunchPrompt) {
        ImGui::OpenPopup(u8"支持提醒");
        g_showLaunchPrompt = false;
    }
    if (ImGui::BeginPopupModal(u8"支持提醒", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text(u8"你已经启动了%u次工具箱了", settings_store::Get().launchCount);
        ImGui::TextUnformatted(u8"可以给我赞助一下吗？");
        ImGui::TextDisabled(u8"（不强制赞助，仅每启动50次时会弹出）");
        if (ImGui::Button(u8"好", {120.0f, 0.0f})) {
            g_showSponsorChoice = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(u8"算了", {120.0f, 0.0f})) {
            g_sponsorModalActive = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if (g_showSponsorChoice) {
        ImGui::OpenPopup(u8"自愿赞助");
        g_showSponsorChoice = false;
    }
    if (ImGui::BeginPopupModal(u8"自愿赞助", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (g_sponsorTexture.shaderResourceView)
            ImGui::Image(reinterpret_cast<ImTextureID>(g_sponsorTexture.shaderResourceView), {504.0f, 240.0f});
        if (ImGui::Button(u8"已赞助", {160.0f, 0.0f})) {
            g_showSponsorThanks = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(u8"算了", {160.0f, 0.0f})) {
            g_sponsorModalActive = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if (g_showSponsorThanks) {
        ImGui::OpenPopup(u8"感谢支持");
        g_showSponsorThanks = false;
    }
    if (ImGui::BeginPopupModal(u8"感谢支持", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted(u8"感谢您的支持！");
        if (ImGui::Button(u8"好", {160.0f, 0.0f})) {
            g_sponsorModalActive = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void ClearSnap() {
    g_draggingWindowId = nullptr;
    g_snapState = {};
    g_currentSnap = {};
}

bool IsCurrentFeatureMovable(const char* id) {
    if (!id) return false;
    const auto& settings = settings_store::Get();
    if (std::strcmp(id, "##smudge_timer") == 0) return settings.smudge.enabled && !settings.smudge.fixed;
    if (std::strcmp(id, "##game_time") == 0) return settings.gameTime.enabled && !settings.gameTime.fixed;
    if (std::strcmp(id, "##hunt_timer") == 0) return settings.huntTimer.enabled && !settings.huntTimer.fixed;
    if (std::strcmp(id, "##ghost_status") == 0) return settings.ghostStatus.enabled && !settings.ghostStatus.fixed;
    if (std::strcmp(id, "##ghost_speed_display") == 0) return settings.ghostSpeedDisplay.enabled && !settings.ghostSpeedDisplay.fixed;
    if (std::strcmp(id, "##ghost_distance") == 0) return settings.ghostDistance.enabled && !settings.ghostDistance.fixed;
    if (std::strcmp(id, "##crucifix_timer") == 0) return settings.crucifix.enabled && !settings.crucifix.fixed;
    if (std::strcmp(id, "##crosshair_info") == 0) return settings.crosshairInfo.enabled && !settings.crosshairInfo.fixed;
    if (std::strcmp(id, "##inventory_bar") == 0) return settings.inventoryBar.enabled && !settings.inventoryBar.fixed;
    if (std::strcmp(id, "##stamina_display") == 0) return settings.staminaDisplay.enabled && !settings.staminaDisplay.fixed;
    if (std::strcmp(id, "##sanity_display") == 0) return settings.sanityDisplay.enabled && !settings.sanityDisplay.fixed;
    if (std::strcmp(id, "##ouija_translation") == 0) return settings.ouijaTranslation.enabled && !settings.ouijaTranslation.fixed;
    return false;
}

bool ApplyPreDrawSnap(timer_logic::Size viewport, bool enabled, bool altDragActive) {
    ImGuiWindow* movingWindow = GImGui->MovingWindow;
    if (!enabled || !altDragActive || !movingWindow || !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        ClearSnap();
        return false;
    }
    const char* movingId = movingWindow->Name;
    if (!IsCurrentFeatureMovable(movingId)) {
        ClearSnap();
        return false;
    }
    const auto* target = timer_logic::FindMovableWindowById(
        g_previousMovableWindows.data(), g_previousMovableWindows.size(), movingId);
    if (!target) {
        ClearSnap();
        return false;
    }
    if (!g_draggingWindowId || std::strcmp(g_draggingWindowId, movingId) != 0) g_snapState = {};
    g_draggingWindowId = target->id;
    const timer_logic::Bounds actualBounds{
        {movingWindow->Pos.x, movingWindow->Pos.y}, {movingWindow->Size.x, movingWindow->Size.y}};
    g_currentSnap = timer_logic::ComputeSnap(actualBounds, viewport, g_snapState, true);
    g_snapState = g_currentSnap.state;
    ImGui::SetWindowPos(movingWindow, {g_currentSnap.position.x, g_currentSnap.position.y}, ImGuiCond_Always);
    target->submitPosition(g_currentSnap.position);
    return true;
}

bool PlacementIdForWindow(const char* id, settings_store::OverlayPlacementId& placementId) {
    if (!id) return false;
    struct Mapping { const char* id; settings_store::OverlayPlacementId placementId; };
    constexpr Mapping mappings[] = {
        {"##smudge_timer", settings_store::OverlayPlacementId::Smudge},
        {"##game_time", settings_store::OverlayPlacementId::GameTime},
        {"##hunt_timer", settings_store::OverlayPlacementId::HuntTimer},
        {"##ghost_status", settings_store::OverlayPlacementId::GhostStatus},
        {"##crucifix_timer", settings_store::OverlayPlacementId::Crucifix},
        {"##crosshair_info", settings_store::OverlayPlacementId::CrosshairInfo},
        {"##inventory_bar", settings_store::OverlayPlacementId::Inventory},
        {"##stamina_display", settings_store::OverlayPlacementId::Stamina},
        {"##ghost_speed_display", settings_store::OverlayPlacementId::GhostSpeed},
        {"##ghost_distance", settings_store::OverlayPlacementId::GhostDistance},
        {"##sanity_display", settings_store::OverlayPlacementId::Sanity},
        {"##ouija_translation", settings_store::OverlayPlacementId::OuijaTranslation},
    };
    for (const auto& mapping : mappings) {
        if (std::strcmp(id, mapping.id) == 0) {
            placementId = mapping.placementId;
            return true;
        }
    }
    return false;
}

bool StabilizeMovableWindows(std::array<timer_logic::MovableWindowResult, 12>& windows,
                            timer_logic::Size viewport, bool altDragActive) {
    bool dragFinished = false;
    for (auto& window : windows) {
        if (!window.id || !window.submitPosition || window.bounds.size.width <= 0.0f ||
            window.bounds.size.height <= 0.0f) continue;
        settings_store::OverlayPlacementId placementId{};
        if (!PlacementIdForWindow(window.id, placementId)) continue;
        const auto* previous = timer_logic::FindMovableWindowById(
            g_previousMovableWindows.data(), g_previousMovableWindows.size(), window.id);
        const bool justReleased = previous && previous->dragging && !window.dragging &&
            viewport.width == g_previousOverlayViewport.width &&
            viewport.height == g_previousOverlayViewport.height &&
            window.bounds.size.width == previous->bounds.size.width &&
            window.bounds.size.height == previous->bounds.size.height &&
            settings_store::Get().overlayPlacements[static_cast<std::size_t>(placementId)].initialized;
        if ((altDragActive && window.dragging) || justReleased) {
            settings_store::RememberOverlayPosition(
                placementId, window.bounds.position, window.bounds.size, viewport);
            // Releasing Alt can make DrawWindow stop updating settings.position.
            // Commit both representations before the next fixed-window draw.
            window.submitPosition(window.bounds.position);
            dragFinished |= justReleased;
            continue;
        }
        const auto stable = settings_store::RestoreOverlayPosition(
            placementId, window.bounds.position, window.bounds.size, viewport);
        if (!(stable == window.bounds.position)) {
            window.submitPosition(stable);
            window.bounds.position = stable;
        }
    }
    g_previousOverlayViewport = viewport;
    return dragFinished;
}

bool PointInside(timer_logic::Position point, timer_logic::Bounds bounds) {
    return bounds.size.width > 0.0f && bounds.size.height > 0.0f &&
        point.x >= bounds.position.x && point.y >= bounds.position.y &&
        point.x <= bounds.position.x + bounds.size.width &&
        point.y <= bounds.position.y + bounds.size.height;
}

bool StepFontSize(float& fontSize, float wheel) {
    if (wheel == 0.0f) return false;
    const int oldIndex = font_store::IndexOf(fontSize);
    const int stepCount = (std::max)(1, static_cast<int>(std::round(std::fabs(wheel))));
    const int direction = wheel > 0.0f ? 1 : -1;
    const int newIndex = std::clamp(
        oldIndex + direction * stepCount, 0, static_cast<int>(font_store::kFontSizeCount) - 1);
    if (newIndex == oldIndex) return false;
    fontSize = font_store::Sizes()[newIndex];
    return true;
}

void ResizeTopmostWindowAtMouse(
    const std::array<timer_logic::MovableWindowResult, 12>& windows, bool active) {
    if (!active) return;
    const ImGuiIO& io = ImGui::GetIO();
    if (io.MouseWheel == 0.0f) return;
    const timer_logic::Position mouse{io.MousePos.x, io.MousePos.y};
    if (g_menuVisible && PointInside(mouse, g_menuBounds)) return;
    const timer_logic::MovableWindowResult* target = nullptr;
    if (GImGui->HoveredWindowBeforeClear) {
        target = timer_logic::FindMovableWindowById(
            windows.data(), windows.size(), GImGui->HoveredWindowBeforeClear->Name);
        if (!target || !target->movable) return;
    } else {
        target = timer_logic::FindTopmostMovableWindowAtPoint(
            windows.data(), windows.size(), mouse);
    }
    if (!target || !target->id) return;

    auto& settings = settings_store::Get();
    bool changed = false;
    if (std::strcmp(target->id, "##smudge_timer") == 0)
        changed = StepFontSize(settings.smudge.fontSize, io.MouseWheel);
    else if (std::strcmp(target->id, "##game_time") == 0)
        changed = StepFontSize(settings.gameTime.fontSize, io.MouseWheel);
    else if (std::strcmp(target->id, "##hunt_timer") == 0)
        changed = StepFontSize(settings.huntTimer.fontSize, io.MouseWheel);
    else if (std::strcmp(target->id, "##ghost_status") == 0)
        changed = StepFontSize(settings.ghostStatus.fontSize, io.MouseWheel);
    else if (std::strcmp(target->id, "##crucifix_timer") == 0)
        changed = StepFontSize(settings.crucifix.fontSize, io.MouseWheel);
    else if (std::strcmp(target->id, "##crosshair_info") == 0)
        changed = StepFontSize(settings.crosshairInfo.fontSize, io.MouseWheel);
    else if (std::strcmp(target->id, "##ghost_speed_display") == 0)
        changed = StepFontSize(settings.ghostSpeedDisplay.fontSize, io.MouseWheel);
    else if (std::strcmp(target->id, "##ghost_distance") == 0)
        changed = StepFontSize(settings.ghostDistance.fontSize, io.MouseWheel);
    else if (std::strcmp(target->id, "##sanity_display") == 0)
        changed = StepFontSize(settings.sanityDisplay.fontSize, io.MouseWheel);
    else if (std::strcmp(target->id, "##ouija_translation") == 0)
        changed = StepFontSize(settings.ouijaTranslation.fontSize, io.MouseWheel);
    else if (std::strcmp(target->id, "##inventory_bar") == 0) {
        const float oldSize = settings.inventoryBar.slotSize;
        const float delta = (io.MouseWheel > 0.0f ? 4.0f : -4.0f) *
            (std::max)(1.0f, std::round(std::fabs(io.MouseWheel)));
        settings.inventoryBar.slotSize = std::clamp(oldSize + delta, 50.0f, 96.0f);
        changed = settings.inventoryBar.slotSize != oldSize;
    } else if (std::strcmp(target->id, "##stamina_display") == 0) {
        const float oldHeight = settings.staminaDisplay.height;
        const float oldWidth = settings.staminaDisplay.width;
        float newHeight = oldHeight;
        if (StepFontSize(newHeight, io.MouseWheel)) {
            const float scale = newHeight / oldHeight;
            settings.staminaDisplay.height = std::clamp(newHeight, 16.0f, 56.0f);
            settings.staminaDisplay.width = std::clamp(oldWidth * scale, 100.0f, 500.0f);
            changed = settings.staminaDisplay.height != oldHeight ||
                settings.staminaDisplay.width != oldWidth;
        }
    }
    if (changed) settings_store::MarkDirty();
}

void DrawSnapGuides(const timer_logic::SnapResult& snap, timer_logic::Size viewport,
                    timer_logic::Bounds menuBounds, bool menuVisible, bool active) {
    if (!active) return;
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    constexpr ImU32 normal = IM_COL32(38, 198, 218, 72);
    constexpr ImU32 strong = IM_COL32(38, 198, 218, 210);
    const float centerX = viewport.width * 0.5f;
    const float centerY = viewport.height * 0.5f;
    const auto drawLines = [&] {
        draw->AddLine({centerX, 0.0f}, {centerX, viewport.height}, snap.xSnapped ? strong : normal,
                      snap.xSnapped ? 2.0f : 1.0f);
        draw->AddLine({0.0f, centerY}, {viewport.width, centerY}, snap.ySnapped ? strong : normal,
                      snap.ySnapped ? 2.0f : 1.0f);
        if (snap.pointSnapped) draw->AddCircleFilled({snap.point.x, snap.point.y}, 5.0f, strong);
    };
    if (!menuVisible || menuBounds.size.width <= 0.0f || menuBounds.size.height <= 0.0f) {
        drawLines();
        return;
    }
    const float left = menuBounds.position.x;
    const float top = menuBounds.position.y;
    const float right = left + menuBounds.size.width;
    const float bottom = top + menuBounds.size.height;
    for (const auto& clip : {ImVec4{0.0f, 0.0f, viewport.width, top},
                             ImVec4{0.0f, bottom, viewport.width, viewport.height},
                             ImVec4{0.0f, top, left, bottom},
                             ImVec4{right, top, viewport.width, bottom}}) {
        draw->PushClipRect({clip.x, clip.y}, {clip.z, clip.w}, true);
        drawLines();
        draw->PopClipRect();
    }
}

void UpdateMenuToggle(bool gameForeground) {
    if (HotkeyPressed(settings_store::HotkeyAction::MenuToggle, gameForeground && g_capturingHotkey < 0))
        g_menuVisible = !g_menuVisible;
}

void ToggleFeatureHotkeys(bool enabled) {
    auto& s = settings_store::Get();
    struct Toggle { settings_store::HotkeyAction action; bool* value; };
    const Toggle toggles[] = {
        {settings_store::HotkeyAction::ToggleSmudge, &s.smudge.enabled},
        {settings_store::HotkeyAction::ToggleGameTime, &s.gameTime.enabled},
        {settings_store::HotkeyAction::ToggleHuntTimer, &s.huntTimer.enabled},
        {settings_store::HotkeyAction::ToggleGhostStatus, &s.ghostStatus.enabled},
        {settings_store::HotkeyAction::ToggleGhostSpeed, &s.ghostSpeedDisplay.enabled},
        {settings_store::HotkeyAction::ToggleGhostDistance, &s.ghostDistance.enabled},
        {settings_store::HotkeyAction::ToggleCrucifix, &s.crucifix.enabled},
        {settings_store::HotkeyAction::ToggleOuijaTranslation, &s.ouijaTranslation.enabled},
        {settings_store::HotkeyAction::ToggleCrosshairInfo, &s.crosshairInfo.enabled},
        {settings_store::HotkeyAction::ToggleInventory, &s.inventoryBar.enabled},
        {settings_store::HotkeyAction::ToggleStamina, &s.staminaDisplay.enabled},
        {settings_store::HotkeyAction::ToggleSanity, &s.sanityDisplay.enabled},
        {settings_store::HotkeyAction::ToggleScreenCrosshair, &s.screenCrosshair.enabled},
        {settings_store::HotkeyAction::TogglePlayerNicknames, &s.playerNicknames.enabled},
    };
    bool changed = false;
    for (const auto& toggle : toggles) {
        if (HotkeyPressed(toggle.action, enabled && g_capturingHotkey < 0)) {
            *toggle.value = !*toggle.value;
            changed = true;
        }
    }
    if (changed) { SyncFeatureSettings(); settings_store::MarkDirty(); }
}

void DrawLayoutDetails(const std::array<timer_logic::MovableWindowResult, 12>& windows,
                       timer_logic::Size viewport, bool active) {
    if (!active) return;
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    const ImU32 border = IM_COL32(40, 220, 210, 230);
    const ImU32 guide = IM_COL32(40, 220, 210, 80);
    draw->AddLine({viewport.width * 0.5f, 0.0f}, {viewport.width * 0.5f, viewport.height}, guide);
    draw->AddLine({0.0f, viewport.height * 0.5f}, {viewport.width, viewport.height * 0.5f}, guide);
    const timer_logic::MovableWindowResult* previous = nullptr;
    for (const auto& window : windows) {
        if (!window.id || window.bounds.size.width <= 0.0f || window.bounds.size.height <= 0.0f) continue;
        const auto& b = window.bounds;
        const ImVec2 min{b.position.x, b.position.y};
        const ImVec2 max{b.position.x + b.size.width, b.position.y + b.size.height};
        draw->AddRect(min, max, border, 0.0f, 0, 1.5f);
        char text[96];
        std::snprintf(text, sizeof(text), "x %.0f  y %.0f  %.0f x %.0f",
                      b.position.x, b.position.y, b.size.width, b.size.height);
        draw->AddText({min.x, min.y - ImGui::GetFontSize() - 2.0f}, border, text);
        if (previous) {
            const float gapX = b.position.x - (previous->bounds.position.x + previous->bounds.size.width);
            const float gapY = b.position.y - (previous->bounds.position.y + previous->bounds.size.height);
            std::snprintf(text, sizeof(text), "dx %.0f  dy %.0f", gapX, gapY);
            draw->AddText({min.x, max.y + 2.0f}, border, text);
        }
        previous = &window;
    }
}

void InitializeImpl() {
    if (g_initialized) return;
    g_swapChain = d3d11_hook::GetSwapChain();
    g_device = d3d11_hook::GetDevice();
    g_context = d3d11_hook::GetContext();
    g_hwnd = d3d11_hook::GetWindowHandle();
    if (!g_swapChain || !g_device || !g_context || !g_hwnd) {
        LOG("Overlay: render objects are not ready swapChain=%p device=%p context=%p hwnd=%p",
            (void*)g_swapChain, (void*)g_device, (void*)g_context, (void*)g_hwnd);
        return;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ApplyCommonDarkStyle();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    if (!font_store::Initialize(io)) {
        LOG("Overlay: font_store::Initialize failed");
        font_store::Shutdown();
        ImGui::DestroyContext();
        return;
    }
    const bool win32Initialized = ImGui_ImplWin32_Init(g_hwnd);
    if (!win32Initialized) {
        LOG("Overlay: ImGui_ImplWin32_Init failed");
        font_store::Shutdown(); ImGui::DestroyContext(); return;
    }
    const bool dx11Initialized = ImGui_ImplDX11_Init(g_device, g_context);
    if (!dx11Initialized) {
        LOG("Overlay: ImGui_ImplDX11_Init failed");
        ImGui_ImplWin32_Shutdown(); font_store::Shutdown(); ImGui::DestroyContext(); return;
    }

    const bool loadedFromDisk = settings_store::Load();
    auto& persistedSettings = settings_store::Get();
    ++persistedSettings.launchCount;
    g_showLaunchPrompt = persistedSettings.launchCount % 50 == 0;
    g_sponsorModalActive = g_showLaunchPrompt;
    settings_store::MarkDirty();
    g_menuPositionApplied = false;
    g_lastMenuViewport = {};
    g_previousMovableWindows = {};
    g_previousOverlayViewport = {};
    il2cpp_runtime::Initialize();
    game_time::Initialize();
    ghost_runtime::Initialize(&ProvideGhostRuntimeGameTime);
    crucifix_timer::Initialize(&ProvideCrucifixGameTime);
    smudge_timer::Initialize(&ProvideSmudgeGameTime);
    ghost_status::Initialize(&ProvideGhostGameTime);
    ghost_speed_sync::Initialize();
    ghost_speed_display::Initialize();
    ghost_distance::Initialize();
    ouija_translation::Initialize();
    crosshair_info::Initialize();
    inventory_bar::Initialize(g_device);
    stamina_display::Initialize();
    sanity_display::Initialize();
    const auto* sponsor = inventory_resources::GetSponsorPng();
    if (sponsor && !inventory_textures::CreateFromMemory(
            g_device, sponsor->data, sponsor->size, g_sponsorTexture)) {
        LOG("Overlay: failed to create sponsor texture");
    }
    screen_crosshair::Initialize();
    player_nicknames::Initialize();
    game_update_dispatcher::Initialize(&crosshair_info::OnGameThreadUpdate, &player_nicknames::OnGameThreadUpdate,
                                       &inventory_bar::OnGameThreadUpdate, &ghost_speed_display::OnGameThreadUpdate,
                                       &ghost_distance::OnGameThreadUpdate, &sanity_display::OnGameThreadUpdate);
    player_sanity_lifecycle::Initialize(&ResetTimerEvents);
    d3d11_hook::OnBeforeResize(&ReleaseBackBufferRTV);
    g_lastFrameBuild = {};
    if (!loadedFromDisk) {
        smudge_timer::RequestResetPosition();
        game_time::RequestResetPosition();
    }
    if (settings_store::NeedsInitialNewFeatureLayout()) {
        ghost_status::RequestResetPosition();
        crucifix_timer::RequestResetPosition();
    }
    if (settings_store::NeedsInitialCrosshairInfoLayout()) crosshair_info::RequestResetPosition();
    if (settings_store::NeedsInitialInventoryLayout()) inventory_bar::RequestResetPosition();
    if (settings_store::NeedsInitialStaminaLayout()) stamina_display::RequestResetPosition();
    d3d11_hook::SetImguiContextReady(true);
    g_initialized = true;
    LOG("Initialize: overlay modules ready");
}

void ShutdownImpl() {
    if (g_initialized) ImGui::GetIO().MouseDrawCursor = false;
    d3d11_hook::SetImguiInputEnabled(false);
    d3d11_hook::SetGameInputBlocked(false);
    d3d11_hook::SetImguiContextReady(false);
    d3d11_hook::OnBeforeResize({});
    ReleaseBackBufferRTV();
    game_update_dispatcher::Shutdown();
    player_sanity_lifecycle::Shutdown();
    ouija_translation::Shutdown();
    ghost_distance::Shutdown();
    ghost_speed_display::Shutdown();
    ghost_speed_sync::Shutdown();
    player_nicknames::Shutdown();
    screen_crosshair::Shutdown();
    inventory_bar::Shutdown();
    stamina_display::Shutdown();
    sanity_display::Shutdown();
    crosshair_info::Shutdown();
    ghost_status::Shutdown();
    ghost_runtime::Shutdown();
    smudge_timer::Shutdown();
    crucifix_timer::Shutdown();
    game_time::Shutdown();
    il2cpp_runtime::Shutdown();
    settings_store::Flush();
    if (!g_initialized) return;
    inventory_textures::Release(g_sponsorTexture);
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    font_store::Shutdown();
    ImGui::DestroyContext();
    g_initialized = false;
    g_swapChain = nullptr;
    g_device = nullptr;
    g_context = nullptr;
    g_hwnd = nullptr;
}

} // namespace

void Initialize() { InitializeImpl(); }
void Shutdown() { ShutdownImpl(); }
bool IsInitialized() { return g_initialized; }
void Render() {
    if (!g_initialized) { InitializeImpl(); if (!g_initialized) return; }
    const bool gameForeground = timer_logic::IsGameForeground(
        reinterpret_cast<std::uintptr_t>(GetForegroundWindow()), reinterpret_cast<std::uintptr_t>(g_hwnd));
    const bool menuWasVisible = g_menuVisible;
    UpdateMenuToggle(gameForeground);
    SyncFeatureSettings();
    const auto& settings = settings_store::Get();
    ToggleFeatureHotkeys(gameForeground && !g_menuVisible);
    const bool altDown = gameForeground && HotkeyDown(
        settings.hotkeys[Index(settings_store::HotkeyAction::LayoutModifier)]);
    const bool hasMovableFeature = (settings.smudge.enabled && !settings.smudge.fixed) ||
                                   (settings.gameTime.enabled && !settings.gameTime.fixed) ||
                                   (settings.huntTimer.enabled && !settings.huntTimer.fixed) ||
                                   (settings.ghostStatus.enabled && !settings.ghostStatus.fixed) ||
                                   (settings.crucifix.enabled && !settings.crucifix.fixed) ||
                                    (settings.crosshairInfo.enabled && !settings.crosshairInfo.fixed) ||
                                     (settings.inventoryBar.enabled && !settings.inventoryBar.fixed) ||
                                      (settings.staminaDisplay.enabled && !settings.staminaDisplay.fixed) ||
                                       (settings.ghostSpeedDisplay.enabled && !settings.ghostSpeedDisplay.fixed) ||
                                       (settings.ghostDistance.enabled && !settings.ghostDistance.fixed) ||
                                       (settings.sanityDisplay.enabled && !settings.sanityDisplay.fixed) ||
                                       (settings.ouijaTranslation.enabled && !settings.ouijaTranslation.fixed);
    const bool altDragActive = altDown && hasMovableFeature;
    const bool interactionActive = (g_menuVisible && settings.menu.blockGameInput) ||
                                   altDragActive || g_sponsorModalActive || g_hotkeyConflictPending;
    ImGui::GetIO().MouseDrawCursor = interactionActive;
    smudge_timer::UpdateHotkeys(gameForeground && !interactionActive);
    crucifix_timer::UpdateHotkeys(gameForeground && !interactionActive);
    d3d11_hook::SetImguiInputEnabled(g_menuVisible || altDragActive || g_sponsorModalActive ||
                                     g_hotkeyConflictPending);
    d3d11_hook::SetGameInputBlocked(interactionActive);
    const auto now = std::chrono::steady_clock::now();
    const bool rebuildFrame = g_lastFrameBuild.time_since_epoch().count() == 0 ||
        menuWasVisible != g_menuVisible ||
        g_menuVisible || altDragActive || g_sponsorModalActive || g_hotkeyConflictPending ||
        now - g_lastFrameBuild >= FrameBuildInterval(settings.menu.frameRate);
    if (!rebuildFrame) {
        SubmitDrawData();
        return;
    }
    g_lastFrameBuild = now;
    if (font_store::HasPendingRequests()) {
        ImGui_ImplDX11_InvalidateDeviceObjects();
        const bool atlasUpdated = font_store::RebuildIfNeeded(ImGui::GetIO());
        const bool deviceObjectsCreated = ImGui_ImplDX11_CreateDeviceObjects();
        if (!atlasUpdated || !deviceObjectsCreated) LOG("Render: dynamic font atlas rebuild failed");
    }
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    ApplyCommonDarkStyle();
    const timer_logic::Size viewport{ImGui::GetIO().DisplaySize.x, ImGui::GetIO().DisplaySize.y};
    const bool guidesEnabled = settings.menu.snapGuides;
    const bool actualDragActive = ApplyPreDrawSnap(viewport, guidesEnabled, altDragActive);
    screen_crosshair::Draw();
    player_nicknames::Draw();
    ghost_runtime::Update();
    ghost_status::Update();
    std::array<timer_logic::MovableWindowResult, 12> movableWindows{
        WithOpacity(settings_store::FeatureId::Smudge, [&] { return smudge_timer::DrawWindow(g_menuVisible, altDragActive); }),
        WithOpacity(settings_store::FeatureId::GameTime, [&] { return game_time::DrawWindow(g_menuVisible, altDragActive); }),
        WithOpacity(settings_store::FeatureId::HuntTimer, [&] { return hunt_timer::DrawWindow(g_menuVisible, altDragActive); }),
        WithOpacity(settings_store::FeatureId::GhostStatus, [&] { return ghost_status::DrawWindow(g_menuVisible, altDragActive); }),
        WithOpacity(settings_store::FeatureId::Crucifix, [&] { return crucifix_timer::DrawWindow(g_menuVisible, altDragActive); }),
        WithOpacity(settings_store::FeatureId::CrosshairInfo, [&] { return crosshair_info::DrawWindow(g_menuVisible, altDragActive); }),
        inventory_bar::DrawWindow(g_menuVisible, altDragActive),
        stamina_display::DrawWindow(g_menuVisible, altDragActive),
        WithOpacity(settings_store::FeatureId::GhostSpeed, [&] { return ghost_speed_display::DrawWindow(g_menuVisible, altDragActive); }),
        WithOpacity(settings_store::FeatureId::GhostDistance, [&] { return ghost_distance::DrawWindow(g_menuVisible, altDragActive); }),
        WithOpacity(settings_store::FeatureId::Sanity, [&] { return sanity_display::DrawWindow(g_menuVisible, altDragActive); }),
        WithOpacity(settings_store::FeatureId::OuijaTranslation, [&] { return ouija_translation::DrawWindow(g_menuVisible, altDragActive); }),
    };
    ResizeTopmostWindowAtMouse(movableWindows, altDown);
    const bool dragFinished = StabilizeMovableWindows(movableWindows, viewport, altDragActive);
    g_previousMovableWindows = movableWindows;
    DrawSnapGuides(g_currentSnap, viewport, g_menuBounds, g_menuVisible, actualDragActive);
    DrawLayoutDetails(movableWindows, viewport, altDown);
    g_menuBounds = DrawMenuWindow();
    DrawHotkeyConflictPopup();
    DrawSideWindows(g_menuBounds);
    DrawSponsorModals();
    if (dragFinished || (menuWasVisible && !g_menuVisible)) settings_store::Flush();
    else settings_store::Tick();
    ImGui::Render();
    SubmitDrawData();
}

} // namespace overlay
