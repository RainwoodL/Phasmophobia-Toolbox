#include "ghost_speed_sync.h"

#include "ghost_speed_sync_protocol.h"
#include "il2cpp_runtime.h"
#include "inline_hook.h"
#include "log.h"

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cmath>
#include <cstring>
#include <mutex>
#include <thread>

namespace ghost_speed_sync {
namespace {

struct SendOptions {
    std::int32_t deliveryMode = 1; // DeliveryMode.Reliable
    bool encrypt = false;
    std::uint8_t channel = 0;
    std::uint16_t padding = 0;
};
static_assert(sizeof(SendOptions) == 8, "Photon SendOptions ABI must remain 8 bytes");

using RaiseEventFn = bool (*)(std::uint8_t, void*, void*, SendOptions, const il2cpp_runtime::Method*);
using GetBoolStaticFn = bool (*)(const il2cpp_runtime::Method*);
using GetObjectStaticFn = void* (*)(const il2cpp_runtime::Method*);
using GetIntFn = std::int32_t (*)(void*, const il2cpp_runtime::Method*);
using GetObjectFn = void* (*)(void*, const il2cpp_runtime::Method*);
using GetIntInstanceFn = std::int32_t (*)(void*, const il2cpp_runtime::Method*);
using EventFn = void (*)(void*, void*, const il2cpp_runtime::Method*);
using ConstructorFn = void (*)(void*, const il2cpp_runtime::Method*);

struct Api {
    RaiseEventFn raiseEvent = nullptr;
    GetBoolStaticFn isMasterClient = nullptr;
    GetBoolStaticFn inRoom = nullptr;
    GetObjectStaticFn masterClient = nullptr;
    GetIntInstanceFn actorNumber = nullptr;
    GetObjectFn eventCustomData = nullptr;
    GetIntFn eventSender = nullptr;
    EventFn onEvent = nullptr;
    ConstructorFn raiseEventOptionsCtor = nullptr;
    const il2cpp_runtime::Method* raiseEventMethod = nullptr;
    const il2cpp_runtime::Method* isMasterClientMethod = nullptr;
    const il2cpp_runtime::Method* inRoomMethod = nullptr;
    const il2cpp_runtime::Method* masterClientMethod = nullptr;
    const il2cpp_runtime::Method* actorNumberMethod = nullptr;
    const il2cpp_runtime::Method* eventCustomDataMethod = nullptr;
    const il2cpp_runtime::Method* eventSenderMethod = nullptr;
    const il2cpp_runtime::Method* onEventMethod = nullptr;
    const il2cpp_runtime::Method* raiseEventOptionsCtorMethod = nullptr;
    il2cpp_runtime::Class* byteClass = nullptr;
    il2cpp_runtime::Class* raiseEventOptionsClass = nullptr;
    std::size_t receiversOffset = 0;
    std::size_t eventCodeOffset = 0;
    void* onEventTarget = nullptr;
    bool receiveReady = false;
    bool sendReady = false;
};

struct RoleState {
    bool known = false;
    bool inRoom = false;
    bool isMaster = false;
    std::int32_t masterActor = 0;
    bool lastLocalSpeedValid = false;
    std::uint32_t nextSequence = 1;
    std::chrono::steady_clock::time_point lastSentAt{};
    bool sendFailureLogged = false;
};

struct RemoteState {
    bool hasSequence = false;
    bool valid = false;
    std::int32_t senderActor = 0;
    std::uint32_t sequence = 0;
    float speed = 0.0f;
    std::chrono::steady_clock::time_point receivedAt{};
};

constexpr std::uint8_t kEventCode = 198;
constexpr auto kRemoteTimeout = std::chrono::milliseconds(2000);
constexpr auto kMinimumSendInterval = std::chrono::milliseconds(100);
constexpr auto kRetryDelay = std::chrono::milliseconds(100);
constexpr std::size_t kArrayDataOffset = 0x20;
constexpr std::size_t kArrayLengthOffset = 0x18;

std::mutex g_apiMutex;
Api g_api{};
std::recursive_mutex g_hookMutex;
inline_hook::Hook g_hook{};
std::atomic<bool> g_hooked{false};
std::atomic<bool> g_stopping{true};
std::mutex g_waitMutex;
std::condition_variable g_waitCondition;
std::thread g_worker;
std::mutex g_stateMutex;
RoleState g_role{};
RemoteState g_remote{};

template <typename T>
bool ResolveEntry(T& function, const il2cpp_runtime::Method*& method,
                  il2cpp_runtime::Class* klass, const char* name, int parameterCount) {
    method = il2cpp_runtime::FindMethod(klass, name, parameterCount);
    void* entry = il2cpp_runtime::GetMethodEntry(method);
    if (!entry || !il2cpp_runtime::IsInGameAssembly(entry) || !il2cpp_runtime::IsExecutable(entry)) return false;
    function = reinterpret_cast<T>(entry);
    return true;
}

bool WriteByte(std::uintptr_t address, std::uint8_t value) {
    __try {
        *reinterpret_cast<std::uint8_t*>(address) = value;
        return true;
    } __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION
                    ? EXCEPTION_EXECUTE_HANDLER
                    : EXCEPTION_CONTINUE_SEARCH) {
        return false;
    }
}

bool WriteBytes(std::uintptr_t address, const void* data, std::size_t size) {
    if (!data || size == 0) return false;
    __try {
        std::memcpy(reinterpret_cast<void*>(address), data, size);
        return true;
    } __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION
                    ? EXCEPTION_EXECUTE_HANDLER
                    : EXCEPTION_CONTINUE_SEARCH) {
        return false;
    }
}

