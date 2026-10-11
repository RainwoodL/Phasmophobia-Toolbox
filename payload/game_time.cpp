#include "game_time.h"

#include "feature_logic.h"
#include "font_store.h"
#include "il2cpp_runtime.h"
#include "settings_store.h"
#include "settings_ui.h"
#include "timer_logic.h"

#include "imgui.h"
#include "imgui_internal.h"

#include <cmath>
#include <cstdint>
#include <mutex>

namespace game_time {
namespace {

constexpr float kMaximumGameSeconds = 24.0f * 60.0f * 60.0f;
using GetInstanceFn = void* (*)(const void* methodInfo);

const il2cpp_runtime::Method* g_getInstanceMethod = nullptr;
GetInstanceFn g_getInstance = nullptr;
std::uintptr_t g_instance = 0;
std::size_t g_timeSpentInvestigatingOffset = 0;
bool g_resetPositionRequested = false;
std::recursive_mutex g_stateMutex;
timer_logic::Bounds g_lastWindowBounds{};
constexpr const char* kWindowId = "##game_time";
void SubmitPosition(timer_logic::Position position) {
    auto& settings = settings_store::Get().gameTime;
    ImGui::SetWindowPos(kWindowId, {position.x, position.y}, ImGuiCond_Always);
    if (!(settings.position == position)) { settings.position = position; settings_store::MarkDirty(); }
}

ImVec4 ToImVec4(timer_logic::Color color) { return {color.r, color.g, color.b, color.a}; }

bool ResolveGetter() {
    std::lock_guard<std::recursive_mutex> lock(g_stateMutex);
    if (g_getInstance) return true;
    auto* klass = il2cpp_runtime::FindClass("", "LevelStats");
    if (!il2cpp_runtime::FindInstanceFieldByName(
            klass, "timeSpentInvestigating", g_timeSpentInvestigatingOffset)) return false;
    g_getInstanceMethod = il2cpp_runtime::FindMethod(klass, "get_Instance", 0);
    void* entry = il2cpp_runtime::GetMethodEntry(g_getInstanceMethod);
    if (!entry || !il2cpp_runtime::IsInGameAssembly(entry) || !il2cpp_runtime::IsExecutable(entry)) return false;
    g_getInstance = reinterpret_cast<GetInstanceFn>(entry);
    return true;
}

timer_logic::MovableWindowResult DrawPositionedWindow(settings_store::GameTimeSettings& settings,
                                                       bool menuVisible, bool altDragActive) {
    (void)menuVisible;
    const ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos({settings.position.x, settings.position.y},
                            settings.fixed ? ImGuiCond_Always : ImGuiCond_FirstUseEver);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground |
                             ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                             ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
    const bool canMove = feature_logic::CanMoveOverlay(settings.fixed, altDragActive);
    if (!canMove) flags |= ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoInputs;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.0f, 0.0f});
    ImGui::Begin(kWindowId, nullptr, flags);
    ImGui::PopStyleVar();
    const bool dragging = canMove && GImGui->MovingWindow == ImGui::GetCurrentWindow();
    ImGui::PushFont(font_store::Get(settings.fontSize));
    const auto snapshot = ReadSnapshot();
    const std::string time = timer_logic::FormatMmSs(snapshot.valid ? timer_logic::NormalizeSeconds(snapshot.seconds) : 0);
    ImGui::TextColored(ToImVec4(settings.color), u8"游戏时间: %s", time.c_str());
    ImGui::PopFont();
    const ImVec2 windowSize = ImGui::GetWindowSize();
    if (g_resetPositionRequested) {
        const auto reset = timer_logic::DefaultOverlayPosition(
            timer_logic::DefaultOverlaySlot::TopCenter,
            {io.DisplaySize.x, io.DisplaySize.y}, {windowSize.x, windowSize.y});
        ImGui::SetWindowPos({reset.x, reset.y});
        settings.position = reset;
        g_resetPositionRequested = false;
        settings_store::MarkDirty();
    }
    const ImVec2 position = ImGui::GetWindowPos();
    const auto clamped = timer_logic::ClampPosition({position.x, position.y}, {windowSize.x, windowSize.y}, {io.DisplaySize.x, io.DisplaySize.y});
    if (clamped.x != position.x || clamped.y != position.y) ImGui::SetWindowPos({clamped.x, clamped.y});
    ImGui::End();
    g_lastWindowBounds = {clamped, {windowSize.x, windowSize.y}};

