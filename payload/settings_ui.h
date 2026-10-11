#pragma once

#include "imgui.h"
#include "settings_store.h"

namespace settings_ui {

inline void LabelControlRow(const char* label) {
    ImGui::TextUnformatted(label);
    ImGui::SameLine(140.0f);
}

inline float Opacity(settings_store::FeatureId feature) {
    return settings_store::Get().featureVisuals[static_cast<std::size_t>(feature)].opacity;
}

inline ImVec4 ApplyOpacity(ImVec4 color, settings_store::FeatureId feature) {
    color.w *= Opacity(feature);
    return color;
}

inline ImU32 ApplyOpacity(ImU32 color, settings_store::FeatureId feature) {
    const int alpha = static_cast<int>(((color >> IM_COL32_A_SHIFT) & 0xFF) * Opacity(feature));
    return (color & ~IM_COL32_A_MASK) | (static_cast<ImU32>(alpha) << IM_COL32_A_SHIFT);
}

} // namespace settings_ui
