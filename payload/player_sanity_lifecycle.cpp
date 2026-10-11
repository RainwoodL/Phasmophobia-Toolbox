#include "player_sanity_lifecycle.h"

#include "il2cpp_runtime.h"
#include "inline_hook.h"
#include "log.h"
#include "timer_logic.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>

namespace player_sanity_lifecycle {
namespace {

using UpdateFn = void (*)(void*, const il2cpp_runtime::Method*);
constexpr auto kRetryDelay = std::chrono::milliseconds(100);
constexpr auto kActiveTimeout = std::chrono::milliseconds(1500);

struct HookState {
    inline_hook::Hook hook{};
    std::atomic<bool> installed{false};
};

std::recursive_mutex g_hookMutex;
std::mutex g_stateMutex;
std::mutex g_waitMutex;
std::condition_variable g_waitCondition;
std::thread g_worker;
std::atomic<bool> g_stopping{true};
HookState g_hook;
std::chrono::steady_clock::time_point g_lastSeen{};
bool g_seen = false;
timer_logic::LifecycleResetState g_lifecycle{};
ResetCallback g_resetCallback = nullptr;
bool g_installFailureLogged = false;

void ResetHookState() {
    g_hook.hook = {};
    g_hook.installed.store(false);
}

void DetourUpdate(void* instance, const il2cpp_runtime::Method* methodInfo) {
    std::lock_guard<std::recursive_mutex> lock(g_hookMutex);
    auto original = reinterpret_cast<UpdateFn>(g_hook.hook.original);
    if (!original) return;
    original(instance, methodInfo);
    if (!g_stopping.load()) {
        std::lock_guard<std::mutex> stateLock(g_stateMutex);
        g_lastSeen = std::chrono::steady_clock::now();
        g_seen = true;
    }
}

bool InstallHook() {
    std::lock_guard<std::recursive_mutex> lock(g_hookMutex);
    if (g_hook.installed.load()) return true;
    auto* klass = il2cpp_runtime::FindClass("", "PlayerSanity");
    const auto* method = il2cpp_runtime::FindMethod(klass, "Update", 0);
    void* target = il2cpp_runtime::GetMethodEntry(method);
    if (!target || !il2cpp_runtime::IsInGameAssembly(target) || !il2cpp_runtime::IsExecutable(target) ||
        !inline_hook::Attach(g_hook.hook, target, reinterpret_cast<void*>(&DetourUpdate))) return false;
    g_hook.installed.store(true);
    g_installFailureLogged = false;
    LOG("PlayerSanityLifecycle: PlayerSanity.Update Hook installed at %p", target);
    return true;
}

void WorkerMain() {
    while (!g_stopping.load()) {
        if (!g_hook.installed.load() && (!il2cpp_runtime::Initialize() || !InstallHook())) {
            if (!g_installFailureLogged) {
                LOG("PlayerSanityLifecycle: PlayerSanity.Update Hook installation failed; retrying");
                g_installFailureLogged = true;
            }
        }

        ResetCallback callback = nullptr;
        bool shouldReset = false;
        bool active = false;
        {
            std::lock_guard<std::mutex> lock(g_stateMutex);
            active = g_seen && std::chrono::steady_clock::now() - g_lastSeen <= kActiveTimeout;
            shouldReset = timer_logic::UpdateLifecycleResetState(g_lifecycle, active);
            if (shouldReset) callback = g_resetCallback;
        }
        if (shouldReset && callback && !g_stopping.load()) {
            LOG("PlayerSanityLifecycle: lifecycle edge reset (%s)", active ? "active" : "inactive");
            callback();
        }

        std::unique_lock<std::mutex> lock(g_waitMutex);
        g_waitCondition.wait_for(lock, kRetryDelay, [] { return g_stopping.load(); });
    }
}

} // namespace

void Initialize(ResetCallback resetCallback) {
    Shutdown();
    {
        std::lock_guard<std::mutex> lock(g_stateMutex);
        g_resetCallback = resetCallback;
        g_lastSeen = {};
        g_seen = false;
        g_lifecycle = {};
    }
    if (!g_hook.hook.attached) ResetHookState();
    g_installFailureLogged = false;
    g_stopping.store(false);
    g_worker = std::thread(&WorkerMain);
}

void Shutdown() {
    g_stopping.store(true);
    g_waitCondition.notify_all();
    if (g_worker.joinable()) g_worker.join();
    {
        std::lock_guard<std::recursive_mutex> lock(g_hookMutex);
        g_hook.installed.store(false);
        if (!inline_hook::Detach(g_hook.hook))
            LOG("PlayerSanityLifecycle: failed to detach PlayerSanity.Update Hook");
        if (!g_hook.hook.attached) ResetHookState();
        g_installFailureLogged = false;
    }
    std::lock_guard<std::mutex> lock(g_stateMutex);
    g_resetCallback = nullptr;
    g_lastSeen = {};
    g_seen = false;
    g_lifecycle = {};
}

} // namespace player_sanity_lifecycle
