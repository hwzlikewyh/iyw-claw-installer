#include "windows_support.h"
#include "command.h"
#include "payload.h"
#include "existing_install.h"
#ifdef _WIN32
#include <shellapi.h>
#include <shlobj.h>
#include <array>
#include <fstream>
#include <stdexcept>
#include <vector>
#include "win7_prerequisites.inc"
namespace setup {
Platform CurrentPlatform() {
    using GetVersion = LONG(WINAPI*)(PRTL_OSVERSIONINFOW);
    const auto query = reinterpret_cast<GetVersion>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion"));
    RTL_OSVERSIONINFOW version{}; version.dwOSVersionInfoSize = sizeof(version);
    if (!query || query(&version) < 0) throw std::runtime_error("无法识别 Windows 版本。");
    constexpr DWORD Windows7Major = 6, Windows7Minor = 1, Windows10Major = 10;
    if (version.dwMajorVersion == Windows7Major && version.dwMinorVersion == Windows7Minor)
        throw std::runtime_error("暂不支持 Win7 系统。");
    if (version.dwMajorVersion < Windows10Major)
        throw std::runtime_error("此安装向导需要 Windows 10 或更新系统。");
    SYSTEM_INFO system{}; GetNativeSystemInfo(&system);
    const auto arch = system.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64 ? "x86_64" :
        system.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_ARM64 ? "aarch64" : "i686";
    return {"windows", arch};
}
fs::path DefaultInstallRoot() {
    const auto installed = FindExistingInstallRoot();
    if (!installed.empty()) return installed;
    std::array<wchar_t, MAX_PATH> local{};
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, local.data())))
        throw std::runtime_error("无法读取当前用户应用目录。");
    return fs::path(local.data()) / "iyw-claw";
}
fs::path UserHome(){
    wchar_t path[MAX_PATH]{};
    if(FAILED(SHGetFolderPathW(nullptr,CSIDL_PROFILE,nullptr,SHGFP_TYPE_CURRENT,path)))
        throw std::runtime_error("无法读取当前用户目录。");return fs::path(path);
}
fs::path EnvironmentHelper(const fs::path& root){return root/"app/iyw-environment.exe";}
void LaunchExistingForUpdate(const fs::path& root) {
    if (root.empty() || !root.is_absolute() || root == root.root_path()) return;
    const auto executable = root / "app" / "iyw-claw.exe";
    if (!fs::is_regular_file(executable)) return;
    VerifySignature(executable);
    SHELLEXECUTEINFOW launch{};
    launch.cbSize = sizeof(launch);
    launch.fMask = SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
    launch.lpVerb = L"open";
    launch.lpFile = executable.c_str();
    launch.lpParameters = L"--restore-installer-session";
    const auto directory = root / "app";
    launch.lpDirectory = directory.c_str();
    launch.nShow = SW_SHOWNORMAL;
    if (!ShellExecuteExW(&launch))
        throw std::runtime_error("无法重新启动旧版本。系统错误：" + std::to_string(GetLastError()));
}
void InstallUnifiedInstaller(const fs::path& root) {
    wchar_t buffer[32768]{};
    constexpr DWORD capacity = static_cast<DWORD>(sizeof(buffer) / sizeof(buffer[0]));
    const auto length = GetModuleFileNameW(nullptr, buffer, capacity);
    if (!length || length >= capacity) throw std::runtime_error("无法定位统一安装向导自身。");
    const fs::path source(buffer, buffer + length);
    const fs::path destination = root / "installer" / fs::u8path("原助理安装向导.exe");
    const fs::path markerPath = destination.parent_path() / "unified-installer.v1";
    std::error_code equivalentError;
    if (fs::equivalent(source, destination, equivalentError) && !equivalentError) {
        VerifySignature(source);
        std::ofstream marker(markerPath, std::ios::binary | std::ios::trunc);
        if (!marker) throw std::runtime_error("无法保存统一安装向导版本标记。");
        marker << "1\n";
        return;
    }
    VerifySignature(source);
    fs::create_directories(destination.parent_path());
    const fs::path temporary = destination.wstring() + L".new";
    fs::copy_file(source, temporary, fs::copy_options::overwrite_existing);
    if (!MoveFileExW(temporary.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        fs::remove(temporary);
        throw std::runtime_error("无法保存统一安装向导。");
    }
    std::ofstream marker(markerPath, std::ios::binary | std::ios::trunc);
    if (!marker) throw std::runtime_error("无法保存统一安装向导版本标记。");
    marker << "1\n";
}
void VerifyEnvironmentBinary(const fs::path& helper){VerifySignature(helper);}
bool ReuseApplication(const Release& release,FileWork& work){
    if(work.directPayload&&(fs::exists(work.progress.destination/"staging/direct-install.json")||
                           fs::exists(work.progress.destination/LegacyPendingMarker)))return false;
    const auto executable=work.progress.destination/"app/iyw-claw.exe";
    const auto helper=EnvironmentHelper(work.progress.destination);
    if(!fs::is_regular_file(executable)||!fs::is_regular_file(helper)||FileVersion(executable)!=release.version)return false;
    try{
        auto appLock=OpenLocked(executable),helperLock=OpenLocked(helper);
        VerifySignature(executable);VerifySignature(helper);
        if(work.directPayload) {
            const auto output=work.staging/"reuse-helper-identity.json";
            if(RunCommand({helper,{"--identity"},output,15})!=0)return false;
            const auto identity=nlohmann::json::parse(ReadOutput(output));
            if(identity.value("installerProtocol",0)!=2)return false;
            for(const auto* path:{"resources/bundle/index.html","xinghe-resources/runtime.json","native-share/config.json","native-share/register.ps1","native-share/share-resources.ps1","native-share/IywAssistant.ShareTarget.msix","installer/webview2-setup.exe"})
                if(!fs::is_regular_file(work.progress.destination/"app"/fs::u8path(path)))return false;
            EnsureWebView(work);
        }
        work.retainedFiles.emplace_back(appLock.release(),HandleClose{});
        work.retainedFiles.emplace_back(helperLock.release(),HandleClose{});
    }catch(const std::exception&){return false;}
    work.progress.events.push_back("主程序版本与签名匹配，复用现有原助理，跳过下载与重装。");
    work.progress.detail="检测到相同稳定版，正在复用主程序";work.job.Publish(work.progress);return true;
}
void CheckSystem(FileWork& work){
    const auto platform=CurrentPlatform();
    work.progress.checks.push_back("系统："+platform.target+" / "+platform.arch);
    CheckWindowsPowerShell(platform,work);
    work.progress.checks.push_back(platform.target=="windows7"?"WebView2：使用 Win7 安装包内固定 109 运行库。":
        "WebView2：安装时快速检测，缺失时补齐微软运行库。");
    work.job.Publish(work.progress);
}
void FinishDeployment(FileWork& work,bool success){if(work.directPayload)FinishDirectInstall(work,success);}
fs::path CreateStaging() {
    GUID id{}; wchar_t text[40]{};
    if (FAILED(CoCreateGuid(&id)) || !StringFromGUID2(id, text, 40)) throw std::runtime_error("无法创建安装任务标识。");
    const auto directory = fs::temp_directory_path() / (std::wstring(L"yuan-setup-") + text);
    if (!fs::create_directory(directory)) throw std::runtime_error("无法创建独立的下载缓存目录。");
    return directory;
}
Handle OpenLocked(const fs::path& path) {
    Handle handle(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
    if (handle.get() == INVALID_HANDLE_VALUE) throw std::runtime_error("无法锁定安装包，请关闭占用文件的程序。");
    return handle;
}
std::string FileVersion(const fs::path& path) {
    DWORD unused = 0;
    const auto size = GetFileVersionInfoSizeW(path.c_str(), &unused);
    if (!size) return {};
    std::vector<char> data(size);
    if (!GetFileVersionInfoW(path.c_str(), 0, size, data.data())) return {};
    VS_FIXEDFILEINFO* info = nullptr; UINT count = 0;
    if (!VerQueryValueW(data.data(), L"\\", reinterpret_cast<void**>(&info), &count) || count < sizeof(*info)) return {};
    return std::to_string(HIWORD(info->dwFileVersionMS)) + "." +
        std::to_string(LOWORD(info->dwFileVersionMS)) + "." + std::to_string(HIWORD(info->dwFileVersionLS));
}
void LaunchInstalled(const Snapshot& state, bool restoreSession) {
    if (state.phase != Phase::Complete || state.executable.empty() || !fs::is_regular_file(state.executable))
        throw std::runtime_error("安装尚未完成，无法启动原助理。");
    STARTUPINFOW start{}; start.cb = sizeof(start); PROCESS_INFORMATION process{};
    const auto directory = state.executable.parent_path();
    std::wstring command = L"\"" + state.executable.wstring() + L"\"";
    if (restoreSession) command += L" --restore-installer-session";
    if (!CreateProcessW(state.executable.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr,
        directory.c_str(), &start, &process)) throw std::runtime_error("启动原助理失败。系统错误：" + std::to_string(GetLastError()));
    CloseHandle(process.hThread); CloseHandle(process.hProcess);
}
}
#endif
