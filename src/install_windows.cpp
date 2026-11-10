#include "windows_support.h"
#ifdef _WIN32
#include <shellapi.h>
#include <stdexcept>
#include <thread>
namespace setup {
void InstallPackage(const Release&, FileWork& work) {
    CheckCancel(work.job);
    const auto root = work.progress.destination.wstring();
    if (root.find_first_of(L"\r\n\"") != std::wstring::npos || root.size() > 240)
        throw std::runtime_error("安装位置包含无效字符或过长，请选择较短的目录。");
    const auto setup = work.staging / "setup.exe";
    // NSIS 的 /D 必须位于末尾，不加引号；整个参数字符串不经过 shell 解释。
    std::wstring parameters = L"/S /UPDATE /D=" + root;
    SHELLEXECUTEINFOW launch{}; launch.cbSize = sizeof(launch);
    launch.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
    launch.lpVerb = L"open"; launch.lpFile = setup.c_str(); launch.lpParameters = parameters.c_str();
    launch.lpDirectory = work.staging.c_str(); launch.nShow = SW_HIDE;
    BeginStage(work, Phase::Installing);
    work.progress.detail = "正式安装器正在部署应用、检查 WebView2 并准备运行环境；此阶段请保持窗口打开。";
    work.progress.logPath = work.progress.destination / "logs/installer.log";
    work.job.Publish(work.progress);
    if (!ShellExecuteExW(&launch) || !launch.hProcess)
        throw std::runtime_error("无法启动正式安装器。系统错误：" + std::to_string(GetLastError()));
    Handle process(launch.hProcess);
    constexpr DWORD PollMs = 250;
    for (;;) {
        const auto wait = WaitForSingleObject(process.get(), PollMs);
        if (wait == WAIT_OBJECT_0) break;
        if (wait != WAIT_TIMEOUT) throw std::runtime_error("等待安装器结果失败，请查看安装日志。");
        Advance(work, 0);
    }
    DWORD result = 0;
    if (!GetExitCodeProcess(process.get(), &result)) throw std::runtime_error("无法读取安装结果。");
    if (result != 0) throw std::runtime_error(result == ERROR_CANCELLED ?
        "已取消管理员授权，安装未完成。" : "正式安装器返回错误 " + std::to_string(result) + "，请查看安装日志后重试。");
}
void VerifyInstalled(const Release& release, FileWork& work) {
    BeginStage(work, Phase::Finalizing);
    const auto executable = work.progress.destination / "app/iyw-claw.exe";
    const auto helper = work.progress.destination / "app/iyw-environment.exe";
    if (!fs::is_regular_file(executable) || !fs::is_regular_file(helper) || !fs::file_size(helper))
        throw std::runtime_error("安装器已退出，但缺少主程序或运行环境助手。");
    if (FileVersion(executable) != release.version)
        throw std::runtime_error("安装后的版本与 Fusion 稳定版不一致，已停止启动。");
    if(work.retainedFiles.size()!=2){VerifySignature(executable);VerifySignature(helper);}
    work.progress.executable = executable;
    work.progress.detail = "原助理 " + release.version + " 已安装，点击下方按钮启动。";
}
}
#endif
