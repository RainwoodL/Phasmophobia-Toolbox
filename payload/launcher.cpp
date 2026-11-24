#include <windows.h>

#include "hud_runtime.h"

#include <cstdio>
#include <cwchar>
#include <filesystem>
#include <string>

namespace {

static_assert(sizeof(void*) == 8, "The Phasmophobia launcher requires Windows x64.");
using UnityMainFn = int (WINAPI*)(HINSTANCE, HINSTANCE, LPWSTR, int);

int StartupError(const wchar_t* message, DWORD code) {
    const HANDLE errorOutput = GetStdHandle(STD_ERROR_HANDLE);
    const DWORD outputType = GetFileType(errorOutput);
    DWORD consoleMode = 0;
    if (outputType == FILE_TYPE_PIPE || outputType == FILE_TYPE_DISK ||
        GetConsoleMode(errorOutput, &consoleMode)) {
        std::fwprintf(stderr, L"%ls (%lu)\n", message, code);
    } else {
        MessageBoxW(nullptr, message, L"Phasmophobia Toolbox", MB_OK | MB_ICONERROR);
    }
    return static_cast<int>(code);
}

} // namespace

int WINAPI wWinMain(_In_ HINSTANCE instance, _In_opt_ HINSTANCE,
                    _In_ LPWSTR commandLine, _In_ int showCommand) {
    std::wstring executable(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
    if (!length || length >= executable.size())
        return StartupError(L"Could not locate Phasmophobia.exe.", !length ? GetLastError() : ERROR_INSUFFICIENT_BUFFER);
    executable.resize(length);
    const auto directory = std::filesystem::path(executable).parent_path();
    const DWORD dataAttributes = GetFileAttributesW((directory / L"Phasmophobia_Data").c_str());
    const DWORD assemblyAttributes = GetFileAttributesW((directory / L"GameAssembly.dll").c_str());
    if (dataAttributes == INVALID_FILE_ATTRIBUTES || !(dataAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
        assemblyAttributes == INVALID_FILE_ATTRIBUTES || (assemblyAttributes & FILE_ATTRIBUTE_DIRECTORY))
        return StartupError(L"Place Phasmophobia.exe in the installed game folder alongside "
                            L"Phasmophobia_Data and GameAssembly.dll.", ERROR_FILE_NOT_FOUND);
    if (!SetCurrentDirectoryW(directory.c_str()))
        return StartupError(L"Could not open the installed Phasmophobia folder.", GetLastError());

    const auto playerPath = directory / L"UnityPlayer.dll";
    HMODULE player = LoadLibraryExW(playerPath.c_str(), nullptr,
                                    LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!player) return StartupError(L"Could not load UnityPlayer.dll from the game folder.", GetLastError());
    const auto unityMain = reinterpret_cast<UnityMainFn>(GetProcAddress(player, "UnityMain2"));
    if (!unityMain) return StartupError(L"This UnityPlayer.dll is missing UnityMain2.", ERROR_PROC_NOT_FOUND);

    hud_runtime::Start(hud_runtime::Startup::GameStarting);
    const int exitCode = unityMain(instance, nullptr, commandLine, showCommand);
    // Game callbacks and their worker threads have process lifetime. Leave their
    // modules mapped and avoid running C++ static destructors underneath them.
    ExitProcess(static_cast<UINT>(exitCode));
}
