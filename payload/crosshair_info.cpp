#include "crosshair_info.h"

#include "feature_logic.h"
#include "font_store.h"
#include "il2cpp_runtime.h"
#include "log.h"
#include "settings_store.h"
#include "settings_ui.h"

#include "imgui.h"
#include "imgui_internal.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace crosshair_info {
namespace {

using GetComponentInParentFn = void* (*)(void*, void*, bool, const il2cpp_runtime::Method*);

struct Metadata {
    bool valid = false;
    std::uintptr_t canvasOffset = 0;
    std::uintptr_t interactionStateOffset = 0x88;
    std::vector<std::uintptr_t> colliderOffsets;
    std::uintptr_t equipmentStoreItemOffset = 0;
    std::uintptr_t storeItemEquipmentTypeOffset = 0;
    std::uintptr_t storeItemTierOffset = 0;
    std::uintptr_t photonPropOffset = 0;
    GetComponentInParentFn getInParent = nullptr;
    const il2cpp_runtime::Method* getInParentMethod = nullptr;
    void* photonType = nullptr;
    void* equipmentType = nullptr;
    void* lightType = nullptr;
    void* doorType = nullptr;
    void* drawerType = nullptr;
};

std::mutex g_snapshotMutex;
std::string g_text;
bool g_visible = false;
std::chrono::steady_clock::time_point g_sampledAt{};
std::atomic<bool> g_enabled{false};
std::atomic<bool> g_showTier{true};
bool g_resetPosition = false;
timer_logic::Size g_appliedWindowSize{};
bool g_hasAppliedWindowSize = false;
Metadata g_metadata{};
std::chrono::steady_clock::time_point g_nextResolve{};
constexpr const char* kWindowId = "##crosshair_info";
void SubmitPosition(timer_logic::Position position) {
    auto& settings = settings_store::Get().crosshairInfo;
    ImGui::SetWindowPos(kWindowId, {position.x, position.y}, ImGuiCond_Always);
    if (!(settings.position == position)) { settings.position = position; settings_store::MarkDirty(); }
}

void Publish(std::string text) {
    std::lock_guard<std::mutex> lock(g_snapshotMutex);
    g_text = std::move(text);
    g_visible = !g_text.empty();
    g_sampledAt = std::chrono::steady_clock::now();
}

void* ParentComponent(void* collider, void* type) {
    return collider && type && g_metadata.getInParent
        ? g_metadata.getInParent(collider, type, false, g_metadata.getInParentMethod) : nullptr;
}

void ResolveMetadata() {
    g_nextResolve = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    Metadata metadata{};
    auto* grabClass = il2cpp_runtime::FindClass("", "PCPropGrab");
    auto* canvasClass = il2cpp_runtime::FindClass("", "PCCanvas");
    auto* equipmentClass = il2cpp_runtime::FindClass("", "Equipment");
    auto* storeItemClass = il2cpp_runtime::FindClass("", "StoreItem");
    auto* photonClass = il2cpp_runtime::FindClass("", "PhotonObjectInteract");
    auto* propClass = il2cpp_runtime::FindClass("", "Prop");
    const bool fieldsResolved =
        il2cpp_runtime::FindInstanceFieldByType(grabClass, canvasClass, metadata.canvasOffset) &&
        il2cpp_runtime::FindInstanceFieldByType(equipmentClass, storeItemClass, metadata.equipmentStoreItemOffset) &&
        il2cpp_runtime::FindInstanceFieldByName(storeItemClass, "equipmentType", metadata.storeItemEquipmentTypeOffset) &&
        il2cpp_runtime::FindInstanceFieldByName(storeItemClass, "tier", metadata.storeItemTierOffset);
    il2cpp_runtime::FindUniqueInstanceEnumField(canvasClass, metadata.interactionStateOffset);
    il2cpp_runtime::FindInstanceFieldByType(photonClass, propClass, metadata.photonPropOffset);
    metadata.colliderOffsets = il2cpp_runtime::FindInstanceFieldsByType(grabClass,
        il2cpp_runtime::FindClass("UnityEngine", "Collider"));
    std::sort(metadata.colliderOffsets.begin(), metadata.colliderOffsets.end());
    auto* componentClass = il2cpp_runtime::FindClass("UnityEngine", "Component");
    metadata.getInParentMethod = il2cpp_runtime::FindMethod(componentClass, "GetComponentInParent", 2);
    metadata.getInParent = reinterpret_cast<GetComponentInParentFn>(il2cpp_runtime::GetMethodEntry(metadata.getInParentMethod));
    metadata.photonType = il2cpp_runtime::GetTypeObject(photonClass);
    metadata.equipmentType = il2cpp_runtime::GetTypeObject(equipmentClass);
    metadata.lightType = il2cpp_runtime::GetTypeObject(il2cpp_runtime::FindClass("", "LightSwitch"));
    metadata.doorType = il2cpp_runtime::GetTypeObject(il2cpp_runtime::FindClass("", "Door"));
    metadata.drawerType = il2cpp_runtime::GetTypeObject(il2cpp_runtime::FindClass("", "Drawer"));
    const auto entry = il2cpp_runtime::GetMethodEntry(metadata.getInParentMethod);
    metadata.valid = fieldsResolved && metadata.colliderOffsets.size() == 2 &&
        entry && il2cpp_runtime::IsInGameAssembly(entry) && il2cpp_runtime::IsExecutable(entry) &&
        metadata.photonType && metadata.equipmentType;
    if (!metadata.valid) {
        LOG("CrosshairInfo: critical field metadata unavailable");
        g_metadata = {};
        return;
    }
    LOG("CrosshairInfo: offsets canvas=0x%llX colliders=0x%llX/0x%llX storeItem=0x%llX equipmentType=0x%llX tier=0x%llX photonProp=0x%llX state=0x%llX",
        static_cast<unsigned long long>(metadata.canvasOffset),
        static_cast<unsigned long long>(metadata.colliderOffsets[0]),
        static_cast<unsigned long long>(metadata.colliderOffsets[1]),
        static_cast<unsigned long long>(metadata.equipmentStoreItemOffset),
        static_cast<unsigned long long>(metadata.storeItemEquipmentTypeOffset),
        static_cast<unsigned long long>(metadata.storeItemTierOffset),
        static_cast<unsigned long long>(metadata.photonPropOffset),
        static_cast<unsigned long long>(metadata.interactionStateOffset));
    g_metadata = metadata;
}

} // namespace

