// ============================================================
// 手动 PE 映射注入 - 不落盘，不调用 LoadLibrary
// ============================================================
#include "manual_pe.h"
#include "../payload/log.h"
#include <cstdio>
#include <vector>
#include <tlhelp32.h>
#include <algorithm>
#include <limits>

namespace manual_pe {

static bool RemoteUnregisterSEH(HANDLE hProcess, LPVOID funcTable);

namespace {

constexpr SIZE_T kMaximumImageSize = 512ull * 1024ull * 1024ull;

bool RangeWithin(std::uint64_t offset, std::uint64_t size, std::uint64_t limit) {
    return offset <= limit && size <= limit - offset;
}

bool ValidateTargetArchitecture(HANDLE process) {
    using IsWow64Process2Fn = BOOL(WINAPI*)(HANDLE, USHORT*, USHORT*);
    const HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
    if (!kernel32) return false;
    const auto fn = reinterpret_cast<IsWow64Process2Fn>(
        GetProcAddress(kernel32, "IsWow64Process2"));
    if (!fn) return true;
    USHORT processMachine = 0, nativeMachine = 0;
    return fn(process, &processMachine, &nativeMachine) &&
           processMachine == IMAGE_FILE_MACHINE_UNKNOWN && nativeMachine == IMAGE_FILE_MACHINE_AMD64;
}

struct ValidatedImage {
    const IMAGE_NT_HEADERS64* nt = nullptr;
    const IMAGE_SECTION_HEADER* sections = nullptr;
};

bool ValidateImage(const void* data, size_t size, ValidatedImage& image) {
    if (!data || size < sizeof(IMAGE_DOS_HEADER)) return false;
    const auto* bytes = static_cast<const BYTE*>(data);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(bytes);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0 ||
        !RangeWithin(static_cast<std::uint32_t>(dos->e_lfanew), sizeof(IMAGE_NT_HEADERS64), size)) return false;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(bytes + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
        !(nt->FileHeader.Characteristics & IMAGE_FILE_DLL) ||
        nt->FileHeader.SizeOfOptionalHeader != sizeof(IMAGE_OPTIONAL_HEADER64) ||
        nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
        nt->OptionalHeader.NumberOfRvaAndSizes < IMAGE_NUMBEROF_DIRECTORY_ENTRIES ||
        nt->FileHeader.NumberOfSections == 0 || nt->FileHeader.NumberOfSections > 96 ||
        nt->OptionalHeader.SizeOfImage == 0 || nt->OptionalHeader.SizeOfImage > kMaximumImageSize ||
        nt->OptionalHeader.SizeOfHeaders == 0 || nt->OptionalHeader.SizeOfHeaders > size ||
        nt->OptionalHeader.SizeOfHeaders > nt->OptionalHeader.SizeOfImage ||
        nt->OptionalHeader.SectionAlignment == 0 || nt->OptionalHeader.FileAlignment == 0) return false;
    const std::uint64_t sectionOffset = static_cast<std::uint64_t>(dos->e_lfanew) +
        offsetof(IMAGE_NT_HEADERS64, OptionalHeader) + nt->FileHeader.SizeOfOptionalHeader;
    const std::uint64_t sectionBytes = static_cast<std::uint64_t>(nt->FileHeader.NumberOfSections) * sizeof(IMAGE_SECTION_HEADER);
    if (!RangeWithin(sectionOffset, sectionBytes, size)) return false;
    const auto* sections = reinterpret_cast<const IMAGE_SECTION_HEADER*>(bytes + sectionOffset);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
        const auto& section = sections[i];
        const std::uint64_t virtualSize = (std::max)(section.Misc.VirtualSize, section.SizeOfRawData);
        if (!RangeWithin(section.VirtualAddress, virtualSize, nt->OptionalHeader.SizeOfImage) ||
            (section.SizeOfRawData && !RangeWithin(section.PointerToRawData, section.SizeOfRawData, size))) return false;
    }
    if (nt->OptionalHeader.AddressOfEntryPoint &&
        !RangeWithin(nt->OptionalHeader.AddressOfEntryPoint, 1, nt->OptionalHeader.SizeOfImage)) return false;
    for (const int index : {IMAGE_DIRECTORY_ENTRY_IMPORT, IMAGE_DIRECTORY_ENTRY_BASERELOC,
                            IMAGE_DIRECTORY_ENTRY_EXCEPTION, IMAGE_DIRECTORY_ENTRY_TLS}) {
        const auto& directory = nt->OptionalHeader.DataDirectory[index];
        if ((directory.VirtualAddress == 0) != (directory.Size == 0) ||
            (directory.VirtualAddress && !RangeWithin(directory.VirtualAddress, directory.Size,
                                                       nt->OptionalHeader.SizeOfImage))) return false;
    }
    image = {nt, sections};
    return true;
}

bool WaitForRemoteThread(HANDLE thread, DWORD& exitCode) {
    if (WaitForSingleObject(thread, INFINITE) != WAIT_OBJECT_0) return false;
    return GetExitCodeThread(thread, &exitCode) && exitCode != STILL_ACTIVE;
}

DWORD SectionProtection(DWORD characteristics) {
    const bool executable = (characteristics & IMAGE_SCN_MEM_EXECUTE) != 0;
    const bool readable = (characteristics & IMAGE_SCN_MEM_READ) != 0;
    const bool writable = (characteristics & IMAGE_SCN_MEM_WRITE) != 0;
    if (executable) return writable ? PAGE_EXECUTE_READWRITE : (readable ? PAGE_EXECUTE_READ : PAGE_EXECUTE);
    return writable ? PAGE_READWRITE : (readable ? PAGE_READONLY : PAGE_NOACCESS);
}

