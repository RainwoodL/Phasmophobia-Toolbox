#include "ghost_speed_display.h"

#include "feature_logic.h"
#include "font_store.h"
#include "ghost_runtime.h"
#include "ghost_status.h"
#include "ghost_speed_sync.h"
#include "il2cpp_runtime.h"
#include "settings_store.h"
#include "settings_ui.h"

#include "imgui.h"
#include "imgui_internal.h"

#include <atomic>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <deque>
#include <mutex>
#include <type_traits>

namespace ghost_speed_display {
namespace {

constexpr float kMaximumReasonableSpeed = 20.0f;
constexpr const char* kWindowId = "##ghost_speed_display";
using GetSpeedFn = float (*)(void*, const il2cpp_runtime::Method*);
using GetObjectFn = void* (*)(void*, const il2cpp_runtime::Method*);
using GetBoolFn = bool (*)(void*, const il2cpp_runtime::Method*);
struct Vector3 { float x, y, z; };
using GetPositionFn = void (*)(void*, Vector3*, const il2cpp_runtime::Method*);
struct MovementSegment { float distance; float seconds; };

const il2cpp_runtime::Method* g_speedMethod = nullptr;
GetSpeedFn g_getSpeed = nullptr;
const il2cpp_runtime::Method* g_photonViewMethod = nullptr;
GetObjectFn g_getPhotonView = nullptr;
const il2cpp_runtime::Method* g_isMineMethod = nullptr;
GetBoolFn g_getIsMine = nullptr;
const il2cpp_runtime::Method* g_transformMethod = nullptr;
GetObjectFn g_getTransform = nullptr;
const il2cpp_runtime::Method* g_positionMethod = nullptr;
GetPositionFn g_getPosition = nullptr;
std::mutex g_snapshotMutex;
float g_lastSpeed = 0.0f;
float g_displaySpeed = 0.0f;
std::chrono::steady_clock::time_point g_nextDisplayUpdate{};
std::uint64_t g_generation = 0;
std::uintptr_t g_sampledGhost = 0;
Vector3 g_previousPosition{};
std::chrono::steady_clock::time_point g_previousPositionTime{};
bool g_positionInitialized = false;
bool g_smoothedSpeedInitialized = false;
bool g_wasHunting = false;
std::deque<MovementSegment> g_movementWindow;
float g_windowDistance = 0.0f;
float g_windowSeconds = 0.0f;
float g_filteredSpeed = 0.0f;
std::atomic<bool> g_enabled{false};
std::uintptr_t g_navMeshAgentOffset = 0;
bool g_resetPositionRequested = false;

bool ResolveApi() {
    if (g_navMeshAgentOffset && g_getSpeed && g_getPhotonView && g_getIsMine && g_getTransform && g_getPosition) return true;
    auto* ghostClass = il2cpp_runtime::FindClass("", "GhostAI");
    auto* agentClass = il2cpp_runtime::FindClass("UnityEngine.AI", "NavMeshAgent");
    if (!il2cpp_runtime::FindInstanceFieldByType(ghostClass, agentClass, g_navMeshAgentOffset)) return false;
    const auto resolve = [](auto& function, const il2cpp_runtime::Method*& method,
                            const char* namespaceName, const char* className, const char* methodName, int parameterCount) {
        method = il2cpp_runtime::FindMethod(
            il2cpp_runtime::FindClass(namespaceName, className), methodName, parameterCount);
        void* entry = il2cpp_runtime::GetMethodEntry(method);
        if (!entry || !il2cpp_runtime::IsInGameAssembly(entry) || !il2cpp_runtime::IsExecutable(entry)) return false;
        function = reinterpret_cast<std::decay_t<decltype(function)>>(entry);
        return true;
    };
    return resolve(g_getSpeed, g_speedMethod, "UnityEngine.AI", "NavMeshAgent", "get_speed", 0) &&
        resolve(g_getPhotonView, g_photonViewMethod, "Photon.Pun", "MonoBehaviourPun", "get_photonView", 0) &&
        resolve(g_getIsMine, g_isMineMethod, "Photon.Pun", "PhotonView", "get_IsMine", 0) &&
        resolve(g_getTransform, g_transformMethod, "UnityEngine", "Component", "get_transform", 0) &&
        resolve(g_getPosition, g_positionMethod, "UnityEngine", "Transform", "get_position_Injected", 1);
}

void ResetMovementSampleLocked(std::uint64_t generation, std::uintptr_t ghost) {
    g_generation = generation;
    g_sampledGhost = ghost;
    g_previousPosition = {};
    g_previousPositionTime = {};
    g_positionInitialized = false;
    g_smoothedSpeedInitialized = false;
    g_movementWindow.clear();
    g_windowDistance = 0.0f;
    g_windowSeconds = 0.0f;
    g_filteredSpeed = 0.0f;
    g_lastSpeed = 0.0f;
    g_displaySpeed = 0.0f;
    g_nextDisplayUpdate = {};
}

void SubmitPosition(timer_logic::Position position) {
    auto& settings = settings_store::Get().ghostSpeedDisplay;
    ImGui::SetWindowPos(kWindowId, {position.x, position.y}, ImGuiCond_Always);
    if (!(settings.position == position) || !settings.positionInitialized) {
        settings.position = position;
        settings.positionInitialized = true;
        settings_store::MarkDirty();
    }
}

ImVec4 ToImVec4(timer_logic::Color color) { return {color.r, color.g, color.b, color.a}; }

} // namespace

void Initialize() {
    ResolveApi();
    SyncSettings();
    const auto ghost = ghost_runtime::ReadSnapshot();
    std::lock_guard<std::mutex> lock(g_snapshotMutex);
    g_generation = ghost.generation;
    g_lastSpeed = 0.0f;
    g_displaySpeed = 0.0f;
    g_nextDisplayUpdate = {};
    g_sampledGhost = ghost.instance;
    g_positionInitialized = false;
    g_smoothedSpeedInitialized = false;
    g_wasHunting = false;
    g_movementWindow.clear();
    g_windowDistance = 0.0f;
    g_windowSeconds = 0.0f;
    g_filteredSpeed = 0.0f;
}

void Shutdown() {
    g_enabled.store(false);
    g_getSpeed = nullptr;
    g_speedMethod = nullptr;
    g_getPhotonView = nullptr;
    g_photonViewMethod = nullptr;
    g_getIsMine = nullptr;
    g_isMineMethod = nullptr;
    g_getTransform = nullptr;
    g_transformMethod = nullptr;
    g_getPosition = nullptr;
    g_positionMethod = nullptr;
    g_navMeshAgentOffset = 0;
    ClearSnapshot();
}

void ClearSnapshot() {
    std::lock_guard<std::mutex> lock(g_snapshotMutex);
    g_generation = 0;
    g_lastSpeed = 0.0f;
    g_displaySpeed = 0.0f;
    g_nextDisplayUpdate = {};
    g_sampledGhost = 0;
    g_positionInitialized = false;
    g_smoothedSpeedInitialized = false;
    g_wasHunting = false;
    g_movementWindow.clear();
    g_windowDistance = 0.0f;
    g_windowSeconds = 0.0f;
    g_filteredSpeed = 0.0f;
}

void SyncSettings() { g_enabled.store(settings_store::Get().ghostSpeedDisplay.enabled); }

void OnGameThreadUpdate() {
    const auto ghost = ghost_runtime::ReadSnapshot();
    const bool hunting = ghost_status::IsHunting();
    {
        std::lock_guard<std::mutex> lock(g_snapshotMutex);
        if (ghost.generation != g_generation || ghost.instance != g_sampledGhost || hunting != g_wasHunting) {
            ResetMovementSampleLocked(ghost.generation, ghost.instance);
            g_wasHunting = hunting;
        }
    }
    if (!hunting || !ghost.valid || !ResolveApi()) {
        ghost_speed_sync::PublishLocalSpeed(false, 0.0f);
        return;
    }
    std::uintptr_t agent = 0;
    if (!il2cpp_runtime::ReadPointer(ghost.instance + g_navMeshAgentOffset, agent) || !agent) {
        ghost_speed_sync::PublishLocalSpeed(false, 0.0f);
        return;
    }
    void* photonView = g_getPhotonView(reinterpret_cast<void*>(ghost.instance), g_photonViewMethod);
    if (!photonView) {
        ghost_speed_sync::PublishLocalSpeed(false, 0.0f);
        return;
    }
    const float nativeSpeed = g_getSpeed(reinterpret_cast<void*>(agent), g_speedMethod);
    const bool nativeSpeedValid = std::isfinite(nativeSpeed) &&
        nativeSpeed >= 0.0f && nativeSpeed <= kMaximumReasonableSpeed;
    const bool localMaster = ghost_speed_sync::PublishLocalSpeed(
        nativeSpeedValid, nativeSpeedValid ? nativeSpeed : 0.0f);
    const bool locallyOwned = localMaster ||
        g_getIsMine(photonView, g_isMineMethod);
    if (locallyOwned && nativeSpeedValid) {
        if (!g_enabled.load()) return;
        std::lock_guard<std::mutex> lock(g_snapshotMutex);
        if (ghost.generation == g_generation && ghost.instance == g_sampledGhost)
            g_lastSpeed = nativeSpeed;
        return;
    }

    // The room master must never estimate speed from replicated positions. If the
    // authoritative NavMesh value is unavailable, the invalid packet above is the
    // only value sent to clients. Position sampling remains a client-only display
    // fallback and is never rebroadcast.
    if (localMaster) {
        if (g_enabled.load()) {
            std::lock_guard<std::mutex> lock(g_snapshotMutex);
            if (ghost.generation == g_generation && ghost.instance == g_sampledGhost)
                g_lastSpeed = 0.0f;
        }
        return;
    }
    if (!g_enabled.load()) return;

    void* transform = g_getTransform(reinterpret_cast<void*>(ghost.instance), g_transformMethod);
    if (!transform) return;
    Vector3 position{};
    g_getPosition(transform, &position, g_positionMethod);
    if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z)) return;
    const auto now = std::chrono::steady_clock::now();
    {
        std::lock_guard<std::mutex> lock(g_snapshotMutex);
        if (ghost.generation != g_generation || ghost.instance != g_sampledGhost) return;
        if (!g_positionInitialized) {
            g_previousPosition = position;
            g_previousPositionTime = now;
            g_positionInitialized = true;
            return;
        }
        const float elapsed = std::chrono::duration<float>(now - g_previousPositionTime).count();
        if (elapsed < 0.008f) return;
        const float dx = position.x - g_previousPosition.x;
        const float dz = position.z - g_previousPosition.z;
        const float distance = std::sqrt(dx * dx + dz * dz);
        g_previousPosition = position;
        g_previousPositionTime = now;
        if (elapsed > 0.5f || distance > 3.0f) {
            g_smoothedSpeedInitialized = false;
            g_movementWindow.clear();
            g_windowDistance = 0.0f;
            g_windowSeconds = 0.0f;
            g_filteredSpeed = 0.0f;
            g_lastSpeed = 0.0f;
            return;
        }
        const float filteredDistance = distance < 0.002f ? 0.0f : distance;
        g_movementWindow.push_back({filteredDistance, elapsed});
        g_windowDistance += filteredDistance;
        g_windowSeconds += elapsed;
        while (!g_movementWindow.empty() &&
               g_windowSeconds - g_movementWindow.front().seconds >= 1.0f) {
            g_windowDistance -= g_movementWindow.front().distance;
            g_windowSeconds -= g_movementWindow.front().seconds;
            g_movementWindow.pop_front();
        }
        if (g_windowSeconds < 0.6f) return;
        const float observedSpeed = g_windowDistance / g_windowSeconds;
        if (!std::isfinite(observedSpeed) || observedSpeed > kMaximumReasonableSpeed) return;
        if (!g_smoothedSpeedInitialized) {
            g_filteredSpeed = observedSpeed;
            g_lastSpeed = observedSpeed;
            g_smoothedSpeedInitialized = true;
        } else {
            g_filteredSpeed = feature_logic::SmoothObservedSpeed(
                g_filteredSpeed, observedSpeed, elapsed, 0.6f);
            if (std::fabs(g_filteredSpeed - g_lastSpeed) >= 0.01f)
                g_lastSpeed = g_filteredSpeed;
        }
    }
}

