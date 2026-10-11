#pragma once

#include <windows.h>

#include <algorithm>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <mutex>
#include <vector>

namespace log_util {
namespace detail {

constexpr wchar_t kMappingName[] = L"Local\\GhostRoomTools.LogPath.v1";
constexpr wchar_t kMutexName[] = L"Local\\GhostRoomTools.LogMutex.v1";
constexpr std::size_t kPathChars = 520;

struct LogFileInfo {
    wchar_t path[kPathChars]{};
    FILETIME writeTime{};
};

inline std::mutex& ProcessMutex() {
    static std::mutex mutex;
    return mutex;
}

inline wchar_t* LogPath() {
    static wchar_t path[kPathChars]{};
    return path;
}

inline HANDLE& MappingHandle() {
    static HANDLE handle = nullptr;
    return handle;
}

inline void*& MappingView() {
    static void* view = nullptr;
    return view;
}

inline HANDLE& NamedMutex() {
    static HANDLE handle = nullptr;
    return handle;
}

inline std::uint32_t HashBytes(const void* data, std::size_t size) {
    const auto* bytes = static_cast<const unsigned char*>(data);
    std::uint32_t hash = 2166136261u;
    for (std::size_t i = 0; i < size; ++i) {
        hash ^= bytes[i];
        hash *= 16777619u;
    }
    return hash;
}

inline std::uint32_t HashText(const char* text) {
    return text ? HashBytes(text, std::strlen(text)) : 0;
}

inline bool BuildDirectory(wchar_t* output, std::size_t capacity) {
    wchar_t profile[kPathChars]{};
    const DWORD length = GetEnvironmentVariableW(L"USERPROFILE", profile, kPathChars);
    if (length == 0 || length >= kPathChars) return false;

    const wchar_t* parts[] = {
        L"AppData", L"LocalLow", L"Kinetic Games", L"Phasmophobia", L"Toolsbox"};
    wchar_t current[kPathChars]{};
    if (_snwprintf_s(current, kPathChars, _TRUNCATE, L"%ls", profile) < 0) return false;
    for (const wchar_t* part : parts) {
        wchar_t next[kPathChars]{};
        if (_snwprintf_s(next, kPathChars, _TRUNCATE, L"%ls\\%ls", current, part) < 0)
            return false;
        if (!CreateDirectoryW(next, nullptr) && GetLastError() != ERROR_ALREADY_EXISTS)
            return false;
        wcsncpy_s(current, kPathChars, next, _TRUNCATE);
    }
    if (_snwprintf_s(output, capacity, _TRUNCATE, L"%ls", current) < 0) return false;
    return true;
}

inline bool CopyPath(const wchar_t* source) {
    if (!source || !source[0]) return false;
    return wcsncpy_s(LogPath(), kPathChars, source, _TRUNCATE) == 0;
}

inline bool CreateSessionFile() {
    wchar_t directory[kPathChars]{};
    if (!BuildDirectory(directory, kPathChars)) return false;

    SYSTEMTIME time{};
    GetLocalTime(&time);
    for (unsigned int suffix = 0; suffix < 1000; ++suffix) {
        wchar_t candidate[kPathChars]{};
        int result = 0;
        if (suffix == 0) {
            result = _snwprintf_s(
                candidate, kPathChars, _TRUNCATE,
                L"%ls\\%04u%02u%02u_%02u%02u%02u_%03u.log",
                directory, time.wYear, time.wMonth, time.wDay, time.wHour,
                time.wMinute, time.wSecond, time.wMilliseconds);
        } else {
            result = _snwprintf_s(
                candidate, kPathChars, _TRUNCATE,
                L"%ls\\%04u%02u%02u_%02u%02u%02u_%03u_%u.log",
                directory, time.wYear, time.wMonth, time.wDay, time.wHour,
                time.wMinute, time.wSecond, time.wMilliseconds, suffix);
        }
        if (result < 0) return false;
        HANDLE file = CreateFileW(candidate, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                  nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) {
            if (GetLastError() == ERROR_FILE_EXISTS) continue;
            return false;
        }
        CloseHandle(file);
        return CopyPath(candidate);
    }
    return false;
}

inline void PublishPath() {
    if (!LogPath()[0]) return;
    HANDLE mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                                        static_cast<DWORD>(kPathChars * sizeof(wchar_t)),
                                        kMappingName);
    if (!mapping) return;
    void* view = MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0,
                               kPathChars * sizeof(wchar_t));
    if (!view) {
        CloseHandle(mapping);
        return;
    }
    std::memset(view, 0, kPathChars * sizeof(wchar_t));
    std::memcpy(view, LogPath(), (std::wcslen(LogPath()) + 1) * sizeof(wchar_t));
    FlushViewOfFile(view, kPathChars * sizeof(wchar_t));
    MappingHandle() = mapping;
    MappingView() = view;
}

