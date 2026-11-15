#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <uxtheme.h>
#endif
#include "window.h"
#include "release.h"
#include "logo_data.h"
#include "diagnostics.h"
#include <wx/dcbuffer.h>
#include <wx/filename.h>
#include <wx/icon.h>
#include <wx/mstream.h>
#include <wx/sizer.h>
#include <wx/utils.h>
#include <memory>
#include <array>
#include <utility>

namespace {
constexpr int InitialWidth = 960;
constexpr int InitialHeight = 640;
}
SetupWindow::SetupWindow(std::optional<setup::UpdateRequest> request)
    : wxFrame(nullptr, wxID_ANY, wxS("原助理 · 安装向导"), wxDefaultPosition, wxDefaultSize,
        wxBORDER_NONE | wxSYSTEM_MENU | wxMINIMIZE_BOX), timer_(this), animation_(this),
        updateRequest_(std::move(request)), updateMode_(updateRequest_.has_value()),
        silentUpdate_(updateRequest_ && updateRequest_->silent) {
    SetClientSize(FromDIP(wxSize(InitialWidth, InitialHeight)));
    canvas_ = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL);
    canvas_->SetBackgroundStyle(wxBG_STYLE_PAINT);
    auto* layout = new wxBoxSizer(wxVERTICAL);
    layout->Add(canvas_, 1, wxEXPAND);
    SetSizer(layout); Layout();
    canvas_->SetName(wxS("原助理安装向导"));
    wxMemoryInputStream stream(LogoData, sizeof(LogoData));
    wxImage brand(stream, wxBITMAP_TYPE_BMP);
    brand.InitAlpha();
    for (int y=0;y<brand.GetHeight();++y) for(int x=0;x<brand.GetWidth();++x)
        if(brand.GetRed(x,y)<22 && brand.GetGreen(x,y)<22 && brand.GetBlue(x,y)<28) brand.SetAlpha(x,y,0);
    logo_ = wxBitmap(brand);
    wxIcon icon; icon.CopyFromBitmap(logo_); SetIcon(icon);
    system_ = wxGetOsDescription() + " / " + wxGetNativeCpuArchitectureName();
    CreateControls();
    BindEvents();
    BindChrome();
    LayoutControls();
    if (updateRequest_) path_->SetValue(wxString::FromUTF8(updateRequest_->destination.u8string()));
    if (silentUpdate_) path_->Hide();
    path_->SetInsertionPoint(0);
    Centre(); primary_->SetFocus();
    StartMotion();
    if (silentUpdate_) {
        CallAfter([this] { StartInstall(); });
    } else if (updateRequest_) {
        configuring_ = true;
        CallAfter([this] { StartInstall(); });
    }
}
void SetupWindow::CreateControls() {
    path_ = new wxTextCtrl(canvas_, wxID_ANY, wxString::FromUTF8(setup::DefaultInstallRoot().u8string()),
        wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
    path_->SetName(wxS("安装位置"));
    path_->SetFont(ui::Font(FromDIP(13)));
    path_->SetBackgroundColour(ui::Ground); path_->SetForegroundColour(ui::Ink);
    browse_ = new ui::Button(canvas_, wxS("更改位置"));
    browse_->SetBackgroundColour(ui::Ground);
    primary_ = new ui::Button(canvas_, wxS("开始设置"), ui::ButtonStyle::Primary);
    secondary_ = new ui::Button(canvas_, wxS("退出"));
    details_ = new ui::Button(canvas_, wxS("查看详情"));
    logs_ = new ui::Button(canvas_, wxS("打开日志"));
    copy_ = new ui::Button(canvas_, wxS("复制诊断信息"));
    diagnosticText_ = new wxTextCtrl(canvas_, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
        wxTE_MULTILINE | wxTE_READONLY | wxTE_DONTWRAP | wxBORDER_NONE);
    diagnosticText_->SetName(wxS("本次安装诊断详情，可滚动并选择文本"));
    diagnosticText_->SetFont(ui::Font(FromDIP(12)));
#ifdef _WIN32
    SetWindowTheme(reinterpret_cast<HWND>(diagnosticText_->GetHandle()), L"", L"");
#endif
    diagnosticText_->SetBackgroundColour(ui::Ground); diagnosticText_->SetForegroundColour(ui::Ink);
    diagnosticText_->Hide();
}
void SetupWindow::BindEvents() {
    primary_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { OnPrimary(); });
    secondary_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { OnSecondary(); });
    browse_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Browse(); });
    details_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { ToggleDiagnostics(); });
    logs_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { OpenLog(); });
    copy_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { CopyDiagnostics(); });
    const std::array<wxWindow*,4> motionControls{path_,browse_,primary_,secondary_};
    for(auto* control:motionControls)
        control->Bind(wxEVT_MOTION,[this](wxMouseEvent& event){WakeMotion();event.Skip();});
    canvas_->Bind(wxEVT_PAINT, &SetupWindow::Paint, this);
    canvas_->Bind(wxEVT_SIZE, [this](wxSizeEvent& event) { LayoutControls(); canvas_->Refresh(); event.Skip(); });
    Bind(wxEVT_TIMER, &SetupWindow::Tick, this, timer_.GetId());
    Bind(wxEVT_TIMER, &SetupWindow::Animate, this, animation_.GetId());
    Bind(wxEVT_CLOSE_WINDOW, &SetupWindow::CloseWindow, this);
    Bind(wxEVT_CHAR_HOOK, [this](wxKeyEvent& event) {
        if (event.GetKeyCode() == WXK_ESCAPE) OnSecondary(); else event.Skip();
    });
}

