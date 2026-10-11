#include "smudge_timer.h"

#include "feature_logic.h"
#include "font_store.h"
#include "il2cpp_runtime.h"
#include "inline_hook.h"
#include "settings_store.h"
#include "settings_ui.h"
#include "timer_logic.h"
#include "log.h"

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

namespace smudge_timer {
namespace {

struct SmudgeEvent {
    bool valid = false;
    std::chrono::steady_clock::time_point ignitedAt{};
    std::string playerName = "--";
    float gameSeconds = 0.0f;
};

struct PhotonMessageInfo {
    std::int32_t time;
    std::int32_t padding;
    void* sender;
    void* photonView;
};
static_assert(sizeof(PhotonMessageInfo) == 24, "PhotonMessageInfo layout must match dump.cs");
static_assert(offsetof(PhotonMessageInfo, sender) == 8, "PhotonMessageInfo.Sender offset must match dump.cs");

using UseNetworkedFn = void (*)(void* instance, bool used, PhotonMessageInfo info, const il2cpp_runtime::Method* methodInfo);

struct HookState {
    inline_hook::Hook hook{};
    std::atomic<bool> installed{false};
};

constexpr auto kInstallRetryDelay = std::chrono::milliseconds(100);

GameTimeProvider g_provider = nullptr;
std::mutex g_eventMutex;
SmudgeEvent g_event{};
std::recursive_mutex g_hookMutex;
std::mutex g_waitMutex;
std::condition_variable g_waitCondition;
std::thread g_installThread;
std::atomic<bool> g_stopping{false};
HookState g_useHook;
std::size_t g_playerNickNameOffset = 0;
std::atomic<float> g_currentFontSize{26.0f};
bool g_iKeyDown = false;
bool g_oKeyDown = false;
bool g_resetPositionRequested = false;
constexpr const char* kWindowId = "##smudge_timer";
void SubmitPosition(timer_logic::Position position) {
    auto& settings = settings_store::Get().smudge;
    ImGui::SetWindowPos(kWindowId, {position.x, position.y}, ImGuiCond_Always);
    if (!(settings.position == position)) { settings.position = position; settings_store::MarkDirty(); }
}

ImVec4 ToImVec4(timer_logic::Color color) { return {color.r, color.g, color.b, color.a}; }

SmudgeEvent ReadEvent() {
    std::lock_guard<std::mutex> lock(g_eventMutex);
    return g_event;
}

bool ResolveHook(HookState& hook, const char* className, const char* methodName, int parameterCount) {
    auto* klass = il2cpp_runtime::FindClass("", className);
    const auto* method = il2cpp_runtime::FindMethod(klass, methodName, parameterCount);
    hook.hook.target = il2cpp_runtime::GetMethodEntry(method);
    return hook.hook.target && il2cpp_runtime::IsInGameAssembly(hook.hook.target) &&
           il2cpp_runtime::IsExecutable(hook.hook.target);
}

void ClearEvent() {
    std::lock_guard<std::mutex> lock(g_eventMutex);
    g_event = {};
}

void DetourUseNetworked(void* instance, bool used, PhotonMessageInfo info, const il2cpp_runtime::Method* methodInfo) {
    std::lock_guard<std::recursive_mutex> hookLock(g_hookMutex);
    auto original = reinterpret_cast<UseNetworkedFn>(g_useHook.hook.original);
    if (!original) return;
    original(instance, used, info, methodInfo);

    std::uintptr_t nickName = 0;
    const std::string playerName = info.sender &&
        g_playerNickNameOffset && il2cpp_runtime::ReadPointer(
            reinterpret_cast<std::uintptr_t>(info.sender) + g_playerNickNameOffset, nickName)
        ? il2cpp_runtime::CopyUtf8String(nickName, "--") : "--";
    const GameTimeValue gameTime = g_provider ? g_provider() : GameTimeValue{false, 0.0f};
    font_store::RequestText(g_currentFontSize.load(), playerName);
    {
        std::lock_guard<std::mutex> eventLock(g_eventMutex);
        g_event = {true, std::chrono::steady_clock::now(), playerName, gameTime.valid ? gameTime.seconds : 0.0f};
    }

}

bool InstallHook(HookState& hook, const char* className, const char* methodName, int parameterCount, void* detour) {
    std::lock_guard<std::recursive_mutex> lock(g_hookMutex);
    if (hook.installed.load()) return true;
    auto* photonPlayerClass = il2cpp_runtime::FindClass("Photon.Realtime", "Player");
    if (g_stopping.load() || !ResolveHook(hook, className, methodName, parameterCount) ||
        !il2cpp_runtime::FindInstanceFieldByName(
            photonPlayerClass, "nickName", g_playerNickNameOffset) ||
        !inline_hook::Attach(hook.hook, hook.hook.target, detour)) return false;
    hook.installed.store(true);
    LOG("SmudgeTimer: %s.%s Hook installed at %p", className, methodName, hook.hook.target);
    return true;
}

void InstallWorker() {
    while (!g_stopping.load()) {
        if (il2cpp_runtime::Initialize()) {
            if (!g_useHook.installed.load()) InstallHook(g_useHook, "WhiteSage", "UseNetworked", 2, reinterpret_cast<void*>(&DetourUseNetworked));
            if (g_useHook.installed.load()) return;
        }
        std::unique_lock<std::mutex> lock(g_waitMutex);
        g_waitCondition.wait_for(lock, kInstallRetryDelay, [] { return g_stopping.load(); });
    }
}

} // namespace

void Initialize(GameTimeProvider provider) {
    Shutdown();
    {
        std::lock_guard<std::recursive_mutex> lock(g_hookMutex);
        g_provider = provider;
    }
    g_stopping.store(false);
    g_useHook.installed.store(false);
    g_iKeyDown = false;
    g_oKeyDown = false;
    g_installThread = std::thread(&InstallWorker);
}

void Shutdown() {
    g_stopping.store(true);
    g_waitCondition.notify_all();
    if (g_installThread.joinable()) g_installThread.join();
    {
        std::lock_guard<std::recursive_mutex> lock(g_hookMutex);
        if (g_useHook.installed.exchange(false) && !inline_hook::Detach(g_useHook.hook))
            LOG("SmudgeTimer: failed to detach WhiteSage.UseNetworked Hook");
        g_playerNickNameOffset = 0;
        g_provider = nullptr;
    }
    ClearEvent();
}

void ResetEvent() { ClearEvent(); }

void UpdateHotkeys(bool gameForeground) {
    const auto& hotkeys = settings_store::Get().hotkeys;
    const auto& start = hotkeys[static_cast<std::size_t>(settings_store::HotkeyAction::SmudgeStart)];
    const auto& clear = hotkeys[static_cast<std::size_t>(settings_store::HotkeyAction::SmudgeClear)];
    const auto down = [](const settings_store::HotkeyBinding& binding) {
        const bool ctrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
        const bool shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
        return binding.key && (GetAsyncKeyState(binding.key) & 0x8000) != 0 &&
               ctrl == binding.ctrl && shift == binding.shift;
    };
    const bool iDown = down(start);
    const bool oDown = down(clear);
    if (gameForeground && iDown && !g_iKeyDown) {
        const GameTimeValue gameTime = g_provider ? g_provider() : GameTimeValue{false, 0.0f};
        constexpr const char* manualName = u8"手动";
        font_store::RequestText(settings_store::Get().smudge.fontSize, manualName);
        std::lock_guard<std::mutex> lock(g_eventMutex);
        g_event = {true, std::chrono::steady_clock::now(), manualName, gameTime.valid ? gameTime.seconds : 0.0f};
    }
    if (gameForeground && oDown && !g_oKeyDown) ClearEvent();
    g_iKeyDown = iDown;
    g_oKeyDown = oDown;
}

void RequestResetPosition() {
    settings_store::ClearOverlayPlacement(settings_store::OverlayPlacementId::Smudge);
    g_resetPositionRequested = true;
}

timer_logic::MovableWindowResult DrawWindow(bool menuVisible, bool altDragActive) {
    (void)menuVisible;
    auto& settings = settings_store::Get().smudge;
    g_currentFontSize.store(settings.fontSize);
    if (!settings.enabled) return {};
    const ImGuiIO& io = ImGui::GetIO();
    const bool resetPosition = g_resetPositionRequested;
    if (resetPosition) {
        settings.position = timer_logic::DefaultOverlayPosition(
            timer_logic::DefaultOverlaySlot::TopLeftPrimary,
            {io.DisplaySize.x, io.DisplaySize.y}, {0.0f, 0.0f});
        g_resetPositionRequested = false;
        settings_store::MarkDirty();
    }
    ImGui::SetNextWindowPos({settings.position.x, settings.position.y},
                            (settings.fixed || resetPosition) ? ImGuiCond_Always : ImGuiCond_FirstUseEver);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground |
                             ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                             ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
    const bool canMove = feature_logic::CanMoveOverlay(settings.fixed, altDragActive);
    if (!canMove) flags |= ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoInputs;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.0f, 0.0f});
    ImGui::Begin(kWindowId, nullptr, flags);
    ImGui::PopStyleVar();
    const bool dragging = canMove && GImGui->MovingWindow == ImGui::GetCurrentWindow();
    const SmudgeEvent event = ReadEvent();
    if (event.valid) font_store::RequestText(settings.fontSize, event.playerName);
    ImGui::PushFont(font_store::Get(settings.fontSize));
    std::uint64_t elapsed = 0;
    if (event.valid) {
        const auto start = std::chrono::duration_cast<std::chrono::milliseconds>(event.ignitedAt.time_since_epoch()).count();
        const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
        elapsed = timer_logic::ElapsedSeconds(static_cast<std::uint64_t>(start), static_cast<std::uint64_t>(now));
    }
    const std::string elapsedText = timer_logic::FormatMmSs(elapsed);
    const std::string recordedText = timer_logic::FormatMmSs(event.valid ? timer_logic::NormalizeSeconds(event.gameSeconds) : 0);
    ImGui::TextColored(ToImVec4(event.valid ? settings.activeColor : settings.idleColor), u8"圣木被点燃: %s", elapsedText.c_str());
    ImGui::TextColored(ToImVec4(settings.playerColor), u8"玩家:%s", event.valid ? event.playerName.c_str() : "--");
    ImGui::SameLine(0.0f, 10.0f);
    ImGui::TextColored(ToImVec4(settings.recordedTimeColor), u8"记录时间: %s", recordedText.c_str());
    ImGui::PopFont();
    const ImVec2 windowSize = ImGui::GetWindowSize();
    const ImVec2 position = ImGui::GetWindowPos();
    const auto clamped = timer_logic::ClampPosition({position.x, position.y}, {windowSize.x, windowSize.y}, {io.DisplaySize.x, io.DisplaySize.y});
    if (clamped.x != position.x || clamped.y != position.y) ImGui::SetWindowPos({clamped.x, clamped.y});
    ImGui::End();
    if ((canMove || clamped.x != position.x || clamped.y != position.y) &&
        (clamped.x != settings.position.x || clamped.y != settings.position.y)) {
        settings.position = clamped;
        settings_store::MarkDirty();
    }
    return {kWindowId, {clamped, {windowSize.x, windowSize.y}}, canMove, dragging, &SubmitPosition};
}

