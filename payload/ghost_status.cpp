#include "ghost_status.h"

#include "feature_logic.h"
#include "font_store.h"
#include "ghost_runtime.h"
#include "il2cpp_runtime.h"
#include "inline_hook.h"
#include "log.h"
#include "settings_store.h"
#include "settings_ui.h"

#include "imgui.h"
#include "imgui_internal.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

namespace ghost_status {
namespace {

struct PhotonMessageInfo {
    std::int32_t time;
    std::int32_t padding;
    void* sender;
    void* photonView;
};
static_assert(sizeof(PhotonMessageInfo) == 24, "PhotonMessageInfo layout must match current x64 IL2CPP dump");

// Current x64 IL2CPP PhotonMessageInfo is passed by value, matching the existing WhiteSage hook.
using HuntingFn = void (*)(void*, bool, int, PhotonMessageInfo, const il2cpp_runtime::Method*);
constexpr std::uintptr_t kStateOffset = 0x30;
constexpr std::uintptr_t kHuntingOffset = 0xF3;
constexpr auto kRetryDelay = std::chrono::milliseconds(100);
constexpr float kSessionRollbackSeconds = 5.0f;

struct State {
    bool hunting = false;
    bool rpcKnown = false;
    bool rpcHunting = false;
    std::chrono::steady_clock::time_point rpcTimestamp{};
    std::uint32_t negativeSamples = 0;
    bool hasObserved = false;
    std::chrono::steady_clock::time_point startedAt{};
    std::uint64_t finalElapsed = 0;
    bool lastGameTimeValid = false;
    float lastGameSeconds = 0.0f;
};

GameTimeProvider g_provider = nullptr;
std::mutex g_stateMutex;
State g_state{};
// Keeps trampoline calls and detach mutually exclusive during shutdown.
std::recursive_mutex g_hookMutex;
inline_hook::Hook g_hook{};
std::atomic<bool> g_entryHooked{false};
std::atomic<bool> g_stopping{false};
std::mutex g_waitMutex;
std::condition_variable g_waitCondition;
std::thread g_installThread;
bool g_resetPositionRequested = false;
constexpr const char* kWindowId = "##ghost_status";
void SubmitPosition(timer_logic::Position position) {
    auto& settings = settings_store::Get().ghostStatus;
    ImGui::SetWindowPos(kWindowId, {position.x, position.y}, ImGuiCond_Always);
    if (!(settings.position == position)) { settings.position = position; settings_store::MarkDirty(); }
}

ImVec4 ToImVec4(timer_logic::Color color) { return {color.r, color.g, color.b, color.a}; }

void ResetStateLocked() { g_state = {}; }

void UpdateSessionLocked(GameTimeValue gameTime) {
    if (timer_logic::SessionTimeWentBack(g_state.lastGameTimeValid, g_state.lastGameSeconds,
                                         gameTime.valid, gameTime.seconds, kSessionRollbackSeconds)) {
        ResetStateLocked();
    }
    if (gameTime.valid) {
        g_state.lastGameTimeValid = true;
        g_state.lastGameSeconds = gameTime.seconds;
    }
}

void SetHuntingLocked(bool hunting, GameTimeValue gameTime) {
    UpdateSessionLocked(gameTime);
    const auto now = std::chrono::steady_clock::now();
    if ((!g_state.hasObserved || !g_state.hunting) && hunting) {
        g_state.startedAt = now;
        g_state.finalElapsed = 0;
    } else if (g_state.hasObserved && g_state.hunting && !hunting) {
        g_state.finalElapsed = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::seconds>(now - g_state.startedAt).count());
    }
    g_state.hunting = hunting;
    g_state.hasObserved = true;
}

void ObserveRpc(void* instance, bool hunting) {
    ghost_runtime::ObserveInstance(instance);
    const GameTimeValue gameTime = g_provider ? g_provider() : GameTimeValue{false, 0.0f};
    std::lock_guard<std::mutex> lock(g_stateMutex);
    UpdateSessionLocked(gameTime);
    g_state.rpcKnown = true;
    g_state.rpcHunting = hunting;
    g_state.rpcTimestamp = std::chrono::steady_clock::now();
    g_state.negativeSamples = 0;
    SetHuntingLocked(hunting, gameTime);
}

void DetourHunting(void* instance, bool hunting, int duration, PhotonMessageInfo info,
                   const il2cpp_runtime::Method* methodInfo) {
    std::lock_guard<std::recursive_mutex> lock(g_hookMutex);
    auto original = reinterpret_cast<HuntingFn>(g_hook.original);
    if (!original) return;
    const auto address = reinterpret_cast<std::uintptr_t>(instance);
    std::uint8_t before = 0, after = 0;
    const bool beforeRead = instance && il2cpp_runtime::ReadByte(address + kHuntingOffset, before);
    original(instance, hunting, duration, info, methodInfo);
    if (beforeRead && il2cpp_runtime::ReadByte(address + kHuntingOffset, after) &&
        before != after && (after != 0) == hunting) ObserveRpc(instance, after != 0);
}

bool InstallHook() {
    std::lock_guard<std::recursive_mutex> lock(g_hookMutex);
    if (g_entryHooked.load()) return true;
    auto* klass = il2cpp_runtime::FindClass("", "GhostAI");
    const auto* method = il2cpp_runtime::FindMethod(klass, "Hunting", 3);
    void* target = il2cpp_runtime::GetMethodEntry(method);
    if (!target || !il2cpp_runtime::IsInGameAssembly(target) || !il2cpp_runtime::IsExecutable(target) ||
        !inline_hook::Attach(g_hook, target, reinterpret_cast<void*>(&DetourHunting))) return false;
    g_entryHooked.store(true);
    LOG("GhostStatus: GhostAI.Hunting Hook installed at %p", target);
    return true;
}

void InstallWorker() {
    while (!g_stopping.load()) {
        if (il2cpp_runtime::Initialize() && InstallHook()) return;
        std::unique_lock<std::mutex> lock(g_waitMutex);
        g_waitCondition.wait_for(lock, kRetryDelay, [] { return g_stopping.load(); });
    }
}

void PollInstance() {
    const GameTimeValue gameTime = g_provider ? g_provider() : GameTimeValue{false, 0.0f};
    {
        std::lock_guard<std::mutex> lock(g_stateMutex);
        const bool rolledBack = timer_logic::SessionTimeWentBack(
            g_state.lastGameTimeValid, g_state.lastGameSeconds, gameTime.valid, gameTime.seconds,
            kSessionRollbackSeconds);
        UpdateSessionLocked(gameTime);
        (void)rolledBack;
    }

    const auto ghost = ghost_runtime::ReadSnapshot();
    const auto instance = ghost.instance;
    const auto now = std::chrono::steady_clock::now();
    if (!instance) return;

    std::uint8_t hunting = 0;
    std::int32_t state = 0;
    const bool fieldsRead = il2cpp_runtime::ReadByte(instance + kHuntingOffset, hunting) &&
                            il2cpp_runtime::ReadInt32(instance + kStateOffset, state);
    if (!fieldsRead) {
        return;
    }

    std::lock_guard<std::mutex> lock(g_stateMutex);
    const auto rpcAge = g_state.rpcKnown ? static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(now - g_state.rpcTimestamp).count()) : 0;
    const auto evidence = timer_logic::ResolveHuntingEvidence(
        g_state.rpcKnown, g_state.rpcHunting, rpcAge, fieldsRead,
        hunting != 0, state == 2, g_state.hunting, g_state.negativeSamples);
    g_state.negativeSamples = evidence.negativeSamples;
    SetHuntingLocked(evidence.hunting, gameTime);
}

} // namespace

