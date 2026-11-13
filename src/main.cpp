#include "window.h"
#include <wx/app.h>
#include <wx/image.h>
#include <wx/msgdlg.h>
#include <wx/snglinst.h>
#include <wx/utils.h>
#include <map>
#include <optional>
#include <stdexcept>
#include <utility>

namespace {
std::optional<setup::UpdateRequest> ParseUpdateRequest(int argc, wxChar** argv) {
    std::map<std::string, std::string> values;
    bool update = false;
    for (int index = 1; index < argc; ++index) {
        const wxString argument(argv[index]);
        if (argument == wxS("--update-mode")) { update = true; continue; }
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
    request.release.version = required("version");
    request.release.url = required("url");
    request.release.sha256 = required("sha256");
    request.release.signature = values["signature"];
    request.release.size = std::stoull(required("size"));
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
public:
    bool OnInit() override {
        SetAppName(wxS("原助理安装向导"));
        SetAppDisplayName(wxS("原助理安装向导"));
        instance_=std::make_unique<wxSingleInstanceChecker>("yuan-assistant-setup-"+wxGetUserId());
        if(instance_->IsAnotherRunning())return false;
        wxInitAllImageHandlers();
        try {
            auto request = ParseUpdateRequest(argc, argv);
            auto* window = new SetupWindow(std::move(request));
            SetTopWindow(window); window->Show(); return true;
        } catch (const std::exception& error) {
            wxMessageBox(wxString::FromUTF8(error.what()), wxS("无法启动安装向导"), wxOK | wxICON_ERROR);
            return false;
        }
    }
};
wxIMPLEMENT_APP(InstallerApp);