bool ApplyImageProtections(HANDLE process, LPVOID base, const ValidatedImage& image) {
    DWORD oldProtect = 0;
    if (!VirtualProtectEx(process, base, image.nt->OptionalHeader.SizeOfHeaders, PAGE_READONLY, &oldProtect)) return false;
    for (WORD i = 0; i < image.nt->FileHeader.NumberOfSections; ++i) {
        const auto& section = image.sections[i];
        const SIZE_T size = (std::max)(section.Misc.VirtualSize, section.SizeOfRawData);
        if (!size) continue;
        LPVOID address = reinterpret_cast<LPVOID>(reinterpret_cast<std::uintptr_t>(base) + section.VirtualAddress);
        if (!VirtualProtectEx(process, address, size, SectionProtection(section.Characteristics), &oldProtect)) return false;
    }
    return true;
}

void ReleaseImageAfterUnregister(HANDLE process, LPVOID base, LPVOID functionTable) {
    // A leaked image is safer than a dynamic unwind table pointing into released memory.
    if (!functionTable || RemoteUnregisterSEH(process, functionTable))
        VirtualFreeEx(process, base, 0, MEM_RELEASE);
}

} // namespace

// 辅助：在目标进程分配内存
static LPVOID RemoteAlloc(HANDLE hProcess, SIZE_T size, DWORD protect = PAGE_READWRITE) {
    return VirtualAllocEx(hProcess, nullptr, size, MEM_COMMIT | MEM_RESERVE, protect);
}

static bool MakeRemoteCodeExecutable(HANDLE process, LPVOID address, SIZE_T size) {
    DWORD oldProtection = 0;
    return VirtualProtectEx(process, address, size, PAGE_EXECUTE_READ, &oldProtection) &&
           FlushInstructionCache(process, address, size);
}

// 辅助：在目标进程写内存
static bool RemoteWrite(HANDLE hProcess, LPVOID dst, const void* src, SIZE_T size) {
    SIZE_T written = 0;
    return WriteProcessMemory(hProcess, dst, src, size, &written) && written == size;
}

// 辅助：在目标进程读内存
static bool RemoteRead(HANDLE hProcess, LPCVOID src, void* dst, SIZE_T size) {
    SIZE_T read = 0;
    return ReadProcessMemory(hProcess, src, dst, size, &read) && read == size;
}

// 辅助：在目标进程已加载模块中查找
static HMODULE FindRemoteModule(DWORD pid, const wchar_t* dllName) {
    HANDLE snap = INVALID_HANDLE_VALUE;
    for (int attempt = 0; attempt < 8; ++attempt) {
        snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
        if (snap != INVALID_HANDLE_VALUE || GetLastError() != ERROR_BAD_LENGTH) break;
        Sleep(25);
    }
    if (snap == INVALID_HANDLE_VALUE) return nullptr;

    MODULEENTRY32W me{};
    me.dwSize = sizeof(me);
    HMODULE found = nullptr;

    if (Module32FirstW(snap, &me)) {
        do {
            if (_wcsicmp(me.szModule, dllName) == 0) {
                found = me.hModule;
                break;
            }
        } while (Module32NextW(snap, &me));
    }
    CloseHandle(snap);
    return found;
}

static FARPROC RemoteAddressOf(HANDLE process, FARPROC localAddress) {
    if (!localAddress) return nullptr;
    HMODULE owner = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(localAddress), &owner) || !owner) return nullptr;
    wchar_t path[MAX_PATH]{};
    if (!GetModuleFileNameW(owner, path, MAX_PATH)) return nullptr;
    const wchar_t* name = wcsrchr(path, L'\\');
    name = name ? name + 1 : path;
    HMODULE remoteOwner = FindRemoteModule(GetProcessId(process), name);
    if (!remoteOwner) return nullptr;
    const auto offset = reinterpret_cast<std::uintptr_t>(localAddress) - reinterpret_cast<std::uintptr_t>(owner);
    return reinterpret_cast<FARPROC>(reinterpret_cast<std::uintptr_t>(remoteOwner) + offset);
}

// 辅助：在目标进程调用 LoadLibraryW（兜底）
// 注意：CreateRemoteThread 的 exit code 只有 32 位，无法承载 64 位 HMODULE
// 所以加载后再用 FindRemoteModule 查询正确的基地址
static HMODULE RemoteLoadLibraryW(HANDLE hProcess, DWORD pid, const wchar_t* dllName) {
    SIZE_T pathSize = (wcslen(dllName) + 1) * sizeof(wchar_t);
    LPVOID remoteStr = RemoteAlloc(hProcess, pathSize, PAGE_READWRITE);
    if (!remoteStr) return nullptr;

    if (!RemoteWrite(hProcess, remoteStr, dllName, pathSize)) {
        VirtualFreeEx(hProcess, remoteStr, 0, MEM_RELEASE);
        return nullptr;
    }

    HMODULE hK32 = GetModuleHandleW(L"kernel32.dll");
    if (!hK32) {
        VirtualFreeEx(hProcess, remoteStr, 0, MEM_RELEASE);
        return nullptr;
    }
    FARPROC pLoadLib = RemoteAddressOf(hProcess, GetProcAddress(hK32, "LoadLibraryW"));
    if (!pLoadLib) {
        VirtualFreeEx(hProcess, remoteStr, 0, MEM_RELEASE);
        return nullptr;
    }

    HANDLE hThread = CreateRemoteThread(
        hProcess, nullptr, 0,
        reinterpret_cast<LPTHREAD_START_ROUTINE>(pLoadLib),
        remoteStr, 0, nullptr);
    if (!hThread) {
        VirtualFreeEx(hProcess, remoteStr, 0, MEM_RELEASE);
        return nullptr;
    }

    DWORD exitCode = 0;
    const bool completed = WaitForRemoteThread(hThread, exitCode);
    CloseHandle(hThread);
    VirtualFreeEx(hProcess, remoteStr, 0, MEM_RELEASE);

    if (!completed || exitCode == 0) return nullptr;

    // 重新查找模块以获取正确的 64 位 HMODULE
    return FindRemoteModule(pid, dllName);
}

