#include "window.h"
#include <algorithm>
#include <cmath>
#include <wx/brush.h>
#include <wx/pen.h>
namespace {constexpr double Left=520;constexpr double TrackWidth=384;}
void SetupWindow::PaintProgress(wxGraphicsContext& g){
    const auto title=cancelling_?wxString(wxS("正在安全停止…")):wxString(BrandText(wxS("正在准备原助理")));
    ui::Text(g,{wxS("安装进行中"),{Left,107},12,ui::Accent,true});
    ui::Text(g,{title,{Left,140},25,ui::Ink,true});
    auto detail=wxString::FromUTF8(snapshot_.detail);
    if(detail.length()>29)detail=detail.Left(28)+wxS("…");
    ui::Text(g,{detail,{Left,184},12,ui::Muted});
    ui::Rounded(g,{Left,222,TrackWidth,6},ui::Line);
    wxString label;
    if(snapshot_.phaseTotal){
        const double progress=animationsEnabled_?visibleProgress_:setup::OverallProgress(snapshot_);
        if(progress>0)ui::Rounded(g,{Left,222,TrackWidth*progress,6},ui::Accent);
        label=wxString::Format(wxS("本阶段 %d%%"),static_cast<int>(progress*100));
    }else{
        constexpr double IndicatorWidth=64;
        const double position=(1-std::cos(motionSeconds_*1.8))*0.5*(TrackWidth-IndicatorWidth);
        ui::Rounded(g,{Left+position,222,IndicatorWidth,6},ui::Accent);
        label=wxString::Format(wxS("步骤 %u / 8"),snapshot_.stage+1);
    }
    ui::Text(g,{label,{Left,242},12,ui::Ink,true});
    ui::Text(g,{wxString::Format(wxS("%.1f 秒"),liveSeconds_),{837,242},11,ui::Muted});
    PaintStageList(g);
    const double seconds=std::max(0.001,liveSeconds_-snapshot_.stageStartedSeconds);
    const double processed=snapshot_.phaseDone/double(setup::MiB);
    const auto amount=snapshot_.phase==setup::Phase::Environment?wxString(wxS("自动复用健康组件，补齐缺失或损坏的组件")):
        snapshot_.phaseTotal?wxString::Format(wxS("%.1f / %.1f MiB   ·   %.1f MiB/s"),
        processed,snapshot_.phaseTotal/double(setup::MiB),processed/seconds):wxString(wxS("正在等待当前操作完成"));
    ui::Text(g,{amount,{Left,485},11,ui::Muted});
}
void SetupWindow::PaintStageList(wxGraphicsContext& g){
    for(unsigned index=0;index<setup::StageCount;++index){
        const double y=269+index*25;
        const bool done=index<snapshot_.completedStages,active=index==snapshot_.stage;
        if(active)ui::Rounded(g,{Left-10,y-3,TrackWidth+10,25},ui::Ground);
        if(active){
            const double pulse=animationsEnabled_?0.55+0.45*std::pow(std::sin(motionSeconds_*2.4),2):1;
            const auto signal=wxColour(ui::Accent.Red(),ui::Accent.Green(),ui::Accent.Blue(),static_cast<unsigned char>(255*pulse));
            ui::Rounded(g,{Left-19,y+6,4,4},signal);
        }
        const auto colour=done?ui::Success:active?ui::Accent:ui::Muted;
        ui::Text(g,{done?wxString(wxS("✓")):wxString::Format("%02u",index+1),{Left,y},12,colour,true});
        ui::Text(g,{wxString::FromUTF8(setup::StageName(index)),{Left+32,y},12,active?ui::Ink:ui::Muted,active});
        const auto status=done?(snapshot_.reused[index]?wxS("复用"):wxS("完成")):active?wxS("进行中"):wxS("等待");
        ui::Text(g,{status,{848,y},11,colour});
    }
}
