#include "command.h"
#include "windows_support.h"
#include "wmf_sources.h"
#include "wmf_identity.h"
#ifdef _WIN32
#include <sddl.h>
#include <array>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <vector>
namespace setup {
namespace {
constexpr DWORD ServiceWaitMs = 60000;
constexpr DWORD ServicePollMs = 150;
constexpr DWORD MaxModulePathChars = 32768;
constexpr DWORD WmfUnsupportedOperation = 0x80240037;
std::wstring Quote(const std::wstring& text) {
    std::wstring out=L"\"";unsigned slashes=0;
    for(auto c:text){
        if(c==L'\\'){++slashes;continue;}
        out.append(c==L'"'?slashes*2+1:slashes,L'\\');slashes=0;out+=c;
    }
    out.append(slashes*2,L'\\');return out+L'"';
}
Handle OutputFile(const fs::path& path){
    SECURITY_ATTRIBUTES security{sizeof(security),nullptr,TRUE};
    Handle file(CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ,&security,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr));
    if(file.get()==INVALID_HANDLE_VALUE)throw std::runtime_error("无法创建环境检查日志。");return file;
}
int RunElevatedCommand(const Command& command,const std::function<void()>& poll){
    std::wstring arguments=Quote(command.executable.wstring());
    for(const auto& arg:command.arguments)arguments+=L" "+Quote(fs::u8path(arg).wstring());
    const auto executable=Quote(command.executable.wstring());
    const auto parameters=arguments.substr(executable.size()+1);
    const auto directory=command.directory.empty()?command.executable.parent_path():command.directory;
    SHELLEXECUTEINFOW launch{};launch.cbSize=sizeof(launch);
    launch.fMask=SEE_MASK_NOCLOSEPROCESS|SEE_MASK_NOASYNC|SEE_MASK_FLAG_NO_UI;
    launch.lpVerb=L"runas";launch.lpFile=command.executable.c_str();
    launch.lpParameters=parameters.c_str();launch.lpDirectory=directory.c_str();launch.nShow=SW_HIDE;
    if(!ShellExecuteExW(&launch)){
        const auto error=GetLastError();
        if(error==ERROR_CANCELLED)throw std::runtime_error("已取消系统组件的管理员授权。");
        throw std::runtime_error("无法启动系统组件安装程序。系统错误："+std::to_string(error));
    }
    Handle process(launch.hProcess);
    for(;;){
        const auto status=WaitForSingleObject(process.get(),150);
        if(status==WAIT_OBJECT_0)break;
        if(status!=WAIT_TIMEOUT)throw std::runtime_error("等待系统组件安装结果失败。");
        if(poll)poll();
    }
    DWORD code=1;
    if(!GetExitCodeProcess(process.get(),&code))throw std::runtime_error("无法读取系统组件安装结果。");
    return static_cast<int>(code);
}
fs::path CurrentModulePath() {
    std::vector<wchar_t> path(MaxModulePathChars);
    const auto size = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (!size || size >= path.size()) throw std::runtime_error("无法定位安装向导程序。");
    return fs::path(path.data());
}
#include "wmf_service.inc"
DWORD RunWusa(const fs::path& wusa, const fs::path& package, const fs::path& installLog) {
    std::wstring commandLine = Quote(wusa.wstring()) + L" " + Quote(package.wstring()) +
        L" /quiet /norestart /log:" + Quote(installLog.wstring());
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION child{};
    if (!CreateProcessW(wusa.c_str(), commandLine.data(), nullptr, nullptr, FALSE,
        CREATE_NO_WINDOW, nullptr, wusa.parent_path().c_str(), &startup, &child))
        throw std::runtime_error("无法在管理员进程中启动 Windows 更新独立安装程序，Windows 错误：" +
            std::to_string(GetLastError()));
    Handle process(child.hProcess), thread(child.hThread);
    if (WaitForSingleObject(process.get(), INFINITE) != WAIT_OBJECT_0)
        throw std::runtime_error("等待 Windows 更新独立安装程序失败。");
    DWORD code = 1;
    if (!GetExitCodeProcess(process.get(), &code))
        throw std::runtime_error("无法读取 Windows 更新独立安装程序结果。");
    return code;
}
fs::path NativeSystemDirectory() {
    BOOL wow64 = FALSE;
    std::array<wchar_t, MAX_PATH> directory{};
    if (!IsWow64Process(GetCurrentProcess(), &wow64))
        throw std::runtime_error("无法检查 Windows 系统架构。");
    if (wow64) {
        const auto size = GetWindowsDirectoryW(directory.data(), static_cast<UINT>(directory.size()));
        if (!size || size >= directory.size()) throw std::runtime_error("无法定位 Windows 目录。");
        return fs::path(directory.data()) / L"Sysnative";
    }
    const auto size = GetSystemDirectoryW(directory.data(), static_cast<UINT>(directory.size()));
    if (!size || size >= directory.size()) throw std::runtime_error("无法定位 Windows 系统目录。");
    return directory.data();
}
#include "wmf_package_manager.inc"
#include "wmf_workspace.inc"
}
int RunCommand(const Command& command,const std::function<void()>& poll){
    if(command.elevated)return RunElevatedCommand(command,poll);
    std::wstring arguments=Quote(command.executable.wstring());
    for(const auto& arg:command.arguments)arguments+=L" "+Quote(fs::u8path(arg).wstring());
    auto out=OutputFile(command.output),err=OutputFile(command.output.wstring()+L".stderr");
    STARTUPINFOW start{};start.cb=sizeof(start);start.dwFlags=STARTF_USESTDHANDLES;
    start.hStdOutput=out.get();start.hStdError=err.get();start.hStdInput=GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION child{};
    const auto directory=command.directory.empty()?command.executable.parent_path():command.directory;
    if(!CreateProcessW(command.executable.c_str(),arguments.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW|BELOW_NORMAL_PRIORITY_CLASS,nullptr,
        directory.c_str(),&start,&child))throw std::runtime_error("无法启动环境助手。系统错误："+std::to_string(GetLastError()));
    Handle process(child.hProcess),thread(child.hThread);
    const auto started=std::chrono::steady_clock::now();
    for(;;){
        const auto status=WaitForSingleObject(process.get(),150);
        if(status==WAIT_OBJECT_0)break;
        if(status!=WAIT_TIMEOUT)throw std::runtime_error("等待环境助手失败。");
        if(poll)poll();
        if(command.timeoutSeconds&&std::chrono::steady_clock::now()-started>std::chrono::seconds(command.timeoutSeconds)){
            TerminateProcess(process.get(),1);WaitForSingleObject(process.get(),5000);
            throw std::runtime_error("环境只读检查超时。");
        }
    }
    DWORD code=1;if(!GetExitCodeProcess(process.get(),&code))throw std::runtime_error("无法读取环境助手退出码。");
    return static_cast<int>(code);
}
int RunWmfInstallerElevated(const WmfInstallRequest& request, const std::function<void()>& poll) {
    const auto session = request.serviceLog.parent_path().filename();
    const auto directory = WmfWorkerDirectory(session);
    const auto code = RunCommand({CurrentModulePath(), {"--wmf-elevated-worker", session.u8string()},
        request.serviceLog, 0, {}, true}, poll);
    for (const auto* name : WmfLogs) {
        std::error_code error;
        fs::copy_file(directory / name, request.serviceLog.parent_path() / name, fs::copy_options::overwrite_existing, error);
    }
    return code;
}
fs::path WmfLiveDirectory(const fs::path& staging) { return WmfWorkerDirectory(staging.filename()); }
int RunWmfElevatedWorker(const fs::path& session) {
    const auto directory = WmfWorkerDirectory(session);
    const auto platform = CurrentPlatform();
    if (platform.target != "windows7") return ERROR_NOT_SUPPORTED;
    if (!CreateWmfDirectory(directory)) return ERROR_ACCESS_DENIED;
    const bool x64 = platform.arch == "x86_64";
    const auto package = directory / WmfPackageName(x64);
    const WmfInstallRequest request{NativeSystemDirectory() / L"wusa.exe", package,
        directory / L"wmf-install.log", directory / L"wmf-service.log"};
    WmfWorkspaceCleanup cleanup{directory, package};
    std::ofstream log(request.serviceLog, std::ios::binary | std::ios::trunc);
    if (!log) return ERROR_ACCESS_DENIED;
    log << std::unitbuf;
    HANDLE rawToken = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &rawToken)) {
        log << "无法检查管理员权限，Windows 错误：" << GetLastError() << '\n';
        return ERROR_ACCESS_DENIED;
    }
    Handle token(rawToken);
    TOKEN_ELEVATION elevation{};
    DWORD size = 0;
    if (!GetTokenInformation(token.get(), TokenElevation, &elevation, sizeof(elevation), &size) ||
        !elevation.TokenIsElevated) {
        log << "WMF 安装权限助手未取得管理员权限。\n";
        return ERROR_ELEVATION_REQUIRED;
    }
    std::unique_ptr<WindowsUpdateServiceLease> service;
    DWORD code = ERROR_INSTALL_FAILURE;
    try {
        CopyVerifiedWmf(package, x64);
        service = std::make_unique<WindowsUpdateServiceLease>(log);
        service->Prepare();
        code = RunWusa(request.wusa, request.package, request.installLog);
        log << "WUSA 退出码：" << code << '\n';
        if (code == WmfUnsupportedOperation) {
            log << "WUSA 更新搜索返回 0x80240037；解包已校验的 WMF MSU，并按包内 XML 清单通过 Package Manager 本地安装。\n";
            try {
                code = RunPkgmgrWmfInstaller(request);
                log << "Package Manager 本地安装退出码：" << code <<
                    "；日志：wmf-pkgmgr-output.log；CBS 详细日志：%WINDIR%\\Logs\\CBS\\CBS.log\n";
            } catch (const std::exception& error) {
                code = ERROR_INSTALL_FAILURE;
                log << "Package Manager 本地安装失败：" << error.what() << '\n';
            }
        }
    } catch (const std::exception& error) {
        log << "WMF 安装过程失败：" << error.what() << '\n';
    }
    if (service && !service->Restore()) return ERROR_SERVICE_CANNOT_ACCEPT_CTRL;
    return static_cast<int>(code);
}
}
#endif
