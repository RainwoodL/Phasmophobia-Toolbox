#include "sanity_display.h"

#include "feature_logic.h"
#include "font_store.h"
#include "il2cpp_runtime.h"
#include "log.h"
#include "settings_store.h"
#include "settings_ui.h"

#include "imgui.h"
#include "imgui_internal.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <mutex>
#include <string>

namespace sanity_display {
namespace {

enum class MonitorState { Unavailable, Unsupported, Supported };

struct Entry {
    std::string name;
    float sanity = 0.0f;
};

struct Snapshot {
    MonitorState state = MonitorState::Unavailable;
    std::array<Entry, 4> players{};
    std::size_t count = 0;
    float average = 0.0f;
    bool averageValid = false;
};

using GetInstanceFn = void* (*)(const il2cpp_runtime::Method*);
using GetAverageInsanityFn = float (*)(void*, const il2cpp_runtime::Method*);

struct Metadata {
    bool valid = false;
    GetInstanceFn getNetwork = nullptr;
    const il2cpp_runtime::Method* networkMethod = nullptr;
    GetInstanceFn getLevelValues = nullptr;
    const il2cpp_runtime::Method* levelValuesMethod = nullptr;
    GetAverageInsanityFn getAverageInsanity = nullptr;
    const il2cpp_runtime::Method* averageMethod = nullptr;
    std::uintptr_t playerSpotsOffset = 0x38;
    std::uintptr_t spotPlayerOffset = 0;
    std::uintptr_t spotAccountNameOffset = 0;
    std::uintptr_t playerSanityOffset = 0;
    std::uintptr_t sanityMonitorOffset = 0;
};

constexpr std::uintptr_t kCurrentDifficultyOffset = 0x38;
constexpr std::uintptr_t kInsanityOffset = 0x30;
constexpr const char* kWindowId = "##sanity_display";

Metadata g_metadata{};
std::mutex g_snapshotMutex;
Snapshot g_snapshot{};
std::atomic<bool> g_enabled{false};
std::atomic<float> g_fontSize{26.0f};
std::chrono::steady_clock::time_point g_nextMetadataRetry{};
bool g_metadataFailureLogged = false;
bool g_resetPositionRequested = false;

void Publish(Snapshot snapshot) {
    std::lock_guard<std::mutex> lock(g_snapshotMutex);
    g_snapshot = std::move(snapshot);
}

bool ResolveEntry(void*& function, const il2cpp_runtime::Method*& method,
                  il2cpp_runtime::Class* klass, const char* name, int parameterCount) {
    method = il2cpp_runtime::FindMethod(klass, name, parameterCount);
    function = il2cpp_runtime::GetMethodEntry(method);
    return function && il2cpp_runtime::IsInGameAssembly(function) && il2cpp_runtime::IsExecutable(function);
}

void ResolveMetadata() {
    Metadata metadata{};
    auto* networkClass = il2cpp_runtime::FindClass("", "Network");
    auto* playerSpotClass = il2cpp_runtime::FindNestedClass(networkClass, "PlayerSpot");
    auto* playerClass = il2cpp_runtime::FindClass("", "Player");
    auto* playerSanityClass = il2cpp_runtime::FindClass("", "PlayerSanity");
    auto* levelValuesClass = il2cpp_runtime::FindClass("", "LevelValues");
    auto* difficultyClass = il2cpp_runtime::FindClass("", "Difficulty");
    auto* gameControllerClass = il2cpp_runtime::FindClass("", "GameController");

    void* networkEntry = nullptr;
    void* levelValuesEntry = nullptr;
    void* averageEntry = nullptr;
    std::size_t playerSpotsVersionOffset = 0;
    il2cpp_runtime::FindInstanceListFieldByElementType(
        networkClass, playerSpotClass, metadata.playerSpotsOffset, playerSpotsVersionOffset);
    const bool resolved =
        ResolveEntry(networkEntry, metadata.networkMethod, networkClass, "get_Instance", 0) &&
        ResolveEntry(levelValuesEntry, metadata.levelValuesMethod, levelValuesClass, "get_Instance", 0) &&
        ResolveEntry(averageEntry, metadata.averageMethod, gameControllerClass,
                     "GetAveragePlayerInsanity", 0) &&
        il2cpp_runtime::FindInstanceFieldByName(
            playerSpotClass, "player", metadata.spotPlayerOffset) &&
        il2cpp_runtime::FindInstanceFieldByName(
            playerSpotClass, "accountName", metadata.spotAccountNameOffset) &&
        il2cpp_runtime::FindInstanceFieldByType(
            playerClass, playerSanityClass, metadata.playerSanityOffset) &&
        il2cpp_runtime::FindInstanceFieldByName(
            difficultyClass, "sanityMonitor", metadata.sanityMonitorOffset);

    metadata.getNetwork = reinterpret_cast<GetInstanceFn>(networkEntry);
    metadata.getLevelValues = reinterpret_cast<GetInstanceFn>(levelValuesEntry);
    metadata.getAverageInsanity = reinterpret_cast<GetAverageInsanityFn>(averageEntry);
    metadata.valid = resolved;
    if (!metadata.valid) {
        if (!g_metadataFailureLogged) LOG("SanityDisplay: metadata unavailable; retrying");
        g_metadataFailureLogged = true;
        g_nextMetadataRetry = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        g_metadata = {};
        return;
    }
    g_metadataFailureLogged = false;
    g_metadata = metadata;
}

void SubmitPosition(timer_logic::Position position) {
    auto& settings = settings_store::Get().sanityDisplay;
    ImGui::SetWindowPos(kWindowId, {position.x, position.y}, ImGuiCond_Always);
    if (!(settings.position == position) || !settings.positionInitialized) {
        settings.position = position;
        settings.positionInitialized = true;
        settings_store::MarkDirty();
    }
}

ImVec4 Color(timer_logic::Color color) { return {color.r, color.g, color.b, color.a}; }

enum class TextAlignment { Left, Center, Right };

TextAlignment AlignmentForPosition(float windowCenterX, float displayWidth) {
    if (windowCenterX < displayWidth / 3.0f) return TextAlignment::Left;
    if (windowCenterX > displayWidth * 2.0f / 3.0f) return TextAlignment::Right;
    return TextAlignment::Center;
}

void DrawAlignedLine(const std::string& text, float lineWidth, TextAlignment alignment,
                     timer_logic::Color color) {
    const float textWidth = ImGui::CalcTextSize(text.c_str()).x;
    float offset = 0.0f;
    if (alignment == TextAlignment::Center) offset = (lineWidth - textWidth) * 0.5f;
    else if (alignment == TextAlignment::Right) offset = lineWidth - textWidth;
    const float startX = ImGui::GetCursorPosX();
    ImGui::SetCursorPosX(startX + (std::max)(0.0f, offset));
    ImGui::TextColored(Color(color), "%s", text.c_str());
}

} // namespace

void Initialize() {
    g_metadata = {};
    g_nextMetadataRetry = {};
    g_metadataFailureLogged = false;
    SyncSettings();
    ResolveMetadata();
}

void Shutdown() {
    g_enabled.store(false);
    g_metadata = {};
    g_nextMetadataRetry = {};
    g_metadataFailureLogged = false;
    ClearSnapshot();
}

void SyncSettings() {
    const auto& settings = settings_store::Get().sanityDisplay;
    g_enabled.store(settings.enabled);
    g_fontSize.store(settings.fontSize, std::memory_order_relaxed);
}
void ClearSnapshot() { Publish({}); }

void OnGameThreadUpdate() {
    if (!g_enabled.load()) { ClearSnapshot(); return; }
    const auto now = std::chrono::steady_clock::now();
    if (!g_metadata.valid && now >= g_nextMetadataRetry) ResolveMetadata();
    if (!g_metadata.valid) { ClearSnapshot(); return; }

    Snapshot snapshot{};
    const auto levelValues = reinterpret_cast<std::uintptr_t>(
        g_metadata.getLevelValues(g_metadata.levelValuesMethod));
    std::uintptr_t difficulty = 0;
    std::uint8_t sanityMonitor = 0;
    if (!levelValues || !il2cpp_runtime::ReadPointer(
            levelValues + kCurrentDifficultyOffset, difficulty) || !difficulty ||
        !il2cpp_runtime::ReadByte(difficulty + g_metadata.sanityMonitorOffset, sanityMonitor)) {
        Publish(std::move(snapshot));
        return;
    }
    if (!sanityMonitor) {
        snapshot.state = MonitorState::Unsupported;
        Publish(std::move(snapshot));
        return;
    }
    snapshot.state = MonitorState::Supported;

    const auto network = reinterpret_cast<std::uintptr_t>(g_metadata.getNetwork(g_metadata.networkMethod));
    std::uintptr_t list = 0;
    if (network && il2cpp_runtime::ReadPointer(network + g_metadata.playerSpotsOffset, list)) {
        for (const auto spot : il2cpp_runtime::ReadManagedList(list, 16)) {
            if (!spot || snapshot.count >= snapshot.players.size()) break;
            std::uintptr_t player = 0;
            std::uintptr_t name = 0;
            std::uintptr_t sanityObject = 0;
            float insanity = 0.0f;
            if (!il2cpp_runtime::ReadPointer(spot + g_metadata.spotPlayerOffset, player) || !player ||
                !il2cpp_runtime::ReadPointer(spot + g_metadata.spotAccountNameOffset, name) ||
                !il2cpp_runtime::ReadPointer(player + g_metadata.playerSanityOffset, sanityObject) || !sanityObject ||
                !il2cpp_runtime::ReadFloat(sanityObject + kInsanityOffset, insanity) || !std::isfinite(insanity)) continue;
            std::string playerName = il2cpp_runtime::CopyUtf8String(name, "");
            if (playerName.empty()) playerName = "--";
            font_store::RequestText(g_fontSize.load(std::memory_order_relaxed), playerName);
            if (snapshot.count < snapshot.players.size()) {
#pragma warning(suppress: 28020) // PREfast does not carry the array-size guard through post-increment.
                snapshot.players[snapshot.count++] = {
                    std::move(playerName), std::clamp(100.0f - insanity, 0.0f, 100.0f)};
            }
        }
    }

    const auto gameController = il2cpp_runtime::ReadStaticObjectFieldByType(
        "GameController", "GameController");
    if (gameController) {
        const float averageInsanity = g_metadata.getAverageInsanity(
            reinterpret_cast<void*>(gameController), g_metadata.averageMethod);
        if (std::isfinite(averageInsanity)) {
            snapshot.average = std::clamp(100.0f - averageInsanity, 0.0f, 100.0f);
            snapshot.averageValid = true;
        }
    }
    Publish(std::move(snapshot));
}

void RequestResetPosition() {
    settings_store::ClearOverlayPlacement(settings_store::OverlayPlacementId::Sanity);
    g_resetPositionRequested = true;
}

timer_logic::MovableWindowResult DrawWindow(bool menuVisible, bool altDragActive) {
    (void)menuVisible;
    auto& settings = settings_store::Get().sanityDisplay;
    if (!settings.enabled) return {};
    Snapshot snapshot;
    { std::lock_guard<std::mutex> lock(g_snapshotMutex); snapshot = g_snapshot; }

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
    std::array<std::string, 5> lines{};
    std::size_t lineCount = 0;
    if (snapshot.state == MonitorState::Unsupported) {
        lines[lineCount++] = u8"游戏设置不支持显示";
    } else if (snapshot.state == MonitorState::Unavailable) {
        lines[lineCount++] = u8"理智数据暂不可用";
    } else {
        for (std::size_t i = 0; i < snapshot.count && lineCount < lines.size(); ++i) {
            char percentage[16]{};
            std::snprintf(percentage, sizeof(percentage), "%.0f%%", snapshot.players[i].sanity);
            lines[lineCount++] = snapshot.players[i].name + u8"：" + percentage;
        }
        if (snapshot.averageValid && lineCount < lines.size())
        {
            char percentage[16]{};
            std::snprintf(percentage, sizeof(percentage), "%.0f%%", snapshot.average);
            lines[lineCount++] = std::string(u8"团队平均值：") + percentage;
        }
        else if (lineCount < lines.size())
            lines[lineCount++] = u8"团队平均值：--";
    }

    float lineWidth = 260.0f * settings.fontSize / 26.0f;
    for (std::size_t i = 0; i < lineCount; ++i)
        lineWidth = (std::max)(lineWidth, ImGui::CalcTextSize(lines[i].c_str()).x);
    const ImVec2 currentPosition = ImGui::GetWindowPos();
    const TextAlignment alignment = AlignmentForPosition(
        currentPosition.x + lineWidth * 0.5f, io.DisplaySize.x);
    for (std::size_t i = 0; i < lineCount; ++i)
        DrawAlignedLine(lines[i], lineWidth, alignment, settings.color);
    ImGui::Dummy({lineWidth, 0.0f});
    ImGui::PopFont();

    const ImVec2 size = ImGui::GetWindowSize();
    if (g_resetPositionRequested || !settings.positionInitialized) {
        settings.position = timer_logic::DefaultOverlayPosition(
            timer_logic::DefaultOverlaySlot::TopRight,
            {io.DisplaySize.x, io.DisplaySize.y}, {size.x, size.y});
        settings.positionInitialized = true;
        g_resetPositionRequested = false;
        ImGui::SetWindowPos({settings.position.x, settings.position.y});
        settings_store::MarkDirty();
    }
    const ImVec2 position = ImGui::GetWindowPos();
    const auto clamped = timer_logic::ClampPosition(
        {position.x, position.y}, {size.x, size.y}, {io.DisplaySize.x, io.DisplaySize.y});
    if (clamped.x != position.x || clamped.y != position.y) ImGui::SetWindowPos({clamped.x, clamped.y});
    ImGui::End();
    if ((canMove || clamped.x != position.x || clamped.y != position.y) && !(settings.position == clamped)) {
        settings.position = clamped;
        settings.positionInitialized = true;
        settings_store::MarkDirty();
    }
    return {kWindowId, {clamped, {size.x, size.y}}, canMove, dragging, &SubmitPosition};
}

void DrawSettings() {
    auto& settings = settings_store::Get().sanityDisplay;
    bool changed = false;
    settings_ui::LabelControlRow(u8"启用:");
    changed |= ImGui::Checkbox("##sanity_enabled", &settings.enabled);
    settings_ui::LabelControlRow(u8"固定:");
    changed |= ImGui::Checkbox("##sanity_fixed", &settings.fixed);
    int index = font_store::IndexOf(settings.fontSize);
    settings_ui::LabelControlRow(u8"字体大小:");
    ImGui::SetNextItemWidth(100.0f);
    if (ImGui::Combo("##sanity_font", &index, font_store::Labels(), static_cast<int>(font_store::kFontSizeCount))) {
        settings.fontSize = font_store::Sizes()[index];
        changed = true;
    }
    settings_ui::LabelControlRow(u8"字体颜色:");
    changed |= ImGui::ColorEdit4("##sanity_color", &settings.color.r, ImGuiColorEditFlags_NoInputs);
    if (ImGui::Button(u8"重置默认设置##sanity")) {
        const bool enabled = settings.enabled;
        settings = {};
        settings.enabled = enabled;
        RequestResetPosition();
        settings_store::ResetFeatureHotkeys(settings_store::FeatureId::Sanity);
        changed = true;
    }
    if (changed) { SyncSettings(); settings_store::MarkDirty(); }
}

} // namespace sanity_display