void DrawSettings() {
    auto& settings = settings_store::Get().smudge;
    settings_ui::LabelControlRow(u8"启用:");
    bool changed = ImGui::Checkbox("##smudge_enabled", &settings.enabled);
    settings_ui::LabelControlRow(u8"固定:");
    changed |= ImGui::Checkbox("##smudge_fixed", &settings.fixed);
    int fontSizeIndex = font_store::IndexOf(settings.fontSize);
    settings_ui::LabelControlRow(u8"字体大小:");
    ImGui::SetNextItemWidth(100.0f);
    if (ImGui::Combo("##smudge_font_size", &fontSizeIndex, font_store::Labels(), static_cast<int>(font_store::kFontSizeCount))) {
        settings.fontSize = font_store::Sizes()[fontSizeIndex];
        changed = true;
    }
    settings_ui::LabelControlRow(u8"无记录颜色:");
    changed |= ImGui::ColorEdit4("##smudge_idle_color", &settings.idleColor.r, ImGuiColorEditFlags_NoInputs);
    settings_ui::LabelControlRow(u8"有记录颜色:");
    changed |= ImGui::ColorEdit4("##smudge_active_color", &settings.activeColor.r, ImGuiColorEditFlags_NoInputs);
    settings_ui::LabelControlRow(u8"玩家颜色:");
    changed |= ImGui::ColorEdit4("##smudge_player_color", &settings.playerColor.r, ImGuiColorEditFlags_NoInputs);
    settings_ui::LabelControlRow(u8"记录时间颜色:");
    changed |= ImGui::ColorEdit4("##smudge_recorded_time_color", &settings.recordedTimeColor.r, ImGuiColorEditFlags_NoInputs);
    if (ImGui::Button(u8"重置默认设置##smudge")) {
        const bool enabled = settings.enabled;
        settings = settings_store::SmudgeSettings{};
        settings.enabled = enabled;
        RequestResetPosition();
        settings_store::ResetFeatureHotkeys(settings_store::FeatureId::Smudge);
        changed = true;
    }
    if (changed) settings_store::MarkDirty();
}

} // namespace smudge_timer
