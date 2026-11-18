#ifdef _WIN32
#define NOMINMAX
#include <winsock2.h>
#include <windows.h>
#include <shobjidl_core.h>
#endif
#include "window.h"
#include "release.h"
#include <wx/app.h>
#include <wx/image.h>
#include <wx/msgdlg.h>
#include <wx/snglinst.h>
#include <wx/utils.h>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <utility>
#include <iostream>
#ifdef _WIN32
#include "payload.h"
#include "command.h"
#include "win7_restart.h"
#endif

namespace {
std::optional<setup::UpdateRequest> ParseUpdateRequest(int argc, wxChar** argv) {
    std::map<std::string, std::string> values;
    bool update = false;
    bool silent = false;
    bool accepted = false;
    for (int index = 1; index < argc; ++index) {
        const wxString argument(argv[index]);
        if (argument == wxS("--update-mode")) { update = true; continue; }
        if (argument == wxS("--update-silent")) { update = true; silent = true; continue; }
        if (argument == wxS("--update-accepted")) { accepted = true; continue; }
        if (argument == wxS("--restore-installer-session")) continue;
        if (!argument.StartsWith(wxS("--update-"))) continue;
        if (index + 1 >= argc) throw std::runtime_error("统一更新参数不完整。");
        values[argument.Mid(9).ToUTF8().data()] = wxString(argv[++index]).ToUTF8().data();
    }
    if (!update) return std::nullopt;
    const auto required = [&values](const char* key) -> const std::string& {
        const auto found = values.find(key);
        if (found == values.end() || found->second.empty())
            throw std::runtime_error("统一更新缺少必要参数。");
        return found->second;
    };
    setup::UpdateRequest request;
    request.destination = setup::fs::u8path(required("install-dir"));
    request.packageCache = setup::fs::u8path(required("package-cache"));
    request.channel = values.find("channel") != values.end() ? values.at("channel") : "stable";
    if (const auto found = values.find("release-id"); found != values.end())
        request.releaseId = found->second;
    request.requestURL = required("url");
    request.accessToken = values.find("token") != values.end() ? values.at("token") : values["access-token"];
    request.silent = silent;
    request.acceptedHandoff = accepted;
    if (request.channel != "stable" ||
        (!request.releaseId.empty() && request.releaseId.find_first_not_of("0123456789") != std::string::npos))
        throw std::runtime_error("更新通道或发布身份不受支持。");
    request.release.version = required("version");
    request.release.url = request.requestURL;
    request.release.sha256 = required("sha256");
    request.release.signature = values["signature"];
    request.release.packageKind=values["package-kind"];
    request.release.releaseId=request.releaseId;
    if(!request.release.packageKind.empty()&&request.release.packageKind!="nsis"&&request.release.packageKind!="app_payload_zip")throw std::runtime_error("不支持此更新包类型。");
    if(request.release.packageKind=="app_payload_zip"&&request.release.signature.empty())throw std::runtime_error("应用载荷缺少发布签名。");
    if(values.count("parent-pid")) {
        const auto& id=values.at("parent-pid");
        if(id.empty()||id.find_first_not_of("0123456789")!=std::string::npos)throw std::runtime_error("更新父进程无效。");
        const auto parsed=std::stoull(id);if(parsed==0||parsed>0xffffffffULL)throw std::runtime_error("更新父进程超出范围。");
        request.parentPid=static_cast<unsigned>(parsed);request.readyFile=setup::fs::u8path(required("ready-file"));
    }
    std::size_t parsedSize = 0;
    const auto& sizeText = required("size");
    request.release.size = std::stoull(sizeText, &parsedSize);
    if (parsedSize != sizeText.size()) throw std::runtime_error("更新包大小参数无效。");
    static const std::set<std::string> allowed{
        "accepted", "access-token", "install-dir", "package-cache", "channel", "release-id", "url", "token",
        "version", "sha256", "size", "signature", "package-kind", "parent-pid", "ready-file",
    };
    for (const auto& [key, value] : values) {
        (void)value;
        if (allowed.find(key) == allowed.end()) throw std::runtime_error("统一更新包含未知参数。");
    }
    std::string error;
    if (!setup::ValidateRequestedRelease(request.release, error)) throw std::runtime_error(error);
    if (request.destination.empty() || !request.destination.is_absolute() ||
        !setup::fs::is_regular_file(request.packageCache))
        throw std::runtime_error("统一更新缓存或安装目录无效。");
    return request;
}
}