void RequestResetPosition() {
    settings_store::ClearOverlayPlacement(settings_store::OverlayPlacementId::GhostSpeed);
    g_resetPositionRequested = true;
}

timer_logic::MovableWindowResult DrawWindow(bool menuVisible, bool altDragActive) {
    (void)menuVisible;
    auto& settings = settings_store::Get().ghostSpeedDisplay;
    if (!settings.enabled) return {};
    float speed = 0.0f;
    if (ghost_status::IsHunting() && !ghost_speed_sync::ReadRemoteSpeed(speed)) {
        std::lock_guard<std::mutex> lock(g_snapshotMutex);
        const auto now = std::chrono::steady_clock::now();
        if (g_nextDisplayUpdate.time_since_epoch().count() == 0 || now >= g_nextDisplayUpdate) {
            g_displaySpeed = g_lastSpeed;
            g_nextDisplayUpdate = now + std::chrono::seconds(1);
        }
        speed = g_displaySpeed;
    }
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
    ImGui::TextColored(ToImVec4(settings.color), u8"鬼魂移速: %.2fm/s", speed);
    ImGui::PopFont();
    const ImVec2 size = ImGui::GetWindowSize();
    if (g_resetPositionRequested || !settings.positionInitialized) {
        const auto reset = timer_logic::DefaultOverlayPosition(
            timer_logic::DefaultOverlaySlot::TopCenterLeft,
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
    if (clamped.x != position.x || clamped.y != position.y) ImGui::SetWindowPos({clamped.x, clamped.y});
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
    auto& settings = settings_store::Get().ghostSpeedDisplay;
    bool changed = false;
    ImGui::TextColored({1.0f, 0.15f, 0.15f, 1.0f},
                       u8"(非单人\\房主时，因网络波动可能会导致速度不稳定)");
    settings_ui::LabelControlRow(u8"启用:");
    changed |= ImGui::Checkbox("##ghost_speed_enabled", &settings.enabled);
    settings_ui::LabelControlRow(u8"固定:");
    changed |= ImGui::Checkbox("##ghost_speed_fixed", &settings.fixed);
    int index = font_store::IndexOf(settings.fontSize);
    settings_ui::LabelControlRow(u8"字体大小:");
    ImGui::SetNextItemWidth(100.0f);
    if (ImGui::Combo("##ghost_speed_font_size", &index, font_store::Labels(), static_cast<int>(font_store::kFontSizeCount))) {
        settings.fontSize = font_store::Sizes()[index];
        changed = true;
    }
    settings_ui::LabelControlRow(u8"字体颜色:");
    changed |= ImGui::ColorEdit4("##ghost_speed_color", &settings.color.r, ImGuiColorEditFlags_NoInputs);
    if (ImGui::Button(u8"重置默认设置##ghost_speed")) {
        const bool enabled = settings.enabled;
        settings = settings_store::GhostSpeedDisplaySettings{};
        settings.enabled = enabled;
        RequestResetPosition();
        settings_store::ResetFeatureHotkeys(settings_store::FeatureId::GhostSpeed);
        changed = true;
    }
    if (changed) settings_store::MarkDirty();
}

} // namespace ghost_speed_display
