#include "font_store.h"
#include "game_time.h"
#include "ghost_distance.h"
#include "ghost_runtime.h"
#include "ghost_speed_display.h"
#include "ghost_speed_sync.h"
#include "ghost_status.h"
#include "hunt_timer.h"
#include "il2cpp_runtime.h"
#include "player_nicknames.h"
#include "settings_store.h"
#include "unity_coordinates.h"
#include "imgui.h"
#include "imgui_internal.h"

#include <array>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

#define CHECK(condition) do { if (!(condition)) { \
    std::fprintf(stderr, "CHECK failed: %s (%s:%d)\n", #condition, __FILE__, __LINE__); return 1; \
} } while (false)

namespace fixture {
timer_logic::Bounds gameBounds{{700, 20}, {200, 26}};
std::uint64_t huntSeconds = 0;
bool hunting = false;
bool missingGhostTransform = false;
std::array<unsigned char, 512> ghost{};
int ghostClass{}, navAgentClass{}, navAgent{};
constexpr std::size_t navAgentField = 0x40;
float navSpeed = 2.75f;
bool roomMaster = true;
bool missingCoordinates = false;
int ghostTransformCalls = 0;
int publishCalls = 0;
bool publishedValid = false;
float publishedSpeed = 0.0f;
float remoteSpeed = 0.0f;
using unity_coordinates::Vector3;
std::array<unsigned char, 512> network{}, local{}, camera{}, spot{}, photon{}, player{}, list{}, socket{}, head{};
std::array<int, 9> classes{};
bool missingCore = false;
bool missingOptional = true;
bool missingHeadSpot = false;
bool malformedList = false;
constexpr std::size_t socketField = 0x38, headSpotField = 0x20;
enum ClassId { Network, Local, Camera, Player, Socket, Spot, Photon, Component, TransformClass };
void* ClassPtr(ClassId id) { return &classes[static_cast<std::size_t>(id)]; }
template<class T> void Put(std::array<unsigned char, 512>& object, std::size_t offset, T value) {
    std::memcpy(object.data() + offset, &value, sizeof(value));
}
void* NetworkInstance(const void*) { return network.data(); }
void* Transform(void* object, const void*) {
    if (object == ghost.data()) {
        ++ghostTransformCalls;
        if (missingGhostTransform) return nullptr;
    }
    return object;
}
Vector3 Position(void* object, const void*) {
    if (object == player.data()) return {1, 2, 3};
    if (object == ghost.data()) return {4, 6, 15};
    if (object == camera.data()) return {0, 0, 0};
    if (object == head.data()) return {0, 1, 3};
    return {};
}
float projectedDepth = 3.0f;
Vector3 Project(void* receiver, Vector3 world, const void*) {
    if (receiver == camera.data() && world.x == 0 && world.y == 1 && world.z == 3)
        return {960, 540, projectedDepth};
    return {};
}
int Width(void*, const void*) { return 1920; }
int Height(void*, const void*) { return 1080; }
struct Method { void* entry; };
float GetSpeed(void* agent, const void*) { return agent == &navAgent ? navSpeed : -1.0f; }
Method nativeSpeedMethod{reinterpret_cast<void*>(&GetSpeed)};
std::array<Method, 6> methods{{
    {reinterpret_cast<void*>(&NetworkInstance)}, {reinterpret_cast<void*>(&Transform)},
    {reinterpret_cast<void*>(&Position)}, {reinterpret_cast<void*>(&Project)},
    {reinterpret_cast<void*>(&Width)}, {reinterpret_cast<void*>(&Height)}}};
bool Read(std::uintptr_t address, void* output, std::size_t count) {
    for (const auto* object : {&network, &local, &camera, &spot, &photon, &player, &list, &socket, &head, &ghost}) {
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
    Put(local, 0x20, player.data());
    Put(local, 0x30, camera.data()); Put(spot, 0x18, photon.data()); Put(spot, 0x38, player.data());
    Put(photon, 0x18, std::int32_t{2}); Put(photon, 0x1C, std::uint8_t{0});
    Put(photon, 0x20, std::uintptr_t{123}); Put(player, 0x28, std::uint8_t{0});
    Put(player, socketField, socket.data()); Put(socket, headSpotField, head.data());
    Put(player, 0x88, player.data()); // The former fallback now holds an unrelated object.
}
}

namespace game_time { timer_logic::Bounds LastWindowBounds() { return fixture::gameBounds; } }
namespace ghost_runtime {
Snapshot ReadSnapshot() { return {true, reinterpret_cast<std::uintptr_t>(fixture::ghost.data()), 1}; }
}
namespace ghost_status { bool IsHunting() { return fixture::hunting; } }
namespace ghost_speed_sync {
bool PublishLocalSpeed(bool valid, float speed) {
    ++fixture::publishCalls;
    fixture::publishedValid = valid;
    fixture::publishedSpeed = speed;
    return fixture::roomMaster;
}
bool ReadRemoteSpeed(float& speed) {
    if (fixture::roomMaster) return false;
    speed = fixture::remoteSpeed;
    return true;
}
}
namespace ghost_status { HuntTimingSnapshot ReadHuntTimingSnapshot() { return {fixture::huntSeconds}; } }

// Exercise the real nickname sampler and renderer with controlled IL2CPP data.
namespace il2cpp_runtime {
Class* FindClass(const char* ns, const char* name) {
    using namespace fixture;
    if (std::strcmp(name, "Network") == 0) return ClassPtr(Network);
    if (std::strcmp(name, "GhostAI") == 0) return &ghostClass;
    if (std::strcmp(name, "NavMeshAgent") == 0) return &navAgentClass;
    if (std::strcmp(name, "LocalPlayer") == 0) return ClassPtr(Local);
    if (std::strcmp(name, "Camera") == 0) return ClassPtr(Camera);
    if (std::strcmp(ns, "UnityEngine") == 0 && std::strcmp(name, "Component") == 0) return ClassPtr(Component);
    if (std::strcmp(ns, "UnityEngine") == 0 && std::strcmp(name, "Transform") == 0) return ClassPtr(TransformClass);
    if (std::strcmp(name, "NetworkedHeadMountedSocket") == 0) return ClassPtr(Socket);
    if (std::strcmp(name, "Player") == 0) return ClassPtr(std::strcmp(ns, "Photon.Realtime") == 0 ? Photon : Player);
    return nullptr;
}
Class* FindNestedClass(Class*, const char*) { return fixture::ClassPtr(fixture::Spot); }
const Method* FindMethod(Class* owner, const char* name, int count) {
    using namespace fixture;
    if (missingCore && std::strcmp(name, "get_Instance") == 0) return nullptr;
    if (owner == &navAgentClass && std::strcmp(name, "get_speed") == 0 && count == 0)
        return &nativeSpeedMethod;
    if (missingCoordinates && std::strcmp(name, "get_transform") == 0) return nullptr;
    if (missingOptional && std::strstr(name, "scaledPixel")) return nullptr;
    struct Signature { ClassId owner; const char* name; int count; };
    constexpr Signature signatures[] = {
        {Network, "get_Instance", 0}, {Component, "get_transform", 0},
        {TransformClass, "get_position", 0}, {Camera, "WorldToScreenPoint", 1},
        {Camera, "get_pixelWidth", 0}, {Camera, "get_pixelHeight", 0}};
    for (std::size_t i = 0; i < methods.size(); ++i)
        if (owner == ClassPtr(signatures[i].owner) && count == signatures[i].count &&
            std::strcmp(name, signatures[i].name) == 0) return &methods[i];
    if (owner != ClassPtr(Camera) || count != 0) return nullptr;
    if (std::strcmp(name, "get_scaledPixelWidth") == 0) return &methods[4];
    if (std::strcmp(name, "get_scaledPixelHeight") == 0) return &methods[5];
    return nullptr;
}
void* GetMethodEntry(const Method* method) { return method ? static_cast<const fixture::Method*>(method)->entry : nullptr; }
bool IsInGameAssembly(const void* entry, std::size_t) {
    return entry == fixture::methods[0].entry || entry == fixture::nativeSpeedMethod.entry;
}
bool IsExecutable(const void* entry) { return entry != nullptr; }
bool FindInstanceFieldByType(Class* owner, Class* target, std::uintptr_t& offset) {
    using namespace fixture;
    if ((owner == ClassPtr(Network) && target == ClassPtr(Local)) ||
        (owner == ClassPtr(Local) && target == ClassPtr(Camera))) { offset = 0x30; return true; }
    if (owner == ClassPtr(Local) && target == ClassPtr(Player)) { offset = 0x20; return true; }
    if (owner == ClassPtr(Player) && target == ClassPtr(Socket)) { offset = socketField; return true; }
    if (owner == &ghostClass && target == &navAgentClass) { offset = navAgentField; return true; }
    return false;
}
bool FindInstanceFieldByName(Class* owner, const char* name, std::size_t& offset) {
    using namespace fixture;
    if (owner == ClassPtr(Spot) && std::strcmp(name, "photonPlayer") == 0) offset = 0x18;
    else if (owner == ClassPtr(Spot) && std::strcmp(name, "player") == 0) offset = 0x38;
    else if (owner == ClassPtr(Photon) && std::strcmp(name, "actorNumber") == 0) offset = 0x18;
    else if (owner == ClassPtr(Photon) && std::strcmp(name, "IsLocal") == 0) offset = 0x1C;
    else if (owner == ClassPtr(Photon) && std::strcmp(name, "nickName") == 0) offset = 0x20;
    else if (owner == ClassPtr(Socket) && !missingHeadSpot && std::strcmp(name, "modelHeadSpot") == 0)
        offset = headSpotField;
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

std::string CaptureWindowText(timer_logic::MovableWindowResult (*draw)(bool, bool)) {
    ImGui::NewFrame();
    ImGui::LogToBuffer();
    draw(false, false);
    const std::string text = ImGui::GetCurrentContext()->LogBuffer.c_str();
    ImGui::LogFinish();
    ImGui::Render();
    return text;
}

int TestGhostDistanceSampling() {
    settings_store::Get().ghostDistance.enabled = true;
    fixture::hunting = true;
    ghost_distance::Initialize();
    ghost_distance::OnGameThreadUpdate();
    CHECK(CaptureWindowText(&ghost_distance::DrawWindow).find("13.00 m") != std::string::npos);
    fixture::missingGhostTransform = true;
    ghost_distance::OnGameThreadUpdate();
    CHECK(CaptureWindowText(&ghost_distance::DrawWindow).find("-- m") != std::string::npos);
    fixture::missingGhostTransform = false;
    ghost_distance::OnGameThreadUpdate();
    CHECK(CaptureWindowText(&ghost_distance::DrawWindow).find("13.00 m") != std::string::npos);
    fixture::hunting = false;
    ghost_distance::OnGameThreadUpdate();
    CHECK(CaptureWindowText(&ghost_distance::DrawWindow).find("-- m") != std::string::npos);
    ghost_distance::Shutdown();
    return 0;
}

int TestGhostSpeedSampling() {
    auto& settings = settings_store::Get().ghostSpeedDisplay;
    fixture::hunting = true;
    fixture::missingCoordinates = true;
    fixture::ghostTransformCalls = 0;
    fixture::Put(fixture::ghost, fixture::navAgentField, &fixture::navAgent);
    settings.enabled = false;
    ghost_speed_display::Initialize();
    ghost_speed_display::OnGameThreadUpdate();
    CHECK(fixture::publishCalls == 1 && fixture::publishedValid && fixture::publishedSpeed == fixture::navSpeed);

    settings.enabled = true;
    ghost_speed_display::SyncSettings();
    fixture::navSpeed = 1.7f;
    ghost_speed_display::OnGameThreadUpdate();
    CHECK(fixture::publishCalls == 2 && fixture::publishedValid && fixture::publishedSpeed == fixture::navSpeed);
    CHECK(CaptureWindowText(&ghost_speed_display::DrawWindow).find("1.70m/s") != std::string::npos);

    fixture::missingCoordinates = false;
    fixture::Put(fixture::ghost, fixture::navAgentField, std::uintptr_t{0});
    ghost_speed_display::OnGameThreadUpdate();
    CHECK(fixture::publishCalls == 3 && !fixture::publishedValid && fixture::publishedSpeed == 0.0f);
    CHECK(fixture::ghostTransformCalls == 0);

    fixture::roomMaster = false;
    ghost_speed_display::OnGameThreadUpdate();
    CHECK(fixture::publishCalls == 4 && !fixture::publishedValid && fixture::ghostTransformCalls == 1);
    fixture::remoteSpeed = 3.25f;
    CHECK(CaptureWindowText(&ghost_speed_display::DrawWindow).find("3.25m/s") != std::string::npos);

    fixture::hunting = false;
    fixture::Put(fixture::ghost, fixture::navAgentField, &fixture::navAgent);
    ghost_speed_display::OnGameThreadUpdate();
    CHECK(fixture::publishCalls == 5 && !fixture::publishedValid && fixture::publishedSpeed == 0.0f);
    CHECK(CaptureWindowText(&ghost_speed_display::DrawWindow).find("0.00m/s") != std::string::npos);
    ghost_speed_display::Shutdown();
    return 0;
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
    CHECK(TestGhostDistanceSampling() == 0);
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
    CHECK(nicknameFrame()); // Missing List/scaled-size metadata and external engine entries.
    fixture::projectedDepth = -1.0f;
    CHECK(!nicknameFrame());
    fixture::projectedDepth = std::numeric_limits<float>::quiet_NaN();
    CHECK(!nicknameFrame());
    fixture::projectedDepth = 3.0f;
    fixture::Put(fixture::socket, fixture::headSpotField, std::uintptr_t{0});
    CHECK(!nicknameFrame()); // Never treat the former head offset as a Transform.
    fixture::Put(fixture::socket, fixture::headSpotField, fixture::head.data());
    fixture::Put(fixture::photon, 0x1C, std::uint8_t{1});
    CHECK(!nicknameFrame()); // Never display local-player data as a remote teammate.
    fixture::Put(fixture::photon, 0x1C, std::uint8_t{0});
    fixture::malformedList = true;
    CHECK(!nicknameFrame());
    fixture::malformedList = false;
    CHECK(nicknameFrame());
    player_nicknames::Shutdown();
    fixture::missingHeadSpot = true;
    player_nicknames::Initialize();
    CHECK(!nicknameFrame());
    player_nicknames::Shutdown();
    fixture::missingHeadSpot = false;
    fixture::missingCore = true;
    player_nicknames::Initialize();
    CHECK(!nicknameFrame()); // No call through a missing required entry point.
    player_nicknames::Shutdown();

    CHECK(TestGhostSpeedSampling() == 0);
    font_store::Shutdown();
    ImGui::DestroyContext();
    return 0;
}
