#include "windows_support.h"
#include "command.h"
#include "win7_restart.h"
#include "wmf_identity.h"
#ifdef _WIN32
#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include "wmf_archive.inc"

namespace setup {
namespace {
constexpr std::uint64_t DotNetBytes = 62'000'832;
constexpr std::uint64_t WmfX64Bytes = 68'054'745;
constexpr std::uint64_t WmfX86Bytes = 44'807'068;
constexpr std::uint64_t WmfX64ZipBytes = 68'076'477;
constexpr std::uint64_t WmfX86ZipBytes = 44'821'734;
constexpr char WmfX64ZipSHA[] = "f383c34aa65332662a17d95409a2ddedadceda74427e35d05024cd0a6a2fa647";
constexpr char WmfX86ZipSHA[] = "eb7e2c4ce2c6cb24206474a6cb8610d9f4bd3a9301f1cd8963b4ff64e529f563";
constexpr DWORD WmfUnsupportedOperation = 0x80240037;
// WMF 5.1 至少需要 .NET Framework 4.5.2；378389 仅为 4.5。
constexpr DWORD DotNetMinimumRelease = 379893;
constexpr char DotNetSHA[] =
    "b4cbb4bc9a3983ec3be9f80447e0d619d15256a9ce66ff414ae6e3856705e237";
constexpr char DotNetURL[] =
    "https://download.visualstudio.microsoft.com/download/pr/"
    "8e396c75-4d0d-41d3-aea8-848babc2736a/"
    "80b431456d8866ebe053eb8b81a168b3/ndp462-kb3151800-x86-x64-allos-enu.exe";
constexpr char WmfX64URL[] =
    "https://download.microsoft.com/download/6/f/5/6f5ff66c-6775-42b0-86c4-47d41f2da187/"
    "Win7AndW2K8R2-KB3191566-x64.zip";
constexpr char WmfX86URL[] =
    "https://download.microsoft.com/download/6/f/5/6f5ff66c-6775-42b0-86c4-47d41f2da187/"
    "Win7-KB3191566-x86.zip";

fs::path NativeSystemDirectory() {
    BOOL wow64 = FALSE;
    std::array<wchar_t, MAX_PATH> windows{};
    if (!IsWow64Process(GetCurrentProcess(), &wow64) ||
        !GetWindowsDirectoryW(windows.data(), static_cast<UINT>(windows.size())))
        throw std::runtime_error("无法定位 Windows 系统组件目录。");
    if (wow64) return fs::path(windows.data()) / L"Sysnative";
    std::array<wchar_t, MAX_PATH> system{};
    if (!GetSystemDirectoryW(system.data(), static_cast<UINT>(system.size())))
        throw std::runtime_error("无法定位 Windows 系统目录。");
    return system.data();
}

std::string ReadVersion(FileWork& work, const fs::path& powershell) {
    const auto output = work.staging / "powershell-check.log";
    const auto code = RunCommand({powershell,
        {"-NoProfile", "-NonInteractive", "-Command", "$ErrorActionPreference='Stop'; $PSVersionTable.PSVersion.ToString(); exit 0"},
        output, 30});
    auto version = ReadOutput(output);
    version.erase(std::remove_if(version.begin(), version.end(), [](unsigned char value) {
        return std::isspace(value);
    }), version.end());
    if (code != 0) {
        auto error = ReadOutput(output.wstring() + L".stderr");
        if (error.size() > 280) error.resize(280);
        throw std::runtime_error("Windows PowerShell 启动异常，退出码：" +
            std::to_string(code) + (error.empty() ? "。" : "。" + error));
    }
    return version;
}

bool AtLeast51(const std::string& value, unsigned& major) {
    std::istringstream input(value);
    char dot = 0;
    unsigned minor = 0;
    if (!(input >> major >> dot >> minor) || dot != '.')
        throw std::runtime_error("无法读取 PowerShell 版本。检测结果：" + value);
    return major > 5 || (major == 5 && minor >= 1);
}

DWORD DotNetRelease(const Platform& platform) {
    HKEY key = nullptr;
    const REGSAM view = platform.arch == "x86_64" ? KEY_WOW64_64KEY : 0;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Microsoft\\NET Framework Setup\\NDP\\v4\\Full",
        0, KEY_QUERY_VALUE | view, &key) != ERROR_SUCCESS) return 0;
    DWORD release = 0, type = 0, size = sizeof(release);
    const auto status = RegQueryValueExW(key, L"Release", nullptr, &type,
        reinterpret_cast<BYTE*>(&release), &size);
    RegCloseKey(key);
    return status == ERROR_SUCCESS && type == REG_DWORD ? release : 0;
}