// 辅助：在目标进程调用 GetProcAddress
// 关键修复：CreateRemoteThread 的 exit code 只有 32 位，无法承载 64 位函数地址
// 所以 shellcode 把 GetProcAddress 的返回值写入远程内存，再读回来
static FARPROC RemoteGetProcAddress(HANDLE hProcess, HMODULE hModule, const char* funcName, WORD ordinal = 0) {
    const SIZE_T nameSize = funcName ? strlen(funcName) + 1 : 0;
    // 总内存 = 函数名 + 8 字节结果槽
    SIZE_T totalSize = nameSize + 8;
    LPVOID remoteMem = RemoteAlloc(hProcess, totalSize, PAGE_READWRITE);
    if (!remoteMem) return nullptr;

    LPVOID remoteName = remoteMem;
    LPVOID remoteResult = reinterpret_cast<LPVOID>(
        reinterpret_cast<uintptr_t>(remoteMem) + nameSize);

    if (funcName && !RemoteWrite(hProcess, remoteName, funcName, nameSize)) {
        VirtualFreeEx(hProcess, remoteMem, 0, MEM_RELEASE);
        return nullptr;
    }
    // 结果槽清零
    uint64_t zero = 0;
    if (!RemoteWrite(hProcess, remoteResult, &zero, sizeof(zero))) {
        VirtualFreeEx(hProcess, remoteMem, 0, MEM_RELEASE);
        return nullptr;
    }

    HMODULE hK32 = GetModuleHandleW(L"kernel32.dll");
    if (!hK32) {
        VirtualFreeEx(hProcess, remoteMem, 0, MEM_RELEASE);
        return nullptr;
    }
    FARPROC pGetProcAddress = RemoteAddressOf(hProcess, GetProcAddress(hK32, "GetProcAddress"));
    if (!pGetProcAddress) {
        VirtualFreeEx(hProcess, remoteMem, 0, MEM_RELEASE);
        return nullptr;
    }

    // 参数打包：GetProcAddress(HMODULE, LPCSTR)
    // x64 ABI: RCX = hModule, RDX = funcName
    // CreateRemoteThread 入口 RSP%16==8，需 sub rsp,0x28 分配 32 字节影子空间 + 8 字节对齐
    //   sub rsp, 0x28
    //   mov rcx, hModule
    //   mov rdx, remoteName
    //   mov rax, GetProcAddress
    //   call rax
    //   mov [remoteResult], rax      ; 保存完整 64 位返回值
    //   add rsp, 0x28
    //   ret
    unsigned char shellcode[80] = {0};
    int p = 0;
    // sub rsp, 0x28
    shellcode[p++] = 0x48; shellcode[p++] = 0x83; shellcode[p++] = 0xEC; shellcode[p++] = 0x28;
    // mov rcx, imm64
    shellcode[p++] = 0x48; shellcode[p++] = 0xB9;
    *reinterpret_cast<uint64_t*>(&shellcode[p]) = reinterpret_cast<uint64_t>(hModule);
    p += 8;
    // mov rdx, imm64
    shellcode[p++] = 0x48; shellcode[p++] = 0xBA;
    *reinterpret_cast<uint64_t*>(&shellcode[p]) = funcName
        ? reinterpret_cast<uint64_t>(remoteName) : static_cast<uint64_t>(ordinal);
    p += 8;
    // mov rax, imm64
    shellcode[p++] = 0x48; shellcode[p++] = 0xB8;
    *reinterpret_cast<uint64_t*>(&shellcode[p]) = reinterpret_cast<uint64_t>(pGetProcAddress);
    p += 8;
    // call rax
    shellcode[p++] = 0xFF;
    shellcode[p++] = 0xD0;
    // mov [imm64], rax   (48 A3 imm64)
    shellcode[p++] = 0x48; shellcode[p++] = 0xA3;
    *reinterpret_cast<uint64_t*>(&shellcode[p]) = reinterpret_cast<uint64_t>(remoteResult);
    p += 8;
    // add rsp, 0x28
    shellcode[p++] = 0x48; shellcode[p++] = 0x83; shellcode[p++] = 0xC4; shellcode[p++] = 0x28;
    // ret
    shellcode[p++] = 0xC3;

    LPVOID remoteShell = RemoteAlloc(hProcess, sizeof(shellcode), PAGE_READWRITE);
    if (!remoteShell) {
        VirtualFreeEx(hProcess, remoteMem, 0, MEM_RELEASE);
        return nullptr;
    }
    if (!RemoteWrite(hProcess, remoteShell, shellcode, sizeof(shellcode)) ||
        !MakeRemoteCodeExecutable(hProcess, remoteShell, sizeof(shellcode))) {
        VirtualFreeEx(hProcess, remoteMem, 0, MEM_RELEASE);
        VirtualFreeEx(hProcess, remoteShell, 0, MEM_RELEASE);
        return nullptr;
    }

    HANDLE hThread = CreateRemoteThread(
        hProcess, nullptr, 0,
        reinterpret_cast<LPTHREAD_START_ROUTINE>(remoteShell),
        nullptr, 0, nullptr);
    if (!hThread) {
        VirtualFreeEx(hProcess, remoteMem, 0, MEM_RELEASE);
        VirtualFreeEx(hProcess, remoteShell, 0, MEM_RELEASE);
        return nullptr;
    }

    DWORD exitCode = 0;
    const bool completed = WaitForRemoteThread(hThread, exitCode);
    CloseHandle(hThread);
    VirtualFreeEx(hProcess, remoteShell, 0, MEM_RELEASE);

    // 读回完整 64 位结果
    uint64_t result = 0;
    bool ok = RemoteRead(hProcess, remoteResult, &result, sizeof(result));
    VirtualFreeEx(hProcess, remoteMem, 0, MEM_RELEASE);

    SecureZeroMemory(shellcode, sizeof(shellcode));
    if (!completed || !ok || result == 0) return nullptr;
    return reinterpret_cast<FARPROC>(result);
}