void Initialize(GameTimeProvider provider) {
    Shutdown();
    g_provider = provider;
    g_stopping.store(false);
    g_installThread = std::thread(&InstallWorker);
}

void Shutdown() {
    g_stopping.store(true);
    g_waitCondition.notify_all();
    if (g_installThread.joinable()) g_installThread.join();
    std::lock_guard<std::recursive_mutex> hookLock(g_hookMutex);
    if (!inline_hook::Detach(g_hook)) LOG("GhostStatus: failed to detach GhostAI.Hunting Hook");
    g_entryHooked.store(g_hook.attached);
    g_provider = nullptr;
    std::lock_guard<std::mutex> stateLock(g_stateMutex);
    ResetStateLocked();
}

void Update() { PollInstance(); }
void ResetSession() {
    std::lock_guard<std::mutex> lock(g_stateMutex);
    ResetStateLocked();
}

bool IsHunting() {
    std::lock_guard<std::mutex> lock(g_stateMutex);
    return g_state.hasObserved && g_state.hunting;
}

HuntTimingSnapshot ReadHuntTimingSnapshot() {
    std::lock_guard<std::mutex> lock(g_stateMutex);
    const auto now = std::chrono::steady_clock::now();
    const auto duration = g_state.hunting ? static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(now - g_state.startedAt).count())
        : g_state.finalElapsed;
    return {duration};
}