Api CopyApi() {
    std::lock_guard<std::mutex> lock(g_apiMutex);
    return g_api;
}

bool ResolveApiLocked() {
    Api api{};
    auto* photonNetworkClass = il2cpp_runtime::FindClass("Photon.Pun", "PhotonNetwork");
    auto* loadBalancingClientClass = il2cpp_runtime::FindClass(
        "Photon.Realtime", "LoadBalancingClient");
    auto* eventDataClass = il2cpp_runtime::FindClass(
        "ExitGames.Client.Photon", "EventData");
    auto* photonPlayerClass = il2cpp_runtime::FindClass("Photon.Realtime", "Player");
    api.raiseEventOptionsClass = il2cpp_runtime::FindClass(
        "Photon.Realtime", "RaiseEventOptions");
    api.byteClass = il2cpp_runtime::FindClass("System", "Byte");
    if (!photonNetworkClass || !loadBalancingClientClass || !eventDataClass ||
        !photonPlayerClass || !api.raiseEventOptionsClass || !api.byteClass) return false;

    const bool roleResolved =
        ResolveEntry(api.isMasterClient, api.isMasterClientMethod, photonNetworkClass,
                     "get_IsMasterClient", 0) &&
        ResolveEntry(api.inRoom, api.inRoomMethod, photonNetworkClass,
                     "get_InRoom", 0) &&
        ResolveEntry(api.masterClient, api.masterClientMethod, photonNetworkClass,
                     "get_MasterClient", 0) &&
        ResolveEntry(api.actorNumber, api.actorNumberMethod, photonPlayerClass,
                     "get_ActorNumber", 0);
    const bool receiveResolved =
        ResolveEntry(api.onEvent, api.onEventMethod, loadBalancingClientClass,
                     "OnEvent", 1) &&
        ResolveEntry(api.eventCustomData, api.eventCustomDataMethod, eventDataClass,
                     "get_CustomData", 0) &&
        ResolveEntry(api.eventSender, api.eventSenderMethod, eventDataClass,
                     "get_Sender", 0) &&
        il2cpp_runtime::FindInstanceFieldByName(eventDataClass, "Code", api.eventCodeOffset);
    if (!roleResolved || !receiveResolved) return false;

    api.onEventTarget = il2cpp_runtime::GetMethodEntry(api.onEventMethod);
    api.receiveReady = api.onEventTarget != nullptr;
    const bool sendResolved =
        ResolveEntry(api.raiseEvent, api.raiseEventMethod, photonNetworkClass,
                     "RaiseEvent", 4) &&
        ResolveEntry(api.raiseEventOptionsCtor, api.raiseEventOptionsCtorMethod,
                     api.raiseEventOptionsClass, ".ctor", 0);
    api.sendReady = sendResolved &&
        il2cpp_runtime::FindInstanceFieldByName(
            api.raiseEventOptionsClass, "Receivers", api.receiversOffset);
    g_api = api;
    return api.receiveReady && api.sendReady;
}

