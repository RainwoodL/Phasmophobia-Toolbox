#include "inventory_bar.h"

#include "feature_logic.h"
#include "font_store.h"
#include "il2cpp_runtime.h"
#include "inventory_resources.h"
#include "inventory_textures.h"
#include "log.h"
#include "settings_store.h"
#include "settings_ui.h"
#include "timer_logic.h"

#include "imgui.h"
#include "imgui_internal.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cfloat>
#include <cstdint>
#include <mutex>

namespace inventory_bar {
namespace {

using GetComponentInParentFn = void* (*)(void*, void*, bool, const il2cpp_runtime::Method*);

struct SnapshotItem {
    feature_logic::InventorySlotState state = feature_logic::InventorySlotState::Empty;
    inventory_resources::InventoryItemVisual visual{};
};

struct Snapshot {
    std::array<SnapshotItem, 4> items{};
    int activeIndex = -1;
};

struct Metadata {
    bool valid = false;
    std::uintptr_t inventoryListOffset = 0x50;
    std::uintptr_t activeSlotOffset = 0;
    std::uintptr_t equipmentStoreItemOffset = 0;
    std::uintptr_t storeItemEquipmentTypeOffset = 0;
    std::uintptr_t storeItemTierOffset = 0;
    GetComponentInParentFn getInParent = nullptr;
    const il2cpp_runtime::Method* getInParentMethod = nullptr;
    void* equipmentType = nullptr;
};

std::mutex g_snapshotMutex;
Snapshot g_snapshot{};
std::atomic<bool> g_enabled{false};
Metadata g_metadata{};
bool g_metadataFailureLogged = false;
std::chrono::steady_clock::time_point g_metadataNextRetry{};
bool g_resetPosition = false;
bool g_positionApplied = false;
feature_logic::InventoryLayout g_layout = feature_logic::InventoryLayout::Horizontal;
timer_logic::Size g_appliedContentSize{};
int g_lastListState = 0;
constexpr const char* kWindowId = "##inventory_bar";
void SubmitPosition(timer_logic::Position position) {
    auto& settings = settings_store::Get().inventoryBar;
    ImGui::SetWindowPos(kWindowId, {position.x, position.y}, ImGuiCond_Always);
    if (!(settings.position == position)) { settings.position = position; settings.positionInitialized = true; settings_store::MarkDirty(); }
}

void Publish(Snapshot snapshot) {
    std::lock_guard<std::mutex> lock(g_snapshotMutex);
    g_snapshot = std::move(snapshot);
}

void ResolveMetadata() {
    Metadata metadata{};
    auto* equipmentClass = il2cpp_runtime::FindClass("", "Equipment");
    auto* storeItemClass = il2cpp_runtime::FindClass("", "StoreItem");
    auto* grabClass = il2cpp_runtime::FindClass("", "PCPropGrab");
    auto* photonObjectClass = il2cpp_runtime::FindClass("", "PhotonObjectInteract");
    auto* componentClass = il2cpp_runtime::FindClass("UnityEngine", "Component");
    auto* intClass = il2cpp_runtime::FindClass("System", "Int32");
    std::size_t listVersionOffset = 0;
    il2cpp_runtime::FindInstanceListFieldByElementType(
        grabClass, photonObjectClass, metadata.inventoryListOffset, listVersionOffset);

    const bool fieldsResolved =
        il2cpp_runtime::FindInstanceFieldByType(equipmentClass, storeItemClass, metadata.equipmentStoreItemOffset) &&
        il2cpp_runtime::FindInstanceFieldByType(
            grabClass, intClass, metadata.activeSlotOffset) &&
        il2cpp_runtime::FindInstanceFieldByName(storeItemClass, "equipmentType", metadata.storeItemEquipmentTypeOffset) &&
        il2cpp_runtime::FindInstanceFieldByName(storeItemClass, "tier", metadata.storeItemTierOffset);
    metadata.getInParentMethod = il2cpp_runtime::FindMethod(componentClass, "GetComponentInParent", 2);
    metadata.getInParent = reinterpret_cast<GetComponentInParentFn>(il2cpp_runtime::GetMethodEntry(metadata.getInParentMethod));
    metadata.equipmentType = il2cpp_runtime::GetTypeObject(equipmentClass);
    metadata.valid = fieldsResolved && metadata.getInParent && metadata.equipmentType;
    if (!metadata.valid) {
        if (!g_metadataFailureLogged) LOG("InventoryBar: critical metadata unavailable; retrying in 2 seconds");
        g_metadataFailureLogged = true;
        g_metadataNextRetry = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        g_metadata = {};
        return;
    }
    LOG("InventoryBar: offsets list=0x%llX activeSlot=0x%llX storeItem=0x%llX equipmentType=0x%llX tier=0x%llX",
        static_cast<unsigned long long>(metadata.inventoryListOffset),
        static_cast<unsigned long long>(metadata.activeSlotOffset),
        static_cast<unsigned long long>(metadata.equipmentStoreItemOffset),
        static_cast<unsigned long long>(metadata.storeItemEquipmentTypeOffset),
        static_cast<unsigned long long>(metadata.storeItemTierOffset));
    g_metadataFailureLogged = false;
    g_metadata = metadata;
}

bool ReadSlots(std::uintptr_t list, std::array<std::uintptr_t, 4>& slots) {
    std::uintptr_t items = 0;
    std::int32_t size = 0;
    if (!list || !il2cpp_runtime::ReadPointer(list + 0x10, items) ||
        !il2cpp_runtime::ReadInt32(list + 0x18, size) || size < 0 || size > 16 || (!items && size != 0)) {
        if (g_lastListState != -1) LOG("InventoryBar: inventory list unavailable or size outside 0..16");
        g_lastListState = -1;
        return false;
    }
    const int count = (std::min)(size, 4);
    for (int i = 0; i < count; ++i) {
        if (!il2cpp_runtime::ReadPointer(items + 0x20 + static_cast<std::uintptr_t>(i) * sizeof(std::uintptr_t), slots[i])) {
            if (g_lastListState != -1) LOG("InventoryBar: inventory list array unreadable");
            g_lastListState = -1;
            slots = {};
            return false;
        }
    }
    g_lastListState = 1;
    return true;
}

SnapshotItem ResolveItem(std::uintptr_t photonObject) {
    feature_logic::InventorySlotEvidence evidence{};
    evidence.slotPresent = photonObject != 0;
    if (!evidence.slotPresent) return {};

    // PhotonObjectInteract is itself a Component; query its parent hierarchy directly.
    evidence.propValid = il2cpp_runtime::IsReadable(
        reinterpret_cast<void*>(photonObject), sizeof(std::uintptr_t));
    if (!evidence.propValid)
        return {feature_logic::ClassifyInventorySlot(evidence), {}};

    void* equipment = g_metadata.getInParent(
        reinterpret_cast<void*>(photonObject), g_metadata.equipmentType,
        false, g_metadata.getInParentMethod);
    evidence.equipmentFound = equipment != nullptr;
    if (!evidence.equipmentFound)
        return {feature_logic::ClassifyInventorySlot(evidence), {}};

    std::uintptr_t storeItem = 0;
    inventory_resources::InventoryItemVisual visual{};
    evidence.storeItemValid =
        il2cpp_runtime::ReadPointer(
            reinterpret_cast<std::uintptr_t>(equipment) + g_metadata.equipmentStoreItemOffset,
            storeItem) && storeItem &&
        il2cpp_runtime::ReadInt32(
            storeItem + g_metadata.storeItemEquipmentTypeOffset, visual.equipmentType) &&
        il2cpp_runtime::ReadInt32(
            storeItem + g_metadata.storeItemTierOffset, visual.tier);
    evidence.equipmentType = visual.equipmentType;
    evidence.tier = visual.tier;
    const auto state = feature_logic::ClassifyInventorySlot(evidence);
    return {state, state == feature_logic::InventorySlotState::Equipment
                       ? visual : inventory_resources::InventoryItemVisual{}};
}

ImVec2 LayoutSize(float slotSize, feature_logic::InventoryLayout layout) {
    const auto size = timer_logic::InventoryLayoutSize(
        slotSize, layout == feature_logic::InventoryLayout::Vertical);
    return {size.width, size.height};
}

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

} // namespace

void Initialize(ID3D11Device* device) {
    const auto& settings = settings_store::Get().inventoryBar;
    g_positionApplied = false;
    g_layout = settings.layout == 1
        ? feature_logic::InventoryLayout::Vertical
        : feature_logic::InventoryLayout::Horizontal;
    const ImVec2 contentSize = LayoutSize(settings.slotSize, g_layout);
    g_appliedContentSize = {contentSize.x, contentSize.y};
    g_resetPosition = false;
    g_lastListState = 0;
    g_metadata = {};
    g_metadataFailureLogged = false;
    g_metadataNextRetry = {};
    inventory_textures::Initialize(device);
    SyncSettings();
}

void Shutdown() {
    g_enabled.store(false);
    ClearSnapshot();
    inventory_textures::Shutdown();
    g_metadata = {};
    g_metadataFailureLogged = false;
    g_metadataNextRetry = {};
    g_resetPosition = false;
    g_positionApplied = false;
    g_appliedContentSize = {};
    g_lastListState = 0;
}

void SyncSettings() { g_enabled.store(settings_store::Get().inventoryBar.enabled); }
void ClearSnapshot() { Publish({}); }
void RequestResetPosition() {
    settings_store::ClearOverlayPlacement(settings_store::OverlayPlacementId::Inventory);
    g_resetPosition = true;
}

void OnGameThreadUpdate(void* pcPropGrab) {
    if (!g_enabled.load() || !pcPropGrab) { ClearSnapshot(); return; }
    if (!g_metadata.valid && std::chrono::steady_clock::now() >= g_metadataNextRetry) ResolveMetadata();
    if (!g_metadata.valid) { ClearSnapshot(); return; }

    Snapshot snapshot{};
    const auto grab = reinterpret_cast<std::uintptr_t>(pcPropGrab);
    std::uintptr_t list = 0;
    std::array<std::uintptr_t, 4> slots{};
    if (!il2cpp_runtime::ReadPointer(grab + g_metadata.inventoryListOffset, list) || !ReadSlots(list, slots)) {
        Publish(std::move(snapshot));
        return;
    }
    for (std::size_t i = 0; i < slots.size(); ++i)
        snapshot.items[i] = ResolveItem(slots[i]);

    std::int32_t rawActiveIndex = -1;
    if (il2cpp_runtime::ReadInt32(grab + g_metadata.activeSlotOffset, rawActiveIndex))
        snapshot.activeIndex = feature_logic::NormalizeInventoryActiveIndex(rawActiveIndex);
    Publish(std::move(snapshot));
}

timer_logic::MovableWindowResult DrawWindow(bool menuVisible, bool altDragActive) {
    (void)menuVisible;
    auto& settings = settings_store::Get().inventoryBar;
    if (!settings.enabled) return {};

    Snapshot snapshot{};
    {
        std::lock_guard<std::mutex> lock(g_snapshotMutex);
        snapshot = g_snapshot;
    }
    const bool canMove = feature_logic::CanMoveOverlay(settings.fixed, altDragActive);
    const auto desiredLayout = settings.layout == 1
        ? feature_logic::InventoryLayout::Vertical
        : feature_logic::InventoryLayout::Horizontal;
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const ImVec2 contentSize = LayoutSize(settings.slotSize, desiredLayout);
    if (!g_resetPosition && settings.positionInitialized &&
        (desiredLayout != g_layout ||
         g_appliedContentSize.width != contentSize.x ||
         g_appliedContentSize.height != contentSize.y)) {
        settings.position = timer_logic::CenterPositionForResize(
            settings.position, g_appliedContentSize, {contentSize.x, contentSize.y},
            {display.x, display.y});
        ImGui::SetNextWindowPos({settings.position.x, settings.position.y}, ImGuiCond_Always);
        g_layout = desiredLayout;
        g_appliedContentSize = {contentSize.x, contentSize.y};
        g_positionApplied = true;
        settings_store::MarkDirty();
    } else if (settings.positionInitialized && !g_positionApplied) {
        ImGui::SetNextWindowPos({settings.position.x, settings.position.y}, ImGuiCond_Always);
        g_positionApplied = true;
    }
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.0f, 0.0f});
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground |
        ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
    if (!canMove) flags |= ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoInputs;
    ImGui::Begin(kWindowId, nullptr, flags);
    ImGui::PopStyleVar();
    const bool dragging = canMove && GImGui->MovingWindow == ImGui::GetCurrentWindow();

    ImGui::Dummy(contentSize);
    const ImVec2 origin = ImGui::GetItemRectMin();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const float requestedKeyFontSize = timer_logic::InventoryKeyFontSize(settings.slotSize);
    const int keyFontIndex = font_store::IndexOf(requestedKeyFontSize);
    const float keyFontSize = font_store::Sizes()[keyFontIndex];
    ImFont* keyFont = font_store::Get(keyFontSize);
    constexpr float gap = 4.0f;
    for (int i = 0; i < 4; ++i) {
        const ImVec2 offset = g_layout == feature_logic::InventoryLayout::Horizontal
            ? ImVec2(i * (settings.slotSize + gap), 0.0f)
            : ImVec2(0.0f, i * (settings.slotSize + gap));
        const ImVec2 min{origin.x + offset.x, origin.y + offset.y};
        const ImVec2 max{min.x + settings.slotSize, min.y + settings.slotSize};
        const ImU32 slotBackground = settings_ui::ApplyOpacity(
            IM_COL32(16, 18, 24, 210), settings_store::FeatureId::Inventory);
        draw->AddRectFilled(min, max, slotBackground, 0.0f);
        const bool active = feature_logic::ShouldHighlightInventorySlot(snapshot.activeIndex, i);
        if (active) {
            const ImVec4 highlightFill{
                settings.highlightColor.r, settings.highlightColor.g,
                settings.highlightColor.b, settings.highlightColor.a * 0.18f};
            draw->AddRectFilled(min, max, settings_ui::ApplyOpacity(
                ImGui::ColorConvertFloat4ToU32(highlightFill), settings_store::FeatureId::Inventory), 0.0f);
        }
        const auto& item = snapshot.items[static_cast<std::size_t>(i)];
        if (item.state == feature_logic::InventorySlotState::Equipment) {
            if (const auto* texture = inventory_textures::Get(item.visual)) {
                const ImageRect image = FitImageRect(
                    min, settings.slotSize, texture->width, texture->height);
                draw->AddImage(reinterpret_cast<ImTextureID>(texture->shaderResourceView),
                               image.min, image.max, {0.0f, 0.0f}, {1.0f, 1.0f},
                               settings_ui::ApplyOpacity(IM_COL32_WHITE, settings_store::FeatureId::Inventory));
            }
        }
        const auto text = feature_logic::InventorySlotTextForState(item.state);
        if (text.firstLine && text.secondLine) {
            constexpr float labelFontSize = 16.0f;
            constexpr float lineGap = 2.0f;
            ImFont* labelFont = font_store::Get(labelFontSize);
            const ImVec2 firstSize = labelFont->CalcTextSizeA(
                labelFontSize, FLT_MAX, 0.0f, text.firstLine);
            const ImVec2 secondSize = labelFont->CalcTextSizeA(
                labelFontSize, FLT_MAX, 0.0f, text.secondLine);
            const float totalHeight = firstSize.y + lineGap + secondSize.y;
            const float firstY = min.y + (settings.slotSize - totalHeight) * 0.5f;
            const auto outlineOffsets = feature_logic::InventoryTextOutlineOffsets();
            const auto drawOutlinedText = [&](ImVec2 position, const char* line) {
                for (const auto offset : outlineOffsets) {
                    draw->AddText(labelFont, labelFontSize,
                                  {position.x + offset.x, position.y + offset.y},
                                  settings_ui::ApplyOpacity(IM_COL32(0, 0, 0, 255), settings_store::FeatureId::Inventory), line);
                }
                draw->AddText(labelFont, labelFontSize, position,
                              settings_ui::ApplyOpacity(IM_COL32_WHITE, settings_store::FeatureId::Inventory), line);
            };
            drawOutlinedText(
                {min.x + (settings.slotSize - firstSize.x) * 0.5f, firstY},
                text.firstLine);
            drawOutlinedText(
                {min.x + (settings.slotSize - secondSize.x) * 0.5f,
                 firstY + firstSize.y + lineGap},
                text.secondLine);
        }
        const char key[] = {static_cast<char>('1' + i), '\0'};
        draw->AddText(keyFont, keyFontSize, {min.x + 4.0f, min.y + 3.0f},
                      settings_ui::ApplyOpacity(IM_COL32_WHITE, settings_store::FeatureId::Inventory), key);
        const ImVec2 innerMin{min.x + 1.0f, min.y + 1.0f};
        const ImVec2 innerMax{max.x - 1.0f, max.y - 1.0f};
        draw->AddRect(innerMin, innerMax, settings_ui::ApplyOpacity(
            IM_COL32(255, 255, 255, 42), settings_store::FeatureId::Inventory), 0.0f, 0, 1.0f);
        const ImU32 outerBorder = active
            ? ImGui::ColorConvertFloat4ToU32({settings.highlightColor.r, settings.highlightColor.g,
                                              settings.highlightColor.b, settings.highlightColor.a})
            : IM_COL32(255, 255, 255, 115);
        draw->AddRect(min, max, settings_ui::ApplyOpacity(
            outerBorder, settings_store::FeatureId::Inventory), 0.0f, 0, active ? 2.0f : 1.0f);
    }

    ImVec2 windowPosition = ImGui::GetWindowPos();
    if (g_resetPosition || !settings.positionInitialized) {
        g_layout = feature_logic::InventoryLayout::Horizontal;
        settings.layout = 0;
        const ImVec2 resetSize = LayoutSize(settings.slotSize, g_layout);
        const auto initial = timer_logic::DefaultOverlayPosition(
            timer_logic::DefaultOverlaySlot::BottomCenter,
            {display.x, display.y}, {resetSize.x, resetSize.y});
        g_appliedContentSize = {resetSize.x, resetSize.y};
        ImGui::SetWindowPos({initial.x, initial.y});
        windowPosition = {initial.x, initial.y};
        settings.position = initial;
        settings.positionInitialized = true;
        g_positionApplied = true;
        g_resetPosition = false;
        settings_store::MarkDirty();
    }

    const ImVec2 finalSize = LayoutSize(settings.slotSize, g_layout);
    const auto clamped = timer_logic::ClampPosition(
        {windowPosition.x, windowPosition.y}, {finalSize.x, finalSize.y}, {display.x, display.y});
    if (clamped.x != windowPosition.x || clamped.y != windowPosition.y) ImGui::SetWindowPos({clamped.x, clamped.y});
    ImGui::End();
    if (clamped.x != settings.position.x || clamped.y != settings.position.y) {
        settings.position = clamped;
        settings_store::MarkDirty();
    }
    return {kWindowId, {clamped, {finalSize.x, finalSize.y}}, canMove, dragging, &SubmitPosition};
}

