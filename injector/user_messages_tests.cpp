#include "user_messages.h"

#include <cassert>
#include <string_view>

namespace {
void CheckUserMessage(std::string_view message) {
    assert(!message.empty());
    assert(message.find(u8"请") != std::string_view::npos);
    for (const auto* internal : {"hook", "Hook", "HOOK", "payload", "DLL", "Dll", "PE",
                                 "RVA", "AMD64", "0x", u8"偏移", u8"注入", u8"错误码"})
        assert(message.find(internal) == std::string_view::npos);
}
} // namespace

int main() {
    using namespace user_messages;
    // Windows access denied and the loader's fifth error have different meanings.
    assert(std::string_view(OpenGameFailed(5)).find(u8"管理员") != std::string_view::npos);
    assert(std::string_view(LoadFailed(static_cast<manual_pe::Error>(5))).find(u8"管理员") == std::string_view::npos);
    assert(std::string_view(OpenGameFailed(ERROR_NOT_ENOUGH_MEMORY)).find(u8"资源不足") != std::string_view::npos);
    assert(std::string_view(OpenGameFailed(ERROR_INVALID_PARAMETER)).find(u8"游戏已关闭") != std::string_view::npos);
    assert(std::string_view(OpenGameFailed(0xFFFFFFFF)).find(u8"管理员") == std::string_view::npos);
    for (DWORD error : {ERROR_ACCESS_DENIED, ERROR_PRIVILEGE_NOT_HELD, ERROR_INVALID_PARAMETER,
                       ERROR_INVALID_HANDLE, ERROR_NOT_FOUND, ERROR_NOT_ENOUGH_MEMORY,
                       ERROR_OUTOFMEMORY, ERROR_NO_SYSTEM_RESOURCES, ERROR_SUCCESS})
        CheckUserMessage(OpenGameFailed(error));
    for (int error = 0; error <= static_cast<int>(manual_pe::Error::ExceptionRegistrationFailed); ++error)
        CheckUserMessage(LoadFailed(static_cast<manual_pe::Error>(error)));
    CheckUserMessage(LoadFailed(static_cast<manual_pe::Error>(999)));
    for (ReadyResult result : {ReadyResult::Failed, ReadyResult::ProcessExited, ReadyResult::Timeout}) {
        CheckUserMessage(GameNotReady(result));
        CheckUserMessage(ToolNotReady(result));
    }
    assert(std::string_view(GameNotReady(ReadyResult::ProcessExited)) != GameNotReady(ReadyResult::Timeout));
    assert(std::string_view(ToolNotReady(ReadyResult::ProcessExited)) != ToolNotReady(ReadyResult::Timeout));
    CheckUserMessage(PrepareFailed());
    return 0;
}