// 远程调用 DllMain(hModule, DLL_PROCESS_ATTACH, nullptr)
static bool RemoteCallDllMain(HANDLE hProcess, LPVOID base, LPVOID entry) {
    // DllMain 签名：BOOL DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
    // x64 ABI: RCX = hinstDLL, RDX = fdwReason, R8 = lpvReserved
    // CreateRemoteThread 入口 RSP%16==8，需 sub rsp,0x28 分配 32 字节影子空间 + 8 字节对齐
    unsigned char shellcode[64] = {0};
    int p = 0;
    // sub rsp, 0x28
    shellcode[p++] = 0x48; shellcode[p++] = 0x83; shellcode[p++] = 0xEC; shellcode[p++] = 0x28;
    // mov rcx, base
    shellcode[p++] = 0x48; shellcode[p++] = 0xB9;
    *reinterpret_cast<uint64_t*>(&shellcode[p]) = reinterpret_cast<uint64_t>(base);
    p += 8;
    // mov rdx, 1 (DLL_PROCESS_ATTACH)
    shellcode[p++] = 0x48; shellcode[p++] = 0xC7; shellcode[p++] = 0xC2;
    *reinterpret_cast<uint32_t*>(&shellcode[p]) = 1;
    p += 4;
    // xor r8, r8
    shellcode[p++] = 0x4D; shellcode[p++] = 0x31; shellcode[p++] = 0xC0;
    // mov rax, entry
    shellcode[p++] = 0x48; shellcode[p++] = 0xB8;
    *reinterpret_cast<uint64_t*>(&shellcode[p]) = reinterpret_cast<uint64_t>(entry);
    p += 8;
    // call rax
    shellcode[p++] = 0xFF;
    shellcode[p++] = 0xD0;
    // add rsp, 0x28
    shellcode[p++] = 0x48; shellcode[p++] = 0x83; shellcode[p++] = 0xC4; shellcode[p++] = 0x28;
    // ret
    shellcode[p++] = 0xC3;

    LPVOID remoteShell = RemoteAlloc(hProcess, sizeof(shellcode), PAGE_READWRITE);
    if (!remoteShell) return false;
    if (!RemoteWrite(hProcess, remoteShell, shellcode, sizeof(shellcode)) ||
        !MakeRemoteCodeExecutable(hProcess, remoteShell, sizeof(shellcode))) {
        VirtualFreeEx(hProcess, remoteShell, 0, MEM_RELEASE);
        return false;
    }

    HANDLE hThread = CreateRemoteThread(
        hProcess, nullptr, 0,
        reinterpret_cast<LPTHREAD_START_ROUTINE>(remoteShell),
        nullptr, 0, nullptr);
    if (!hThread) {
        VirtualFreeEx(hProcess, remoteShell, 0, MEM_RELEASE);
        return false;
    }

    DWORD exitCode = 0;
    const bool completed = WaitForRemoteThread(hThread, exitCode);
    CloseHandle(hThread);
    VirtualFreeEx(hProcess, remoteShell, 0, MEM_RELEASE);
    SecureZeroMemory(shellcode, sizeof(shellcode));

    return completed && exitCode != 0;
}