void DrawSettings() {
    auto& settings = settings_store::Get().inventoryBar;
    bool changed = false;
    settings_ui::LabelControlRow(u8"启用:"); changed |= ImGui::Checkbox("##inventory_enabled", &settings.enabled);
    settings_ui::LabelControlRow(u8"固定:"); changed |= ImGui::Checkbox("##inventory_fixed", &settings.fixed);
    settings_ui::LabelControlRow(u8"布局:");
    ImGui::SetNextItemWidth(100.0f);
    constexpr const char* layouts[] = {u8"横版", u8"竖版"};
    changed |= ImGui::Combo("##inventory_layout", &settings.layout, layouts, 2);
    settings_ui::LabelControlRow(u8"槽位大小:"); ImGui::SetNextItemWidth(100.0f);
    changed |= ImGui::SliderFloat("##inventory_slot_size", &settings.slotSize, 50.0f, 96.0f, "%.0f px");
    settings_ui::LabelControlRow(u8"高光颜色:");
    changed |= ImGui::ColorEdit4("##inventory_highlight", &settings.highlightColor.r, ImGuiColorEditFlags_NoInputs);
    if (ImGui::Button(u8"重置默认设置##inventory")) {
        const bool enabled = settings.enabled;
        settings = settings_store::InventoryBarSettings{};
        settings.enabled = enabled;
        g_layout = feature_logic::InventoryLayout::Horizontal;
        g_appliedContentSize = timer_logic::InventoryLayoutSize(settings.slotSize, false);
        g_positionApplied = false;
        RequestResetPosition();
        settings_store::ResetFeatureHotkeys(settings_store::FeatureId::Inventory);
        changed = true;
    }
    if (changed) { SyncSettings(); settings_store::MarkDirty(); }
}

} // namespace inventory_bar