double SetupWindow::Width() const { return canvas_->GetClientSize().x / (FromDIP(100) / 100.0); }
double SetupWindow::Height() const { return canvas_->GetClientSize().y / (FromDIP(100) / 100.0); }
bool SetupWindow::Running() const { return setup::StageIndex(snapshot_.phase) >= 0; }
void SetupWindow::LayoutControls() {
    if (!path_) return;
    const int width = static_cast<int>(Width());
    const bool welcome = snapshot_.phase == setup::Phase::Ready && !configuring_;
    auto place = [this](wxWindow* window, const wxRect& rect) {
        window->SetSize(wxRect(FromDIP(rect.GetPosition()), FromDIP(rect.GetSize())));
    };
    place(path_, wxRect(536, 261, 247, 28));
    place(browse_, wxRect(800, 254, 98, 40));
    place(details_, wxRect(520, 468, 116, 34));
    place(logs_, wxRect(640, 468, 116, 34));
    place(copy_, wxRect(760, 468, 144, 34));
    place(diagnosticText_, wxRect(532, 252, 360, 196));
    place(primary_, welcome ? wxRect(520, 386, 384, 50) : wxRect(702, 519, 202, 48));
    place(secondary_, welcome ? wxRect(520, 448, 384, 34) : Running() ?
        wxRect(758, 519, 146, 48) : wxRect(520, 519, 158, 48));
    if (close_) place(close_, wxRect(width - 48, 12, 32, 32));
    if (minimize_) place(minimize_, wxRect(width - 84, 12, 32, 32));
}
void SetupWindow::Paint(wxPaintEvent&) {
    wxAutoBufferedPaintDC dc(canvas_);
    if (!backdrop_.IsOk()) BuildBackdrop();
    if(!card_.IsOk())BuildArtwork();
    if(pageDirty_||!page_.IsOk())BuildStaticPage();
    dc.DrawBitmap(page_,0,0,false);
    std::unique_ptr<wxGraphicsContext> graphics(wxGraphicsContext::Create(dc));
    if (!graphics) return;
    const double scale = FromDIP(100) / 100.0;
    graphics->Scale(scale, scale);
    PaintArtwork(*graphics);
    if(Running())PaintTips(*graphics);
    if(!Running()&&transition_>=1)return;
    graphics->PushState();
    graphics->Clip(500, 90, 430, 483);
    const auto offset = (1.0 - transition_) * 24;
    graphics->Translate(offset, 0);
    graphics->BeginLayer(transition_);
    PaintContent(*graphics);
    graphics->EndLayer();
    graphics->PopState();
    if(!inlineMessage_.empty())ui::Text(*graphics,{inlineMessage_,{520,490},11,ui::Accent});
}
void SetupWindow::UpdateControls() {
    pageDirty_=true;
    const bool ready = snapshot_.phase == setup::Phase::Ready;
    const bool failed = snapshot_.phase == setup::Phase::Failed;
    details_->Show(failed); logs_->Show(failed); copy_->Show(failed);
    logs_->Enable(failed && !snapshot_.logPath.empty());
    if (!failed) diagnosticText_->Hide();
    if (silentUpdate_) {
        details_->Hide(); logs_->Hide(); copy_->Hide(); diagnosticText_->Hide();
        path_->Hide(); browse_->Hide();
        primary_->Hide(); secondary_->Hide();
        close_->Hide(); minimize_->Hide();
    }
    details_->SetLabel(diagnosticText_->IsShown() ? wxS("收起详情") : wxS("查看详情"));
    logs_->SetLabel(wxS("打开日志"));
    copy_->SetLabel(wxS("复制诊断信息"));
    path_->Show(ready && configuring_); browse_->Show(ready && configuring_);
    path_->Enable(!updateMode_);browse_->Enable(!updateMode_);
    primary_->Show(!Running()); primary_->Enable(!Running());
    secondary_->Enable(!cancelling_ && (!Running() || snapshot_.cancellable));
    primary_->SetLabel(snapshot_.phase == setup::Phase::Complete ? wxS("启动原助理") : ready ?
        (configuring_ ? wxS("下载并安装  →") : wxS("开始安装  →")) : wxS("返回设置并重试"));
    secondary_->SetLabel(Running() ? (!snapshot_.cancellable ? wxS("正在完成安装") : cancelling_ ? wxS("取消中…") : wxS("取消安装")) :
        snapshot_.phase == setup::Phase::Complete ? wxS("完成") : ready && configuring_ ? wxS("上一步") : wxS("退出"));
    LayoutControls();
    primary_->Refresh(); secondary_->Refresh(); canvas_->Refresh();
}
void SetupWindow::BindChrome() {
    close_ = new ui::Button(canvas_, wxS("×")); close_->SetName(wxS("关闭"));
    minimize_ = new ui::Button(canvas_, wxS("—")); minimize_->SetName(wxS("最小化"));
    close_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Close(); });
    minimize_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Iconize(); });
    canvas_->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent& event) {
        if (event.GetY() > FromDIP(60)) return;
        dragging_ = true; dragOffset_ = wxGetMousePosition() - GetPosition(); canvas_->CaptureMouse();
    });
    canvas_->Bind(wxEVT_MOTION, [this](wxMouseEvent& event) {
        WakeMotion();
        if (dragging_ && event.LeftIsDown()) Move(wxGetMousePosition() - dragOffset_);
    });
    canvas_->Bind(wxEVT_LEFT_UP, [this](wxMouseEvent&) {
        dragging_ = false; if (canvas_->HasCapture()) canvas_->ReleaseMouse();
    });
    canvas_->Bind(wxEVT_MOUSE_CAPTURE_LOST, [this](wxMouseCaptureLostEvent&) { dragging_ = false; });
    UpdateControls();
}