class InstallerApp : public wxApp {
    std::unique_ptr<wxSingleInstanceChecker> instance_;
    bool capabilityMode_ = false;
    int processResult_ = 0;
public:
    int OnRun() override { return capabilityMode_ ? processResult_ : wxApp::OnRun(); }
    bool OnInit() override {
#ifdef _WIN32
        // 更新调用继承的 app 工作目录会被本进程锁定，必须在线程启动前释放。
        try {
            setup::fs::current_path(setup::fs::temp_directory_path());
        } catch (const std::exception&) {
            wxMessageBox(wxS("无法切换安装向导工作目录，请检查系统临时目录后重试。"),
                wxS("无法启动安装向导"), wxOK | wxICON_ERROR);
            return false;
        }
        (void)SetCurrentProcessExplicitAppUserModelID(L"app.iywclaw.installer");
        if(argc==4&&wxString(argv[1])==wxS("--elevated-worker")) {
            capabilityMode_=true;
            try {
                unsigned long parent=0;
                if(!wxString(argv[3]).ToULong(&parent)||!parent||parent>0xffffffffUL)throw std::runtime_error("安装调用方编号无效。");
                processResult_=setup::RunElevatedInstall(setup::fs::path(wxString(argv[2]).ToStdWstring()),static_cast<unsigned>(parent));
            }catch(const std::exception&){processResult_=1;}
            return true;
        }
        if(argc==3&&wxString(argv[1])==wxS("--wmf-elevated-worker")) {
            capabilityMode_=true;
            try {
                processResult_=setup::RunWmfElevatedWorker(setup::fs::path(wxString(argv[2]).ToStdWstring()));
            } catch(const std::exception&) { processResult_=1; }
            return true;
        }
        if(argc==2&&wxString(argv[1])==wxS("--capabilities")) {
            std::cout << "{\"protocol\":2,\"payload\":\"app_payload_zip\",\"target\":\"windows\",\"arch\":\"x86_64\","
                "\"nsis_targets\":[\"windows/x86_64\",\"windows/i686\",\"windows7/x86_64\",\"windows7/i686\"]}" << std::endl;
            capabilityMode_=true;
            return true;
        }
        if(argc==3&&wxString(argv[1])==wxS("--uninstall-root")) {
            if(wxMessageBox(wxS("卸载原助理应用？会保留项目、会话、配置和运行环境。"),wxS("卸载原助理"),wxYES_NO|wxNO_DEFAULT)!=wxYES)return false;
            try{setup::UninstallDirect(setup::fs::path(wxString(argv[2]).ToStdWstring()));wxMessageBox(wxS("原助理应用已卸载，用户数据已保留。"),wxS("卸载完成"));}
            catch(const std::exception& error){wxMessageBox(wxString::FromUTF8(error.what()),wxS("卸载未完成"),wxOK|wxICON_ERROR);}
            return false;
        }
#endif
        SetAppName(wxS("原助理安装向导"));
        SetAppDisplayName(wxS("原助理安装向导"));
        instance_=std::make_unique<wxSingleInstanceChecker>("yuan-assistant-setup-"+wxGetUserId());
        if(instance_->IsAnotherRunning())return false;
        wxInitAllImageHandlers();
        try {
            auto request = ParseUpdateRequest(argc, argv);
#ifdef _WIN32
            std::optional<setup::fs::path> resumeRoot;
            if (argc == 2 && wxString(argv[1]) == wxS("--resume-after-restart")) {
                const auto session = setup::LoadRestartSession();
                resumeRoot = session.destination;
                request = session.selection;
            }
            auto* window = new SetupWindow(std::move(request), resumeRoot);
#else
            auto* window = new SetupWindow(std::move(request));
#endif
            SetTopWindow(window);
            if (!window->StartsHidden()) window->Show();
            return true;
        } catch (const std::exception& error) {
            wxMessageBox(wxString::FromUTF8(error.what()), wxS("无法启动安装向导"), wxOK | wxICON_ERROR);
            return false;
        }
    }
};
wxIMPLEMENT_APP(InstallerApp);