void DownloadCached(const std::string& url, const fs::path& target,
    std::uint64_t expectedSize, const std::string& expectedHash,
    bool microsoftSignature, FileWork& work) {
    std::error_code error;
    if (fs::is_regular_file(target, error) && !error &&
        fs::file_size(target, error) == expectedSize && !error) {
        try {
            if (expectedHash.empty() || HashFile(target) == expectedHash) {
                if (microsoftSignature) VerifyMicrosoftSignature(target);
                return;
            }
        } catch (const std::exception&) { }
    }
    fs::remove(target, error);
    const auto partial = target.wstring() + L".part";
    fs::remove(partial, error);
    std::ofstream file(partial, std::ios::binary | std::ios::trunc);
    if (!file) throw std::runtime_error("无法创建系统组件下载缓存。");
    work.progress.detail = "正在下载缺少的 Windows 系统组件";
    work.progress.phaseDone = 0;
    work.progress.phaseTotal = expectedSize;
    work.job.Publish(work.progress);
    try {
        ReadHttps(url, [&](const char* bytes, std::size_t count) {
            if (work.progress.phaseDone + count > expectedSize)
                throw std::runtime_error("微软系统组件下载大小超出预期。");
            file.write(bytes, static_cast<std::streamsize>(count));
            if (!file) throw std::runtime_error("写入微软系统组件缓存失败。");
            Advance(work, count);
        }, work.job);
        file.flush();
        if (!file || work.progress.phaseDone != expectedSize)
            throw std::runtime_error("微软系统组件下载不完整。");
        file.close();
        if (!expectedHash.empty() && HashFile(partial) != expectedHash)
            throw std::runtime_error("微软系统组件校验失败。");
        if (microsoftSignature) VerifyMicrosoftSignature(partial);
        fs::rename(partial, target);
    } catch (...) {
        file.close();
        fs::remove(partial, error);
        throw;
    }
}

void CheckSystemInstallerResult(int code, const char* component, const fs::path& details) {
    if (RequiresSystemRestart(static_cast<unsigned>(code)))
        throw SystemRestart(std::string(component) + " 已安装，Windows 需要重启后才能继续。");
    constexpr DWORD MissingServicingSource = 0x800f081f;
    if (!details.empty() && static_cast<DWORD>(code) == MissingServicingSource)
        throw std::runtime_error("Windows 本地安装 WMF 时找不到所需组件源（0x800F081F）。请提供本次安装时段的 CBS.log。\n" + ReadOutput(details));
    if (code == 1058) {
        const auto log = details.empty() ? std::string() : ReadOutput(details);
        throw std::runtime_error(std::string(component) +
            " 安装失败：Windows 服务被禁用（错误 1058）。请检查 Windows Modules Installer "
            "(TrustedInstaller) 和 Windows Update (wuauserv) 服务设置；若由组织策略禁用，请联系管理员。" +
            (log.empty() ? std::string() : "\n服务状态记录：\n" + log));
    }
    if (!details.empty() && static_cast<DWORD>(code) == WmfUnsupportedOperation)
        throw std::runtime_error("Windows Management Framework 5.1 安装失败：Windows Update Agent "
            "更新搜索返回不支持操作错误（0x80240037）；已尝试解包并通过 Package Manager 本地安装。"
            "请查看诊断中的本地安装返回码和 CBS 日志；这不是服务被禁用错误。" +
            (details.empty() ? std::string() : "\n服务状态记录：\n" + ReadOutput(details)));
    if (code == 0) return;
    const auto log = details.empty() ? std::string() : ReadOutput(details);
    throw std::runtime_error(std::string(component) + " 安装失败，Windows 退出码：" +
        std::to_string(code) + "。请查看 Windows 组件安装日志和系统服务状态后重试。" +
        (log.empty() ? std::string() : "\n服务状态记录：\n" + log));
}

