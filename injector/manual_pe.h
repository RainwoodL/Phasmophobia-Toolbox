#pragma once
// ============================================================
// 手动 PE 映射注入 - 不落盘，不调用 LoadLibrary
// ============================================================
#include <windows.h>
#include <cstdint>
#include <cstring>

namespace manual_pe {

// 注入失败原因（用于诊断）
enum class Error {
    Ok,
    InvalidPE,            // PE 格式无效
    AllocImageFailed,     // 远程分配镜像内存失败
    WriteHeadersFailed,   // 写入 PE 头失败
    WriteSectionFailed,   // 写入节区失败
    RelocOffsetNotFound,  // 重定位表 RVA 无法映射到文件偏移
    ReadImportFailed,     // 读取导入表失败
    ImportFailed,         // 解析导入表失败（找不到 DLL 或函数）
    DllMainFailed,        // DllMain 返回 FALSE
    TargetArchitectureMismatch,
    RemoteThreadFailed,
    ExceptionRegistrationFailed,
};

const char* ErrorString(Error e);

// 在目标进程内手动映射 PE 并调用 DllMain
// err（可选）输出失败原因
bool Inject(HANDLE hProcess, const void* peData, size_t peSize, Error* err = nullptr);

} // namespace manual_pe
