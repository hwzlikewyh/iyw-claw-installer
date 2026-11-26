#include "payload.h"
#include "windows_support.h"
#include "command.h"
#include <shlobj.h>
#include <fstream>
#include <stdexcept>
namespace setup {
namespace {
constexpr auto StandaloneName = L"MicrosoftEdgeWebView2RuntimeInstallerX64.exe";
constexpr std::uint64_t MaxStandaloneBytes = 512 * MiB;
constexpr auto RegistrationTimeout = std::chrono::seconds(8);
bool SupportsStandalone(const OSVERSIONINFOEXW& os,DWORD edition,const std::string& arch) {
    constexpr DWORD win10_1507=10240, win10_1607=14393, win10_1709=16299;
    if(arch!="x86_64" || os.dwMajorVersion!=10)return false;
    if(os.wProductType==VER_NT_SERVER || os.wProductType==VER_NT_DOMAIN_CONTROLLER)
        return os.dwBuildNumber>=win10_1607;
    if(os.wProductType!=VER_NT_WORKSTATION)return false;
    if(os.dwBuildNumber>=win10_1709)return true;
    if(os.dwBuildNumber!=win10_1507 && os.dwBuildNumber!=win10_1607)return false;
    return edition==PRODUCT_ENTERPRISE_S || edition==PRODUCT_ENTERPRISE_S_N ||
        edition==PRODUCT_ENTERPRISE_S_EVALUATION || edition==PRODUCT_ENTERPRISE_S_N_EVALUATION;
}
bool RuntimeFilesPresent(HKEY hive, const wchar_t* key, DWORD view, const wchar_t* version) {
    const std::wstring value(version);
    if (value.empty() || value == L"0.0.0.0" || value.find_first_not_of(L"0123456789.") != std::wstring::npos ||
        value.front() == L'.' || value.back() == L'.') return false;
    wchar_t directory[MAX_PATH]{};
    DWORD bytes=sizeof(directory);
    fs::path base;
    if (RegGetValueW(hive,key,L"location",RRF_RT_REG_SZ|view,nullptr,directory,&bytes)==ERROR_SUCCESS &&
        fs::path(directory).is_absolute()) base=directory;
    else {
        const auto folder=hive==HKEY_CURRENT_USER?CSIDL_LOCAL_APPDATA:CSIDL_PROGRAM_FILESX86;
        if (FAILED(SHGetFolderPathW(nullptr,folder,nullptr,SHGFP_TYPE_CURRENT,directory))) return false;
        base=fs::path(directory)/L"Microsoft/EdgeWebView/Application";
    }
    const auto arch=CurrentPlatform().arch;
    const auto loader=arch=="x86_64"?L"x64":arch=="i686"?L"x86":L"arm64";
    const auto runtime=base/value;
    std::error_code ec;
    return fs::is_regular_file(runtime/L"msedgewebview2.exe",ec) &&
        fs::is_regular_file(runtime/L"msedge.dll",ec) &&
        fs::is_regular_file(runtime/L"EBWebView"/loader/L"EmbeddedBrowserWebView.dll",ec);
}
bool WebViewInstalled() {
    constexpr const wchar_t* key=L"Software\\Microsoft\\EdgeUpdate\\Clients\\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}";
    for(const auto hive:{HKEY_CURRENT_USER,HKEY_LOCAL_MACHINE}) {
        for(const auto view:{RRF_SUBKEY_WOW6432KEY,RRF_SUBKEY_WOW6464KEY}) {
            wchar_t version[128]{};DWORD bytes=sizeof(version);
            if(RegGetValueW(hive,key,L"pv",RRF_RT_REG_SZ|view,nullptr,version,&bytes)==ERROR_SUCCESS &&
                RuntimeFilesPresent(hive,key,view,version))return true;
        }
    }
    return false;
}
void ShowWebViewStep(FileWork& work, const std::string& detail) {
    work.progress.detail=detail;
    work.progress.events.push_back(detail);
    work.job.Publish(work.progress);
}
bool InstallWebView(const fs::path& installer, const fs::path& log, FileWork& work) {
    auto lock=OpenLocked(installer);
    VerifyMicrosoftSignature(installer);
    const auto code=RunCommand({installer,{"/silent","/install"},log,0,work.staging},[&]{Advance(work,0);});
    const auto result=std::to_string(static_cast<DWORD>(code));
    work.progress.events.push_back("WebView2 安装器退出码 "+result+"，日志 "+log.filename().u8string());
    // Edge Update 可能在子进程退出后才完成注册，成功回执要以运行库可见为准。
    const auto deadline=std::chrono::steady_clock::now()+RegistrationTimeout;
    do {
        if(WebViewInstalled())return true;
        if(code!=0)break;
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    } while(std::chrono::steady_clock::now()<deadline);
    return WebViewInstalled();
}
void WriteStandalone(const fs::path& path, FileWork& work) {
    std::ofstream file(path,std::ios::binary|std::ios::trunc);
    file.exceptions(std::ios::failbit|std::ios::badbit);
    std::uint64_t received=0;
    ReadWebViewStandalone([&](const char* bytes,std::size_t count){
        received+=count;
        if(received>MaxStandaloneBytes)throw std::runtime_error("WebView2 独立安装包超出大小限制。");
        file.write(bytes,static_cast<std::streamsize>(count));
    },work.job);
    file.close();
}
bool TryBootstrapper(FileWork& work) {
    const auto app=work.deployment.empty()?work.progress.destination/"app":work.deployment/"incoming/app";
    ShowWebViewStep(work,"正在由微软在线引导器为当前系统选择 WebView2");
    try {
        return InstallWebView(app/"installer/webview2-setup.exe",work.staging/"webview2-install.log",work);
    } catch(const std::exception& error) {
        work.progress.events.push_back(std::string("WebView2 在线安装失败：")+error.what());
        return false;
    }
}
bool TryStandalone(FileWork& work) {
    ShowWebViewStep(work,"正在从微软下载 WebView2 独立安装包");
    const auto path=work.staging/StandaloneName;
    std::error_code cleanup;
    bool installed=false;
    try {
        WriteStandalone(path,work);
        installed=InstallWebView(path,work.staging/"webview2-standalone.log",work);
    } catch(const std::exception& error) {
        work.progress.events.push_back(std::string("WebView2 独立安装失败：")+error.what());
    }
    fs::remove(path,cleanup);
    return installed;
}
}
void EnsureWebView(FileWork& work) {
    if(WebViewInstalled()){work.progress.checks.push_back("WebView2：已安装，跳过补装。");return;}
    const bool standalone=SupportsStandalone(NativeWindowsVersion(),WindowsProductEdition(),CurrentPlatform().arch) &&
        IsProcessorFeaturePresent(PF_SSE3_INSTRUCTIONS_AVAILABLE);
    bool installed=TryBootstrapper(work);
    if(!installed) {
        CheckCancel(work.job);
        if(standalone)installed=TryStandalone(work);
    }
    if(!installed)throw std::runtime_error("当前系统未能安装可用的 WebView2。请查看安装详情中的 Windows 版本和微软安装器退出码。");
    work.progress.checks.push_back("WebView2：已自动补装，当前用户运行库检查通过。");
}
}
