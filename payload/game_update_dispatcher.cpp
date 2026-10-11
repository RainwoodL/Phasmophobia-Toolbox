#include "game_update_dispatcher.h"

#include "il2cpp_runtime.h"
#include "inline_hook.h"
#include "log.h"
#include "settings_store.h"
#include "crosshair_info.h"
#include "inventory_bar.h"
#include "player_nicknames.h"
#include "ghost_speed_display.h"
#include "ghost_distance.h"
#include "sanity_display.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>

namespace game_update_dispatcher {
namespace {

using UpdateFn = void (*)(void*, const il2cpp_runtime::Method*);

std::recursive_mutex g_hookMutex;
std::mutex g_waitMutex;
std::condition_variable g_waitCondition;
std::thread g_installThread;
std::atomic<bool> g_stopping{true};
std::atomic<bool> g_hooked{false};
std::atomic<int> g_frameIntervalMs{16};
void* g_target = nullptr;
inline_hook::Hook g_hook{};
CrosshairCallback g_crosshairCallback = nullptr;
NicknamesCallback g_nicknamesCallback = nullptr;
InventoryCallback g_inventoryCallback = nullptr;
GhostSpeedCallback g_ghostSpeedCallback = nullptr;
GhostDistanceCallback g_ghostDistanceCallback = nullptr;
SanityCallback g_sanityCallback = nullptr;

using Clock = std::chrono::steady_clock;
Clock::time_point g_nextCrosshairUpdate{};
Clock::time_point g_nextNicknamesUpdate{};
Clock::time_point g_nextInventoryUpdate{};
Clock::time_point g_nextGhostSpeedUpdate{};
Clock::time_point g_nextGhostDistanceUpdate{};
Clock::time_point g_nextSanityUpdate{};

bool ShouldRun(Clock::time_point now, Clock::time_point& next,
               std::chrono::milliseconds interval) {
    if (now < next) return false;
    next = now + interval;
    return true;
}

std::chrono::milliseconds FrameInterval() {
    switch (settings_store::Get().menu.frameRate) {
    case settings_store::OverlayFrameRate::Low: return std::chrono::milliseconds(100);
    case settings_store::OverlayFrameRate::Medium: return std::chrono::milliseconds(33);
    case settings_store::OverlayFrameRate::Ultra: return std::chrono::milliseconds(1);
    default: return std::chrono::milliseconds(16);
    }
}

void InvokeCallbacks(void* instance) {
    const auto now = Clock::now();
    const auto frameInterval = std::chrono::milliseconds(g_frameIntervalMs.load());
    __try { if (g_crosshairCallback && ShouldRun(now, g_nextCrosshairUpdate, frameInterval)) g_crosshairCallback(instance); }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        g_nextCrosshairUpdate = now + std::chrono::seconds(1);
        crosshair_info::ClearSnapshot();
        LOG("GameUpdateDispatcher: crosshair callback failed; retrying in 1 second");
    }
    __try { if (g_nicknamesCallback && ShouldRun(now, g_nextNicknamesUpdate, frameInterval)) g_nicknamesCallback(); }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        g_nextNicknamesUpdate = now + std::chrono::seconds(1);
        player_nicknames::ClearSnapshot();
        LOG("GameUpdateDispatcher: nicknames callback failed; retrying in 1 second");
    }
    __try { if (g_inventoryCallback && ShouldRun(now, g_nextInventoryUpdate, frameInterval)) g_inventoryCallback(instance); }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        g_nextInventoryUpdate = now + std::chrono::seconds(1);
        inventory_bar::ClearSnapshot();
        LOG("GameUpdateDispatcher: inventory callback failed; retrying in 1 second");
    }
    __try { if (g_ghostSpeedCallback && ShouldRun(now, g_nextGhostSpeedUpdate, std::chrono::milliseconds(50))) g_ghostSpeedCallback(); }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        g_nextGhostSpeedUpdate = now + std::chrono::seconds(1);
        ghost_speed_display::ClearSnapshot();
        LOG("GameUpdateDispatcher: ghost speed callback failed; retrying in 1 second");
    }
    __try { if (g_ghostDistanceCallback && ShouldRun(now, g_nextGhostDistanceUpdate, frameInterval)) g_ghostDistanceCallback(); }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        g_nextGhostDistanceUpdate = now + std::chrono::seconds(1);
        ghost_distance::ClearSnapshot();
        LOG("GameUpdateDispatcher: ghost distance callback failed; retrying in 1 second");
    }
    __try { if (g_sanityCallback && ShouldRun(now, g_nextSanityUpdate, frameInterval)) g_sanityCallback(); }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        g_nextSanityUpdate = now + std::chrono::seconds(1);
        sanity_display::ClearSnapshot();
        LOG("GameUpdateDispatcher: sanity callback failed; retrying in 1 second");
    }
}

