#include "payload.h"
#include "windows_support.h"
#include "command.h"
#include <stdexcept>
namespace setup {
namespace {
bool WebViewInstalled() {
    constexpr const wchar_t* key=L"Software\\Microsoft\\EdgeUpdate\\Clients\\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}";
    for(const auto hive:{HKEY_CURRENT_USER,HKEY_LOCAL_MACHINE}) {
        for(const auto view:{RRF_SUBKEY_WOW6432KEY,RRF_SUBKEY_WOW6464KEY}) {
            wchar_t version[128]{};DWORD bytes=sizeof(version);
            if(RegGetValueW(hive,key,L"pv",RRF_RT_REG_SZ|view,nullptr,version,&bytes)==ERROR_SUCCESS&&version[0]&&std::wstring(version)!=L"0.0.0.0")return true;
        }
    }
    return false;
}
}
void EnsureWebView(FileWork& work) {
    if(WebViewInstalled()){work.progress.checks.push_back("WebView2：已安装，跳过补装。");return;}
    const auto installer=(work.deployment.empty()?work.progress.destination/"app":work.deployment/"incoming/app")/"installer/webview2-setup.exe";
    auto lock=OpenLocked(installer);
    VerifyMicrosoftSignature(installer);
    work.progress.detail="正在补齐 WebView2 运行环境";work.job.Publish(work.progress);
    // 微软安装器由普通用户启动，沿用 per-user 安装；不会运行 NSIS。
    const auto code=RunCommand({installer,{"/silent","/install"},work.staging/"webview2-install.log",0},[&]{Advance(work,0);});
    if(code!=0||!WebViewInstalled())throw std::runtime_error("WebView2 安装未就绪，退出码："+std::to_string(code));
}
}
