#include "ouija_translation.h"

#include "feature_logic.h"
#include "font_store.h"
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
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

namespace ouija_translation {
namespace {

struct PhotonMessageInfo {
    std::int32_t time;
    std::int32_t padding;
    void* sender;
    void* photonView;
};
static_assert(sizeof(PhotonMessageInfo) == 24,
              "PhotonMessageInfo layout must match the current x64 IL2CPP dump");

using PlayMessageSequenceFn = void (*)(void*, void*, PhotonMessageInfo,
                                       const il2cpp_runtime::Method*);
constexpr auto kRetryDelay = std::chrono::milliseconds(100);
constexpr auto kDisplayDuration = std::chrono::seconds(15);
constexpr const char* kWindowId = "##ouija_translation";

struct Snapshot {
    bool valid = false;
    std::string original;
    std::string translated;
    std::chrono::steady_clock::time_point receivedAt{};
};

std::mutex g_snapshotMutex;
Snapshot g_snapshot{};
std::recursive_mutex g_hookMutex;
inline_hook::Hook g_hook{};
std::atomic<bool> g_hooked{false};
std::atomic<bool> g_stopping{true};
std::atomic<bool> g_enabled{true};
std::mutex g_waitMutex;
std::condition_variable g_waitCondition;
std::thread g_installThread;
bool g_resetPositionRequested = false;
std::uintptr_t g_answerListOffset = 0;
std::size_t g_listVersionOffset = 0;

ImVec4 Color(timer_logic::Color color) { return {color.r, color.g, color.b, color.a}; }

void PublishAnswer(void* il2cppString) {
    if (!g_enabled.load()) return;
    const std::string original = il2cpp_runtime::CopyUtf8String(
        reinterpret_cast<std::uintptr_t>(il2cppString), "");
    const std::string translated = feature_logic::TranslateOuijaAnswer(original);
    if (translated.empty()) return;
    std::lock_guard<std::mutex> lock(g_snapshotMutex);
    g_snapshot = {true, original, translated, std::chrono::steady_clock::now()};
}

void DetourPlayMessageSequence(void* instance, void* message, PhotonMessageInfo info,
                               const il2cpp_runtime::Method* methodInfo) {
    std::lock_guard<std::recursive_mutex> lock(g_hookMutex);
    auto original = reinterpret_cast<PlayMessageSequenceFn>(g_hook.original);
    if (!original) return;
    std::uintptr_t beforeList = 0, afterList = 0;
    std::int32_t beforeVersion = 0, afterVersion = 0;
    const auto address = reinterpret_cast<std::uintptr_t>(instance);
    const bool beforeRead = instance &&
        il2cpp_runtime::ReadPointer(address + g_answerListOffset, beforeList) && beforeList &&
        il2cpp_runtime::ReadInt32(beforeList + g_listVersionOffset, beforeVersion);
    original(instance, message, info, methodInfo);
    // Accepted messages clear/repopulate List<string> after all RPC guards.
    // The list version also distinguishes repeated answers and pooled tweens.
    if (beforeRead && il2cpp_runtime::ReadPointer(address + g_answerListOffset, afterList) &&
        afterList == beforeList &&
        il2cpp_runtime::ReadInt32(afterList + g_listVersionOffset, afterVersion) &&
        afterVersion != beforeVersion) PublishAnswer(message);
}

bool InstallHook() {
    std::lock_guard<std::recursive_mutex> lock(g_hookMutex);
    if (g_hooked.load()) return true;
    auto* klass = il2cpp_runtime::FindClass("", "OuijaBoard");
    if (!il2cpp_runtime::FindInstanceListFieldByElementType(klass,
            il2cpp_runtime::FindClass("System", "String"),
            g_answerListOffset, g_listVersionOffset)) return false;
    const auto* method = il2cpp_runtime::FindMethod(
        klass, "PlayMessageSequence", 2);
    void* target = il2cpp_runtime::GetMethodEntry(method);
    if (!target || !il2cpp_runtime::IsInGameAssembly(target) ||
        !il2cpp_runtime::IsExecutable(target) ||
        !inline_hook::Attach(g_hook, target,
                            reinterpret_cast<void*>(&DetourPlayMessageSequence))) return false;
    g_hooked.store(true);
    LOG("OuijaTranslation: OuijaBoard.PlayMessageSequence Hook installed at %p", target);
    return true;
}

void InstallWorker() {
    while (!g_stopping.load()) {
        if (il2cpp_runtime::Initialize() && InstallHook()) return;
        std::unique_lock<std::mutex> lock(g_waitMutex);
        g_waitCondition.wait_for(lock, kRetryDelay, [] { return g_stopping.load(); });
    }
}

void SubmitPosition(timer_logic::Position position) {
    auto& settings = settings_store::Get().ouijaTranslation;
    ImGui::SetWindowPos(kWindowId, {position.x, position.y}, ImGuiCond_Always);
    if (!(settings.position == position) || !settings.positionInitialized) {
        settings.position = position;
        settings.positionInitialized = true;
        settings_store::MarkDirty();
    }
}

} // namespace

void Initialize() {
    Shutdown();
    SyncSettings();
    g_stopping.store(false);
    g_installThread = std::thread(&InstallWorker);
}

void Shutdown() {
    g_stopping.store(true);
    g_waitCondition.notify_all();
    if (g_installThread.joinable()) g_installThread.join();
    std::lock_guard<std::recursive_mutex> hookLock(g_hookMutex);
    if (!inline_hook::Detach(g_hook))
        LOG("OuijaTranslation: failed to detach PlayMessageSequence Hook");
    g_hooked.store(g_hook.attached);
    ClearSnapshot();
}

void SyncSettings() {
    const bool enabled = settings_store::Get().ouijaTranslation.enabled;
    g_enabled.store(enabled);
    if (!enabled) ClearSnapshot();
}

void ClearSnapshot() {
    std::lock_guard<std::mutex> lock(g_snapshotMutex);
    g_snapshot = {};
}

void RequestResetPosition() {
    settings_store::ClearOverlayPlacement(settings_store::OverlayPlacementId::OuijaTranslation);
    g_resetPositionRequested = true;
}

timer_logic::MovableWindowResult DrawWindow(bool menuVisible, bool altDragActive) {
    (void)menuVisible;
    auto& settings = settings_store::Get().ouijaTranslation;
    if (!settings.enabled) return {};

    Snapshot snapshot;
    {
        std::lock_guard<std::mutex> lock(g_snapshotMutex);
        snapshot = g_snapshot;
    }
    const bool fresh = snapshot.valid &&
        std::chrono::steady_clock::now() - snapshot.receivedAt <= kDisplayDuration;
    const bool positioningPreview = altDragActive && !settings.fixed;
    if (!fresh && !positioningPreview) return {};

    const std::string text = positioningPreview
        ? std::string(u8"通灵板翻译")
        : snapshot.translated;
    font_store::RequestText(settings.fontSize, text);

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
    ImGui::TextColored(Color(settings.color), "%s", text.c_str());
    ImGui::PopFont();

    const ImVec2 size = ImGui::GetWindowSize();
    if (g_resetPositionRequested || !settings.positionInitialized) {
        settings.position = timer_logic::DefaultUpperCenter(
            {io.DisplaySize.x, io.DisplaySize.y}, {size.x, size.y});
        settings.positionInitialized = true;
        ImGui::SetWindowPos({settings.position.x, settings.position.y});
        g_resetPositionRequested = false;
        settings_store::MarkDirty();
    }
    const ImVec2 position = ImGui::GetWindowPos();
    const auto clamped = timer_logic::ClampPosition(
        {position.x, position.y}, {size.x, size.y}, {io.DisplaySize.x, io.DisplaySize.y});
    if (clamped.x != position.x || clamped.y != position.y)
        ImGui::SetWindowPos({clamped.x, clamped.y});
    ImGui::End();
    if (!(clamped == settings.position) &&
        (canMove || clamped.x != position.x || clamped.y != position.y)) {
        settings.position = clamped;
        settings.positionInitialized = true;
        settings_store::MarkDirty();
    }
    return {kWindowId, {clamped, {size.x, size.y}}, canMove, dragging, &SubmitPosition};
}

void DrawSettings() {
    auto& settings = settings_store::Get().ouijaTranslation;
    settings_ui::LabelControlRow(u8"启用:");
    bool changed = ImGui::Checkbox("##ouija_translation_enabled", &settings.enabled);
    settings_ui::LabelControlRow(u8"固定:");
    changed |= ImGui::Checkbox("##ouija_translation_fixed", &settings.fixed);
    int fontSizeIndex = font_store::IndexOf(settings.fontSize);
    settings_ui::LabelControlRow(u8"字体大小:");
    ImGui::SetNextItemWidth(100.0f);
    if (ImGui::Combo("##ouija_translation_font_size", &fontSizeIndex,
                     font_store::Labels(), static_cast<int>(font_store::kFontSizeCount))) {
        settings.fontSize = font_store::Sizes()[fontSizeIndex];
        changed = true;
    }
    settings_ui::LabelControlRow(u8"字体颜色:");
    changed |= ImGui::ColorEdit4("##ouija_translation_color", &settings.color.r,
                                 ImGuiColorEditFlags_NoInputs);
    if (ImGui::Button(u8"重置默认设置##ouija_translation")) {
        const bool enabled = settings.enabled;
        settings = settings_store::OuijaTranslationSettings{};
        settings.enabled = enabled;
        RequestResetPosition();
        settings_store::ResetFeatureHotkeys(settings_store::FeatureId::OuijaTranslation);
        changed = true;
    }
    if (changed) {
        SyncSettings();
        settings_store::MarkDirty();
    }
}

} // namespace ouija_translation