void DetourUpdate(void* instance, const il2cpp_runtime::Method* methodInfo) {
    std::lock_guard<std::recursive_mutex> lock(g_hookMutex);
    auto original = reinterpret_cast<UpdateFn>(g_hook.original);
    if (!original) return;
    original(instance, methodInfo);
    if (!g_stopping.load()) InvokeCallbacks(instance);
}

bool InstallHook() {
    std::lock_guard<std::recursive_mutex> lock(g_hookMutex);
    if (g_hooked.load()) return true;
    auto* klass = il2cpp_runtime::FindClass("", "PCPropGrab");
    const auto* method = il2cpp_runtime::FindMethod(klass, "Update", 0);
    g_target = il2cpp_runtime::GetMethodEntry(method);
    if (!g_target || !il2cpp_runtime::IsInGameAssembly(g_target) || !il2cpp_runtime::IsExecutable(g_target) ||
        !inline_hook::Attach(g_hook, g_target, reinterpret_cast<void*>(&DetourUpdate))) return false;
    g_hooked.store(true);
    LOG("GameUpdateDispatcher: PCPropGrab.Update Hook installed at %p", g_target);
    return true;
}

void InstallWorker() {
    while (!g_stopping.load()) {
        if (il2cpp_runtime::Initialize() && InstallHook()) return;
        std::unique_lock<std::mutex> lock(g_waitMutex);
        g_waitCondition.wait_for(lock, std::chrono::milliseconds(100), [] { return g_stopping.load(); });
    }
}

} // namespace

void SyncSettings() { g_frameIntervalMs.store(static_cast<int>(FrameInterval().count())); }

bool Initialize(CrosshairCallback crosshairCallback, NicknamesCallback nicknamesCallback,
                InventoryCallback inventoryCallback, GhostSpeedCallback ghostSpeedCallback,
                GhostDistanceCallback ghostDistanceCallback, SanityCallback sanityCallback) {
    Shutdown();
    std::lock_guard<std::recursive_mutex> lock(g_hookMutex);
    g_crosshairCallback = crosshairCallback;
    g_nicknamesCallback = nicknamesCallback;
    g_inventoryCallback = inventoryCallback;
    g_ghostSpeedCallback = ghostSpeedCallback;
    g_ghostDistanceCallback = ghostDistanceCallback;
    g_sanityCallback = sanityCallback;
    g_nextCrosshairUpdate = g_nextNicknamesUpdate = g_nextInventoryUpdate = {};
    g_nextGhostSpeedUpdate = g_nextGhostDistanceUpdate = g_nextSanityUpdate = {};
    SyncSettings();
    g_stopping.store(false);
    g_installThread = std::thread(&InstallWorker);
    return true;
}

void Shutdown() {
    g_stopping.store(true);
    g_waitCondition.notify_all();
    if (g_installThread.joinable()) g_installThread.join();
    std::lock_guard<std::recursive_mutex> lock(g_hookMutex);
    if (!inline_hook::Detach(g_hook)) LOG("GameUpdateDispatcher: failed to detach PCPropGrab.Update Hook");
    g_hooked.store(g_hook.attached);
    g_target = nullptr;
    g_crosshairCallback = nullptr;
    g_nicknamesCallback = nullptr;
    g_inventoryCallback = nullptr;
    g_ghostSpeedCallback = nullptr;
    g_ghostDistanceCallback = nullptr;
    g_sanityCallback = nullptr;
}

} // namespace game_update_dispatcher
