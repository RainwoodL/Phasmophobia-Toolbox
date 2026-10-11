// ============================================================
// 注入器主程序 - 单 exe，不落盘 payload.dll
// ============================================================
#include <windows.h>
#include <tlhelp32.h>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <thread>
#include <chrono>
#include <fcntl.h>
#include <io.h>
#include <conio.h>
#include <array>

#include "payload_dll.h"
#include "manual_pe.h"
#include "user_messages.h"
#include "../payload/log.h"

// ============================================================
// 工具函数
// ============================================================
static void WaitForKey() {
    printf("%s\n", "按任意键关闭本窗口。");
    (void)_getwch();
}

static DWORD FindProcessByName(const wchar_t* processName) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return 0;

    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    DWORD pid = 0;

    if (Process32FirstW(snapshot, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, processName) == 0) {
                pid = pe.th32ProcessID;
                break;
            }
        } while (Process32NextW(snapshot, &pe));
    }

    CloseHandle(snapshot);
    return pid;
}

static DWORD WaitForProcess(const wchar_t* processName) {
    while (true) {
        const DWORD pid = FindProcessByName(processName);
        if (pid != 0) return pid;
        Sleep(1000);
    }
}

static HANDLE OpenGameProcess(DWORD pid) {
    constexpr DWORD access = SYNCHRONIZE | PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
        PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ;
    DWORD lastError = ERROR_SUCCESS;
    for (int attempt = 0; attempt < 20; ++attempt) {
        HANDLE process = OpenProcess(access, FALSE, pid);
        if (process) return process;
        lastError = GetLastError();
        if (lastError != ERROR_ACCESS_DENIED) break;
        Sleep(500);
    }
    SetLastError(lastError);
    return nullptr;
}

struct RemoteModuleInfo {
    HMODULE base = nullptr;
    DWORD size = 0;
};

static bool operator==(const RemoteModuleInfo& left, const RemoteModuleInfo& right) {
    return left.base == right.base && left.size == right.size;
}

static bool FindRemoteModules(DWORD pid,
                              const std::array<const wchar_t*, 4>& names,
                              std::array<RemoteModuleInfo, 4>& result) {
    for (int attempt = 0; attempt < 8; ++attempt) {
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
        if (snapshot != INVALID_HANDLE_VALUE) {
            MODULEENTRY32W module{};
            module.dwSize = sizeof(module);
            result = {};
            if (Module32FirstW(snapshot, &module)) {
                do {
                    for (std::size_t i = 0; i < names.size(); ++i) {
                        if (_wcsicmp(module.szModule, names[i]) == 0) {
                            result[i] = {module.hModule, module.modBaseSize};
                            break;
                        }
                    }
                } while (Module32NextW(snapshot, &module));
            }
            CloseHandle(snapshot);
            for (const auto& info : result)
                if (!info.base || info.size == 0) return false;
            return true;
        }
        if (GetLastError() != ERROR_BAD_LENGTH) return false;
        Sleep(50);
    }
    return false;
}

using user_messages::ReadyResult;

static ReadyResult WaitForGameReady(HANDLE process, DWORD pid) {
    constexpr std::array<const wchar_t*, 4> requiredModules{
        L"GameAssembly.dll", L"UnityPlayer.dll", L"dxgi.dll", L"d3d11.dll"};
    std::array<RemoteModuleInfo, 4> previous{};
    int stableChecks = 0;
    constexpr int maxChecks = 120;
    for (int check = 0; check < maxChecks; ++check) {
        const DWORD waitResult = WaitForSingleObject(process, 0);
        if (waitResult == WAIT_OBJECT_0) return ReadyResult::ProcessExited;
        if (waitResult != WAIT_TIMEOUT) return ReadyResult::Failed;
        std::array<RemoteModuleInfo, 4> current{};
        const bool modulesReady = FindRemoteModules(pid, requiredModules, current);
        if (modulesReady && current == previous) {
            ++stableChecks;
            if (stableChecks >= 3) return ReadyResult::Ready;
        } else {
            stableChecks = modulesReady ? 1 : 0;
        }
        if (modulesReady) previous = current;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return ReadyResult::Timeout;
}

static ReadyResult WaitForPayloadReady(HANDLE process) {
    for (int elapsed = 0; elapsed < 65; ++elapsed) {
        const DWORD waitResult = WaitForSingleObject(process, 0);
        if (waitResult == WAIT_OBJECT_0) return ReadyResult::ProcessExited;
        if (waitResult != WAIT_TIMEOUT) return ReadyResult::Failed;
        HANDLE failed = OpenEventW(SYNCHRONIZE, FALSE, L"Local\\GhostRoomTools.Payload.Failed.v11");
        if (failed) {
            const bool signaled = WaitForSingleObject(failed, 0) == WAIT_OBJECT_0;
            CloseHandle(failed);
            if (signaled) return ReadyResult::Failed;
        }
        HANDLE ready = OpenEventW(SYNCHRONIZE, FALSE, L"Local\\GhostRoomTools.Payload.Ready.v11");
        if (ready) {
            const bool signaled = WaitForSingleObject(ready, 0) == WAIT_OBJECT_0;
            CloseHandle(ready);
            if (signaled) return ReadyResult::Ready;
        }
        Sleep(1000);
    }
    return ReadyResult::Timeout;
}

static bool EnableDebugPrivilege() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(),
                          TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY,
                          &token)) return false;

    LUID luid{};
    if (!LookupPrivilegeValueW(nullptr, L"SeDebugPrivilege", &luid)) {
        CloseHandle(token);
        return false;
    }

    TOKEN_PRIVILEGES tp{};
    tp.PrivilegeCount = 1;
    tp.Privileges[0].Luid = luid;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

    BOOL ok = AdjustTokenPrivileges(token, FALSE, &tp, sizeof(tp), nullptr, nullptr);
    CloseHandle(token);
    return ok && GetLastError() != ERROR_NOT_ALL_ASSIGNED;
}