    if ((canMove || clamped.x != position.x || clamped.y != position.y) &&
        (clamped.x != settings.position.x || clamped.y != settings.position.y)) {
        settings.position = clamped;
        settings_store::MarkDirty();
    }
    return {kWindowId, {clamped, {windowSize.x, windowSize.y}}, canMove, dragging, &SubmitPosition};
}

} // namespace

void Initialize() {
    std::lock_guard<std::recursive_mutex> lock(g_stateMutex);
    ResolveGetter();
}
void Shutdown() {
    std::lock_guard<std::recursive_mutex> lock(g_stateMutex);
    g_getInstanceMethod = nullptr;
    g_getInstance = nullptr;
    g_instance = 0;
    g_timeSpentInvestigatingOffset = 0;
    g_lastWindowBounds = {};
}

GameTimeSnapshot ReadSnapshot() {
    std::lock_guard<std::recursive_mutex> lock(g_stateMutex);
    if (!ResolveGetter()) return {false, 0.0f};
    // A previous session's object can remain readable after singleton replacement.
    g_instance = reinterpret_cast<std::uintptr_t>(g_getInstance(g_getInstanceMethod));
    float seconds = 0.0f;
    if (!g_instance || !il2cpp_runtime::ReadFloat(g_instance + g_timeSpentInvestigatingOffset, seconds) ||
        !std::isfinite(seconds) || seconds < 0.0f || seconds > kMaximumGameSeconds) {
        g_instance = 0;
        return {false, 0.0f};
    }
    return {true, seconds};
}

timer_logic::Bounds LastWindowBounds() { return g_lastWindowBounds; }

void RequestResetPosition() {
    settings_store::ClearOverlayPlacement(settings_store::OverlayPlacementId::GameTime);
    g_resetPositionRequested = true;
}

timer_logic::MovableWindowResult DrawWindow(bool menuVisible, bool altDragActive) {
    auto& settings = settings_store::Get().gameTime;
    return settings.enabled ? DrawPositionedWindow(settings, menuVisible, altDragActive)
                            : timer_logic::MovableWindowResult{};
}

void DrawSettings() {
    auto& settings = settings_store::Get().gameTime;
    settings_ui::LabelControlRow(u8"启用:");
    bool changed = ImGui::Checkbox("##game_time_enabled", &settings.enabled);
    settings_ui::LabelControlRow(u8"固定:");
    changed |= ImGui::Checkbox("##game_time_fixed", &settings.fixed);
    int fontSizeIndex = font_store::IndexOf(settings.fontSize);
    settings_ui::LabelControlRow(u8"字体大小:");
    ImGui::SetNextItemWidth(100.0f);
    if (ImGui::Combo("##game_time_font_size", &fontSizeIndex, font_store::Labels(), static_cast<int>(font_store::kFontSizeCount))) {
        settings.fontSize = font_store::Sizes()[fontSizeIndex];
        changed = true;
    }
    settings_ui::LabelControlRow(u8"字体颜色:");
    changed |= ImGui::ColorEdit4("##game_time_font_color", &settings.color.r, ImGuiColorEditFlags_NoInputs);
    if (ImGui::Button(u8"重置默认设置##game_time")) {
        const bool enabled = settings.enabled;
        settings = settings_store::GameTimeSettings{};
        settings.enabled = enabled;
        RequestResetPosition();
        settings_store::ResetFeatureHotkeys(settings_store::FeatureId::GameTime);
        changed = true;
    }
    if (changed) settings_store::MarkDirty();
}

} // namespace game_time
