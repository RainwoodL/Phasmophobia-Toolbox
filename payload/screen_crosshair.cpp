#include "screen_crosshair.h"

#include "feature_logic.h"
#include "settings_store.h"
#include "settings_ui.h"

#include "imgui.h"

namespace screen_crosshair {

void Initialize() {}
void Shutdown() {}

void Draw() {
    const auto& settings = settings_store::Get().screenCrosshair;
    if (!settings.enabled) return;
    const ImVec2 center{ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.5f};
    const float radius = static_cast<float>(feature_logic::ClampCrosshairRadius(settings.radius));
    const ImU32 color = ImGui::ColorConvertFloat4ToU32(settings_ui::ApplyOpacity(
        {settings.color.r, settings.color.g, settings.color.b, settings.color.a},
        settings_store::FeatureId::ScreenCrosshair));
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    if (settings.shape == settings_store::CrosshairShape::FilledCircle) draw->AddCircleFilled(center, radius, color);
    else if (settings.shape == settings_store::CrosshairShape::HollowCircle) draw->AddCircle(center, radius, color, 0, 1.0f);
    else {
        draw->AddLine({center.x - radius, center.y}, {center.x + radius, center.y}, color, 1.0f);
        draw->AddLine({center.x, center.y - radius}, {center.x, center.y + radius}, color, 1.0f);
    }
}

void DrawSettings() {
    auto& settings = settings_store::Get().screenCrosshair;
    bool changed = false;
    settings_ui::LabelControlRow(u8"启用:");
    changed |= ImGui::Checkbox("##screen_crosshair_enabled", &settings.enabled);
    int shape = static_cast<int>(settings.shape);
    settings_ui::LabelControlRow(u8"形状:");
    ImGui::SetNextItemWidth(100.0f);
    changed |= ImGui::Combo("##screen_crosshair_shape", &shape, u8"实心圆\0空心圆\0十字\0");
    settings.shape = static_cast<settings_store::CrosshairShape>(shape);
    settings_ui::LabelControlRow(u8"半径:");
    ImGui::SetNextItemWidth(180.0f);
    changed |= ImGui::SliderInt("##screen_crosshair_radius", &settings.radius, 1, 30);
    settings_ui::LabelControlRow(u8"颜色:");
    changed |= ImGui::ColorEdit4("##screen_crosshair_color", &settings.color.r, ImGuiColorEditFlags_NoInputs);
    if (ImGui::Button(u8"重置默认设置##screen_crosshair")) {
        const bool enabled = settings.enabled;
        settings = settings_store::ScreenCrosshairSettings{};
        settings.enabled = enabled;
        settings_store::ResetFeatureHotkeys(settings_store::FeatureId::ScreenCrosshair);
        changed = true;
    }
    if (changed) settings_store::MarkDirty();
}

} // namespace screen_crosshair
