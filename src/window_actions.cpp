#include "window.h"
#include "release.h"
#include "diagnostics.h"
#include <wx/clipbrd.h>
#include <wx/dataobj.h>
#include <wx/filename.h>
#include <wx/dirdlg.h>
#include <wx/msgdlg.h>
#include <wx/utils.h>
#include <utility>

namespace {
constexpr int ProgressRefreshMs = 100;
wxString ToText(const std::string& value) { return wxString::FromUTF8(value); }
}
void SetupWindow::Browse() {
    wxDirDialog dialog(this, wxS("选择原助理安装位置"), path_->GetValue(), wxDD_DEFAULT_STYLE);
    if (dialog.ShowModal() == wxID_OK) {
        path_->SetValue(dialog.GetPath()); path_->SetInsertionPoint(0);
    }
}
void SetupWindow::ToggleDiagnostics() {
    const bool show = !diagnosticText_->IsShown();
    if (show) {
        diagnosticText_->SetBackgroundColour(ui::Ground);
        diagnosticText_->SetForegroundColour(ui::Ink);
        diagnosticText_->ChangeValue(ToText(setup::diagnostics::Report(snapshot_)));
        diagnosticText_->SetInsertionPoint(0);
    }
    diagnosticText_->Show(show);
    details_->SetLabel(show ? wxS("收起详情") : wxS("查看详情"));
    if (show) diagnosticText_->SetFocus();
    pageDirty_ = true; canvas_->Refresh();
}
void SetupWindow::OpenLog() {
    const auto path = ToText(snapshot_.logPath.u8string());
    if (wxFileExists(path) && wxLaunchDefaultApplication(path)) return;
    logs_->SetLabel(wxS("日志不可用"));
    if (!diagnosticText_->IsShown()) ToggleDiagnostics();
}
void SetupWindow::CopyDiagnostics() {
    auto report = ToText(setup::diagnostics::Report(snapshot_));
    try { report.Replace(ToText(setup::UserHome().u8string()), wxS("%USERPROFILE%")); }
    catch (const std::exception&) { }
    if (!wxTheClipboard->Open()) { copy_->SetLabel(wxS("复制失败，请重试")); return; }
    const bool copied = wxTheClipboard->SetData(new wxTextDataObject(report));
    wxTheClipboard->Flush(); wxTheClipboard->Close();
    copy_->SetLabel(copied ? wxS("已复制诊断信息") : wxS("复制失败，请重试"));
}
void SetupWindow::StartInstall() {
    try {
        const auto folder = setup::fs::u8path(path_->GetValue().ToUTF8().data());
        if (folder.empty() || !folder.is_absolute()) {
            inlineMessage_=wxS("请选择完整的文件夹路径，例如 D:\\原助理。");canvas_->Refresh();
            path_->SetFocus(); return;
        }
        inlineMessage_.clear();closing_=false;
        started_ = std::chrono::steady_clock::now(); liveSeconds_ = 0; cancelling_ = false;
        tipStartedSeconds_ = motionSeconds_;
        if (updateRequest_) {
            updateRequest_->destination = folder;
            job_.Start(std::move(*updateRequest_));
            updateRequest_.reset();
        } else {
            job_.Start(folder);
        }
        snapshot_ = job_.Read(); UpdateControls();
        WakeMotion();
        timer_.Start(ProgressRefreshMs);
    } catch (const std::exception& error) {
        inlineMessage_=ToText(error.what());canvas_->Refresh();
    }
}
void SetupWindow::OnPrimary() {
    if (Running()) return;
    if (snapshot_.phase == setup::Phase::Complete) {
        try { setup::LaunchInstalled(snapshot_); Close(); }
        catch (const std::exception& error) { inlineMessage_=ToText(error.what());canvas_->Refresh(); }
        return;
    }
    if (snapshot_.phase != setup::Phase::Ready || !configuring_) {
        snapshot_ = {}; cancelling_ = false; configuring_ = true; inlineMessage_.clear();
        BeginTransition();
        UpdateControls(); path_->SetInsertionPoint(0); primary_->SetFocus(); return;
    }
    StartInstall();
}
void SetupWindow::OnSecondary() {
    if (Running()) {
        snapshot_=job_.Read();
        if (!snapshot_.cancellable) {inlineMessage_=wxS("正在部署应用与运行环境，请稍候。");UpdateControls();return;}
        cancelling_ = true; job_.Cancel(); UpdateControls();
    } else if (snapshot_.phase == setup::Phase::Ready && configuring_) {
        configuring_ = false; BeginTransition(); UpdateControls(); primary_->SetFocus();
    } else Close();
}
void SetupWindow::Tick(wxTimerEvent&) {
    const auto before = snapshot_.phase;
    snapshot_ = job_.Read();
    if (snapshot_.phase != before) UpdateControls();
    liveSeconds_ = std::chrono::duration<double>(std::chrono::steady_clock::now() - started_).count();
    if (!Running()) {
        timer_.Stop(); job_.Join(); cancelling_ = false;
        if (closing_) { Destroy(); return; }
        BeginTransition(); UpdateControls(); primary_->SetFocus();
    }
    canvas_->Refresh(false);
}
void SetupWindow::CloseWindow(wxCloseEvent& event) {
    if(Running())snapshot_=job_.Read();
    if (Running() && event.CanVeto()) {
        if (!job_.Read().cancellable) {
            closing_=true;inlineMessage_=wxS("正在完成安装，结束后将自动关闭。");UpdateControls();event.Veto();return;
        }
        if (!cancelling_) OnSecondary();
        if (!Running()) { Destroy(); return; }
        closing_ = cancelling_;
        event.Veto(); return;
    }
    timer_.Stop(); animation_.Stop(); job_.Cancel(); job_.Join(); Destroy();
}