// ============================================================
// 主入口
// ============================================================
int main() {
    // 控制台 UTF-8 输出（配合 /utf-8 编译的字符串字面量）
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    // 禁用 stdout 缓冲，确保重定向时也能立即看到输出
    setvbuf(stdout, nullptr, _IONBF, 0);
    setvbuf(stderr, nullptr, _IONBF, 0);
    log_util::InitializeSession();
    LOG("injector start");

    if (!EnableDebugPrivilege()) LOG("debug privilege unavailable");

    printf("%s\n", "本软件永久免费！请勿上当受骗！");
    printf("%s\n\n", "加入QQ群:304393689 有更新会在群里发");

    printf("%s\n", "等待游戏启动，请打开游戏...");
    DWORD pid = WaitForProcess(L"Phasmophobia.exe");
    printf("%s\n", "检测到游戏已启动！");

    HANDLE hProcess = OpenGameProcess(pid);
    if (!hProcess) {
        const DWORD error = GetLastError();
        LOG("OpenProcess failed err=%lu", error);
        printf("%s\n", user_messages::OpenGameFailed(error));
        WaitForKey();
        return 1;
    }

    printf("%s\n", "正在等待游戏准备就绪，请稍候...");
    Sleep(5000);

    const ReadyResult gameResult = WaitForGameReady(hProcess, pid);
    if (gameResult != ReadyResult::Ready) {
        LOG("game readiness failed code=%u", static_cast<unsigned int>(gameResult));
        printf("%s\n", user_messages::GameNotReady(gameResult));
        CloseHandle(hProcess);
        WaitForKey();
        return 1;
    }

    if (kPayloadSize < sizeof(IMAGE_DOS_HEADER)) {
        LOG("payload data unavailable");
        printf("%s\n", user_messages::PrepareFailed());
        CloseHandle(hProcess);
        WaitForKey();
        return 1;
    }

    printf("%s\n", "正在启动工具箱...");
    LOG("manual map begin");

    manual_pe::Error err = manual_pe::Error::Ok;
    bool ok = false;
    try {
        ok = manual_pe::Inject(hProcess, kPayloadData, kPayloadSize, &err);
    } catch (const std::exception&) {
        LOG("manual map exception");
    } catch (...) {
        LOG("manual map unknown exception");
    }

    if (!ok) {
        LOG("manual map failed code=%u", static_cast<unsigned int>(err));
        printf("%s\n", user_messages::LoadFailed(err));
        CloseHandle(hProcess);
        WaitForKey();
        return 1;
    }

    const ReadyResult readyResult = WaitForPayloadReady(hProcess);
    if (readyResult != ReadyResult::Ready) {
        LOG("payload readiness failed code=%u", static_cast<unsigned int>(readyResult));
        printf("%s\n", user_messages::ToolNotReady(readyResult));
        CloseHandle(hProcess);
        WaitForKey();
        return 1;
    }

    CloseHandle(hProcess);
    LOG("injector complete");

    printf("%s\n\n", "工具箱已启动！进入游戏后按菜单快捷键即可打开菜单。");
    printf("%s\n", "本软件永久免费！请勿上当受骗！");
    printf("%s\n\n", "加入QQ群:304393689 有更新会在群里发");

    for (int i = 3; i >= 1; --i) {
        printf("%ds 后关闭本窗口\n", i);
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    return 0;
}
