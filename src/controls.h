#pragma once
#include <wx/control.h>
#include <wx/graphics.h>
#include <wx/colour.h>
#include <wx/timer.h>
#include <chrono>

namespace ui {
inline const wxColour Ground("#1b1b20");
inline const wxColour Surface("#101014");
inline const wxColour Ink("#f3f3f6");
inline const wxColour Muted("#a2a2ae");
inline const wxColour Line("#303038");
inline const wxColour Accent("#7d9fff");
inline const wxColour Success("#71dcb0");
inline const wxColour Danger("#ff929a");
wxFont Font(int pixels, bool bold = false);
struct Label {
    wxString text;
    wxPoint2DDouble position;
    int size = 14;
    wxColour colour = Ink;
    bool bold = false;
};
void Text(wxGraphicsContext& graphics, const Label& label);
void Rounded(wxGraphicsContext& graphics, const wxRect2DDouble& rect, const wxColour& fill);
void CenterText(wxGraphicsContext& graphics, const Label& label, double width);
enum class ButtonStyle { Primary, Secondary, Ghost };

class Button : public wxControl {
public:
    Button(wxWindow* parent, const wxString& label, ButtonStyle style = ButtonStyle::Ghost);
    void Activate();
    bool Enable(bool enable = true) override;
private:
    void Paint(wxPaintEvent& event);
    void Animate(wxTimerEvent& event);
    void Motion();
    ButtonStyle style_;
    wxTimer animation_;
    std::chrono::steady_clock::time_point lastFrame_;
    double hoverAmount_ = 0;
    bool hover_ = false;
    bool pressed_ = false;
};
}
