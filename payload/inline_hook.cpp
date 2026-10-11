#include "inline_hook.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <detours.h>

#include <mutex>
#include <vector>

namespace inline_hook {
namespace {

std::mutex g_transactionMutex;

class ThreadHandles {
public:
    ~ThreadHandles() {
        for (HANDLE handle : handles_) CloseHandle(handle);
    }

    bool Enlist() {
        if (DetourUpdateThread(GetCurrentThread()) != NO_ERROR) return false;

        const DWORD processId = GetCurrentProcessId();
        const DWORD currentThreadId = GetCurrentThreadId();
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        if (snapshot == INVALID_HANDLE_VALUE) return false;

        THREADENTRY32 entry{};
        entry.dwSize = sizeof(entry);
        BOOL found = Thread32First(snapshot, &entry);
        while (found) {
            if (entry.th32OwnerProcessID == processId && entry.th32ThreadID != currentThreadId) {
                HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT |
                                           THREAD_SET_CONTEXT,
                                           FALSE, entry.th32ThreadID);
                if (thread) {
                    if (DetourUpdateThread(thread) != NO_ERROR) {
                        CloseHandle(thread);
                        CloseHandle(snapshot);
                        return false;
                    }
                    handles_.push_back(thread);
                } else if (GetLastError() != ERROR_INVALID_PARAMETER) {
                    CloseHandle(snapshot);
                    return false;
                }
            }
            found = Thread32Next(snapshot, &entry);
        }
        CloseHandle(snapshot);
        return true;
    }

private:
    std::vector<HANDLE> handles_;
};

bool RunTransaction(Hook& hook, bool attach) {
    std::lock_guard<std::mutex> lock(g_transactionMutex);
    if (DetourTransactionBegin() != NO_ERROR) {
        DetourTransactionAbort();
        return false;
    }

    ThreadHandles threads;
    if (!threads.Enlist()) {
        DetourTransactionAbort();
        return false;
    }

    const LONG operation = attach
        ? DetourAttach(&hook.original, hook.detour)
        : DetourDetach(&hook.original, hook.detour);
    if (operation != NO_ERROR) {
        DetourTransactionAbort();
        return false;
    }
    if (DetourTransactionCommit() != NO_ERROR) return false;
    FlushInstructionCache(GetCurrentProcess(), hook.target, 32);
    return true;
}

} // namespace

bool Attach(Hook& hook, void* target, void* detour) {
    if (hook.attached) return hook.target == target && hook.detour == detour && hook.original;
    if (!target || !detour) return false;
    hook.target = target;
    hook.original = target;
    hook.detour = detour;
    if (!RunTransaction(hook, true)) {
        hook = {};
        return false;
    }
    hook.attached = true;
    return true;
}

bool Detach(Hook& hook) {
    if (!hook.attached) {
        hook = {};
        return true;
    }
    if (!RunTransaction(hook, false)) return false;
    hook = {};
    return true;
}

} // namespace inline_hook
