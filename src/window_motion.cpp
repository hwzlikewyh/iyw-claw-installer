#include "window.h"
#include <algorithm>
#include <cmath>
#include <wx/dcmemory.h>
#include <wx/utils.h>
#ifdef _WIN32
#include <windows.h>
#endif
namespace {
constexpr int FrameMs=16;
constexpr double TransitionSeconds=0.34;
constexpr double PointerSmoothing=9;
constexpr double ProgressSmoothing=7;
constexpr double MaxFrameStep=0.1;
constexpr double Pi=3.141592653589793;
}
void SetupWindow::StartMotion(){
    lastFrame_=lastActivity_=std::chrono::steady_clock::now();
#ifdef _WIN32
    BOOL enabled=TRUE;
    SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION,0,&enabled,0);
    animationsEnabled_=enabled!=FALSE;
#endif
    Bind(wxEVT_ICONIZE,[this](wxIconizeEvent& event){
        if(event.IsIconized())animation_.Stop();
        else {lastFrame_=lastActivity_=std::chrono::steady_clock::now();if(animationsEnabled_)animation_.Start(FrameMs);canvas_->Refresh();}
        event.Skip();
    });
    if(animationsEnabled_)animation_.Start(FrameMs);
}
void SetupWindow::WakeMotion(){
    lastActivity_=std::chrono::steady_clock::now();
    if(animationsEnabled_&&!animation_.IsRunning()&&!IsIconized()){
        lastFrame_=lastActivity_;animation_.Start(FrameMs);
    }
}
void SetupWindow::BeginTransition(){
    transitionClock_=std::chrono::steady_clock::now();transition_=animationsEnabled_?0:1;
    visibleProgress_=0;
    pageDirty_=true;
    WakeMotion();
}
void SetupWindow::UpdatePointer(double elapsed){
    const auto position=canvas_->ScreenToClient(wxGetMousePosition());
    const bool inside=canvas_->GetClientRect().Contains(position);
    const double scale=FromDIP(100)/100.0;
    const double x=inside?(position.x/scale-240)/240:0;
    const double y=inside?(position.y/scale-300)/300:0;
    const double weight=1-std::exp(-PointerSmoothing*elapsed);
    pointerX_+=(std::clamp(x,-1.0,1.0)-pointerX_)*weight;
    pointerY_+=(std::clamp(y,-1.0,1.0)-pointerY_)*weight;
}
void SetupWindow::Animate(wxTimerEvent&){
    if(IsIconized()||!IsShownOnScreen())return;
    const auto now=std::chrono::steady_clock::now();
    if(!Running()&&transition_>=1&&now-lastActivity_>std::chrono::seconds(8)){
        animation_.Stop();return;
    }
    const double dt=std::clamp(std::chrono::duration<double>(now-lastFrame_).count(),0.0,MaxFrameStep);
    lastFrame_=now;motionSeconds_+=dt;
    UpdatePointer(dt);
    if(transition_<1){
        const double t=std::clamp(std::chrono::duration<double>(now-transitionClock_).count()/TransitionSeconds,0.0,1.0);
        transition_=1-std::pow(1-t,3);
        if(t>=1){transition_=1;pageDirty_=true;}
    }
    const double target=setup::OverallProgress(snapshot_);
    visibleProgress_+=(target-visibleProgress_)*(1-std::exp(-ProgressSmoothing*dt));
    const auto art=wxRect(FromDIP(wxPoint(20,160)),FromDIP(wxSize(450,320)));
    canvas_->RefreshRect(art,false);
    if(Running())canvas_->RefreshRect(wxRect(FromDIP(wxPoint(48,468)),FromDIP(wxSize(412,86))),false);
    // 位移动画会超出内容原始边界；整列重绘才能清除右侧遗留像素。
    if(Running()||transition_<1||pageDirty_)
        canvas_->RefreshRect(wxRect(FromDIP(wxPoint(480,88)),FromDIP(wxSize(476,486))),false);
}
void SetupWindow::BuildBackdrop(){
    const auto size=canvas_->GetClientSize();if(size.x<1||size.y<1)return;
    wxImage image(size.x,size.y);auto* bytes=image.GetData();
    const double scale=FromDIP(100)/100.0;
    for(int y=0;y<size.y;++y){
        for(int x=0;x<size.x;++x){
            const double px=x/scale,py=y/scale;
            const double blue=std::exp(-std::pow((px-230)/190,2)-std::pow((py-325)/170,2));
            const double cyan=std::exp(-std::pow((px-100)/130,2)-std::pow((py-370)/100,2));
            const double top=std::exp(-std::pow((px-450)/350,2)-std::pow((py-60)/90,2));
            const double grain=std::sin(x*12.9898+y*78.233)*0.6;
            const auto index=(y*size.x+x)*3;
            bytes[index]=static_cast<unsigned char>(std::clamp(12+blue*10+top*4+grain,0.0,255.0));
            bytes[index+1]=static_cast<unsigned char>(std::clamp(13+blue*20+cyan*12+top*5+grain,0.0,255.0));
            bytes[index+2]=static_cast<unsigned char>(std::clamp(18+blue*63+cyan*23+top*13+grain,0.0,255.0));
        }
    }
    backdrop_=wxBitmap(image);
}