void Initialize() {
    g_appliedWindowSize = {};
    g_hasAppliedWindowSize = false;
    SyncSettings();
    ResolveMetadata();
}
void Shutdown() {
    ClearSnapshot();
    g_resetPosition = false;
    g_appliedWindowSize = {};
    g_hasAppliedWindowSize = false;
    g_metadata = {};
}
void SyncSettings() {
    const auto& settings = settings_store::Get().crosshairInfo;
    g_enabled.store(settings.enabled);
    g_showTier.store(settings.showTier);
}
void ClearSnapshot() {
    std::lock_guard<std::mutex> lock(g_snapshotMutex);
    g_text.clear();
    g_visible = false;
    g_sampledAt = {};
}
void RequestResetPosition() {
    settings_store::ClearOverlayPlacement(settings_store::OverlayPlacementId::CrosshairInfo);
    g_resetPosition = true;
}

void OnGameThreadUpdate(void* pcPropGrab) {
    if (!g_enabled.load() || !pcPropGrab) { Publish({}); return; }
    if (!g_metadata.valid && std::chrono::steady_clock::now() >= g_nextResolve) ResolveMetadata();
    if (!g_metadata.valid) { Publish({}); return; }
    const auto grab = reinterpret_cast<std::uintptr_t>(pcPropGrab);
    std::uintptr_t canvas = 0;
    std::int32_t state = 0;
    if (!il2cpp_runtime::ReadPointer(grab + g_metadata.canvasOffset, canvas) || !canvas ||
        !il2cpp_runtime::ReadInt32(canvas + g_metadata.interactionStateOffset, state) ||
        state < 1 || state > 3) { Publish({}); return; }

    std::uintptr_t collider = 0;
    const std::size_t preferred = state == 3 ? 1u : 0u;
    if (preferred < g_metadata.colliderOffsets.size())
        il2cpp_runtime::ReadPointer(grab + g_metadata.colliderOffsets[preferred], collider);
    if (!collider) {
        for (const auto offset : g_metadata.colliderOffsets)
            if (il2cpp_runtime::ReadPointer(grab + offset, collider) && collider) break;
    }
    if (!collider) { Publish({}); return; }
    void* component = reinterpret_cast<void*>(collider);
    if (void* equipment = ParentComponent(component, g_metadata.equipmentType)) {
        std::uintptr_t storeItem = 0;
        std::int32_t equipmentType = -1;
        std::int32_t tier = 0;
        if (il2cpp_runtime::ReadPointer(reinterpret_cast<std::uintptr_t>(equipment) + g_metadata.equipmentStoreItemOffset, storeItem) && storeItem &&
            il2cpp_runtime::ReadInt32(storeItem + g_metadata.storeItemEquipmentTypeOffset, equipmentType) &&
            il2cpp_runtime::ReadInt32(storeItem + g_metadata.storeItemTierOffset, tier)) {
            const std::string label = feature_logic::FormatEquipmentLabel(equipmentType, tier, g_showTier.load());
            if (!label.empty()) { Publish(label); return; }
        }
    }
    if (ParentComponent(component, g_metadata.lightType)) { Publish(u8"灯光开关"); return; }
    if (ParentComponent(component, g_metadata.drawerType)) { Publish(u8"抽屉"); return; }
    if (ParentComponent(component, g_metadata.doorType)) { Publish(u8"门"); return; }
    if (ParentComponent(component, g_metadata.photonType)) { Publish(u8"可交互物"); return; }
    Publish({});
}

