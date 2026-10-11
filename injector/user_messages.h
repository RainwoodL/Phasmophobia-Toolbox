#pragma once

#include "manual_pe.h"

namespace user_messages {

enum class ReadyResult { Ready, Failed, ProcessExited, Timeout };

inline const char* OpenGameFailed(DWORD error) {
    switch (error) {
    case ERROR_ACCESS_DENIED:
    case ERROR_PRIVILEGE_NOT_HELD:
        return u8"没有权限，请右键点击工具箱，选择“以管理员身份运行”。";
    case ERROR_INVALID_PARAMETER:
    case ERROR_INVALID_HANDLE:
    case ERROR_NOT_FOUND:
        return u8"游戏已关闭或正在重新启动，请进入游戏主菜单后重新运行工具箱。";
    case ERROR_NOT_ENOUGH_MEMORY:
    case ERROR_OUTOFMEMORY:
    case ERROR_NO_SYSTEM_RESOURCES:
        return u8"电脑可用资源不足，请关闭不需要的程序后重试。";
    default:
        return u8"暂时无法连接游戏，请重启游戏后重新运行工具箱；若仍然失败，请联系作者。";
    }
}

inline const char* LoadFailed(manual_pe::Error error) {
    switch (error) {
    case manual_pe::Error::InvalidPE:
    case manual_pe::Error::RelocOffsetNotFound:
    case manual_pe::Error::ReadImportFailed:
        return u8"工具箱文件可能不完整或与当前版本不兼容，请重新下载最新版工具箱。";
    case manual_pe::Error::AllocImageFailed:
        return u8"无法为工具箱准备运行空间，请关闭不需要的程序并重启游戏后重试。";
    case manual_pe::Error::ImportFailed:
    case manual_pe::Error::TargetArchitectureMismatch:
        return u8"工具箱与当前游戏或运行环境不兼容，请确认使用最新版工具箱，并在 Steam 中验证游戏文件完整性。";
    case manual_pe::Error::WriteHeadersFailed:
    case manual_pe::Error::WriteSectionFailed:
    case manual_pe::Error::RemoteThreadFailed:
        return u8"工具箱未能在游戏中启动，请重启游戏后重试；若仍然失败，请联系作者。";
    case manual_pe::Error::DllMainFailed:
    case manual_pe::Error::ExceptionRegistrationFailed:
        return u8"工具箱启动准备失败，请重启游戏并使用最新版工具箱重试。";
    default:
        return u8"工具箱启动失败，请重启游戏后重试；若仍然失败，请联系作者。";
    }
}

inline const char* GameNotReady(ReadyResult result) {
    if (result == ReadyResult::ProcessExited)
        return u8"游戏已关闭，请重新启动游戏，进入主菜单后再运行工具箱。";
    if (result == ReadyResult::Timeout)
        return u8"等待游戏启动超时，请进入游戏主菜单后重新运行工具箱。";
    return u8"无法确认游戏是否已准备好，请重启游戏后重新运行工具箱。";
}

inline const char* ToolNotReady(ReadyResult result) {
    if (result == ReadyResult::ProcessExited)
        return u8"游戏在工具箱启动过程中关闭了，请重启游戏后重新运行工具箱。";
    if (result == ReadyResult::Timeout)
        return u8"等待工具箱启动超时，请重启游戏后重试；若仍然失败，请检查是否有新版工具箱。";
    return u8"工具箱未能正常显示，请重启游戏并使用最新版工具箱重试；若仍然失败，请联系作者。";
}

inline const char* PrepareFailed() {
    return u8"工具箱启动文件未能准备好，请关闭不需要的程序后重试；若仍然失败，请重新下载最新版工具箱。";
}

} // namespace user_messages
