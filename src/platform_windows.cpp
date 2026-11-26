#include "windows_support.h"
#include "command.h"
#include "payload.h"
#include "existing_install.h"
#ifdef _WIN32
#include <shellapi.h>
#include <shlobj.h>
#include <wincrypt.h>
#include <array>
#include <fstream>
#include <stdexcept>
#include <vector>
#include "win7_prerequisites.inc"
namespace setup {
const OSVERSIONINFOEXW& NativeWindowsVersion() {
    static const auto version=[] {
        using QueryVersion = LONG(WINAPI*)(OSVERSIONINFOW*);
        const auto query=reinterpret_cast<QueryVersion>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"),"RtlGetVersion"));
        OSVERSIONINFOEXW info{};info.dwOSVersionInfoSize=sizeof(info);
        if(!query || query(reinterpret_cast<OSVERSIONINFOW*>(&info))<0)throw std::runtime_error("无法识别 Windows 版本。");
        return info;
    }();
    return version;
}
DWORD WindowsProductEdition() {
    const auto& v=NativeWindowsVersion();
    DWORD edition=PRODUCT_UNDEFINED;
    GetProductInfo(v.dwMajorVersion,v.dwMinorVersion,v.wServicePackMajor,v.wServicePackMinor,&edition);
    return edition;
}
Platform CurrentPlatform() {
    const auto& version=NativeWindowsVersion();
    constexpr DWORD Windows7Major = 6, Windows7Minor = 1, Windows10Major = 10;
    if (version.dwMajorVersion == Windows7Major && version.dwMinorVersion == Windows7Minor)
        throw std::runtime_error("暂不支持 Win7 系统。");
    if (version.dwMajorVersion < Windows10Major)
        throw std::runtime_error("此安装向导需要 Windows 10 或更新系统。");
    using QueryMachine=BOOL(WINAPI*)(HANDLE,USHORT*,USHORT*);
    const auto queryMachine=reinterpret_cast<QueryMachine>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"IsWow64Process2"));
    USHORT processMachine=0,nativeMachine=0;
    if(queryMachine && queryMachine(GetCurrentProcess(),&processMachine,&nativeMachine)) {
        if(nativeMachine==IMAGE_FILE_MACHINE_ARM64)return {"windows","aarch64"};
        if(nativeMachine==IMAGE_FILE_MACHINE_AMD64)return {"windows","x86_64"};
        if(nativeMachine==IMAGE_FILE_MACHINE_I386)return {"windows","i686"};
    }
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
    return fs::path(local.data()) / setup::Brand().nameW;
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
    const fs::path destination = root / "installer" / fs::u8path(setup::Brand().installerName + ".exe");
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
    if(!fs::is_regular_file(executable)||!fs::is_regular_file(helper))return false;
    try{
        auto appLock=OpenLocked(executable),helperLock=OpenLocked(helper);
        RequireAppVersion(executable,release.version,work);
        VerifySignature(executable);VerifySignature(helper);
        if(work.directPayload) {
            const auto output=work.staging/"reuse-helper-identity.json";
            if(RunCommand({helper,{"--identity"},output,15})!=0)return false;
            const auto identity=nlohmann::json::parse(ReadOutput(output));
            if(identity.value("installerProtocol",0)!=2)return false;
            for(const auto* path:{"resources/bundle/index.html","xinghe-resources/runtime.json","native-share/config.json","native-share/register.ps1","native-share/share-resources.ps1","native-share/IywAssistant.ShareTarget.msix","installer/webview2-setup.exe"})
                if(!fs::is_regular_file(work.progress.destination/"app"/fs::u8path(path)))return false;
        }
        work.retainedFiles.emplace_back(appLock.release(),HandleClose{});
        work.retainedFiles.emplace_back(helperLock.release(),HandleClose{});
    }catch(const std::exception&){return false;}
    work.progress.events.push_back("主程序版本与签名匹配，复用现有原助理，跳过下载与重装。");
    work.progress.detail="检测到相同稳定版，正在复用主程序";work.job.Publish(work.progress);return true;
}
void CheckSystem(FileWork& work){
    const auto platform=CurrentPlatform();
    const auto& os=NativeWindowsVersion();
    work.progress.checks.push_back("系统："+platform.target+" / "+platform.arch+"，版本 "+
        std::to_string(os.dwMajorVersion)+"."+std::to_string(os.dwMinorVersion)+"."+std::to_string(os.dwBuildNumber)+
        (os.wProductType==VER_NT_WORKSTATION?"，工作站":"，服务器")+"，SKU "+std::to_string(WindowsProductEdition()));
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
namespace {
std::string EncodedPath(const fs::path& path) {
    const auto bytes=path.u8string();
    constexpr DWORD flags=CRYPT_STRING_BASE64|CRYPT_STRING_NOCRLF;
    DWORD size=0;
    const auto data=reinterpret_cast<const BYTE*>(bytes.data());
    const auto length=static_cast<DWORD>(bytes.size());
    if(!CryptBinaryToStringA(data,length,flags,nullptr,&size))throw std::runtime_error("无法编码应用文件路径。");
    std::string encoded(size,'\0');
    if(!CryptBinaryToStringA(data,length,flags,encoded.data(),&size))throw std::runtime_error("无法编码应用文件路径。");
    encoded.resize(size);
    return encoded;
}
std::string NativeAppVersion(const fs::path& path, FileWork& work) {
    // 在独立的原生进程中读取固定版本字段，避免 32 位向导映射大型 EXE 时耗尽地址空间。
    const auto ps=NativeSystemDirectory()/L"WindowsPowerShell/v1.0/powershell.exe";
    const auto output=work.staging/"app-version.log";
    const std::string command=
        "& { param($p) $ErrorActionPreference='Stop'; "
        "$v=[Diagnostics.FileVersionInfo]::GetVersionInfo([Text.Encoding]::UTF8.GetString([Convert]::FromBase64String($p))); "
        "if(!$v.FileVersion -and !$v.FileMajorPart -and !$v.FileMinorPart -and !$v.FileBuildPart){exit 1}; "
        "[Console]::Write(('{0}.{1}.{2}' -f $v.FileMajorPart,$v.FileMinorPart,$v.FileBuildPart)) }";
    const auto code=RunCommand({ps,{"-NoLogo","-NoProfile","-NonInteractive","-Command",command,"'"+EncodedPath(path)+"'"},output,30});
    if(code!=0)return {};
    const auto value=ReadOutput(output);
    if(value.empty()||value.size()>sizeof("65535.65535.65535")-1||
       value.find_first_not_of("0123456789.")!=std::string::npos)return {};
    return value;
}
}
void RequireAppVersion(const fs::path& path, const std::string& expected, FileWork& work) {
    auto actual=FileVersion(path);
    if(actual.empty()){
        const auto error=GetLastError();
        work.progress.events.push_back("向导未能读取 "+path.filename().u8string()+" 的版本资源，Windows 错误 "+
            std::to_string(error)+"；使用系统进程复核。");
        actual=NativeAppVersion(path,work);
        if(actual.empty())throw std::runtime_error("无法读取应用入口版本；目标版本 "+expected+
            "，Windows 错误 "+std::to_string(error)+"。详见 app-version.log.stderr。");
    }
    if(actual!=expected)throw std::runtime_error("应用入口版本 "+actual+" 与目标版本 "+expected+" 不一致。");
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
