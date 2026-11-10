#include "controls.h"
#include <wx/access.h>
#include <wx/dcbuffer.h>
#include <wx/settings.h>
#include <wx/fontenum.h>
#include <memory>
#include <algorithm>
#include <cmath>

namespace ui {
wxFont Font(int pixels, bool bold) {
    auto font = wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT);
#ifdef _WIN32
    font.SetFaceName("Microsoft YaHei UI");
#elif defined(__APPLE__)
    font.SetFaceName("PingFang SC");
#else
    if (wxFontEnumerator::IsValidFacename("Noto Sans CJK SC")) font.SetFaceName("Noto Sans CJK SC");
#endif
    font.SetPixelSize(wxSize(0, pixels));
    // wxWidgets 的 GDI+ 后端只将 700 映射为粗体，600 会退回常规字重。
    font.SetWeight(bold ? wxFONTWEIGHT_BOLD : wxFONTWEIGHT_NORMAL);
    return font;
}
void Text(wxGraphicsContext& graphics, const Label& label) {
    graphics.SetFont(Font(label.size, label.bold), label.colour);
    graphics.DrawText(label.text, label.position.m_x, label.position.m_y);
}
void Rounded(wxGraphicsContext& graphics, const wxRect2DDouble& rect, const wxColour& fill) {
    graphics.SetPen(*wxTRANSPARENT_PEN);
    graphics.SetBrush(wxBrush(fill));
    const auto radius = std::min(8.0, std::min(rect.m_width, rect.m_height) / 2);
    graphics.DrawRoundedRectangle(rect.m_x, rect.m_y, rect.m_width, rect.m_height, radius);
}
void CenterText(wxGraphicsContext& graphics, const Label& label, double width) {
    graphics.SetFont(Font(label.size, label.bold), label.colour);
    wxDouble textWidth, textHeight;
    graphics.GetTextExtent(label.text, &textWidth, &textHeight);
    graphics.DrawText(label.text, (width - textWidth) / 2, label.position.m_y);
}

#if wxUSE_ACCESSIBILITY
class ButtonAccess : public wxAccessible {
public:
    explicit ButtonAccess(Button* owner) : wxAccessible(owner), owner_(owner) {}
    wxAccStatus GetName(int, wxString* name) override { *name = owner_->GetLabel(); return wxACC_OK; }
    wxAccStatus GetRole(int, wxAccRole* role) override { *role = wxROLE_SYSTEM_PUSHBUTTON; return wxACC_OK; }
    wxAccStatus GetDefaultAction(int, wxString* action) override { *action = wxS("按下"); return wxACC_OK; }
    wxAccStatus DoDefaultAction(int) override { owner_->Activate(); return wxACC_OK; }
    wxAccStatus GetState(int, long* state) override {
        *state = wxACC_STATE_SYSTEM_FOCUSABLE;
        if (!owner_->IsEnabled()) *state |= wxACC_STATE_SYSTEM_UNAVAILABLE;
        if (owner_->HasFocus()) *state |= wxACC_STATE_SYSTEM_FOCUSED;
        return wxACC_OK;
    }
private:
    Button* owner_;
};
#endif

Button::Button(wxWindow* parent, const wxString& label, ButtonStyle style)
    : wxControl(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE), style_(style), animation_(this) {
    SetLabel(label);
    SetName(label);
    SetBackgroundColour(Surface);
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetCursor(wxCURSOR_HAND);
    Bind(wxEVT_PAINT, &Button::Paint, this);
    Bind(wxEVT_TIMER, &Button::Animate, this, animation_.GetId());
    Bind(wxEVT_ENTER_WINDOW, [this](wxMouseEvent&) { hover_ = true; Motion(); });
    Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent&) { hover_ = false; Motion(); });
    Bind(wxEVT_SET_FOCUS, [this](wxFocusEvent& e) { Refresh(); e.Skip(); });
    Bind(wxEVT_KILL_FOCUS, [this](wxFocusEvent& e) { Refresh(); e.Skip(); });
    Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent&) {
        if (!IsEnabled()) return;
        pressed_ = true; SetFocus(); CaptureMouse(); Refresh();
    });
    Bind(wxEVT_LEFT_UP, [this](wxMouseEvent& e) {
        const bool activate = pressed_ && GetClientRect().Contains(e.GetPosition());
        pressed_ = false;
        if (HasCapture()) ReleaseMouse();
        Refresh();
        if (activate) Activate();
    });
    Bind(wxEVT_MOUSE_CAPTURE_LOST, [this](wxMouseCaptureLostEvent&) { pressed_ = false; Refresh(); });
    Bind(wxEVT_KEY_DOWN, [this](wxKeyEvent& e) {
        if (e.GetKeyCode() == WXK_SPACE || e.GetKeyCode() == WXK_RETURN) Activate();
        else e.Skip();
    });
#if wxUSE_ACCESSIBILITY
    SetAccessible(new ButtonAccess(this));
#endif
}

void Button::Activate() {
    if (!IsEnabled()) return;
    wxCommandEvent event(wxEVT_BUTTON, GetId());
    event.SetEventObject(this);
    ProcessWindowEvent(event);
}
bool Button::Enable(bool enabled) {
    const bool changed = wxControl::Enable(enabled);
    Refresh();
    return changed;
}
void Button::Paint(wxPaintEvent&) {
    wxAutoBufferedPaintDC dc(this);
    const auto background=GetBackgroundColour();
    dc.SetBackground(wxBrush(background)); dc.Clear();
    std::unique_ptr<wxGraphicsContext> graphics(wxGraphicsContext::Create(dc));
    if (!graphics) return;
    const auto size = GetClientSize();
    const bool primary = style_ == ButtonStyle::Primary;
    const int lift=static_cast<int>(hoverAmount_*16);
    const wxColour fill = !IsEnabled() ? wxColour("#27272d") : primary ? wxColour(49+lift,91+lift,239) :
        wxColour(std::min(255,background.Red()+lift),std::min(255,background.Green()+lift),std::min(255,background.Blue()+lift));
    Rounded(*graphics, wxRect2DDouble(1, 1, size.x - 2, size.y - 2), fill);
    const wxColour ink = !IsEnabled() ? Muted : primary ? Ink : Muted;
    graphics->SetFont(Font(FromDIP(primary ? 15 : 13), primary), ink);
    wxDouble width, height;
    graphics->GetTextExtent(GetLabel(), &width, &height);
    graphics->DrawText(GetLabel(), (size.x - width) / 2, (size.y - height) / 2);
    if (HasFocus()) {
        graphics->SetPen(wxPen(primary ? wxColour("#a0bcff") : Accent, FromDIP(1)));
        graphics->SetBrush(*wxTRANSPARENT_BRUSH);
        graphics->DrawRoundedRectangle(3, 3, size.x - 6, size.y - 6, FromDIP(6));
    }
}
void Button::Motion(){lastFrame_=std::chrono::steady_clock::now();if(!animation_.IsRunning())animation_.Start(16);}
void Button::Animate(wxTimerEvent&){
    const auto now=std::chrono::steady_clock::now();
    const double dt=std::min(0.1,std::chrono::duration<double>(now-lastFrame_).count());lastFrame_=now;
    const double target=hover_?1:0;hoverAmount_+=(target-hoverAmount_)*(1-std::exp(-18*dt));
    if(std::abs(target-hoverAmount_)<0.01){hoverAmount_=target;animation_.Stop();}
    if(!IsShownOnScreen()){animation_.Stop();return;}Refresh(false);
}
}