void RequestResetPosition() {
    settings_store::ClearOverlayPlacement(settings_store::OverlayPlacementId::GhostStatus);
    g_resetPositionRequested = true;
}

timer_logic::MovableWindowResult DrawWindow(bool menuVisible, bool altDragActive) {
    (void)menuVisible;
    auto& settings = settings_store::Get().ghostStatus;
    if (!settings.enabled) return {};
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
    State state;
    { std::lock_guard<std::mutex> lock(g_stateMutex); state = g_state; }
    const auto now = std::chrono::steady_clock::now();
    const auto elapsedMs = state.hunting ? static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(now - state.startedAt).count()) : 0;
    const auto color = state.hunting && (!settings.flashing || !timer_logic::BlinkPhase(elapsedMs, 500))
        ? settings.huntingColor : settings.waitingColor;
    ImGui::TextColored(ToImVec4(color), u8"鬼魂状态: %s",
                       state.hunting ? u8"猎杀" : u8"等待");
    ImGui::PopFont();
    const ImVec2 size = ImGui::GetWindowSize();
    if (g_resetPositionRequested) {
        settings.position = timer_logic::DefaultOverlayPosition(
            timer_logic::DefaultOverlaySlot::TopCenterRight,
            {io.DisplaySize.x, io.DisplaySize.y}, {size.x, size.y});
        ImGui::SetWindowPos({settings.position.x, settings.position.y});
        g_resetPositionRequested = false;
        settings_store::MarkDirty();
    }
    const ImVec2 position = ImGui::GetWindowPos();
    const auto clamped = timer_logic::ClampPosition({position.x, position.y}, {size.x, size.y}, {io.DisplaySize.x, io.DisplaySize.y});
    if (clamped.x != position.x || clamped.y != position.y) ImGui::SetWindowPos({clamped.x, clamped.y});
    ImGui::End();
    if ((clamped.x != settings.position.x || clamped.y != settings.position.y) &&
        (canMove || clamped.x != position.x || clamped.y != position.y)) {
        settings.position = clamped;
        settings_store::MarkDirty();
    }
    return {kWindowId, {clamped, {size.x, size.y}}, canMove, dragging, &SubmitPosition};
}

void DrawSettings() {
    auto& settings = settings_store::Get().ghostStatus;
    settings_ui::LabelControlRow(u8"启用:");
    bool changed = ImGui::Checkbox("##ghost_status_enabled", &settings.enabled);
    settings_ui::LabelControlRow(u8"固定:");
    changed |= ImGui::Checkbox("##ghost_status_fixed", &settings.fixed);
    settings_ui::LabelControlRow(u8"状态变化闪烁:");
    changed |= ImGui::Checkbox("##ghost_status_flashing", &settings.flashing);
    int index = font_store::IndexOf(settings.fontSize);
    settings_ui::LabelControlRow(u8"字体大小:");
    ImGui::SetNextItemWidth(100.0f);
    if (ImGui::Combo("##ghost_status_font_size", &index, font_store::Labels(), static_cast<int>(font_store::kFontSizeCount))) {
        settings.fontSize = font_store::Sizes()[index]; changed = true;
    }
    settings_ui::LabelControlRow(u8"等待颜色:");
    changed |= ImGui::ColorEdit4("##ghost_status_waiting_color", &settings.waitingColor.r, ImGuiColorEditFlags_NoInputs);
    settings_ui::LabelControlRow(u8"猎杀颜色:");
    changed |= ImGui::ColorEdit4("##ghost_status_hunting_color", &settings.huntingColor.r, ImGuiColorEditFlags_NoInputs);
    if (ImGui::Button(u8"重置默认设置##ghost_status")) {
        const bool enabled = settings.enabled;
        settings = settings_store::GhostStatusSettings{};
        settings.enabled = enabled;
        RequestResetPosition();
        settings_store::ResetFeatureHotkeys(settings_store::FeatureId::GhostStatus);
        changed = true;
    }
    if (changed) settings_store::MarkDirty();
}

} // namespace ghost_status
