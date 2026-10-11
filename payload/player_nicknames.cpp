#include "player_nicknames.h"

#include "feature_logic.h"
#include "font_store.h"
#include "il2cpp_runtime.h"
#include "settings_store.h"
#include "settings_ui.h"

#include "imgui.h"

#include <cmath>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace player_nicknames {
namespace {

struct Vector3 { float x, y, z; };
struct Entry { std::int32_t actor; std::string name; float x, y, distance; };
using GetInstanceFn = void* (*)(const il2cpp_runtime::Method*);
using GetTransformFn = void* (*)(void*, const il2cpp_runtime::Method*);
using GetPositionFn = void (*)(void*, Vector3*, const il2cpp_runtime::Method*);
using WorldToScreenFn = void (*)(void*, Vector3*, int, Vector3*, const il2cpp_runtime::Method*);
using GetIntFn = int (*)(void*, const il2cpp_runtime::Method*);

// Kept in lockstep with Roomten hide_seek/combat.cpp for the current game build.
constexpr std::uintptr_t kPlayerIsDead = 0x28;
constexpr std::uintptr_t kPlayerHeadTarget = 0x88;
// Verified against the current dump; generic List<T> metadata may be unavailable
// during startup. ReadManagedList still validates the object, array and count.
constexpr std::uintptr_t kNetworkPlayerSpots = 0x38;

struct Api {
    GetInstanceFn networkInstance = nullptr; const il2cpp_runtime::Method* networkMethod = nullptr;
    GetTransformFn getTransform = nullptr; const il2cpp_runtime::Method* transformMethod = nullptr;
    GetPositionFn getPosition = nullptr; const il2cpp_runtime::Method* positionMethod = nullptr;
    WorldToScreenFn worldToScreen = nullptr; const il2cpp_runtime::Method* worldMethod = nullptr;
    GetIntFn pixelWidth = nullptr; const il2cpp_runtime::Method* widthMethod = nullptr;
    GetIntFn pixelHeight = nullptr; const il2cpp_runtime::Method* heightMethod = nullptr;
};

struct Metadata {
    bool valid = false;
    std::uintptr_t localPlayerOffset = 0;
    std::uintptr_t cameraOffset = 0;
    std::uintptr_t headSocketOffset = 0;
    std::uintptr_t modelHeadSpotOffset = 0;
    std::uintptr_t spotsOffset = 0;
    std::uintptr_t spotPhotonOffset = 0;
    std::uintptr_t spotPlayerOffset = 0;
    std::size_t actorOffset = 0, isLocalOffset = 0, nicknameOffset = 0;
};

Api g_api{};
Metadata g_metadata{};
std::chrono::steady_clock::time_point g_nextResolve{};
std::mutex g_mutex;
std::vector<Entry> g_entries;
std::atomic<bool> g_enabled{false};
std::atomic<std::int64_t> g_lastUpdateMs{0};
std::atomic<bool> g_resolutionReady{false};

std::int64_t NowMilliseconds() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

void ClearEntries() {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_entries.clear();
}

template <typename T>
void Resolve(T& fn, const il2cpp_runtime::Method*& method, const char* ns, const char* klass, const char* name, int count) {
    method = il2cpp_runtime::FindMethod(il2cpp_runtime::FindClass(ns, klass), name, count);
    void* entry = il2cpp_runtime::GetMethodEntry(method);
    // Engine methods can resolve directly to executable UnityPlayer code.
    fn = entry && il2cpp_runtime::IsExecutable(entry)
        ? reinterpret_cast<T>(entry) : nullptr;
}

void ResolveApi() {
    g_nextResolve = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    Resolve(g_api.networkInstance, g_api.networkMethod, "", "Network", "get_Instance", 0);
    Resolve(g_api.getTransform, g_api.transformMethod, "UnityEngine", "Component", "get_transform", 0);
    Resolve(g_api.getPosition, g_api.positionMethod, "UnityEngine", "Transform", "get_position_Injected", 1);
    Resolve(g_api.worldToScreen, g_api.worldMethod, "UnityEngine", "Camera", "WorldToScreenPoint_Injected", 3);
    Resolve(g_api.pixelWidth, g_api.widthMethod, "UnityEngine", "Camera", "get_scaledPixelWidth", 0);
    Resolve(g_api.pixelHeight, g_api.heightMethod, "UnityEngine", "Camera", "get_scaledPixelHeight", 0);
    if (!g_api.pixelWidth)
        Resolve(g_api.pixelWidth, g_api.widthMethod, "UnityEngine", "Camera", "get_pixelWidth", 0);
    if (!g_api.pixelHeight)
        Resolve(g_api.pixelHeight, g_api.heightMethod, "UnityEngine", "Camera", "get_pixelHeight", 0);

    Metadata metadata{};
    auto* networkClass = il2cpp_runtime::FindClass("", "Network");
    auto* localPlayerClass = il2cpp_runtime::FindClass("", "LocalPlayer");
    auto* cameraClass = il2cpp_runtime::FindClass("UnityEngine", "Camera");
    auto* playerClass = il2cpp_runtime::FindClass("", "Player");
    auto* socketClass = il2cpp_runtime::FindClass("", "NetworkedHeadMountedSocket");
    auto* spotClass = il2cpp_runtime::FindNestedClass(networkClass, "PlayerSpot");
    auto* photonClass = il2cpp_runtime::FindClass("Photon.Realtime", "Player");
    std::size_t listVersion = 0;
    metadata.spotsOffset = kNetworkPlayerSpots;
    il2cpp_runtime::FindInstanceListFieldByElementType(networkClass, spotClass, metadata.spotsOffset, listVersion);
    // The direct head target below remains usable without a head-mounted socket.
    il2cpp_runtime::FindInstanceFieldByType(playerClass, socketClass, metadata.headSocketOffset);
    il2cpp_runtime::FindInstanceFieldByName(socketClass, "modelHeadSpot", metadata.modelHeadSpotOffset);
    metadata.valid =
        il2cpp_runtime::FindInstanceFieldByType(networkClass, localPlayerClass, metadata.localPlayerOffset) &&
        il2cpp_runtime::FindInstanceFieldByType(localPlayerClass, cameraClass, metadata.cameraOffset) &&
        il2cpp_runtime::FindInstanceFieldByName(spotClass, "photonPlayer", metadata.spotPhotonOffset) &&
        il2cpp_runtime::FindInstanceFieldByName(spotClass, "player", metadata.spotPlayerOffset) &&
        il2cpp_runtime::FindInstanceFieldByName(photonClass, "actorNumber", metadata.actorOffset) &&
        il2cpp_runtime::FindInstanceFieldByName(photonClass, "IsLocal", metadata.isLocalOffset) &&
        il2cpp_runtime::FindInstanceFieldByName(photonClass, "nickName", metadata.nicknameOffset) &&
        g_api.networkInstance && g_api.getTransform && g_api.getPosition &&
        g_api.worldToScreen && g_api.pixelWidth && g_api.pixelHeight;
    g_metadata = metadata;
    g_resolutionReady.store(metadata.valid);
}

bool PositionOf(void* transform, Vector3& value) {
    if (!transform || !g_api.getPosition) return false;
    g_api.getPosition(transform, &value, g_api.positionMethod);
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

void* HeadTransform(std::uintptr_t player) {
    std::uintptr_t socket = 0, head = 0;
    if (g_metadata.headSocketOffset && g_metadata.modelHeadSpotOffset &&
        il2cpp_runtime::ReadPointer(player + g_metadata.headSocketOffset, socket) && socket &&
        il2cpp_runtime::ReadPointer(socket + g_metadata.modelHeadSpotOffset, head) && head) {
        return reinterpret_cast<void*>(head);
    }
    if (il2cpp_runtime::ReadPointer(player + kPlayerHeadTarget, head) && head)
        return reinterpret_cast<void*>(head);
    return nullptr;
}


} // namespace

void Initialize() { SyncSettings(); ResolveApi(); }
void Shutdown() {
    ClearEntries();
    g_lastUpdateMs.store(0);
    g_api = {};
    g_metadata = {};
    g_resolutionReady.store(false);
    g_nextResolve = {};
}
void SyncSettings() { g_enabled.store(settings_store::Get().playerNicknames.enabled); }
void ClearSnapshot() {
    g_lastUpdateMs.store(0, std::memory_order_release);
    ClearEntries();
}

void OnGameThreadUpdate() {
    if (!g_enabled.load()) { ClearEntries(); return; }
    if (!g_metadata.valid && std::chrono::steady_clock::now() >= g_nextResolve) ResolveApi();
    if (!g_metadata.valid) { ClearSnapshot(); return; }
    std::vector<Entry> entries;
    entries.reserve(16);
    void* networkObject = g_api.networkInstance ? g_api.networkInstance(g_api.networkMethod) : nullptr;
    const auto network = reinterpret_cast<std::uintptr_t>(networkObject);
    std::uintptr_t list = 0, localPlayer = 0, camera = 0;
    if (!network || !il2cpp_runtime::ReadPointer(network + g_metadata.spotsOffset, list) ||
        !il2cpp_runtime::ReadPointer(network + g_metadata.localPlayerOffset, localPlayer) || !localPlayer ||
        !il2cpp_runtime::ReadPointer(localPlayer + g_metadata.cameraOffset, camera) || !camera) {
        ClearEntries();
        return;
    }
    void* cameraTransform = g_api.getTransform ?
        g_api.getTransform(reinterpret_cast<void*>(camera), g_api.transformMethod) : nullptr;
    Vector3 cameraPosition{};
    const int width = g_api.pixelWidth ? g_api.pixelWidth(reinterpret_cast<void*>(camera), g_api.widthMethod) : 0;
    const int height = g_api.pixelHeight ? g_api.pixelHeight(reinterpret_cast<void*>(camera), g_api.heightMethod) : 0;
    if (!g_api.worldToScreen || !PositionOf(cameraTransform, cameraPosition) || width <= 0 || height <= 0) {
        ClearEntries();
        return;
    }

    const auto spots = il2cpp_runtime::ReadManagedList(list, 16);
    for (const auto spot : spots) {
        std::uintptr_t photon = 0, player = 0, nickname = 0;
        std::uint8_t isLocal = 0;
        std::uint8_t isDead = 0;
        std::int32_t actor = 0;
        if (!spot || !il2cpp_runtime::ReadPointer(spot + g_metadata.spotPhotonOffset, photon) || !photon ||
            !il2cpp_runtime::ReadByte(photon + g_metadata.isLocalOffset, isLocal) || isLocal ||
            !il2cpp_runtime::ReadInt32(photon + g_metadata.actorOffset, actor) ||
            !il2cpp_runtime::ReadPointer(photon + g_metadata.nicknameOffset, nickname) ||
            !il2cpp_runtime::ReadPointer(spot + g_metadata.spotPlayerOffset, player) || !player ||
            !il2cpp_runtime::ReadByte(player + kPlayerIsDead, isDead) || isDead) continue;
        const std::string name = il2cpp_runtime::CopyUtf8String(nickname, "");
        if (name.empty()) continue;
        font_store::RequestText(24.0f, name);
        Vector3 head{};
        if (!PositionOf(HeadTransform(player), head)) continue;
        Vector3 screen{};
        g_api.worldToScreen(reinterpret_cast<void*>(camera), &head, 2, &screen, g_api.worldMethod);
        if (screen.z <= 0.0f) continue;
        const auto normalized = feature_logic::NormalizeScreenPoint(screen.x, screen.y, static_cast<float>(width), static_cast<float>(height));
        if (!normalized.valid) continue;
        const float dx = head.x - cameraPosition.x;
        const float dy = head.y - cameraPosition.y;
        const float dz = head.z - cameraPosition.z;
        const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);
        entries.push_back({actor, name, normalized.x, normalized.y, distance});
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    g_entries = std::move(entries);
    g_lastUpdateMs.store(NowMilliseconds(), std::memory_order_release);
}

void Draw() {
    const auto& settings = settings_store::Get().playerNicknames;
    if (!settings.enabled) return;
    const auto now = NowMilliseconds();
    const auto lastUpdate = g_lastUpdateMs.load(std::memory_order_acquire);
    if (lastUpdate == 0 || now - lastUpdate > 500) {
        ClearEntries();
        return;
    }
    std::vector<Entry> entries; { std::lock_guard<std::mutex> lock(g_mutex); entries = g_entries; }
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    ImFont* font = font_store::Get(24.0f);
    for (const auto& entry : entries) {
        const float fontSize = feature_logic::NicknameFontSize(entry.distance);
        const ImVec2 textSize = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, entry.name.c_str());
        const ImVec2 position{entry.x*display.x-textSize.x*0.5f,
                              feature_logic::NicknameAnchorY(entry.y*display.y, settings.height)};
        draw->AddText(font, fontSize, position, ImGui::ColorConvertFloat4ToU32(settings_ui::ApplyOpacity(
            {settings.color.r, settings.color.g, settings.color.b, settings.color.a},
            settings_store::FeatureId::PlayerNicknames)), entry.name.c_str());
    }
}

void DrawSettings() {
    auto& settings = settings_store::Get().playerNicknames;
    bool changed = false;
    if (settings.enabled && !g_resolutionReady.load())
        ImGui::TextUnformatted(u8"数据暂不可用，正在重试");
    settings_ui::LabelControlRow(u8"启用:"); changed |= ImGui::Checkbox("##nicknames_enabled", &settings.enabled);
    settings_ui::LabelControlRow(u8"高度:"); ImGui::SetNextItemWidth(180.0f);
    changed |= ImGui::SliderFloat("##nicknames_height", &settings.height, 0.0f, 100.0f, "%.1f px");
    settings_ui::LabelControlRow(u8"颜色:"); changed |= ImGui::ColorEdit4("##nicknames_color", &settings.color.r, ImGuiColorEditFlags_NoInputs);
    if (ImGui::Button(u8"重置默认设置##nicknames")) {
        settings_store::ResetFeatureSettings(settings_store::Get(), settings_store::FeatureId::PlayerNicknames);
        settings_store::ResetFeatureHotkeys(settings_store::FeatureId::PlayerNicknames);
        changed=true;
    }
    if (changed) { SyncSettings(); settings_store::MarkDirty(); }
}

} // namespace player_nicknames
