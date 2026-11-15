#include "windows_support.h"
#include "payload.h"
#ifdef _WIN32
#include <shellapi.h>
#include <algorithm>
#include <fstream>
#include <stdexcept>
#include <thread>
namespace setup {
namespace {
constexpr std::uint64_t MaxLogBytes = 64 * 1024;
constexpr std::size_t MaxReasonBytes = 900;
std::uint64_t LogSize(const fs::path& path) {
    std::error_code error;
    const auto size = fs::file_size(path, error);
    return error ? 0 : size;
}
fs::file_time_type LogStamp(const fs::path& path) {
    std::error_code error;
    const auto stamp = fs::last_write_time(path, error);
    return error ? fs::file_time_type::min() : stamp;
}
std::string DecodeLog(const std::string& bytes) {
    if (bytes.empty()) return {};
    const auto length = static_cast<int>(bytes.size());
    std::wstring wide;
    if (bytes.size() >= 2 && static_cast<unsigned char>(bytes[0]) == 0xff &&
        static_cast<unsigned char>(bytes[1]) == 0xfe) {
        for (std::size_t i = 2; i + 1 < bytes.size(); i += 2)
            wide.push_back(static_cast<wchar_t>(static_cast<unsigned char>(bytes[i]) |
                (static_cast<unsigned char>(bytes[i + 1]) << 8)));
    } else {
        const auto page = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(), length, nullptr, 0) ? CP_UTF8 : CP_ACP;
        wide.resize(MultiByteToWideChar(page, 0, bytes.data(), length, nullptr, 0));
        if (wide.empty()) return "安装日志编码无法识别，请打开原始日志查看。";
        MultiByteToWideChar(page, 0, bytes.data(), length, wide.data(), static_cast<int>(wide.size()));
    }
    const auto count = static_cast<int>(wide.size());
    std::string text(WideCharToMultiByte(CP_UTF8, 0, wide.data(), count, nullptr, 0, nullptr, nullptr), '\0');
    if (!text.empty()) WideCharToMultiByte(CP_UTF8, 0, wide.data(), count, text.data(), static_cast<int>(text.size()), nullptr, nullptr);
    return text;
}
std::string ReadInstallLog(const fs::path& path, std::uint64_t offset) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return "本次无法读取正式安装日志，请检查日志位置是否存在及其访问权限。";
    const auto size = LogSize(path);
    if (size < offset) offset = 0;
    char header[2]{}; file.read(header, sizeof(header)); file.clear();
    const bool utf16 = static_cast<unsigned char>(header[0]) == 0xff && static_cast<unsigned char>(header[1]) == 0xfe;
    auto begin = std::max(offset, size > MaxLogBytes ? size - MaxLogBytes : 0);
    if (utf16 && begin % 2) ++begin;
    if (size <= begin) return "正式安装器本次没有写入新的详细日志，具体原因尚未取得。";
    std::string bytes(static_cast<std::size_t>(size - begin), '\0');
    file.seekg(static_cast<std::streamoff>(begin));
    file.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    bytes.resize(static_cast<std::size_t>(file.gcount()));
    if (begin > offset && !utf16) {
        const auto newline = bytes.find('\n');
        if (newline != std::string::npos) bytes.erase(0, newline + 1);
    }
    if (utf16 && begin) bytes.insert(0, "\xff\xfe", 2);
    auto text = DecodeLog(bytes);
    if (begin > offset && utf16) {
        const auto newline = text.find('\n');
        if (newline != std::string::npos) text.erase(0, newline + 1);
    }
    return text;
}
std::string FailureReason(const std::string& text) {
    for (const auto* marker : {"error=", "process control failed:", "failure:", "app rename failed:"}) {
        const auto found = text.rfind(marker);
        if (found == std::string::npos) continue;
        const auto end = text.find('\n', found);
        return text.substr(found, std::min(end == std::string::npos ? text.size() - found : end - found, MaxReasonBytes));
    }
    return text.substr(0, MaxReasonBytes);
}
void RecordInstallFailure(FileWork& work, DWORD code, const std::pair<std::uint64_t, fs::file_time_type>& before) {
    auto text = ReadInstallLog(work.progress.logPath, before.first);
    const auto processLog = work.progress.destination / "logs/installer-process-control.log";
    const auto stamp = LogStamp(processLog);
    work.progress.events.push_back("正式安装日志位置：" + work.progress.logPath.u8string());
    if (stamp != fs::file_time_type::min() && stamp != before.second)
        text += "\n进程检查原始异常：\n" + ReadInstallLog(processLog, 0);
    const auto copy = work.staging / "formal-installer.log";
    std::ofstream log(copy, std::ios::binary);
    log << "正式安装器退出码：" << code << '\n' << text;
    if (log.good()) work.progress.logPath = copy;
    work.progress.events.push_back("正式安装器退出码：" + std::to_string(code) + "\n" + text);
    if (code == ERROR_CANCELLED) throw std::runtime_error("已取消管理员授权，安装未完成。退出码：1223。");
    throw std::runtime_error("正式安装器退出码：" + std::to_string(code) + "。\n" + FailureReason(text));
}
}
void InstallPackage(const Release& release, FileWork& work) {
    if(release.packageKind==PayloadKind){BeginDirectInstall(release,work);return;}
    CheckCancel(work.job);
    const auto root = work.progress.destination.wstring();
    if (root.find_first_of(L"\r\n\"") != std::wstring::npos || root.size() > 240)
        throw std::runtime_error("安装位置包含无效字符或过长，请选择较短的目录。");
    const auto setup = work.staging / "setup.exe";
    // NSIS 的 /D 必须位于末尾，不加引号；整个参数字符串不经过 shell 解释。
    std::wstring parameters = L"/S /UPDATE";
    if (work.updateMode) parameters += L" /R /ARGS --restore-installer-session";
    parameters += L" /D=" + root;
    SHELLEXECUTEINFOW launch{}; launch.cbSize = sizeof(launch);
    launch.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
    launch.lpVerb = L"open"; launch.lpFile = setup.c_str(); launch.lpParameters = parameters.c_str();
    launch.lpDirectory = work.staging.c_str(); launch.nShow = SW_HIDE;
    BeginStage(work, Phase::Installing);
    work.progress.detail = "正式安装器正在部署应用、检查 WebView2 并准备运行环境；此阶段请保持窗口打开。";
    work.progress.logPath = work.progress.destination / "logs/installer.log";
    const auto logBaseline = std::make_pair(LogSize(work.progress.logPath),
        LogStamp(work.progress.destination / "logs/installer-process-control.log"));
    work.job.Publish(work.progress);
    if (!ShellExecuteExW(&launch) || !launch.hProcess)
        throw std::runtime_error("无法启动正式安装器。系统错误：" + std::to_string(GetLastError()));
    Handle process(launch.hProcess);
    work.installerStarted = true;
    constexpr DWORD PollMs = 250;
    for (;;) {
        const auto wait = WaitForSingleObject(process.get(), PollMs);
        if (wait == WAIT_OBJECT_0) break;
        if (wait != WAIT_TIMEOUT) throw std::runtime_error("等待安装器结果失败，请查看安装日志。");
        Advance(work, 0);
    }
    DWORD result = 0;
    if (!GetExitCodeProcess(process.get(), &result)) throw std::runtime_error("无法读取安装结果。");
    if (result != 0) RecordInstallFailure(work, result, logBaseline);
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
    if(work.directPayload){RegisterDirectInstall(release,work);}
    try { if(!work.directPayload)InstallUnifiedInstaller(work.progress.destination); }
    catch (const std::exception& error) {
        work.progress.events.push_back(std::string("统一更新入口安装失败，仍可使用原更新方式：") + error.what());
    }
    work.progress.executable = executable;
    work.progress.detail = "原助理 " + release.version + " 已安装，点击下方按钮启动。";
}
}
#endif
