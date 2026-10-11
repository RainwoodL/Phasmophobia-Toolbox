#include "stamina_display.h"

#include "feature_logic.h"
#include "font_store.h"
#include "il2cpp_runtime.h"
#include "settings_store.h"
#include "settings_ui.h"

#include "imgui.h"
#include "imgui_internal.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <mutex>

namespace stamina_display {
namespace {

constexpr std::uintptr_t kCurrentStaminaOffset = 0x40;
constexpr float kMaximumStamina = 3.0f;
constexpr const char* kWindowId = "##stamina_display";
using GetInstanceFn = void* (*)(const void* methodInfo);

const il2cpp_runtime::Method* g_instanceMethod = nullptr;
GetInstanceFn g_getInstance = nullptr;
bool g_resetPositionRequested = false;
std::recursive_mutex g_stateMutex;
std::uintptr_t g_localPlayerOffset = 0;
std::uintptr_t g_playerStaminaOffset = 0;

ImU32 Color(timer_logic::Color color) {
    return ImGui::ColorConvertFloat4ToU32(settings_ui::ApplyOpacity(
        {color.r, color.g, color.b, color.a}, settings_store::FeatureId::Stamina));
}

bool ResolveGetter() {
    std::lock_guard<std::recursive_mutex> lock(g_stateMutex);
    if (g_getInstance) return true;
    auto* klass = il2cpp_runtime::FindClass("", "Network");
    auto* localPlayerClass = il2cpp_runtime::FindClass("", "LocalPlayer");
    auto* staminaClass = il2cpp_runtime::FindClass("", "PlayerStamina");
    if (!il2cpp_runtime::FindInstanceFieldByType(klass, localPlayerClass, g_localPlayerOffset) ||
        !il2cpp_runtime::FindInstanceFieldByType(localPlayerClass, staminaClass, g_playerStaminaOffset)) return false;
    g_instanceMethod = il2cpp_runtime::FindMethod(klass, "get_Instance", 0);
    void* entry = il2cpp_runtime::GetMethodEntry(g_instanceMethod);
    if (!entry || !il2cpp_runtime::IsInGameAssembly(entry) || !il2cpp_runtime::IsExecutable(entry)) return false;
    g_getInstance = reinterpret_cast<GetInstanceFn>(entry);
    return true;
}

void SubmitPosition(timer_logic::Position position) {
    auto& settings = settings_store::Get().staminaDisplay;
    ImGui::SetWindowPos(kWindowId, {position.x, position.y}, ImGuiCond_Always);
    if (!(settings.position == position)) {
        settings.position = position;
        settings.positionInitialized = true;
        settings_store::MarkDirty();
    }
}

void DrawBar(const settings_store::StaminaDisplaySettings& settings, float percentage) {
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 size{settings.width, settings.height};
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const float rounding = std::max(2.0f, settings.height * 0.18f);
    draw->AddRectFilled(origin, {origin.x + size.x, origin.y + size.y}, settings_ui::ApplyOpacity(
        IM_COL32(0, 0, 0, 150), settings_store::FeatureId::Stamina), rounding);
    draw->AddRectFilled(origin, {origin.x + size.x * percentage, origin.y + size.y}, Color(settings.color), rounding);
    draw->AddRect(origin, {origin.x + size.x, origin.y + size.y}, Color(settings.color), rounding, 0, 1.0f);

    char text[16]{};
    std::snprintf(text, sizeof(text), "%d%%", static_cast<int>(std::lround(percentage * 100.0f)));
    ImFont* font = font_store::Get(timer_logic::SnapFontSize(settings.height));
    const ImVec2 textSize = font->CalcTextSizeA(font->FontSize, FLT_MAX, 0.0f, text);
    const ImVec2 textPosition{origin.x + (size.x - textSize.x) * 0.5f, origin.y + (size.y - textSize.y) * 0.5f};
    draw->AddText(font, font->FontSize, {textPosition.x + 1.0f, textPosition.y + 1.0f},
                  settings_ui::ApplyOpacity(IM_COL32(0, 0, 0, 220), settings_store::FeatureId::Stamina), text);
    draw->AddText(font, font->FontSize, textPosition,
                  settings_ui::ApplyOpacity(IM_COL32_WHITE, settings_store::FeatureId::Stamina), text);
    ImGui::Dummy(size);
}

} // namespace

void Initialize() { ResolveGetter(); }

void Shutdown() {
    std::lock_guard<std::recursive_mutex> lock(g_stateMutex);
    g_instanceMethod = nullptr;
    g_getInstance = nullptr;
    g_localPlayerOffset = 0;
    g_playerStaminaOffset = 0;
}

Snapshot ReadSnapshot() {
    std::lock_guard<std::recursive_mutex> lock(g_stateMutex);
    if (!ResolveGetter()) return {};
    const auto network = reinterpret_cast<std::uintptr_t>(g_getInstance(g_instanceMethod));
    std::uintptr_t localPlayer = 0;
    std::uintptr_t stamina = 0;
    float current = 0.0f;
    if (!network || !il2cpp_runtime::ReadPointer(network + g_localPlayerOffset, localPlayer) || !localPlayer ||
        !il2cpp_runtime::ReadPointer(localPlayer + g_playerStaminaOffset, stamina) || !stamina ||
        !il2cpp_runtime::ReadFloat(stamina + kCurrentStaminaOffset, current)) return {};
    const auto normalized = feature_logic::NormalizeStamina(current, kMaximumStamina);
    return {normalized.valid, normalized.current, kMaximumStamina, normalized.percentage};
}

void RequestResetPosition() {
    settings_store::ClearOverlayPlacement(settings_store::OverlayPlacementId::Stamina);
    g_resetPositionRequested = true;
}

timer_logic::MovableWindowResult DrawWindow(bool menuVisible, bool altDragActive) {
    (void)menuVisible;
    auto& settings = settings_store::Get().staminaDisplay;
    if (!settings.enabled) return {};
    const Snapshot snapshot = ReadSnapshot();
    if (!snapshot.valid) return {};

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
    DrawBar(settings, snapshot.percentage);

    const ImVec2 windowSize = ImGui::GetWindowSize();
    if (g_resetPositionRequested || !settings.positionInitialized) {
        const auto reset = timer_logic::DefaultOverlayPosition(
            timer_logic::DefaultOverlaySlot::BottomLeft,
            {io.DisplaySize.x, io.DisplaySize.y}, {windowSize.x, windowSize.y});
        ImGui::SetWindowPos({reset.x, reset.y});
        settings.position = reset;
        settings.positionInitialized = true;
        g_resetPositionRequested = false;
        settings_store::MarkDirty();
    }
    const ImVec2 position = ImGui::GetWindowPos();
    const auto clamped = timer_logic::ClampPosition(
        {position.x, position.y}, {windowSize.x, windowSize.y}, {io.DisplaySize.x, io.DisplaySize.y});
    if (clamped.x != position.x || clamped.y != position.y) ImGui::SetWindowPos({clamped.x, clamped.y});
    ImGui::End();
    if ((canMove || clamped.x != position.x || clamped.y != position.y) &&
        !(settings.position == clamped)) {
        settings.position = clamped;
        settings.positionInitialized = true;
        settings_store::MarkDirty();
    }
    return {kWindowId, {clamped, {windowSize.x, windowSize.y}}, canMove, dragging, &SubmitPosition};
}

void DrawSettings() {
    auto& settings = settings_store::Get().staminaDisplay;
    bool changed = false;
    settings_ui::LabelControlRow(u8"启用:");
    changed |= ImGui::Checkbox("##stamina_enabled", &settings.enabled);
    settings_ui::LabelControlRow(u8"固定:");
    changed |= ImGui::Checkbox("##stamina_fixed", &settings.fixed);
    settings_ui::LabelControlRow(u8"显示高度:");
    ImGui::SetNextItemWidth(180.0f);
    changed |= ImGui::SliderFloat("##stamina_height", &settings.height, 16.0f, 56.0f, "%.0f px");
    settings_ui::LabelControlRow(u8"显示宽度:");
    ImGui::SetNextItemWidth(180.0f);
    changed |= ImGui::SliderFloat("##stamina_width", &settings.width, 100.0f, 500.0f, "%.0f px");
    settings_ui::LabelControlRow(u8"显示颜色:");
    changed |= ImGui::ColorEdit4("##stamina_color", &settings.color.r, ImGuiColorEditFlags_NoInputs);
    if (ImGui::Button(u8"重置默认设置##stamina")) {
        const bool enabled = settings.enabled;
        settings = settings_store::StaminaDisplaySettings{};
        settings.enabled = enabled;
        RequestResetPosition();
        settings_store::ResetFeatureHotkeys(settings_store::FeatureId::Stamina);
        changed = true;
    }
    if (changed) settings_store::MarkDirty();
}

} // namespace stamina_display
