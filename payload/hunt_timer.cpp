#include "hunt_timer.h"

#include "feature_logic.h"
#include "font_store.h"
#include "game_time.h"
#include "ghost_status.h"
#include "settings_store.h"
#include "settings_ui.h"
#include "timer_logic.h"

#include "imgui.h"
#include "imgui_internal.h"

#include <cstdint>
#include <string>

namespace hunt_timer {
namespace {

constexpr const char* kWindowId = "##hunt_timer";
constexpr float kGameTimeGap = 6.0f;
bool g_resetPositionRequested = false;

ImVec4 ToImVec4(timer_logic::Color color) { return {color.r, color.g, color.b, color.a}; }

timer_logic::Position DefaultPosition(timer_logic::Size viewport, timer_logic::Size window) {
    const auto gameBounds = game_time::LastWindowBounds();
    if (settings_store::Get().gameTime.enabled &&
        gameBounds.size.width > 0.0f && gameBounds.size.height > 0.0f) {
        float y = gameBounds.position.y + gameBounds.size.height + kGameTimeGap;
        if (y + window.height > viewport.height)
            y = gameBounds.position.y - window.height - kGameTimeGap;
        return timer_logic::ClampPosition({gameBounds.position.x, y}, window, viewport);
    }
    return timer_logic::DefaultOverlayPosition(
        timer_logic::DefaultOverlaySlot::TopCenter, viewport, window);
}

void SubmitPosition(timer_logic::Position position) {
    auto& settings = settings_store::Get().huntTimer;
    ImGui::SetWindowPos(kWindowId, {position.x, position.y}, ImGuiCond_Always);
    if (!(settings.position == position) || !settings.positionInitialized) {
        settings.position = position;
        settings.positionInitialized = true;
        settings_store::MarkDirty();
    }
}

} // namespace

void RequestResetPosition() {
    settings_store::ClearOverlayPlacement(settings_store::OverlayPlacementId::HuntTimer);
    g_resetPositionRequested = true;
}

void ResetSettings() {
    settings_store::ResetFeatureSettings(settings_store::Get(), settings_store::FeatureId::HuntTimer);
    RequestResetPosition();
    settings_store::ResetFeatureHotkeys(settings_store::FeatureId::HuntTimer);
}

timer_logic::MovableWindowResult DrawWindow(bool menuVisible, bool altDragActive) {
    (void)menuVisible;
    auto& settings = settings_store::Get().huntTimer;
    if (!settings.enabled) return {};
    const ImGuiIO& io = ImGui::GetIO();
    const auto timing = ghost_status::ReadHuntTimingSnapshot();
    const std::string text = u8"猎杀时长: " + timer_logic::FormatMmSs(timing.durationSeconds);
    ImGui::PushFont(font_store::Get(settings.fontSize));
    const ImVec2 textSize = ImGui::CalcTextSize(text.c_str());
    ImGui::PopFont();
    const timer_logic::Size viewport{io.DisplaySize.x, io.DisplaySize.y};
    const timer_logic::Size measuredSize{textSize.x, textSize.y};
    const bool applyDefaultPosition = g_resetPositionRequested || !settings.positionInitialized;
    if (applyDefaultPosition) {
        // Reset both position representations; a stale anchor must not undo it.
        settings_store::ClearOverlayPlacement(settings_store::OverlayPlacementId::HuntTimer);
        settings.position = DefaultPosition(viewport, measuredSize);
        settings.positionInitialized = true;
        g_resetPositionRequested = false;
        settings_store::MarkDirty();
    }
    const bool canMove = feature_logic::CanMoveOverlay(settings.fixed, altDragActive);
    auto* existingWindow = ImGui::FindWindowByName(kWindowId);
    const bool moving = canMove && existingWindow && GImGui->MovingWindow == existingWindow;
    if (!moving || applyDefaultPosition) {
        const auto restored = settings_store::RestoreOverlayPosition(
            settings_store::OverlayPlacementId::HuntTimer, settings.position, measuredSize, viewport);
        ImGui::SetNextWindowPos({restored.x, restored.y}, ImGuiCond_Always);
    }
    // Resolve size and position before drawing, including the first enabled frame.
    ImGui::SetNextWindowSize(textSize, ImGuiCond_Always);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
    if (!canMove) flags |= ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoInputs;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.0f, 0.0f});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, {0.0f, 0.0f});
    ImGui::Begin(kWindowId, nullptr, flags);
    ImGui::PopStyleVar(2);
    const bool dragging = canMove && GImGui->MovingWindow == ImGui::GetCurrentWindow();

    ImGui::PushFont(font_store::Get(settings.fontSize));
    ImGui::TextColored(ToImVec4(settings.color), "%s", text.c_str());
    ImGui::PopFont();

    const ImVec2 windowSize = ImGui::GetWindowSize();
    const ImVec2 position = ImGui::GetWindowPos();
    const auto clamped = timer_logic::ClampPosition(
        {position.x, position.y}, {windowSize.x, windowSize.y},
        {io.DisplaySize.x, io.DisplaySize.y});
    if (clamped.x != position.x || clamped.y != position.y)
        ImGui::SetWindowPos({clamped.x, clamped.y});
    ImGui::End();
    SubmitPosition(clamped);
    return {kWindowId, {clamped, {windowSize.x, windowSize.y}}, canMove, dragging, &SubmitPosition};
}

void DrawSettings() {
    auto& settings = settings_store::Get().huntTimer;
    bool changed = false;
    settings_ui::LabelControlRow(u8"启用:");
    changed |= ImGui::Checkbox("##hunt_timer_enabled", &settings.enabled);
    settings_ui::LabelControlRow(u8"固定:");
    changed |= ImGui::Checkbox("##hunt_timer_fixed", &settings.fixed);
    int fontSizeIndex = font_store::IndexOf(settings.fontSize);
    settings_ui::LabelControlRow(u8"字体大小:");
    ImGui::SetNextItemWidth(100.0f);
    if (ImGui::Combo("##hunt_timer_font_size", &fontSizeIndex, font_store::Labels(),
                     static_cast<int>(font_store::kFontSizeCount))) {
        settings.fontSize = font_store::Sizes()[fontSizeIndex];
        changed = true;
    }
    settings_ui::LabelControlRow(u8"字体颜色:");
    changed |= ImGui::ColorEdit4("##hunt_timer_color", &settings.color.r,
                                 ImGuiColorEditFlags_NoInputs);
    if (ImGui::Button(u8"重置默认设置##hunt_timer")) {
        ResetSettings();
        changed = true;
    }
    ImGui::SameLine();
    if (ImGui::Button(u8"重置位置##hunt_timer_position")) RequestResetPosition();
    if (changed) settings_store::MarkDirty();
}

} // namespace hunt_timer
