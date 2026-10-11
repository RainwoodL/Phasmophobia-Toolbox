#include "crucifix_timer.h"

#include "feature_logic.h"
#include "font_store.h"
#include "il2cpp_runtime.h"
#include "inline_hook.h"
#include "log.h"
#include "settings_store.h"
#include "settings_ui.h"

#include "imgui.h"
#include "imgui_internal.h"

#include <windows.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

namespace crucifix_timer {
namespace {

struct PhotonMessageInfo {
    std::int32_t time;
    std::int32_t padding;
    void* sender;
    void* photonView;
};
static_assert(sizeof(PhotonMessageInfo) == 24, "PhotonMessageInfo layout must match current x64 IL2CPP dump");
using CrucifixUseFn = void (*)(void*, bool, PhotonMessageInfo, const il2cpp_runtime::Method*);
constexpr auto kRetryDelay = std::chrono::milliseconds(100);

struct BurnEvent {
    bool valid = false;
    std::chrono::steady_clock::time_point burnedAt{};
    float gameSeconds = 0.0f;
};

GameTimeProvider g_gameTimeProvider = nullptr;
std::mutex g_eventMutex;
BurnEvent g_event{};
// Keeps trampoline calls and detach mutually exclusive during shutdown.
std::recursive_mutex g_hookMutex;
inline_hook::Hook g_hook{};
std::atomic<bool> g_entryHooked{false};
std::atomic<bool> g_stopping{false};
std::mutex g_waitMutex;
std::condition_variable g_waitCondition;
std::thread g_installThread;
bool g_pKeyDown = false;
bool g_resetPositionRequested = false;
std::size_t g_useCountOffset = 0;
constexpr const char* kWindowId = "##crucifix_timer";
void SubmitPosition(timer_logic::Position position) {
    auto& settings = settings_store::Get().crucifix;
    ImGui::SetWindowPos(kWindowId, {position.x, position.y}, ImGuiCond_Always);
    if (!(settings.position == position)) { settings.position = position; settings_store::MarkDirty(); }
}

ImVec4 ToImVec4(timer_logic::Color color) { return {color.r, color.g, color.b, color.a}; }

void RecordBurn(GameTimeValue gameTime) {
    std::lock_guard<std::mutex> lock(g_eventMutex);
    g_event = {true, std::chrono::steady_clock::now(), gameTime.valid ? gameTime.seconds : 0.0f};
}

void DetourCrucifixUse(void* instance, bool used, PhotonMessageInfo info,
                       const il2cpp_runtime::Method* methodInfo) {
    std::lock_guard<std::recursive_mutex> lock(g_hookMutex);
    std::int32_t before = -1;
    const bool beforeValid = instance && il2cpp_runtime::ReadInt32(
        reinterpret_cast<std::uintptr_t>(instance) + g_useCountOffset, before) && before >= 0 && before <= 5;
    auto original = reinterpret_cast<CrucifixUseFn>(g_hook.original);
    if (!original) return;
    original(instance, used, info, methodInfo);
    std::int32_t after = -1;
    const bool afterValid = instance && il2cpp_runtime::ReadInt32(
        reinterpret_cast<std::uintptr_t>(instance) + g_useCountOffset, after) && after >= 0 && after <= 5;
    if (beforeValid && afterValid && before != after) {
        RecordBurn(g_gameTimeProvider ? g_gameTimeProvider() : GameTimeValue{false, 0.0f});
    } else if (!beforeValid || !afterValid) {
        LOG("CrucifixTimer: useCount read invalid; burn event ignored");
    }
}

bool InstallHook() {
    std::lock_guard<std::recursive_mutex> lock(g_hookMutex);
    if (g_entryHooked.load()) return true;
    auto* klass = il2cpp_runtime::FindClass("", "Crucifix");
    if (!il2cpp_runtime::FindInstanceFieldByName(klass, "useCount", g_useCountOffset)) {
        LOG("CrucifixTimer: useCount field offset unavailable; hook not installed");
        return false;
    }
    const auto* method = il2cpp_runtime::FindMethod(klass, "CrucifixUseNetworked", 2);
    void* target = il2cpp_runtime::GetMethodEntry(method);
    if (!target || !il2cpp_runtime::IsInGameAssembly(target) || !il2cpp_runtime::IsExecutable(target) ||
        !inline_hook::Attach(g_hook, target, reinterpret_cast<void*>(&DetourCrucifixUse))) return false;
    g_entryHooked.store(true);
    LOG("CrucifixTimer: Crucifix.CrucifixUseNetworked Hook installed at %p, useCount offset=0x%llX",
        target, static_cast<unsigned long long>(g_useCountOffset));
    return true;
}

void InstallWorker() {
    while (!g_stopping.load()) {
        if (il2cpp_runtime::Initialize() && InstallHook()) return;
        std::unique_lock<std::mutex> lock(g_waitMutex);
        g_waitCondition.wait_for(lock, kRetryDelay, [] { return g_stopping.load(); });
    }
}

} // namespace

void Initialize(GameTimeProvider gameTimeProvider) {
    Shutdown();
    g_gameTimeProvider = gameTimeProvider;
    g_stopping.store(false);
    g_installThread = std::thread(&InstallWorker);
}

void Shutdown() {
    g_stopping.store(true);
    g_waitCondition.notify_all();
    if (g_installThread.joinable()) g_installThread.join();
    std::lock_guard<std::recursive_mutex> hookLock(g_hookMutex);
    if (!inline_hook::Detach(g_hook)) LOG("CrucifixTimer: failed to detach CrucifixUseNetworked Hook");
    g_entryHooked.store(g_hook.attached);
    g_useCountOffset = 0;
    g_gameTimeProvider = nullptr;
    g_pKeyDown = false;
    ResetEvent();
}

void ResetEvent() {
    std::lock_guard<std::mutex> lock(g_eventMutex);
    g_event = {};
}

void UpdateHotkeys(bool gameForeground) {
    const auto& binding = settings_store::Get().hotkeys[
        static_cast<std::size_t>(settings_store::HotkeyAction::CrucifixClear)];
    const bool ctrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
    const bool shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
    const bool down = binding.key && (GetAsyncKeyState(binding.key) & 0x8000) != 0 &&
                      ctrl == binding.ctrl && shift == binding.shift;
    if (feature_logic::HotkeyPressed(gameForeground, down, g_pKeyDown)) ResetEvent();
    g_pKeyDown = down;
}

void RequestResetPosition() {
    settings_store::ClearOverlayPlacement(settings_store::OverlayPlacementId::Crucifix);
    g_resetPositionRequested = true;
}

timer_logic::MovableWindowResult DrawWindow(bool menuVisible, bool altDragActive) {
    (void)menuVisible;
    auto& settings = settings_store::Get().crucifix;
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
    BurnEvent event;
    { std::lock_guard<std::mutex> lock(g_eventMutex); event = g_event; }
    std::uint64_t elapsed = 0;
    if (event.valid) elapsed = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now() - event.burnedAt).count());
    const std::string elapsedText = timer_logic::FormatMmSs(elapsed);
    const std::string recordedText = timer_logic::FormatMmSs(event.valid ? timer_logic::NormalizeSeconds(event.gameSeconds) : 0);
    ImGui::TextColored(ToImVec4(event.valid ? settings.activeColor : settings.idleColor),
                       u8"十字架被燃烧: %s", elapsedText.c_str());
    ImGui::TextColored(ToImVec4(settings.recordColor), u8"记录时间: %s", recordedText.c_str());
    ImGui::PopFont();
    const ImVec2 size = ImGui::GetWindowSize();
    if (g_resetPositionRequested) {
        settings.position = timer_logic::DefaultOverlayPosition(
            timer_logic::DefaultOverlaySlot::TopLeftSecondary,
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
    auto& settings = settings_store::Get().crucifix;
    settings_ui::LabelControlRow(u8"启用:");
    bool changed = ImGui::Checkbox("##crucifix_enabled", &settings.enabled);
    settings_ui::LabelControlRow(u8"固定:");
    changed |= ImGui::Checkbox("##crucifix_fixed", &settings.fixed);
    int index = font_store::IndexOf(settings.fontSize);
    settings_ui::LabelControlRow(u8"字体大小:");
    ImGui::SetNextItemWidth(100.0f);
    if (ImGui::Combo("##crucifix_font_size", &index, font_store::Labels(), static_cast<int>(font_store::kFontSizeCount))) {
        settings.fontSize = font_store::Sizes()[index]; changed = true;
    }
    settings_ui::LabelControlRow(u8"无记录颜色:");
    changed |= ImGui::ColorEdit4("##crucifix_idle_color", &settings.idleColor.r, ImGuiColorEditFlags_NoInputs);
    settings_ui::LabelControlRow(u8"有记录颜色:");
    changed |= ImGui::ColorEdit4("##crucifix_active_color", &settings.activeColor.r, ImGuiColorEditFlags_NoInputs);
    settings_ui::LabelControlRow(u8"记录时间颜色:");
    changed |= ImGui::ColorEdit4("##crucifix_record_color", &settings.recordColor.r, ImGuiColorEditFlags_NoInputs);
    if (ImGui::Button(u8"重置默认设置##crucifix")) {
        const bool enabled = settings.enabled;
        settings = settings_store::CrucifixSettings{};
        settings.enabled = enabled;
        RequestResetPosition();
        settings_store::ResetFeatureHotkeys(settings_store::FeatureId::Crucifix);
        changed = true;
    }
    if (changed) settings_store::MarkDirty();
}

} // namespace crucifix_timer
