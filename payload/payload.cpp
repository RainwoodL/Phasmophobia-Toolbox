#include <windows.h>
#include "d3d11_hook.h"
#include "overlay.h"
#include "log.h"

namespace {

HANDLE g_instanceMarker = nullptr;
HANDLE g_readyEvent = nullptr;
HANDLE g_failedEvent = nullptr;
volatile LONG g_readySignaled = FALSE;
constexpr const wchar_t* kReadyEventName = L"Local\\GhostRoomTools.Payload.Ready.v11";
constexpr const wchar_t* kFailedEventName = L"Local\\GhostRoomTools.Payload.Failed.v11";

DWORD WINAPI WorkerThread(LPVOID) {
    LOG("WorkerThread: start");
    g_readyEvent = CreateEventW(nullptr, TRUE, FALSE, kReadyEventName);
    g_failedEvent = CreateEventW(nullptr, TRUE, FALSE, kFailedEventName);
    if (!g_readyEvent || !g_failedEvent) {
        LOG("WorkerThread: CreateEvent failed ready=%p failed=%p err=%lu",
            (void*)g_readyEvent, (void*)g_failedEvent, GetLastError());
        if (g_readyEvent) { CloseHandle(g_readyEvent); g_readyEvent = nullptr; }
        if (g_failedEvent) { CloseHandle(g_failedEvent); g_failedEvent = nullptr; }
        return 0;
    }

    bool installed = false;
    for (int attempt = 0; attempt < 60 && !installed; ++attempt) {
        installed = d3d11_hook::Install();
        if (!installed) {
            LOG("WorkerThread: d3d11 hook attempt %d failed", attempt + 1);
            Sleep(1000);
        }
    }
    if (installed) {
        LOG("WorkerThread: d3d11 hook installed, registering frame callback");
        d3d11_hook::OnFrame([]() {
            overlay::Render();
            if (overlay::IsInitialized() &&
                InterlockedCompareExchange(&g_readySignaled, TRUE, FALSE) == FALSE) {
                if (g_readyEvent) SetEvent(g_readyEvent);
            }
        });
        LOG("WorkerThread: frame callback registered");
    } else {
        LOG("WorkerThread: d3d11_hook::Install FAILED");
        if (g_failedEvent) SetEvent(g_failedEvent);
    }
    // Keep named event objects alive for injector diagnostics.
    return 0;
}

} // namespace

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID reserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        LOG("DllMain: DLL_PROCESS_ATTACH, module=%p", (void*)module);
        g_instanceMarker = CreateMutexW(nullptr, FALSE, L"Local\\GhostRoomTools.Payload.v11");
        if (!g_instanceMarker || GetLastError() == ERROR_ALREADY_EXISTS) {
            if (g_instanceMarker) { CloseHandle(g_instanceMarker); g_instanceMarker = nullptr; }
            return FALSE;
        }
        DisableThreadLibraryCalls(module);
        HANDLE thread = CreateThread(nullptr, 0, WorkerThread, nullptr, 0, nullptr);
        if (thread) {
            CloseHandle(thread);
        } else {
            LOG("DllMain: CreateThread failed, err=%lu", GetLastError());
            CloseHandle(g_instanceMarker);
            g_instanceMarker = nullptr;
            return FALSE;
        }
    } else if (reason == DLL_PROCESS_DETACH) {
        // Full teardown joins threads and performs D3D/file I/O. Never do that under loader lock.
        (void)reserved;
    }
    return TRUE;
}