bool RefreshRole(const Api& api, bool clearOnTransition) {
    if (!api.inRoom || !api.isMasterClient || !api.masterClient || !api.actorNumber) return false;
    const bool inRoom = api.inRoom(api.inRoomMethod);
    const bool isMaster = inRoom && api.isMasterClient(api.isMasterClientMethod);
    std::int32_t masterActor = 0;
    if (inRoom) {
        void* master = api.masterClient(api.masterClientMethod);
        if (master) masterActor = api.actorNumber(master, api.actorNumberMethod);
    }

    std::lock_guard<std::mutex> lock(g_stateMutex);
    const bool changed = !g_role.known || g_role.inRoom != inRoom ||
        g_role.isMaster != isMaster || g_role.masterActor != masterActor;
    if (changed && clearOnTransition) g_remote = {};
    if (changed) g_role.lastLocalSpeedValid = false;
    if (changed) g_role.nextSequence = 1;
    if (changed) g_role.lastSentAt = {};
    if (changed) g_role.sendFailureLogged = false;
    g_role.known = true;
    g_role.inRoom = inRoom;
    g_role.isMaster = isMaster;
    g_role.masterActor = masterActor;
    return true;
}

bool SendPacket(const Api& api, const ghost_speed_sync_protocol::Packet& packet) {
    if (!api.sendReady) return false;
    void* options = il2cpp_runtime::NewObject(api.raiseEventOptionsClass);
    void* payload = il2cpp_runtime::NewArray(api.byteClass, ghost_speed_sync_protocol::kPacketSize);
    if (!options || !payload) return false;
    if (api.raiseEventOptionsCtor) api.raiseEventOptionsCtor(options, api.raiseEventOptionsCtorMethod);

    std::array<std::uint8_t, ghost_speed_sync_protocol::kPacketSize> bytes{};
    if (!ghost_speed_sync_protocol::Encode(packet, bytes) ||
        !WriteBytes(reinterpret_cast<std::uintptr_t>(payload) + kArrayDataOffset,
                    bytes.data(), bytes.size()) ||
        !WriteByte(reinterpret_cast<std::uintptr_t>(options) + api.receiversOffset, 0)) return false;

    SendOptions sendOptions{};
    return api.raiseEvent(kEventCode, payload, options, sendOptions, api.raiseEventMethod);
}

void ProcessEvent(const Api& api, void* eventData) {
    if (!eventData || !api.eventCustomData || !api.eventSender) return;
    std::uint8_t eventCode = 0;
    if (!il2cpp_runtime::ReadByte(reinterpret_cast<std::uintptr_t>(eventData) + api.eventCodeOffset, eventCode) ||
        eventCode != kEventCode) return;
    if (!RefreshRole(api, true)) return;

    const std::int32_t senderActor = api.eventSender(eventData, api.eventSenderMethod);
    std::uintptr_t payload = reinterpret_cast<std::uintptr_t>(
        api.eventCustomData(eventData, api.eventCustomDataMethod));
    std::int32_t length = 0;
    if (!payload || !il2cpp_runtime::ReadInt32(payload + kArrayLengthOffset, length) ||
        length != static_cast<std::int32_t>(ghost_speed_sync_protocol::kPacketSize)) return;

    std::array<std::uint8_t, ghost_speed_sync_protocol::kPacketSize> bytes{};
    if (!il2cpp_runtime::ReadBytes(payload + kArrayDataOffset, bytes.data(), bytes.size())) return;
    ghost_speed_sync_protocol::Packet packet{};
    if (!ghost_speed_sync_protocol::Decode(bytes.data(), bytes.size(), packet)) return;

    std::lock_guard<std::mutex> lock(g_stateMutex);
    if (!g_role.inRoom || g_role.isMaster || senderActor <= 0 ||
        senderActor != g_role.masterActor) return;
    if (g_remote.hasSequence && g_remote.senderActor == senderActor &&
        !ghost_speed_sync_protocol::IsNewer(packet.sequence, g_remote.sequence)) return;
    g_remote.hasSequence = true;
    g_remote.senderActor = senderActor;
    g_remote.sequence = packet.sequence;
    g_remote.receivedAt = std::chrono::steady_clock::now();
    g_remote.valid = packet.valid;
    g_remote.speed = packet.valid ? packet.speed : 0.0f;
}

