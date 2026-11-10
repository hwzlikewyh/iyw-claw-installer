#include "window.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <wx/dcmemory.h>
#include <wx/brush.h>
#include <wx/pen.h>
namespace {
constexpr double Pi=3.141592653589793;
constexpr double Cx=240,Cy=319;
constexpr double ArtLeft=20,ArtTop=175,ArtWidth=440,ArtHeight=280;
constexpr unsigned Segments=90;
constexpr double TipPeriod=4.5,TipTransition=0.6,TipTravel=14;
struct Tip {const wchar_t* title;const wchar_t* detail;};
const std::array<Tip,4> Tips{{
    {wxS("一句话，开始下一件事。"),wxS("把想法说出来，和原助理一起完成。")},
    {wxS("把资料，变成清晰答案。"),wxS("阅读、梳理与总结，让重点更容易看见。")},
    {wxS("从一个念头，到一份作品。"),wxS("写文字、做内容，让灵感继续向前。")},
    {wxS("少一点重复，多一点专注。"),wxS("让 AI 帮你整理步骤，专注真正重要的事。")}
}};
void DrawTip(wxGraphicsContext& g,const Tip& tip,double transition){
    if(std::abs(transition)>=1)return;
    const auto alpha=static_cast<unsigned char>(255*(1-std::abs(transition)));
    const wxColour ink(ui::Ink.Red(),ui::Ink.Green(),ui::Ink.Blue(),alpha);
    const wxColour muted(ui::Muted.Red(),ui::Muted.Green(),ui::Muted.Blue(),alpha);
    g.PushState();g.Translate(0,transition*TipTravel);
    ui::Text(g,{tip.title,{54,493},20,ink,true});
    ui::Text(g,{tip.detail,{54,527},12,muted});
    g.PopState();
}
wxPoint2DDouble Orbit(double angle,double scale=1){
    const double x=193*std::cos(angle)*scale,y=95*std::sin(angle)*scale;
    constexpr double rotation=-0.34;
    return {Cx+x*std::cos(rotation)-y*std::sin(rotation),Cy+x*std::sin(rotation)+y*std::cos(rotation)};
}
}
void SetupWindow::BuildArtwork(){
    orbit_=wxBitmap(440,280,32);wxMemoryDC orbitDC(orbit_);
    orbitDC.SetBackground(wxBrush(ui::Surface));orbitDC.Clear();
    std::unique_ptr<wxGraphicsContext> ring(wxGraphicsContext::Create(orbitDC));
    ring->Translate(-ArtLeft,-ArtTop);PaintHalo(*ring,false);PaintHalo(*ring,true);ring.reset();
    wxImage ringImage=orbit_.ConvertToImage();ringImage.InitAlpha();
    for(int y=0;y<ringImage.GetHeight();++y)for(int x=0;x<ringImage.GetWidth();++x){
        if(ringImage.GetRed(x,y)==ui::Surface.Red()&&ringImage.GetGreen(x,y)==ui::Surface.Green()&&ringImage.GetBlue(x,y)==ui::Surface.Blue())ringImage.SetAlpha(x,y,0);
    }
    orbit_=wxBitmap(ringImage);
    card_=wxBitmap(220,228,32);wxMemoryDC cardDC(card_);
    cardDC.SetBackground(wxBrush(ui::Surface));cardDC.Clear();
    std::unique_ptr<wxGraphicsContext> card(wxGraphicsContext::Create(cardDC));
    card->Translate(102,102);
    for(int depth=18;depth>0;--depth){
        card->SetPen(*wxTRANSPARENT_PEN);card->SetBrush(wxBrush(wxColour(24+depth,46+depth,125+depth*3)));
        card->DrawRoundedRectangle(-91+depth*0.65,-91+depth*0.9,182,182,36);
    }
    card->DrawBitmap(logo_,-92,-92,184,184);card->SetBrush(*wxTRANSPARENT_BRUSH);
    card->SetPen(wxPen(wxColour(162,202,255),1.1));card->DrawRoundedRectangle(-91.5,-91.5,183,183,35);card.reset();
    wxImage image=card_.ConvertToImage();image.InitAlpha();
    for(int y=0;y<image.GetHeight();++y)for(int x=0;x<image.GetWidth();++x)
        if(image.GetRed(x,y)==ui::Surface.Red()&&image.GetGreen(x,y)==ui::Surface.Green()&&image.GetBlue(x,y)==ui::Surface.Blue())image.SetAlpha(x,y,0);
    card_=wxBitmap(image);
}
void SetupWindow::PaintHalo(wxGraphicsContext& g,bool front){
    for(unsigned segment=0;segment<Segments;++segment){
        const double angle=2*Pi*segment/Segments;if((std::sin(angle)>0)!=front)continue;
        const auto a=Orbit(angle),b=Orbit(2*Pi*(segment+1)/Segments);
        const double shine=std::pow((1+std::cos(angle-0.65))/2,4);
        g.SetPen(wxPen(wxColour(35+shine*74,70+shine*136,160+shine*95),front?2.4:1.4));
        g.StrokeLine(a.m_x,a.m_y,b.m_x,b.m_y);
        const auto outerA=Orbit(angle,1.065),outerB=Orbit(2*Pi*(segment+1)/Segments,1.065);
        g.SetPen(wxPen(wxColour(33,49,82),0.6));g.StrokeLine(outerA.m_x,outerA.m_y,outerB.m_x,outerB.m_y);
        if(segment%3==0){g.SetPen(wxPen(wxColour(41,61,105),0.7));g.StrokeLine(a.m_x,a.m_y,outerA.m_x,outerA.m_y);}
    }
}
void SetupWindow::PaintArtwork(wxGraphicsContext& g){
    if(orbitGraphic_.IsNull())orbitGraphic_=g.CreateBitmap(orbit_);
    if(cardGraphic_.IsNull())cardGraphic_=g.CreateBitmap(card_);
    g.PushState();g.Translate(pointerX_*5,pointerY_*4);
    g.DrawBitmap(orbitGraphic_,ArtLeft,ArtTop,ArtWidth,ArtHeight);
    const double bob=std::sin(motionSeconds_*0.9)*5;
    g.PushState();g.Translate(Cx+pointerX_*10,Cy-12+bob+pointerY_*7);
    g.Rotate(-0.13+pointerX_*0.035+std::sin(motionSeconds_*0.55)*0.018);
    g.DrawBitmap(cardGraphic_,-102,-102,220,228);g.PopState();
    const auto dot=Orbit(std::fmod(motionSeconds_*0.55,2*Pi));
    g.SetPen(*wxTRANSPARENT_PEN);g.SetBrush(wxBrush(wxColour(113,190,255)));g.DrawEllipse(dot.m_x-3,dot.m_y-3,6,6);
    g.PopState();
}
void SetupWindow::PaintRoute(wxGraphicsContext& g){
    ui::Text(g,{wxS("灵感，即将上线。"),{54,108},23,ui::Ink,true});
    ui::Text(g,{wxS("原助理 · 与 AI 一起，把想法变成现实"),{54,147},12,ui::Muted});
    if(Running())return;
    const bool complete=snapshot_.phase==setup::Phase::Complete;
    const auto text=complete?wxString(wxS("已就绪 · 开始你的下一件事")):wxString(wxS("你的下一位搭档，正在这里。"));
    ui::Text(g,{text,{54,493},15,ui::Ink,true});
    ui::Text(g,{wxS("原生运行    /    稳定分发    /    自动配置"),{54,526},11,ui::Muted});
}
void SetupWindow::PaintTips(wxGraphicsContext& g){
    // 使用既有动画时钟，最小化时一并暂停；每次安装重新从第一句开始。
    const double elapsed=animationsEnabled_?std::max(0.0,motionSeconds_-tipStartedSeconds_):0;
    const auto index=static_cast<std::size_t>(elapsed/TipPeriod)%Tips.size();
    const double phase=std::fmod(elapsed,TipPeriod);
    const double t=std::clamp((phase-(TipPeriod-TipTransition))/TipTransition,0.0,1.0);
    const auto visible=(index+(t>=0.5?1:0))%Tips.size();
    const double fade=t<0.5?t*2:(t-0.5)*2;
    const double blend=fade*fade*(3-2*fade);
    g.PushState();g.Clip(50,487,408,65);
    DrawTip(g,Tips[visible],t<0.5?-blend:1-blend);
    g.PopState();
    constexpr double MarkerWidth=22,MarkerGap=8;
    for(std::size_t marker=0;marker<Tips.size();++marker){
        const double x=54+marker*(MarkerWidth+MarkerGap);
        ui::Rounded(g,{x,476,MarkerWidth,3},marker==visible?ui::Accent:ui::Line);
    }
}
void SetupWindow::BuildStaticPage(){
    page_=wxBitmap(canvas_->GetClientSize(),32);wxMemoryDC dc(page_);
    dc.SetBackground(wxBrush(ui::Surface));dc.Clear();
    std::unique_ptr<wxGraphicsContext> graphics(wxGraphicsContext::Create(dc));
    const double scale=FromDIP(100)/100.0;graphics->Scale(scale,scale);
    graphics->DrawBitmap(backdrop_,0,0,Width(),Height());PaintShell(*graphics);PaintRoute(*graphics);
    if(!Running()&&transition_>=1)PaintContent(*graphics);
    if(!inlineMessage_.empty())ui::Text(*graphics,{inlineMessage_,{520,490},11,ui::Accent});
    pageDirty_=false;
}
