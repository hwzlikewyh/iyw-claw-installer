#pragma once
#include "controls.h"
#include "installer.h"
#include <wx/frame.h>
#include <wx/panel.h>
#include <wx/textctrl.h>
#include <wx/timer.h>
#include <chrono>

class SetupWindow : public wxFrame {
public:
    SetupWindow();
private:
    void Paint(wxPaintEvent& event);
    void PaintShell(wxGraphicsContext& graphics);
    void PaintContent(wxGraphicsContext& graphics);
    void PaintReady(wxGraphicsContext& graphics);
    void PaintConfigure(wxGraphicsContext& graphics);
    void PaintProgress(wxGraphicsContext& graphics);
    void PaintStageList(wxGraphicsContext& graphics);
    void PaintResult(wxGraphicsContext& graphics);
    void PaintMark(wxGraphicsContext& graphics);
    void PaintArtwork(wxGraphicsContext& graphics);
    void PaintRoute(wxGraphicsContext& graphics);
    void PaintTips(wxGraphicsContext& graphics);
    void PaintHalo(wxGraphicsContext& graphics, bool front);
    void BuildBackdrop();
    void BuildArtwork();
    void BuildStaticPage();
    void StartMotion();
    void Animate(wxTimerEvent& event);
    void BeginTransition();
    void WakeMotion();
    void UpdatePointer(double elapsed);
    void BindChrome();
    void CreateControls();
    void BindEvents();
    void LayoutControls();
    void StartInstall();
    void OnPrimary();
    void OnSecondary();
    void Tick(wxTimerEvent& event);
    void CloseWindow(wxCloseEvent& event);
    void Browse();
    void UpdateControls();
    void ToggleDiagnostics();
    void OpenLog();
    void CopyDiagnostics();
    bool Running() const;
    double Width() const;
    double Height() const;
    wxString PhaseTitle() const;
    wxPanel* canvas_ = nullptr;
    wxTextCtrl* path_ = nullptr;
    ui::Button* browse_ = nullptr;
    ui::Button* primary_ = nullptr;
    ui::Button* secondary_ = nullptr;
    ui::Button* details_ = nullptr;
    ui::Button* logs_ = nullptr;
    ui::Button* copy_ = nullptr;
    wxTextCtrl* diagnosticText_ = nullptr;
    ui::Button* close_ = nullptr;
    ui::Button* minimize_ = nullptr;
    wxBitmap logo_;
    wxBitmap backdrop_;
    wxBitmap page_;
    wxBitmap orbit_;
    wxBitmap card_;
    wxGraphicsBitmap orbitGraphic_;
    wxGraphicsBitmap cardGraphic_;
    bool pageDirty_ = true;
    wxTimer timer_;
    wxTimer animation_;
    setup::InstallJob job_;
    setup::Snapshot snapshot_;
    std::chrono::steady_clock::time_point started_;
    double liveSeconds_ = 0;
    bool closing_ = false;
    bool cancelling_ = false;
    bool dragging_ = false;
    bool configuring_ = false;
    wxString system_;
    wxString inlineMessage_;
    wxPoint dragOffset_;
    std::chrono::steady_clock::time_point lastFrame_;
    std::chrono::steady_clock::time_point transitionClock_;
    std::chrono::steady_clock::time_point lastActivity_;
    double motionSeconds_ = 0;
    double tipStartedSeconds_ = 0;
    double pointerX_ = 0;
    double pointerY_ = 0;
    double visibleProgress_ = 0;
    double transition_ = 1;
    bool animationsEnabled_ = true;
};