void RunSystemInstaller(FileWork& work, const fs::path& executable,
    std::vector<std::string> arguments, const char* component) {
    work.progress.cancellable = false;
    work.progress.detail = std::string("Windows 正在安装 ") + component + "；请勿关闭窗口";
    work.progress.phaseDone = 0;
    work.progress.phaseTotal = 0;
    work.job.Publish(work.progress);
    const auto code = RunCommand({executable, arguments,
        work.staging / "system-component-install.log", 0, {}, true},
        [&] { Advance(work, 0); });
    work.progress.cancellable = true;
    work.job.Publish(work.progress);
    CheckSystemInstallerResult(code, component, {});
}

void RunWmfInstaller(FileWork& work, const WmfInstallRequest& request) {
    work.progress.cancellable = false;
    work.progress.detail = "正在授权并准备 WMF 5.1";
    work.progress.phaseDone = 0;
    work.progress.phaseTotal = 0;
    work.job.Publish(work.progress);
    int code = 0;
    try {
        const auto directory = WmfLiveDirectory(work.staging);
        auto nextCheck = std::chrono::steady_clock::now();
        code = RunWmfInstallerElevated(request, [&] {
            const auto now = std::chrono::steady_clock::now();
            if (now < nextCheck) return;
            nextCheck = now + std::chrono::seconds(1);
            std::error_code error;
            const std::string status = fs::exists(directory / L"wmf-pkgmgr-output.log", error) ? "Windows 正在写入本地 WMF 组件" :
                fs::exists(directory / L"wmf-expand-output.log", error) ? "正在解包 WMF 本地安装源" :
                fs::exists(directory / L"wmf-install.log", error) ? "Windows 正在检查 WMF 更新适用性" : "正在授权并准备 WMF 5.1";
            if (work.progress.detail != status) {
                work.progress.detail = status;
                const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - work.started).count();
                work.progress.events.push_back(std::to_string(elapsed) + " s  " + status);
            }
            Advance(work, 0);
        });
    } catch (...) {
        work.progress.cancellable = true;
        work.job.Publish(work.progress);
        throw;
    }
    work.progress.cancellable = true;
    work.job.Publish(work.progress);
    CheckSystemInstallerResult(code, "Windows Management Framework 5.1", request.serviceLog);
}

void InstallDotNet(FileWork& work, const fs::path& cache) {
    if (DotNetRelease(CurrentPlatform()) >= DotNetMinimumRelease) return;
    fs::create_directories(cache);
    const auto package = cache / "ndp462-kb3151800-x86-x64-allos-enu.exe";
    DownloadCached(DotNetURL, package, DotNetBytes, DotNetSHA, true, work);
    RunSystemInstaller(work, package,
        {"/q", "/norestart", "/ChainingPackage", "YuanAssistant"}, ".NET Framework");
    if (DotNetRelease(CurrentPlatform()) < DotNetMinimumRelease)
        throw std::runtime_error(".NET Framework 安装后仍未就绪。请检查安装日志并重试。");
}

void CheckWindowsManagementFramework(FileWork& work, const fs::path& cache,
    const Platform& platform) {
    if (platform.arch != "x86_64" && platform.arch != "i686")
        throw std::runtime_error("Windows 7 当前架构不支持受管 PowerShell 5.1 安装。");
    const bool x64 = platform.arch == "x86_64";
    const auto package = cache / WmfPackageName(x64);
    const auto url = x64 ? WmfX64URL : WmfX86URL;
    const auto hash = x64 ? WmfX64SHA : WmfX86SHA;
    const auto size = x64 ? WmfX64Bytes : WmfX86Bytes;
    if (!WmfPackageMatches(package, size, hash)) {
        const auto archive = cache / (x64 ? "wmf-x64.zip" : "wmf-x86.zip");
        DownloadCached(url, archive, x64 ? WmfX64ZipBytes : WmfX86ZipBytes,
            x64 ? WmfX64ZipSHA : WmfX86ZipSHA, false, work);
        ExtractWmfArchive(archive, package, work);
        WaitForWmfPackage(package, size, hash, work);
    }
    const auto wusa = NativeSystemDirectory() / L"wusa.exe";
    const WmfInstallRequest request{wusa, package, work.staging / "wmf-install.log",
        work.staging / "wmf-service.log"};
    RunWmfInstaller(work, request);
}