// 远程注册 x64 SEH 异常处理表（RtlAddFunctionTable）
// 不注册会导致 payload 内的 __try/__except 与 CRT init 异常时崩溃
static bool RemoteRegisterSEH(HANDLE hProcess, LPVOID base, LPVOID funcTable, DWORD count) {
    HMODULE hK32 = GetModuleHandleW(L"kernel32.dll");
    if (!hK32) return false;
    FARPROC pRtlAddFunctionTable = RemoteAddressOf(hProcess, GetProcAddress(hK32, "RtlAddFunctionTable"));
    if (!pRtlAddFunctionTable) return false;

    // RtlAddFunctionTable(PRUNTIME_FUNCTION FunctionTable, DWORD EntryCount, DWORD64 BaseAddress)
    // x64 ABI: RCX=funcTable, RDX=count, R8=base
    unsigned char shellcode[80] = {0};
    int p = 0;
    // sub rsp, 0x28
    shellcode[p++] = 0x48; shellcode[p++] = 0x83; shellcode[p++] = 0xEC; shellcode[p++] = 0x28;
    // mov rcx, funcTable
    shellcode[p++] = 0x48; shellcode[p++] = 0xB9;
    *reinterpret_cast<uint64_t*>(&shellcode[p]) = reinterpret_cast<uint64_t>(funcTable); p += 8;
    // mov rdx, count
    shellcode[p++] = 0x48; shellcode[p++] = 0xC7; shellcode[p++] = 0xC2;
    *reinterpret_cast<uint32_t*>(&shellcode[p]) = count; p += 4;
    // mov r8, base
    shellcode[p++] = 0x49; shellcode[p++] = 0xB8;
    *reinterpret_cast<uint64_t*>(&shellcode[p]) = reinterpret_cast<uint64_t>(base); p += 8;
    // mov rax, RtlAddFunctionTable
    shellcode[p++] = 0x48; shellcode[p++] = 0xB8;
    *reinterpret_cast<uint64_t*>(&shellcode[p]) = reinterpret_cast<uint64_t>(pRtlAddFunctionTable); p += 8;
    // call rax
    shellcode[p++] = 0xFF; shellcode[p++] = 0xD0;
    // add rsp, 0x28
    shellcode[p++] = 0x48; shellcode[p++] = 0x83; shellcode[p++] = 0xC4; shellcode[p++] = 0x28;
    // ret
    shellcode[p++] = 0xC3;

    LPVOID remoteShell = RemoteAlloc(hProcess, sizeof(shellcode), PAGE_READWRITE);
    if (!remoteShell) return false;
    if (!RemoteWrite(hProcess, remoteShell, shellcode, sizeof(shellcode)) ||
        !MakeRemoteCodeExecutable(hProcess, remoteShell, sizeof(shellcode))) {
        VirtualFreeEx(hProcess, remoteShell, 0, MEM_RELEASE);
        return false;
    }

    HANDLE hThread = CreateRemoteThread(
        hProcess, nullptr, 0,
        reinterpret_cast<LPTHREAD_START_ROUTINE>(remoteShell),
        nullptr, 0, nullptr);
    if (!hThread) {
        VirtualFreeEx(hProcess, remoteShell, 0, MEM_RELEASE);
        return false;
    }

    DWORD exitCode = 0;
    const bool completed = WaitForRemoteThread(hThread, exitCode);
    CloseHandle(hThread);
    VirtualFreeEx(hProcess, remoteShell, 0, MEM_RELEASE);
    SecureZeroMemory(shellcode, sizeof(shellcode));
    return completed && exitCode != 0;
}

static bool RemoteUnregisterSEH(HANDLE hProcess, LPVOID funcTable) {
    HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
    if (!kernel32) return false;
    FARPROC rtlDeleteFunctionTable = RemoteAddressOf(hProcess, GetProcAddress(kernel32, "RtlDeleteFunctionTable"));
    if (!rtlDeleteFunctionTable) return false;
    unsigned char shellcode[48]{};
    int p = 0;
    shellcode[p++] = 0x48; shellcode[p++] = 0x83; shellcode[p++] = 0xEC; shellcode[p++] = 0x28;
    shellcode[p++] = 0x48; shellcode[p++] = 0xB9;
    std::memcpy(&shellcode[p], &funcTable, sizeof(funcTable)); p += sizeof(funcTable);
    shellcode[p++] = 0x48; shellcode[p++] = 0xB8;
    std::memcpy(&shellcode[p], &rtlDeleteFunctionTable, sizeof(rtlDeleteFunctionTable)); p += sizeof(rtlDeleteFunctionTable);
    shellcode[p++] = 0xFF; shellcode[p++] = 0xD0;
    shellcode[p++] = 0x48; shellcode[p++] = 0x83; shellcode[p++] = 0xC4; shellcode[p++] = 0x28;
    shellcode[p++] = 0xC3;
    LPVOID remoteShell = RemoteAlloc(hProcess, sizeof(shellcode), PAGE_READWRITE);
    if (!remoteShell) return false;
    if (!RemoteWrite(hProcess, remoteShell, shellcode, sizeof(shellcode)) ||
        !MakeRemoteCodeExecutable(hProcess, remoteShell, sizeof(shellcode))) {
        VirtualFreeEx(hProcess, remoteShell, 0, MEM_RELEASE);
        SecureZeroMemory(shellcode, sizeof(shellcode));
        return false;
    }
    HANDLE thread = CreateRemoteThread(hProcess, nullptr, 0,
        reinterpret_cast<LPTHREAD_START_ROUTINE>(remoteShell), nullptr, 0, nullptr);
    if (!thread) {
        VirtualFreeEx(hProcess, remoteShell, 0, MEM_RELEASE);
        SecureZeroMemory(shellcode, sizeof(shellcode));
        return false;
    }
    DWORD exitCode = 0;
    const bool completed = WaitForRemoteThread(thread, exitCode);
    CloseHandle(thread);
    VirtualFreeEx(hProcess, remoteShell, 0, MEM_RELEASE);
    SecureZeroMemory(shellcode, sizeof(shellcode));
    return completed && exitCode != 0;
}

