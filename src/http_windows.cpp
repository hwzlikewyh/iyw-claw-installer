#include "release.h"
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <winhttp.h>
#include <memory>
#include <stdexcept>
#include <vector>
namespace setup {
namespace {
struct HttpClose { void operator()(void* handle) const { if (handle) WinHttpCloseHandle(handle); } };
using Http = std::unique_ptr<void, HttpClose>;
struct Address { std::wstring host, path; INTERNET_PORT port = INTERNET_DEFAULT_HTTPS_PORT; };
[[noreturn]] void NetworkError(const char* action) {
    throw std::runtime_error(std::string(action) + "，请检查网络或代理后重试。系统错误：" + std::to_string(GetLastError()));
}
std::wstring Wide(const std::string& text) { return fs::u8path(text).wstring(); }
Address ParseURL(const std::wstring& url, bool webViewDownload) {
    URL_COMPONENTS parts{}; parts.dwStructSize = sizeof(parts);
    parts.dwHostNameLength = parts.dwUrlPathLength = parts.dwExtraInfoLength = DWORD(-1);
    parts.dwUserNameLength = parts.dwPasswordLength = DWORD(-1);
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &parts) || parts.nScheme != INTERNET_SCHEME_HTTPS ||
        parts.dwUserNameLength || parts.dwPasswordLength || parts.nPort != INTERNET_DEFAULT_HTTPS_PORT)
        throw std::runtime_error("下载地址必须使用有效的 HTTPS 地址。");
    Address result{std::wstring(parts.lpszHostName, parts.dwHostNameLength),
        std::wstring(parts.lpszUrlPath, parts.dwUrlPathLength), parts.nPort};
    if (parts.dwExtraInfoLength) result.path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
    const auto& host = result.host;
    if (webViewDownload) {
        if (host != L"go.microsoft.com" && host != L"msedge.sf.dl.delivery.mp.microsoft.com")
            throw std::runtime_error("WebView2 下载重定向到了非预期的域名。");
        return result;
    }
    const std::wstring tosSuffix = L".volces.com";
    const bool tos = host.size() > tosSuffix.size() && host.compare(host.size() - tosSuffix.size(), tosSuffix.size(), tosSuffix) == 0 &&
        host.find(L".tos-") != std::wstring::npos;
    if (host != L"gateway.iyw.cn" && host != L"vol-ai.iywtu.com" &&
        host != L"download.microsoft.com" && host != L"download.visualstudio.microsoft.com" && !tos)
        throw std::runtime_error("Fusion 返回了未授权的下载域名。");
    return result;
}
Http OpenSession() {
    Http session(WinHttpOpen(L"YuanAssistant-Installer/0.2", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session) NetworkError("无法创建网络会话");
    WinHttpSetTimeouts(session.get(), 10000, 15000, 15000, 20000);
    const auto platform=CurrentPlatform();
    if(platform.target=="windows7"){
        DWORD protocols=WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
        if(!WinHttpSetOption(session.get(),WINHTTP_OPTION_SECURE_PROTOCOLS,&protocols,sizeof(protocols)))
            NetworkError("系统缺少 TLS 1.2 支持，请安装 Windows 安全更新");
    }
    return session;
}
Http OpenRequest(void* connection, const Address& address,const std::wstring& extra,const std::string* body) {
    Http request(WinHttpOpenRequest(connection, body?L"POST":L"GET", address.path.c_str(), nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE | WINHTTP_FLAG_REFRESH));
    if (!request) NetworkError("无法创建下载请求");
    DWORD policy = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    if (!WinHttpSetOption(request.get(), WINHTTP_OPTION_REDIRECT_POLICY, &policy, sizeof(policy)))
        NetworkError("无法设置下载重定向策略");
    const std::wstring headers = L"Cache-Control: no-cache\r\n"+extra+
        (body?L"Content-Type: application/json\r\n":L"");
    const DWORD length=body?static_cast<DWORD>(body->size()):0;
    if (!WinHttpSendRequest(request.get(), headers.c_str(), DWORD(-1),body?const_cast<char*>(body->data()):nullptr,length,length,0) ||
        !WinHttpReceiveResponse(request.get(), nullptr)) NetworkError("无法连接安装服务");
    return request;
}
std::wstring Redirect(void* request) {
    DWORD size = 0;
    WinHttpQueryHeaders(request, WINHTTP_QUERY_LOCATION, nullptr, nullptr, &size, nullptr);
    if (!size || size > 32 * 1024) throw std::runtime_error("下载重定向地址无效。");
    std::vector<wchar_t> buffer(size / sizeof(wchar_t) + 1);
    if (!WinHttpQueryHeaders(request, WINHTTP_QUERY_LOCATION, nullptr, buffer.data(), &size, nullptr))
        NetworkError("无法读取下载重定向");
    return buffer.data();
}
void ReadBody(void* request, const Receive& receive, InstallJob& job) {
    constexpr DWORD BufferSize = 256 * 1024;
    std::vector<char> buffer(BufferSize);
    for (;;) {
        CheckCancel(job);
        DWORD count = 0;
        if (!WinHttpReadData(request, buffer.data(), BufferSize, &count)) NetworkError("下载中断");
        if (!count) break;
        receive(buffer.data(), count);
    }
}
}
bool TransferHttps(const std::string& url, const Receive& receive, InstallJob& job,
    const std::pair<std::uint64_t,std::uint64_t>* range,const std::string* body=nullptr, bool webViewDownload=false) {
    auto session = OpenSession();
    auto current = Wide(url);
    constexpr unsigned RedirectLimit = 5;
    for (unsigned attempt = 0; attempt <= RedirectLimit; ++attempt) {
        CheckCancel(job);
        const auto address = ParseURL(current,webViewDownload);
        Http connection(WinHttpConnect(session.get(), address.host.c_str(), address.port, 0));
        if (!connection) NetworkError("无法连接安装服务");
        std::wstring extra=webViewDownload?L"":L"X-IYW-Installation-ID: yuan-assistant-installer\r\n";
        if(range)extra+=L"Range: bytes="+std::to_wstring(range->first)+L"-"+std::to_wstring(range->second)+L"\r\n";
        auto request = OpenRequest(connection.get(), address,extra,body);
        DWORD status = 0, size = sizeof(status);
        if (!WinHttpQueryHeaders(request.get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            nullptr, &status, &size, nullptr)) NetworkError("无法读取下载状态");
        if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308) {
            if(body)throw std::runtime_error("环境计划接口不允许重定向。");
            current = Redirect(request.get()); continue;
        }
        if(range&&status==200)return false;
        if(status!=(range?206u:200u))throw std::runtime_error("安装服务返回 HTTP "+std::to_string(status)+"，请稍后重试。");
        if(range){
            wchar_t value[128]{};DWORD length=sizeof(value);
            if(!WinHttpQueryHeaders(request.get(),WINHTTP_QUERY_CONTENT_RANGE,nullptr,value,&length,nullptr))NetworkError("分段响应缺少范围信息");
            const auto prefix=L"bytes "+std::to_wstring(range->first)+L"-"+std::to_wstring(range->second)+L"/";
            if(std::wstring(value).rfind(prefix,0)!=0)throw std::runtime_error("服务器返回了错误的下载分段。");
        }
        ReadBody(request.get(), receive, job); return true;
    }
    throw std::runtime_error("安装包下载重定向次数过多。");
}
void ReadHttps(const std::string& url,const Receive& receive,InstallJob& job){TransferHttps(url,receive,job,nullptr);}
void ReadWebViewStandalone(const Receive& receive,InstallJob& job){
    TransferHttps("https://go.microsoft.com/fwlink/?linkid=2124701",receive,job,nullptr,nullptr,true);
}
void PostHttps(const std::string& url,const std::string& body,const Receive& receive,InstallJob& job){TransferHttps(url,receive,job,nullptr,&body);}
bool ReadHttpsRange(const std::string& url,const std::pair<std::uint64_t,std::uint64_t>& range,const Receive& receive,InstallJob& job){
    return TransferHttps(url,receive,job,&range);
}
}
#endif
