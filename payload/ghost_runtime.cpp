#include "ghost_runtime.h"

#include "il2cpp_runtime.h"
#include "log.h"
#include "timer_logic.h"

#include <atomic>
#include <chrono>
#include <mutex>

namespace ghost_runtime {
namespace {

constexpr std::uintptr_t kStateOffset = 0x30;
constexpr auto kDiscoveryDelay = std::chrono::milliseconds(750);
constexpr float kSessionRollbackSeconds = 5.0f;

GameTimeProvider g_provider = nullptr;
std::atomic<std::uintptr_t> g_instance{0};
std::atomic<std::uint64_t> g_generation{1};
std::mutex g_stateMutex;
bool g_lastGameTimeValid = false;
float g_lastGameSeconds = 0.0f;
std::chrono::steady_clock::time_point g_nextDiscovery{};
bool g_discoveryUnavailableLogged = false;

std::uintptr_t DiscoverInstance() {
    const std::uintptr_t levelController =
        il2cpp_runtime::ReadStaticObjectFieldByType("LevelController", "LevelController");
    auto* levelClass = il2cpp_runtime::FindClass("", "LevelController");
    auto* ghostClass = il2cpp_runtime::FindClass("", "GhostAI");
    std::uintptr_t ghostOffset = 0;
    std::uintptr_t instance = 0;
    if (!levelController || !il2cpp_runtime::FindInstanceFieldByType(levelClass, ghostClass, ghostOffset) ||
        !il2cpp_runtime::ReadPointer(levelController + ghostOffset, instance)) return 0;
    return instance;
}

} // namespace

void Initialize(GameTimeProvider provider) {
    Shutdown();
    g_provider = provider;
    g_generation.store(1);
}

void Shutdown() {
    g_instance.store(0);
    g_provider = nullptr;
    std::lock_guard<std::mutex> lock(g_stateMutex);
    g_nextDiscovery = {};
    g_discoveryUnavailableLogged = false;
    g_lastGameTimeValid = false;
    g_lastGameSeconds = 0.0f;
}

void ObserveInstance(void* instance) {
    if (instance) g_instance.store(reinterpret_cast<std::uintptr_t>(instance));
}

void ResetSession() {
    g_instance.store(0);
    g_generation.fetch_add(1);
    std::lock_guard<std::mutex> lock(g_stateMutex);
    g_nextDiscovery = {};
    g_lastGameTimeValid = false;
    g_lastGameSeconds = 0.0f;
}

void Update() {
    const GameTimeValue gameTime = g_provider ? g_provider() : GameTimeValue{};
    {
        std::lock_guard<std::mutex> lock(g_stateMutex);
        if (timer_logic::SessionTimeWentBack(g_lastGameTimeValid, g_lastGameSeconds,
                                             gameTime.valid, gameTime.seconds, kSessionRollbackSeconds)) {
            g_instance.store(0);
            g_generation.fetch_add(1);
            g_nextDiscovery = {};
        }
        if (gameTime.valid) {
            g_lastGameTimeValid = true;
            g_lastGameSeconds = gameTime.seconds;
        }
    }

    const auto now = std::chrono::steady_clock::now();
    bool shouldDiscover = false;
    {
        std::lock_guard<std::mutex> lock(g_stateMutex);
        if (now >= g_nextDiscovery) {
            g_nextDiscovery = now + kDiscoveryDelay;
            shouldDiscover = true;
        }
    }
    auto instance = g_instance.load();
    if (shouldDiscover) {
        const auto discovered = DiscoverInstance();
        if (discovered) {
            instance = discovered;
            g_instance.store(instance);
            std::lock_guard<std::mutex> lock(g_stateMutex);
            g_discoveryUnavailableLogged = false;
        } else if (!instance) {
            std::lock_guard<std::mutex> lock(g_stateMutex);
            if (!g_discoveryUnavailableLogged) {
                LOG("GhostRuntime: reflective GhostAI discovery unavailable; waiting for RPC instance");
                g_discoveryUnavailableLogged = true;
            }
        }
    }
    std::int32_t state = 0;
    if (instance && !il2cpp_runtime::ReadInt32(instance + kStateOffset, state)) {
        g_instance.store(0);
        std::lock_guard<std::mutex> lock(g_stateMutex);
        g_nextDiscovery = now + kDiscoveryDelay;
    }
}

Snapshot ReadSnapshot() {
    const auto instance = g_instance.load();
    return {instance != 0, instance, g_generation.load()};
}

} // namespace ghost_runtime