// 处理导入表
static bool ProcessImports(HANDLE hProcess, DWORD pid, LPVOID base, IMAGE_IMPORT_DESCRIPTOR* imports,
                           SIZE_T importSize, SIZE_T imageSize) {
    if (!imports || importSize < sizeof(IMAGE_IMPORT_DESCRIPTOR)) return false;
    const SIZE_T descriptorCount = importSize / sizeof(IMAGE_IMPORT_DESCRIPTOR);
    bool descriptorsTerminated = false;
    for (SIZE_T descriptorIndex = 0; descriptorIndex < descriptorCount; ++descriptorIndex) {
        auto* p = imports + descriptorIndex;
        if (p->Name == 0) { descriptorsTerminated = true; break; }
        if (!RangeWithin(p->Name, 1, imageSize) || !p->FirstThunk ||
            !RangeWithin(p->FirstThunk, sizeof(std::uint64_t), imageSize) ||
            (p->OriginalFirstThunk && !RangeWithin(p->OriginalFirstThunk, sizeof(std::uint64_t), imageSize))) return false;
        // 读取 DLL 名
        char dllName[256] = {0};
        const SIZE_T dllNameBytes = (std::min<SIZE_T>)(sizeof(dllName) - 1, imageSize - p->Name);
        LPVOID nameAddr = reinterpret_cast<LPVOID>(
            reinterpret_cast<uintptr_t>(base) + p->Name);
        if (!RemoteRead(hProcess, nameAddr, dllName, dllNameBytes)) return false;

        // 转换为宽字符
        wchar_t wDllName[256] = {0};
        if (!std::memchr(dllName, '\0', dllNameBytes) ||
            MultiByteToWideChar(CP_ACP, 0, dllName, -1, wDllName, 255) <= 0) return false;

        HMODULE hMod = FindRemoteModule(pid, wDllName);
        if (!hMod) hMod = RemoteLoadLibraryW(hProcess, pid, wDllName);
        if (!hMod) {
            LOG("import dll failed err=%lu", GetLastError());
            return false;
        }

        // 处理 OriginalFirstThunk（ILT）和 FirstThunk（IAT）
        uintptr_t* ilt = nullptr;
        uintptr_t* iat = nullptr;

        if (p->OriginalFirstThunk) {
            ilt = reinterpret_cast<uintptr_t*>(
                reinterpret_cast<uintptr_t>(base) + p->OriginalFirstThunk);
        }
        iat = reinterpret_cast<uintptr_t*>(
            reinterpret_cast<uintptr_t>(base) + p->FirstThunk);

        uintptr_t* thunkSource = ilt ? ilt : iat;

        const SIZE_T sourceRva = p->OriginalFirstThunk ? p->OriginalFirstThunk : p->FirstThunk;
        const SIZE_T maximumThunks = (std::min)((imageSize - sourceRva) / sizeof(std::uint64_t),
                                                (imageSize - p->FirstThunk) / sizeof(std::uint64_t));
        bool thunksTerminated = false;
        for (SIZE_T i = 0; i < maximumThunks; ++i) {
            uint64_t thunk = 0;
            if (!RemoteRead(hProcess, thunkSource + i, &thunk, sizeof(thunk))) return false;
            if (thunk == 0) { thunksTerminated = true; break; }

            FARPROC funcAddr = nullptr;
            WORD ordinal = 0;
            char funcName[256] = {0};
            if (thunk & 0x8000000000000000ULL) {
                // 序号导入
                ordinal = static_cast<WORD>(thunk & 0xFFFF);
                funcAddr = RemoteGetProcAddress(hProcess, hMod, nullptr, ordinal);
            } else {
                // 名称导入：读 IMAGE_IMPORT_BY_NAME
                uint16_t hint = 0;
                const DWORD nameRva = static_cast<DWORD>(thunk);
                if (!RangeWithin(nameRva, sizeof(hint) + 1, imageSize)) return false;
                LPVOID namePtr = reinterpret_cast<LPVOID>(reinterpret_cast<uintptr_t>(base) + nameRva);
                if (!RemoteRead(hProcess, namePtr, &hint, sizeof(hint))) return false;
                const SIZE_T nameBytes = (std::min<SIZE_T>)(sizeof(funcName) - 1, imageSize - nameRva - sizeof(hint));
                if (!RemoteRead(hProcess,
                    reinterpret_cast<LPVOID>(reinterpret_cast<uintptr_t>(namePtr) + 2),
                    funcName, nameBytes) || !std::memchr(funcName, '\0', nameBytes)) return false;

                funcAddr = RemoteGetProcAddress(hProcess, hMod, funcName);
            }

            if (!funcAddr) {
                if (funcName[0])
                    LOG("import function failed");
                else
                    LOG("import ordinal failed");
                return false;
            }

            // 写入 IAT
            uint64_t addrValue = reinterpret_cast<uint64_t>(funcAddr);
            if (!RemoteWrite(hProcess, iat + i, &addrValue, sizeof(addrValue))) return false;
        }
        if (!thunksTerminated) return false;
    }
    return descriptorsTerminated;
}

// 错误码转字符串
const char* ErrorString(Error e) {
    switch (e) {
        case Error::Ok:                 return "Ok";
        case Error::InvalidPE:          return "Invalid PE format";
        case Error::AllocImageFailed:   return "VirtualAllocEx (image) failed";
        case Error::WriteHeadersFailed: return "Write PE headers failed";
        case Error::WriteSectionFailed: return "Write section failed";
        case Error::RelocOffsetNotFound:return "Relocation RVA->file offset not found";
        case Error::ReadImportFailed:   return "Read import directory failed";
        case Error::ImportFailed:       return "Resolve imports failed (missing DLL or function)";
        case Error::DllMainFailed:      return "DllMain returned FALSE";
        case Error::TargetArchitectureMismatch: return "Target process is not native AMD64";
        case Error::RemoteThreadFailed: return "Remote helper thread failed";
        case Error::ExceptionRegistrationFailed: return "Register exception table failed";
    }
    return "Unknown";
}