void CheckWmf3Conflict(FileWork& work, const fs::path& powershell) {
    const auto output = work.staging / "wmf-compatibility.log";
    const auto code = RunCommand({powershell,
        {"-NoProfile", "-NonInteractive", "-Command",
         "if (Get-WmiObject -Query \"select * from Win32_QuickFixEngineering where HotFixID = 'KB2506143'\") { exit 3 } else { exit 0 }"},
        output, 30});
    if (code == 3) throw std::runtime_error(
        "当前 Windows 7 安装了不兼容的 WMF 3.0（KB2506143）。请先从控制面板卸载此更新并重启，再重新运行安装程序。");
    if (code != 0) throw std::runtime_error(
        "无法确认 Windows 7 的 PowerShell 升级条件，已停止安装。请检查详细日志后重试。");
}
bool HasOnlyWin7LegacyEngine(const Platform& platform) {
    if (platform.target != "windows7") return false;
    const auto view = platform.arch == "x86_64" ? KEY_WOW64_64KEY : 0;
    HKEY modern = nullptr;
    const auto result = RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\PowerShell\\3\\PowerShellEngine",
        0, KEY_QUERY_VALUE | view, &modern);
    if (modern) RegCloseKey(modern);
    if (result != ERROR_FILE_NOT_FOUND && result != ERROR_PATH_NOT_FOUND) return false;
    const DWORD flags = RRF_RT_REG_SZ | (view ? RRF_SUBKEY_WOW6464KEY : 0);
    wchar_t version[16]{};
    DWORD bytes = sizeof(version);
    return RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\PowerShell\\1\\PowerShellEngine",
        L"PowerShellVersion", flags, nullptr, version, &bytes) == ERROR_SUCCESS && std::wstring(version) == L"2.0";
}
}

void CheckWindowsPowerShell(const Platform& platform, FileWork& work) {
    const auto powershell = NativeSystemDirectory() /
        L"WindowsPowerShell/v1.0/powershell.exe";
    const bool available = fs::is_regular_file(powershell);
    std::string version;
    unsigned major = 0;
    bool versionKnown = false;
    bool versionAtLeast51 = false;
    if (available) {
        try {
            version = HasOnlyWin7LegacyEngine(platform) ? "2.0" : ReadVersion(work, powershell);
            versionAtLeast51 = AtLeast51(version, major);
            versionKnown = true;
        } catch (const std::exception&) {
            if (platform.target != "windows7") throw;
            work.progress.checks.push_back(
                "无法读取 PowerShell 版本：将尝试修复 Windows 7 前置组件。");
        }
    }
    if (versionKnown) {
        if (versionAtLeast51) {
            if (platform.target == "windows7")
                InstallDotNet(work, UserHome() / L".iyw-claw/cache/prerequisites");
            work.progress.checks.push_back("PowerShell " + version + "：检查通过。");
            return;
        }
        if (platform.target != "windows7")
            throw std::runtime_error("此 Windows 版本的 PowerShell " + version +
                " 未达到 5.1；检查系统更新后重试。");
        if (major == 3) CheckWmf3Conflict(work, powershell);
        work.progress.checks.push_back("PowerShell " + version + "：需要补齐 WMF 5.1。");
    } else if (!available && platform.target != "windows7") {
        throw std::runtime_error("此 Windows 版本缺少 Windows PowerShell；请检查系统组件后重试。");
    }

    const auto cache = UserHome() / L".iyw-claw/cache/prerequisites";
    InstallDotNet(work, cache);
    fs::create_directories(cache);
    CheckWindowsManagementFramework(work, cache, platform);
    std::string installedVersion;
    try {
        installedVersion = ReadVersion(work, powershell);
    } catch (const std::exception& error) {
        throw std::runtime_error("Windows 7 前置组件安装完成，但 PowerShell 仍无法正常启动。" +
            std::string(error.what()));
    }
    unsigned installedMajor = 0;
    if (!AtLeast51(installedVersion, installedMajor))
        throw std::runtime_error("WMF 5.1 安装程序已完成，但 PowerShell 仍为 " +
            installedVersion + "。请重启 Windows 后重试。");
    work.progress.checks.push_back("Windows 7 已补齐 WMF 5.1 与 .NET Framework 前置组件。");
}
}
#endif
