#include "font_store.h"
#include "game_time.h"
#include "ghost_status.h"
#include "hunt_timer.h"
#include "il2cpp_runtime.h"
#include "player_nicknames.h"
#include "settings_store.h"
#include "imgui.h"
#include "imgui_internal.h"

#include <array>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#define CHECK(condition) do { if (!(condition)) { \
    std::fprintf(stderr, "CHECK failed: %s (%s:%d)\n", #condition, __FILE__, __LINE__); return 1; \
} } while (false)

namespace fixture {
timer_logic::Bounds gameBounds{{700, 20}, {200, 26}};
std::uint64_t huntSeconds = 0;
struct Vector3 { float x, y, z; };
std::array<unsigned char, 512> network{}, local{}, camera{}, spot{}, photon{}, player{}, list{}, head{};
std::array<int, 7> classes{};
bool missingCore = false;
bool missingOptional = true;
bool malformedList = false;
enum ClassId { Network, Local, Camera, Player, Socket, Spot, Photon };
void* ClassPtr(ClassId id) { return &classes[static_cast<std::size_t>(id)]; }
template<class T> void Put(std::array<unsigned char, 512>& object, std::size_t offset, T value) {
    std::memcpy(object.data() + offset, &value, sizeof(value));
}
void* NetworkInstance(const void*) { return network.data(); }
void* Transform(void* object, const void*) { return object; }
void Position(void* object, Vector3* value, const void*) {
    *value = object == camera.data() ? Vector3{0, 0, 0} : Vector3{0, 1, 3};
}
void Project(void*, Vector3*, int, Vector3* screen, const void*) { *screen = {960, 540, 3}; }
int Width(void*, const void*) { return 1920; }
int Height(void*, const void*) { return 1080; }
struct Method { void* entry; };
std::array<Method, 6> methods{{
    {reinterpret_cast<void*>(&NetworkInstance)}, {reinterpret_cast<void*>(&Transform)},
    {reinterpret_cast<void*>(&Position)}, {reinterpret_cast<void*>(&Project)},
    {reinterpret_cast<void*>(&Width)}, {reinterpret_cast<void*>(&Height)}}};
bool Read(std::uintptr_t address, void* output, std::size_t count) {
    for (const auto* object : {&network, &local, &camera, &spot, &photon, &player, &list, &head}) {
        const auto begin = reinterpret_cast<std::uintptr_t>(object->data());
        if (address >= begin && address - begin <= object->size() - count) {
            std::memcpy(output, reinterpret_cast<const void*>(address), count);
            return true;
        }
    }
    return false;
}
void Prepare() {
    Put(network, 0x30, local.data()); Put(network, 0x38, list.data());
    Put(local, 0x30, camera.data()); Put(spot, 0x18, photon.data()); Put(spot, 0x38, player.data());
    Put(photon, 0x18, std::int32_t{2}); Put(photon, 0x1C, std::uint8_t{0});
    Put(photon, 0x20, std::uintptr_t{123}); Put(player, 0x28, std::uint8_t{0});
    Put(player, 0x88, head.data());
}
}

namespace game_time { timer_logic::Bounds LastWindowBounds() { return fixture::gameBounds; } }
namespace ghost_status { HuntTimingSnapshot ReadHuntTimingSnapshot() { return {fixture::huntSeconds}; } }

// Exercise the real nickname sampler and renderer with controlled IL2CPP data.
namespace il2cpp_runtime {
Class* FindClass(const char* ns, const char* name) {
    using namespace fixture;
    if (std::strcmp(name, "Network") == 0) return ClassPtr(Network);
    if (std::strcmp(name, "LocalPlayer") == 0) return ClassPtr(Local);
    if (std::strcmp(name, "Camera") == 0) return ClassPtr(Camera);
    if (std::strcmp(name, "NetworkedHeadMountedSocket") == 0) return ClassPtr(Socket);
    if (std::strcmp(name, "Player") == 0) return ClassPtr(std::strcmp(ns, "Photon.Realtime") == 0 ? Photon : Player);
    return ClassPtr(Camera);
}
Class* FindNestedClass(Class*, const char*) { return fixture::ClassPtr(fixture::Spot); }
const Method* FindMethod(Class*, const char* name, int) {
    using namespace fixture;
    if (missingCore && std::strcmp(name, "get_Instance") == 0) return nullptr;
    if (missingOptional && std::strstr(name, "scaledPixel")) return nullptr;
    const char* names[] = {"get_Instance", "get_transform", "get_position_Injected",
                          "WorldToScreenPoint_Injected", "get_pixelWidth", "get_pixelHeight"};
    for (std::size_t i = 0; i < methods.size(); ++i)
        if (std::strcmp(name, names[i]) == 0) return &methods[i];
    if (std::strcmp(name, "get_scaledPixelWidth") == 0) return &methods[4];
    if (std::strcmp(name, "get_scaledPixelHeight") == 0) return &methods[5];
    return nullptr;
}
void* GetMethodEntry(const Method* method) { return method ? static_cast<const fixture::Method*>(method)->entry : nullptr; }
bool IsInGameAssembly(const void*, std::size_t) { return false; } // Engine entry points outside GameAssembly.
bool IsExecutable(const void* entry) { return entry != nullptr; }
bool FindInstanceFieldByType(Class* owner, Class* target, std::uintptr_t& offset) {
    using namespace fixture;
    if ((owner == ClassPtr(Network) && target == ClassPtr(Local)) ||
        (owner == ClassPtr(Local) && target == ClassPtr(Camera))) { offset = 0x30; return true; }
    return false; // Missing optional socket metadata must not hide nicknames.
}
bool FindInstanceFieldByName(Class* owner, const char* name, std::size_t& offset) {
    using namespace fixture;
    if (owner == ClassPtr(Spot) && std::strcmp(name, "photonPlayer") == 0) offset = 0x18;
    else if (owner == ClassPtr(Spot) && std::strcmp(name, "player") == 0) offset = 0x38;
    else if (owner == ClassPtr(Photon) && std::strcmp(name, "actorNumber") == 0) offset = 0x18;
    else if (owner == ClassPtr(Photon) && std::strcmp(name, "IsLocal") == 0) offset = 0x1C;
    else if (owner == ClassPtr(Photon) && std::strcmp(name, "nickName") == 0) offset = 0x20;
    else return false;
    return true;
}
bool FindInstanceListFieldByElementType(Class*, Class*, std::uintptr_t&, std::size_t&) { return false; }
bool ReadPointer(std::uintptr_t a, std::uintptr_t& v) { return fixture::Read(a, &v, sizeof(v)); }
bool ReadInt32(std::uintptr_t a, std::int32_t& v) { return fixture::Read(a, &v, sizeof(v)); }
bool ReadByte(std::uintptr_t a, std::uint8_t& v) { return fixture::Read(a, &v, sizeof(v)); }
std::vector<std::uintptr_t> ReadManagedList(std::uintptr_t address, std::int32_t) {
    if (fixture::malformedList || address != reinterpret_cast<std::uintptr_t>(fixture::list.data())) return {};
    return {reinterpret_cast<std::uintptr_t>(fixture::spot.data())};
}
std::string CopyUtf8String(std::uintptr_t address, const std::string& fallback) { return address == 123 ? "Teammate" : fallback; }
}