// 主入口
bool Inject(HANDLE hProcess, const void* peData, size_t peSize, Error* err) {
    auto setErr = [&](Error e) { if (err) *err = e; };
    setErr(Error::Ok);

    ValidatedImage validated{};
    if (!ValidateImage(peData, peSize, validated)) { setErr(Error::InvalidPE); return false; }
    if (!ValidateTargetArchitecture(hProcess)) { setErr(Error::TargetArchitectureMismatch); return false; }
    const auto* nt = validated.nt;
    const auto* section = validated.sections;
    if (!nt || !section) { setErr(Error::InvalidPE); return false; }

    SIZE_T imageSize = nt->OptionalHeader.SizeOfImage;
    ULONGLONG preferredBase = nt->OptionalHeader.ImageBase;

    // 1. 在目标进程分配镜像内存
    LPVOID remoteBase = RemoteAlloc(hProcess, imageSize, PAGE_READWRITE);
    if (!remoteBase) { setErr(Error::AllocImageFailed); return false; }

    // 2. 写入 PE 头
    SIZE_T headersSize = nt->OptionalHeader.SizeOfHeaders;
    if (!RemoteWrite(hProcess, remoteBase, peData, headersSize)) {
        VirtualFreeEx(hProcess, remoteBase, 0, MEM_RELEASE);
        setErr(Error::WriteHeadersFailed);
        return false;
    }

    // 3. 写入每个节区
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
        if (section[i].SizeOfRawData == 0) continue;
        LPVOID dst = reinterpret_cast<LPVOID>(
            reinterpret_cast<uintptr_t>(remoteBase) + section[i].VirtualAddress);
        const void* src = reinterpret_cast<const BYTE*>(peData) + section[i].PointerToRawData;
        if (!RemoteWrite(hProcess, dst, src, section[i].SizeOfRawData)) {
            VirtualFreeEx(hProcess, remoteBase, 0, MEM_RELEASE);
            setErr(Error::WriteSectionFailed);
            return false;
        }
    }

    // 4. 处理重定位
    int64_t delta = reinterpret_cast<int64_t>(remoteBase) - static_cast<int64_t>(preferredBase);
    if (delta != 0) {
        DWORD relocRVA = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC].VirtualAddress;
        DWORD relocSize = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC].Size;
        if (relocRVA && relocSize) {
            // 重定位表在目标进程里读取比较慢，直接从源 PE 读到本地，处理完写回
            // 注意：relocRVA 可能在 .reloc 节，其 PointerToRawData 不一定等于 RVA
            // 所以要从映射后的内存读，但映射在远端，我们用本地副本
            // 简化：重新从原始 PE 按 RVA 找到文件偏移
            // 用 IMAGE_FIRST_SECTION 的 PointerToRawData 转换
            DWORD fileOffset = 0;
            bool offsetFound = false;
            for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
                const std::uint64_t mappedSize = (std::max)(section[i].Misc.VirtualSize, section[i].SizeOfRawData);
                if (relocRVA >= section[i].VirtualAddress &&
                    static_cast<std::uint64_t>(relocRVA) < section[i].VirtualAddress + mappedSize) {
                    fileOffset = section[i].PointerToRawData + (relocRVA - section[i].VirtualAddress);
                    offsetFound = true;
                    break;
                }
            }
            if (!offsetFound || !RangeWithin(fileOffset, relocSize, peSize)) {
                VirtualFreeEx(hProcess, remoteBase, 0, MEM_RELEASE);
                setErr(Error::RelocOffsetNotFound);
                return false;
            }

            // 读取重定位块到本地缓冲
            std::vector<BYTE> relocBuf(relocSize);
            std::memcpy(relocBuf.data(), reinterpret_cast<const BYTE*>(peData) + fileOffset, relocSize);

            // 逐块处理并写回目标进程
            auto* p = relocBuf.data();
            auto* end = p + relocSize;
            while (p < end) {
                if (static_cast<SIZE_T>(end - p) < sizeof(IMAGE_BASE_RELOCATION)) {
                    VirtualFreeEx(hProcess, remoteBase, 0, MEM_RELEASE);
                    setErr(Error::InvalidPE);
                    return false;
                }
                auto* block = reinterpret_cast<IMAGE_BASE_RELOCATION*>(p);
                if (block->SizeOfBlock < sizeof(IMAGE_BASE_RELOCATION) ||
                    block->SizeOfBlock > static_cast<DWORD>(end - p) ||
                    (block->SizeOfBlock - sizeof(IMAGE_BASE_RELOCATION)) % sizeof(WORD) != 0) {
                    VirtualFreeEx(hProcess, remoteBase, 0, MEM_RELEASE);
                    setErr(Error::InvalidPE);
                    return false;
                }

                DWORD blockVA = block->VirtualAddress;
                DWORD entryCount = (block->SizeOfBlock - sizeof(IMAGE_BASE_RELOCATION)) / sizeof(WORD);
                WORD* entries = reinterpret_cast<WORD*>(p + sizeof(IMAGE_BASE_RELOCATION));

                for (DWORD i = 0; i < entryCount; ++i) {
                    WORD type = entries[i] >> 12;
                    WORD offset = entries[i] & 0x0FFF;
                    if (type == 0) continue;
                    if (type != IMAGE_REL_BASED_DIR64 ||
                        !RangeWithin(static_cast<std::uint64_t>(blockVA) + offset, sizeof(std::uint64_t), imageSize)) {
                        VirtualFreeEx(hProcess, remoteBase, 0, MEM_RELEASE);
                        setErr(Error::InvalidPE);
                        return false;
                    }
                    if (type == IMAGE_REL_BASED_DIR64) {
                        LPVOID target = reinterpret_cast<LPVOID>(
                            reinterpret_cast<uintptr_t>(remoteBase) + blockVA + offset);
                        uint64_t value = 0;
                        if (!RemoteRead(hProcess, target, &value, sizeof(value))) {
                            VirtualFreeEx(hProcess, remoteBase, 0, MEM_RELEASE);
                            setErr(Error::RelocOffsetNotFound);
                            return false;
                        }
                        value += delta;
                        if (!RemoteWrite(hProcess, target, &value, sizeof(value))) {
                            VirtualFreeEx(hProcess, remoteBase, 0, MEM_RELEASE);
                            setErr(Error::RelocOffsetNotFound);
                            return false;
                        }
                    }
                }
                p += block->SizeOfBlock;
            }
        } else {
            VirtualFreeEx(hProcess, remoteBase, 0, MEM_RELEASE);
            setErr(Error::InvalidPE);
            return false;
        }
    }

    // 5. 处理导入表
    DWORD importRVA = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
    DWORD importSize = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].Size;
    if (importRVA && importSize) {
        // 从目标进程读取导入表
        LPVOID importAddr = reinterpret_cast<LPVOID>(
            reinterpret_cast<uintptr_t>(remoteBase) + importRVA);
        std::vector<BYTE> importBuf(importSize);
        if (!RemoteRead(hProcess, importAddr, importBuf.data(), importSize)) {
            VirtualFreeEx(hProcess, remoteBase, 0, MEM_RELEASE);
            setErr(Error::ReadImportFailed);
            return false;
        }
        DWORD pid = GetProcessId(hProcess);
        if (!ProcessImports(hProcess, pid, remoteBase,
            reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(importBuf.data()), importSize, imageSize)) {
            VirtualFreeEx(hProcess, remoteBase, 0, MEM_RELEASE);
            setErr(Error::ImportFailed);
            return false;
        }
    }

    // 6. 注册 x64 SEH 异常处理表（必须在 DllMain 之前，CRT init 可能依赖）
    DWORD excRVA = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION].VirtualAddress;
    DWORD excSize = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION].Size;
    LPVOID registeredFunctionTable = nullptr;
    if (excRVA && excSize) {
        LPVOID funcTable = reinterpret_cast<LPVOID>(
            reinterpret_cast<uintptr_t>(remoteBase) + excRVA);
        if (excSize % sizeof(RUNTIME_FUNCTION) != 0 ||
            !RemoteRegisterSEH(hProcess, remoteBase, funcTable, excSize / sizeof(RUNTIME_FUNCTION))) {
            VirtualFreeEx(hProcess, remoteBase, 0, MEM_RELEASE);
            setErr(Error::ExceptionRegistrationFailed);
            return false;
        }
        registeredFunctionTable = funcTable;
    }

    if (!ApplyImageProtections(hProcess, remoteBase, validated) ||
        !FlushInstructionCache(hProcess, remoteBase, imageSize)) {
        ReleaseImageAfterUnregister(hProcess, remoteBase, registeredFunctionTable);
        setErr(Error::WriteSectionFailed);
        return false;
    }

    // 6.5 调用 TLS 回调（如果存在）
    // 调用编译器或依赖库可能生成的 TLS 回调；payload 本身不依赖静态 TLS 数据。
    DWORD tlsRVA = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_TLS].VirtualAddress;
    DWORD tlsSize = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_TLS].Size;
    if (tlsRVA && tlsSize >= sizeof(IMAGE_TLS_DIRECTORY)) {
        IMAGE_TLS_DIRECTORY tlsDir{};
        LPVOID tlsAddr = reinterpret_cast<LPVOID>(
            reinterpret_cast<uintptr_t>(remoteBase) + tlsRVA);
        if (RemoteRead(hProcess, tlsAddr, &tlsDir, sizeof(tlsDir))) {
            // AddressOfCallBacks 是基于首选基址的绝对地址，需要应用 delta
            uint64_t cbArrayAddr = tlsDir.AddressOfCallBacks;
            if (cbArrayAddr != 0) {
                // 把绝对地址转换成基于 remoteBase 的偏移再读
                // AddressOfCallBacks 在 PE 文件里存的是 VA，运行时也按 VA 用
                // 我们直接按 cbArrayAddr 读远程内存
                for (DWORD i = 0; i < 64; ++i) {
                    uint64_t cb = 0;
                    LPVOID cbSlot = reinterpret_cast<LPVOID>(
                        cbArrayAddr + i * sizeof(uint64_t));
                    // 如果 cbArrayAddr 是基于 preferredBase 的，需要先加 delta
                    // 但实际上 TLS 目录里的指针在加载时也会被重定位（delta 已应用）
                    // 所以 cbArrayAddr 已经是正确的远程地址
                    if (!RemoteRead(hProcess, cbSlot, &cb, sizeof(cb))) break;
                    if (cb == 0) break;
                    // cb 是回调地址，同样已经被重定位过
                    LPVOID entry = reinterpret_cast<LPVOID>(cb);
                    // 调用 callback(base, DLL_PROCESS_ATTACH, nullptr)
                    if (!RemoteCallDllMain(hProcess, remoteBase, entry)) {
                        ReleaseImageAfterUnregister(hProcess, remoteBase, registeredFunctionTable);
                        setErr(Error::RemoteThreadFailed);
                        return false;
                    }
                }
            }
        }
    }

    // 7. 调用 DllMain
    if (nt->OptionalHeader.AddressOfEntryPoint == 0) return true;
    LPVOID entryPoint = reinterpret_cast<LPVOID>(reinterpret_cast<uintptr_t>(remoteBase) + nt->OptionalHeader.AddressOfEntryPoint);
    if (!RemoteCallDllMain(hProcess, remoteBase, entryPoint)) {
        ReleaseImageAfterUnregister(hProcess, remoteBase, registeredFunctionTable);
        setErr(Error::DllMainFailed);
        return false;
    }
    return true;
}

} // namespace manual_pe
