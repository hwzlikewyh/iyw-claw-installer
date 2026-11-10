#include "window.h"
#include <wx/app.h>
#include <wx/image.h>
#include <wx/msgdlg.h>
#include <wx/snglinst.h>
#include <wx/utils.h>

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
            auto* window = new SetupWindow();
            SetTopWindow(window); window->Show(); return true;
        } catch (const std::exception& error) {
            wxMessageBox(wxString::FromUTF8(error.what()), wxS("无法启动安装向导"), wxOK | wxICON_ERROR);
            return false;
        }
    }
};
wxIMPLEMENT_APP(InstallerApp);
