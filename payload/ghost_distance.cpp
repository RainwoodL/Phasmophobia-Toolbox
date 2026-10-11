#include "ghost_distance.h"

#include "feature_logic.h"
#include "font_store.h"
#include "ghost_runtime.h"
#include "ghost_status.h"
#include "il2cpp_runtime.h"
#include "settings_store.h"
#include "settings_ui.h"

#include "imgui.h"
#include "imgui_internal.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <string>
#include <type_traits>

namespace ghost_distance {
namespace {

constexpr float kMaximumReasonableDistance = 1000.0f;
constexpr long long kSnapshotMaximumAgeMs = 500;
constexpr float kGhostSpeedGap = 20.0f;
constexpr const char* kWindowId = "##ghost_distance";

struct Vector3 { float x, y, z; };
using GetInstanceFn = void* (*)(const il2cpp_runtime::Method*);
using GetObjectFn = void* (*)(void*, const il2cpp_runtime::Method*);
using GetPositionFn = void (*)(void*, Vector3*, const il2cpp_runtime::Method*);

struct Api {
    GetInstanceFn networkInstance = nullptr;
    const il2cpp_runtime::Method* networkMethod = nullptr;
    GetObjectFn getTransform = nullptr;
    const il2cpp_runtime::Method* transformMethod = nullptr;
    GetPositionFn getPosition = nullptr;
    const il2cpp_runtime::Method* positionMethod = nullptr;
};

struct Metadata {
    bool valid = false;
    std::uintptr_t networkLocalPlayerOffset = 0;
    std::uintptr_t localPlayerPlayerOffset = 0;
};

struct Snapshot {
    bool valid = false;
    float meters = 0.0f;
    std::int64_t updatedMilliseconds = 0;
};

Api g_api{};
Metadata g_metadata{};
std::chrono::steady_clock::time_point g_nextResolve{};
std::mutex g_snapshotMutex;
Snapshot g_snapshot{};
std::atomic<bool> g_enabled{false};
bool g_resetPositionRequested = false;

std::int64_t NowMilliseconds() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

template <typename T>
bool ResolveMethod(T& function, const il2cpp_runtime::Method*& method,
                   const char* namespaceName, const char* className,
                   const char* methodName, int parameterCount, bool requireGameAssembly) {
    method = il2cpp_runtime::FindMethod(
        il2cpp_runtime::FindClass(namespaceName, className), methodName, parameterCount);
    void* entry = il2cpp_runtime::GetMethodEntry(method);
    if (!entry || !il2cpp_runtime::IsExecutable(entry) ||
        (requireGameAssembly && !il2cpp_runtime::IsInGameAssembly(entry))) return false;
    function = reinterpret_cast<std::decay_t<decltype(function)>>(entry);
    return true;
}

void ResolveApi() {
    g_nextResolve = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    Api api{};
    Metadata metadata{};
    auto* networkClass = il2cpp_runtime::FindClass("", "Network");
    auto* localPlayerClass = il2cpp_runtime::FindClass("", "LocalPlayer");
    auto* playerClass = il2cpp_runtime::FindClass("", "Player");
    const bool resolved =
        ResolveMethod(api.networkInstance, api.networkMethod, "", "Network",
                      "get_Instance", 0, true) &&
        ResolveMethod(api.getTransform, api.transformMethod, "UnityEngine",
                      "Component", "get_transform", 0, false) &&
        ResolveMethod(api.getPosition, api.positionMethod, "UnityEngine",
                      "Transform", "get_position_Injected", 1, false) &&
        il2cpp_runtime::FindInstanceFieldByType(
            networkClass, localPlayerClass, metadata.networkLocalPlayerOffset) &&
        il2cpp_runtime::FindInstanceFieldByType(
            localPlayerClass, playerClass, metadata.localPlayerPlayerOffset);
    metadata.valid = resolved;
    g_api = api;
    g_metadata = metadata;
}

bool PositionOf(std::uintptr_t component, Vector3& value) {
    if (!component || !g_api.getTransform || !g_api.getPosition) return false;
    void* transform = g_api.getTransform(reinterpret_cast<void*>(component), g_api.transformMethod);
    if (!transform) return false;
    g_api.getPosition(transform, &value, g_api.positionMethod);
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

void Publish(float meters) {
    std::lock_guard<std::mutex> lock(g_snapshotMutex);
    g_snapshot = {true, meters, NowMilliseconds()};
}

Snapshot ReadSnapshot() {
    std::lock_guard<std::mutex> lock(g_snapshotMutex);
    return g_snapshot;
}

void SubmitPosition(timer_logic::Position position) {
    auto& settings = settings_store::Get().ghostDistance;
    ImGui::SetWindowPos(kWindowId, {position.x, position.y}, ImGuiCond_Always);
    if (!(settings.position == position) || !settings.positionInitialized) {
        settings.position = position;
        settings.positionInitialized = true;
        settings_store::MarkDirty();
    }
}

timer_logic::Position DefaultPosition(timer_logic::Size viewport,
                                      timer_logic::Size distanceWindow) {
    const settings_store::GhostSpeedDisplaySettings speedDefaults{};
    ImGui::PushFont(font_store::Get(speedDefaults.fontSize));
    const ImVec2 speedText = ImGui::CalcTextSize(u8"鬼魂移速: 0.00m/s");
    ImGui::PopFont();
    const auto speedPosition = timer_logic::DefaultOverlayPosition(
        timer_logic::DefaultOverlaySlot::TopCenterLeft, viewport,
        {speedText.x, speedText.y});
    return {speedPosition.x - kGhostSpeedGap - distanceWindow.width, speedPosition.y};
}

ImVec4 ToImVec4(timer_logic::Color color) { return {color.r, color.g, color.b, color.a}; }

} // namespace

void Initialize() {
    ResolveApi();
    SyncSettings();
    ClearSnapshot();
}

void Shutdown() {
    g_enabled.store(false);
    ClearSnapshot();
    g_api = {};
    g_metadata = {};
    g_nextResolve = {};
}

void ClearSnapshot() {
    std::lock_guard<std::mutex> lock(g_snapshotMutex);
    g_snapshot = {};
}

void SyncSettings() { g_enabled.store(settings_store::Get().ghostDistance.enabled); }

void OnGameThreadUpdate() {
    if (!g_enabled.load() || !ghost_status::IsHunting()) {
        ClearSnapshot();
        return;
    }
    if (!g_metadata.valid && std::chrono::steady_clock::now() >= g_nextResolve) ResolveApi();
    const auto ghost = ghost_runtime::ReadSnapshot();
    if (!g_metadata.valid || !ghost.valid || !ghost.instance) {
        ClearSnapshot();
        return;
    }
    void* networkObject = g_api.networkInstance ? g_api.networkInstance(g_api.networkMethod) : nullptr;
    const auto network = reinterpret_cast<std::uintptr_t>(networkObject);
    std::uintptr_t localPlayer = 0;
    std::uintptr_t player = 0;
    if (!network ||
        !il2cpp_runtime::ReadPointer(
            network + g_metadata.networkLocalPlayerOffset, localPlayer) || !localPlayer ||
        !il2cpp_runtime::ReadPointer(
            localPlayer + g_metadata.localPlayerPlayerOffset, player) || !player) {
        ClearSnapshot();
        return;
    }
    Vector3 playerPosition{};
    Vector3 ghostPosition{};
    if (!PositionOf(player, playerPosition) || !PositionOf(ghost.instance, ghostPosition)) {
        ClearSnapshot();
        return;
    }
    const float dx = playerPosition.x - ghostPosition.x;
    const float dy = playerPosition.y - ghostPosition.y;
    const float dz = playerPosition.z - ghostPosition.z;
    const float meters = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (!std::isfinite(meters) || meters < 0.0f || meters > kMaximumReasonableDistance) {
        ClearSnapshot();
        return;
    }
    Publish(meters);
}

void RequestResetPosition() {
    settings_store::ClearOverlayPlacement(settings_store::OverlayPlacementId::GhostDistance);
    g_resetPositionRequested = true;
}

timer_logic::MovableWindowResult DrawWindow(bool menuVisible, bool altDragActive) {
    (void)menuVisible;
    auto& settings = settings_store::Get().ghostDistance;
    if (!settings.enabled) return {};
    const Snapshot snapshot = ReadSnapshot();
    const bool hunting = ghost_status::IsHunting();
    const bool fresh = snapshot.valid && feature_logic::IsSnapshotFresh(
        NowMilliseconds() - snapshot.updatedMilliseconds, kSnapshotMaximumAgeMs);
    const std::string distanceText = feature_logic::FormatGhostDistance(
        hunting, fresh, snapshot.meters);
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
    ImGui::TextColored(ToImVec4(settings.color), u8"鬼魂距离: %s", distanceText.c_str());
    ImGui::PopFont();
    const ImVec2 size = ImGui::GetWindowSize();
    if (g_resetPositionRequested || !settings.positionInitialized) {
        const auto reset = DefaultPosition(
            {io.DisplaySize.x, io.DisplaySize.y}, {size.x, size.y});
        ImGui::SetWindowPos({reset.x, reset.y});
        settings.position = reset;
        settings.positionInitialized = true;
        g_resetPositionRequested = false;
        settings_store::MarkDirty();
    }
    const ImVec2 position = ImGui::GetWindowPos();
    const auto clamped = timer_logic::ClampPosition(
        {position.x, position.y}, {size.x, size.y}, {io.DisplaySize.x, io.DisplaySize.y});
    if (clamped.x != position.x || clamped.y != position.y)
        ImGui::SetWindowPos({clamped.x, clamped.y});
    ImGui::End();
    if ((canMove || clamped.x != position.x || clamped.y != position.y) &&
        (!(settings.position == clamped) || !settings.positionInitialized)) {
        settings.position = clamped;
        settings.positionInitialized = true;
        settings_store::MarkDirty();
    }
    return {kWindowId, {clamped, {size.x, size.y}}, canMove, dragging, &SubmitPosition};
}

void DrawSettings() {
    auto& settings = settings_store::Get().ghostDistance;
    bool changed = false;
    settings_ui::LabelControlRow(u8"启用:");
    changed |= ImGui::Checkbox("##ghost_distance_enabled", &settings.enabled);
    settings_ui::LabelControlRow(u8"固定:");
    changed |= ImGui::Checkbox("##ghost_distance_fixed", &settings.fixed);
    int index = font_store::IndexOf(settings.fontSize);
    settings_ui::LabelControlRow(u8"字体大小:");
    ImGui::SetNextItemWidth(100.0f);
    if (ImGui::Combo("##ghost_distance_font_size", &index, font_store::Labels(),
                     static_cast<int>(font_store::kFontSizeCount))) {
        settings.fontSize = font_store::Sizes()[index];
        changed = true;
    }
    settings_ui::LabelControlRow(u8"字体颜色:");
    changed |= ImGui::ColorEdit4(
        "##ghost_distance_color", &settings.color.r, ImGuiColorEditFlags_NoInputs);
    if (ImGui::Button(u8"重置默认设置##ghost_distance")) {
        const bool enabled = settings.enabled;
        settings = settings_store::GhostDistanceSettings{};
        settings.enabled = enabled;
        RequestResetPosition();
        settings_store::ResetFeatureHotkeys(settings_store::FeatureId::GhostDistance);
        changed = true;
    }
    if (changed) settings_store::MarkDirty();
}

} // namespace ghost_distance