timer_logic::MovableWindowResult DrawWindow(bool menuVisible, bool altDragActive) {
    (void)menuVisible;
    auto& settings = settings_store::Get().crosshairInfo;
    if (!settings.enabled) return {};
    std::string text;
    std::chrono::steady_clock::time_point sampledAt;
    bool visible = false;
    {
        std::lock_guard<std::mutex> lock(g_snapshotMutex);
        visible = g_visible;
        text = g_text;
        sampledAt = g_sampledAt;
    }
    const auto age = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - sampledAt).count();
    const bool freshTarget = visible && feature_logic::IsSnapshotFresh(age, 250);
    const bool canMove = feature_logic::CanMoveOverlay(settings.fixed, altDragActive);
    if (!freshTarget && !canMove) return {};
    if (!freshTarget) text = u8"准星信息";
    ImGui::SetNextWindowPos({settings.position.x, settings.position.y}, settings.fixed ? ImGuiCond_Always : ImGuiCond_FirstUseEver);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground |
        ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
    if (!canMove) flags |= ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoInputs;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.0f, 0.0f});
    ImGui::Begin(kWindowId, nullptr, flags);
    ImGui::PopStyleVar();
    const bool dragging = canMove && GImGui->MovingWindow == ImGui::GetCurrentWindow();
    ImGui::PushFont(font_store::Get(settings.fontSize));
    ImGui::TextColored({settings.color.r, settings.color.g, settings.color.b, settings.color.a}, "%s", text.c_str());
    ImGui::PopFont();
    const ImVec2 size = ImGui::GetWindowSize();
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    ImVec2 position = ImGui::GetWindowPos();
    const bool appearing = ImGui::IsWindowAppearing();
    const timer_logic::Size currentSize{size.x, size.y};
    if (g_resetPosition) {
        const auto initial = timer_logic::DefaultOverlayPosition(
            timer_logic::DefaultOverlaySlot::CenterContext,
            {display.x, display.y}, currentSize);
        ImGui::SetWindowPos({initial.x, initial.y});
        position = {initial.x, initial.y};
        settings.position = initial;
        g_appliedWindowSize = currentSize;
        g_hasAppliedWindowSize = true;
        g_resetPosition = false;
        settings_store::MarkDirty();
    } else {
        const auto resizeDecision = timer_logic::DecideAutoResize(
            appearing, g_hasAppliedWindowSize, g_appliedWindowSize, currentSize);
        if (resizeDecision.initializeBaseline) {
            g_appliedWindowSize = currentSize;
            g_hasAppliedWindowSize = true;
        } else if (resizeDecision.recenter) {
            const auto centered = timer_logic::CenterPositionForResize(
                {position.x, position.y}, g_appliedWindowSize, currentSize,
                {display.x, display.y});
            ImGui::SetWindowPos({centered.x, centered.y});
            position = {centered.x, centered.y};
            settings.position = centered;
            g_appliedWindowSize = currentSize;
            settings_store::MarkDirty();
        }
    }
    const auto clamped = timer_logic::ClampPosition(
        {position.x, position.y}, currentSize, {display.x, display.y});
    if (clamped.x != position.x || clamped.y != position.y)
        ImGui::SetWindowPos({clamped.x, clamped.y});
    ImGui::End();
    if (!(clamped == settings.position) &&
        (canMove || clamped.x != position.x || clamped.y != position.y)) {
        settings.position = clamped;
        settings_store::MarkDirty();
    }
    return {kWindowId, {clamped, currentSize}, canMove, dragging, &SubmitPosition};
}

void DrawSettings() {
    auto& settings = settings_store::Get().crosshairInfo;
    bool changed = false;
    settings_ui::LabelControlRow(u8"启用:"); changed |= ImGui::Checkbox("##crosshair_info_enabled", &settings.enabled);
    settings_ui::LabelControlRow(u8"固定:"); changed |= ImGui::Checkbox("##crosshair_info_fixed", &settings.fixed);
    settings_ui::LabelControlRow(u8"显示等级:"); changed |= ImGui::Checkbox("##crosshair_info_tier", &settings.showTier);
    int index = font_store::IndexOf(settings.fontSize);
    settings_ui::LabelControlRow(u8"字体大小:"); ImGui::SetNextItemWidth(100.0f);
    if (ImGui::Combo("##crosshair_info_font", &index, font_store::Labels(), static_cast<int>(font_store::kFontSizeCount))) {
        settings.fontSize = font_store::Sizes()[index]; changed = true;
    }
    settings_ui::LabelControlRow(u8"颜色:"); changed |= ImGui::ColorEdit4("##crosshair_info_color", &settings.color.r, ImGuiColorEditFlags_NoInputs);
    if (ImGui::Button(u8"重置默认设置##crosshair_info")) {
        const bool enabled = settings.enabled; settings = settings_store::CrosshairInfoSettings{}; settings.enabled = enabled;
        RequestResetPosition();
        settings_store::ResetFeatureHotkeys(settings_store::FeatureId::CrosshairInfo);
        changed = true;
    }
    if (changed) { SyncSettings(); settings_store::MarkDirty(); }
}

} // namespace crosshair_info