void DetourOnEvent(void* instance, void* eventData, const il2cpp_runtime::Method* methodInfo) {
    std::lock_guard<std::recursive_mutex> lock(g_hookMutex);
    auto original = reinterpret_cast<EventFn>(g_hook.original);
    if (original) original(instance, eventData, methodInfo);
    const Api api = CopyApi();
    ProcessEvent(api, eventData);
}

bool InstallHook() {
    std::lock_guard<std::recursive_mutex> hookLock(g_hookMutex);
    if (g_hooked.load()) return true;
    {
        std::lock_guard<std::mutex> apiLock(g_apiMutex);
        if (!ResolveApiLocked()) return false;
    }
    const Api api = CopyApi();
    if (!api.receiveReady || !api.onEventTarget ||
        !inline_hook::Attach(g_hook, api.onEventTarget, reinterpret_cast<void*>(&DetourOnEvent))) return false;
    g_hooked.store(true);
    LOG("GhostSpeedSync: reliable OnEvent/RaiseEvent bridge installed at %p", api.onEventTarget);
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

void Initialize() {
    Shutdown();
    {
        std::lock_guard<std::mutex> lock(g_stateMutex);
        g_role = {};
        g_remote = {};
    }
    g_stopping.store(false);
    g_worker = std::thread(&InstallWorker);
}

void Shutdown() {
    g_stopping.store(true);
    g_waitCondition.notify_all();
    if (g_worker.joinable()) g_worker.join();
    {
        std::lock_guard<std::recursive_mutex> lock(g_hookMutex);
        if (!inline_hook::Detach(g_hook)) LOG("GhostSpeedSync: failed to detach OnEvent Hook");
        g_hooked.store(g_hook.attached);
    }
    {
        std::lock_guard<std::mutex> lock(g_apiMutex);
        g_api = {};
    }
    std::lock_guard<std::mutex> lock(g_stateMutex);
    g_role = {};
    g_remote = {};
}

bool PublishLocalSpeed(bool valid, float speed) {
    const Api api = CopyApi();
    if (!api.receiveReady || !api.sendReady || !RefreshRole(api, true)) return false;

    ghost_speed_sync_protocol::Packet packet{};
    const auto now = std::chrono::steady_clock::now();
    {
        std::lock_guard<std::mutex> lock(g_stateMutex);
        if (!g_role.isMaster || !g_role.inRoom) return false;
        if (valid && (!std::isfinite(speed) || speed < 0.0f || speed > 20.0f)) valid = false;
        if (!valid && !g_role.lastLocalSpeedValid) return true;
        if (valid && g_role.lastLocalSpeedValid &&
            g_role.lastSentAt.time_since_epoch().count() != 0 &&
            now - g_role.lastSentAt < kMinimumSendInterval) return true;
        packet = {valid, g_role.nextSequence, valid ? speed : 0.0f};
    }
    const bool sent = SendPacket(api, packet);
    bool logFailure = false;
    {
        std::lock_guard<std::mutex> lock(g_stateMutex);
        if (g_role.isMaster && g_role.inRoom) {
            if (sent) {
                if (g_role.nextSequence == packet.sequence) ++g_role.nextSequence;
                g_role.lastLocalSpeedValid = valid;
                g_role.lastSentAt = now;
                g_role.sendFailureLogged = false;
            } else if (!g_role.sendFailureLogged) {
                g_role.sendFailureLogged = true;
                logFailure = true;
            }
        }
    }
    if (logFailure) LOG("GhostSpeedSync: reliable RaiseEvent failed; retrying");
    return true;
}

bool ReadRemoteSpeed(float& speed) {
    std::lock_guard<std::mutex> lock(g_stateMutex);
    if (!g_role.known || !g_role.inRoom || g_role.isMaster || !g_remote.valid) return false;
    if (std::chrono::steady_clock::now() - g_remote.receivedAt > kRemoteTimeout) {
        g_remote = {};
        return false;
    }
    speed = g_remote.speed;
    return true;
}

} // namespace ghost_speed_sync