int main() {
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = {1920, 1080};
    CHECK(font_store::Initialize(io));
    io.Fonts->SetTexID(static_cast<ImTextureID>(1));
    auto& settings = settings_store::Get();
    settings = {};
    const auto huntFrame = [] {
        ImGui::NewFrame();
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, settings_store::Get().featureVisuals[
            static_cast<std::size_t>(settings_store::FeatureId::HuntTimer)].opacity);
        const auto result = hunt_timer::DrawWindow(false, false);
        ImGui::PopStyleVar();
        ImGui::Render();
        return result;
    };
    const auto visible = [](const timer_logic::MovableWindowResult& result) {
        auto* window = ImGui::FindWindowByName("##hunt_timer");
        return result.id && window && !window->Hidden && window->DrawList->VtxBuffer.Size > 0 &&
            result.bounds.position.x >= 0 && result.bounds.position.y >= 0 &&
            result.bounds.position.x + result.bounds.size.width <= ImGui::GetIO().DisplaySize.x &&
            result.bounds.position.y + result.bounds.size.height <= ImGui::GetIO().DisplaySize.y;
    };
    auto frame = huntFrame();
    CHECK(visible(frame)); // Idle timer must draw without a hunt or a menu.
    CHECK(frame.bounds.position.y >= 52);
    CHECK(settings.huntTimer.position == frame.bounds.position);
    settings_store::SetAllFeaturesEnabled(settings, false);
    CHECK(!huntFrame().id);
    settings_store::SetAllFeaturesEnabled(settings, true);
    CHECK(visible(huntFrame()));
    settings.huntTimer.positionInitialized = false;
    settings.overlayPlacements[static_cast<std::size_t>(settings_store::OverlayPlacementId::HuntTimer)] = {{1, 1}, true};
    frame = huntFrame();
    CHECK(visible(frame) && frame.bounds.position.y < 100); // Stale anchor cannot undo a reset.
    settings.featureVisuals[static_cast<std::size_t>(settings_store::FeatureId::HuntTimer)].opacity = 0;
    settings.huntTimer.color.a = 0;
    hunt_timer::ResetSettings();
    CHECK(settings.featureVisuals[static_cast<std::size_t>(settings_store::FeatureId::HuntTimer)].opacity == 1);
    CHECK(settings.huntTimer.color.a == 1 && visible(huntFrame()));
    settings.gameTime.enabled = false;
    fixture::gameBounds = {{9000, 9000}, {300, 56}};
    hunt_timer::RequestResetPosition();
    CHECK(visible(huntFrame()));
    io.DisplaySize = {640, 480};
    fixture::huntSeconds = 601;
    settings.huntTimer.fontSize = 56;
    CHECK(visible(huntFrame()));
    settings_store::ResetAllFeatureSettings(settings);
    hunt_timer::RequestResetPosition();
    CHECK(visible(huntFrame()));

    fixture::Prepare();
    io.DisplaySize = {1920, 1080};
    settings.playerNicknames.enabled = true;
    const auto nicknameFrame = [] {
        player_nicknames::OnGameThreadUpdate();
        ImGui::NewFrame();
        player_nicknames::Draw();
        const bool hasText = ImGui::GetForegroundDrawList()->VtxBuffer.Size > 0;
        ImGui::Render();
        return hasText;
    };
    player_nicknames::Initialize();
    CHECK(nicknameFrame()); // Missing List/socket/scaled-size metadata and external engine entries.
    fixture::Put(fixture::photon, 0x1C, std::uint8_t{1});
    CHECK(!nicknameFrame()); // Never display local-player data as a remote teammate.
    fixture::Put(fixture::photon, 0x1C, std::uint8_t{0});
    fixture::malformedList = true;
    CHECK(!nicknameFrame());
    fixture::malformedList = false;
    CHECK(nicknameFrame());
    player_nicknames::Shutdown();
    fixture::missingCore = true;
    player_nicknames::Initialize();
    CHECK(!nicknameFrame()); // No call through a missing required entry point.
    player_nicknames::Shutdown();
    font_store::Shutdown();
    ImGui::DestroyContext();
    return 0;
}