inline bool AttachPublishedPath() {
    HANDLE mapping = OpenFileMappingW(FILE_MAP_READ, FALSE, kMappingName);
    if (!mapping) return false;
    void* view = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0,
                               kPathChars * sizeof(wchar_t));
    if (!view) {
        CloseHandle(mapping);
        return false;
    }
    wchar_t path[kPathChars]{};
    std::memcpy(path, view, sizeof(path));
    const bool copied = CopyPath(path) && GetFileAttributesW(LogPath()) != INVALID_FILE_ATTRIBUTES;
    if (copied) {
        MappingHandle() = mapping;
        MappingView() = view;
    } else {
        UnmapViewOfFile(view);
        CloseHandle(mapping);
    }
    return copied;
}

inline bool EnsurePath() {
    std::lock_guard<std::mutex> lock(ProcessMutex());
    if (LogPath()[0]) return true;
    if (AttachPublishedPath()) return true;
    return CreateSessionFile();
}

inline void RotateLogs() {
    wchar_t directory[kPathChars]{};
    if (!BuildDirectory(directory, kPathChars)) return;
    wchar_t pattern[kPathChars]{};
    if (_snwprintf_s(pattern, kPathChars, _TRUNCATE, L"%ls\\*.log", directory) < 0) return;

    WIN32_FIND_DATAW data{};
    HANDLE find = FindFirstFileW(pattern, &data);
    if (find == INVALID_HANDLE_VALUE) return;
    std::vector<LogFileInfo> files;
    do {
        if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) continue;
        LogFileInfo info{};
        if (_snwprintf_s(info.path, kPathChars, _TRUNCATE, L"%ls\\%ls", directory, data.cFileName) >= 0) {
            info.writeTime = data.ftLastWriteTime;
            files.push_back(info);
        }
    } while (FindNextFileW(find, &data));
    FindClose(find);

    if (files.size() <= 10) return;
    std::sort(files.begin(), files.end(), [](const LogFileInfo& left, const LogFileInfo& right) {
        return CompareFileTime(&left.writeTime, &right.writeTime) < 0;
    });
    const std::size_t removeCount = files.size() - 10;
    for (std::size_t i = 0; i < removeCount; ++i) {
        if (_wcsicmp(files[i].path, LogPath()) != 0) (void)DeleteFileW(files[i].path);
    }
}

inline HANDLE LockFile() {
    if (!NamedMutex()) NamedMutex() = CreateMutexW(nullptr, FALSE, kMutexName);
    if (!NamedMutex()) return nullptr;
    return WaitForSingleObject(NamedMutex(), 5000) == WAIT_OBJECT_0 ? NamedMutex() : nullptr;
}

inline void UnlockFile(HANDLE mutex) {
    if (mutex) ReleaseMutex(mutex);
}

} // namespace detail

inline void InitializeSession() {
    std::lock_guard<std::mutex> lock(detail::ProcessMutex());
    if (!detail::LogPath()[0]) {
        if (!detail::CreateSessionFile()) return;
        detail::PublishPath();
    }
    HANDLE mutex = detail::LockFile();
    if (mutex) {
        detail::RotateLogs();
        detail::UnlockFile(mutex);
    }
}

inline void Write(const char* sourceFile, unsigned int sourceLine, const char* fmt, ...) {
    if (!detail::EnsurePath()) return;
    std::lock_guard<std::mutex> lock(detail::ProcessMutex());
    HANDLE namedMutex = detail::LockFile();
    if (!namedMutex) return;

    char rendered[2048]{};
    va_list args;
    va_start(args, fmt);
    (void)vsnprintf_s(rendered, sizeof(rendered), _TRUNCATE, fmt, args);
    va_end(args);

    const char* fileName = sourceFile ? std::strrchr(sourceFile, '\\') : nullptr;
    if (fileName) ++fileName;
    else fileName = sourceFile;
    std::uint32_t eventCode = detail::HashText(fileName);
    eventCode ^= sourceLine * 0x9E3779B9u;
    const std::uint32_t detailCode = detail::HashText(rendered);

    FILE* file = nullptr;
    if (_wfopen_s(&file, detail::LogPath(), L"ab") == 0 && file) {
        SYSTEMTIME time{};
        GetLocalTime(&time);
        std::fprintf(file, "%04u-%02u-%02uT%02u:%02u:%02u.%03u|M=%c|C=%08X|L=%u|D=%08X\n",
                     time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute,
                     time.wSecond, time.wMilliseconds,
#ifdef TOOLSBOX_INJECTOR
                     'I',
#else
                     'P',
#endif
                     eventCode, sourceLine, detailCode);
        std::fclose(file);
    }
    detail::UnlockFile(namedMutex);
}

} // namespace log_util

#define LOG(...) log_util::Write(__FILE__, __LINE__, __VA_ARGS__)
