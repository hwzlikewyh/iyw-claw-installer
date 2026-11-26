#pragma once
#include "payload.h"
#include "windows_support.h"
#include <stdexcept>

namespace setup {
inline bool DirectoryBusy(DWORD error) {
    return error == ERROR_ACCESS_DENIED || error == ERROR_SHARING_VIOLATION ||
        error == ERROR_LOCK_VIOLATION;
}

inline DWORD TryMoveAppDirectory(const fs::path& from, const fs::path& to) {
    // 同卷改名是提交边界，禁止覆盖目标或跨卷复制。
    constexpr DWORD RetryDelayMs = 150;
    constexpr unsigned Attempts = 21;
    for (unsigned attempt = 1; ; ++attempt) {
        if (MoveFileExW(from.c_str(), to.c_str(), 0)) return ERROR_SUCCESS;
        const auto error = GetLastError();
        if (!DirectoryBusy(error) || attempt == Attempts) return error;
        Sleep(RetryDelayMs);
    }
}

inline std::string DirectoryMoveFailure(const fs::path& from, const fs::path& to, DWORD error) {
    return "应用目录切换失败，win32_error=" + std::to_string(error) +
        "，从 " + from.u8string() + " 到 " + to.u8string();
}

inline void MoveAppDirectory(const fs::path& from, const fs::path& to) {
    const auto error = TryMoveAppDirectory(from, to);
    if (error != ERROR_SUCCESS) throw std::runtime_error(DirectoryMoveFailure(from, to, error));
}

inline void ActivatePreparedApp(const Release& release, FileWork& work) {
    const auto source = work.deployment / "incoming/app";
    const auto target = work.progress.destination / "app";
    const auto error = TryMoveAppDirectory(source, target);
    if (error == ERROR_SUCCESS) return;
    const auto reason = DirectoryMoveFailure(source, target, error);
    if (!DirectoryBusy(error) || fs::exists(target)) throw std::runtime_error(reason);

    // 原暂存文件可能仍被扫描或独占。直接从已锁定并验签的安装包重新展开，
    // 确保整个候选目录校验通过之后才对外可见。
    work.progress.events.push_back(reason);
    work.progress.detail = "正在从本地安装包重新准备应用";
    work.progress.events.push_back(work.progress.detail);
    work.job.Publish(work.progress);
    const auto candidate = work.deployment / "replacement";
    ExtractPayload(work, candidate);
    ValidatePayloadTree(candidate, release, work);
    MoveAppDirectory(candidate / "app", target);
}
}
