#include "window.h"
#include "diagnostics.h"
#include <wx/brush.h>
#include <wx/pen.h>
namespace {constexpr double Left=520;constexpr double Right=904;}
void SetupWindow::PaintShell(wxGraphicsContext& g){
    g.SetPen(wxPen(ui::Line,1));g.SetBrush(*wxTRANSPARENT_BRUSH);g.DrawRectangle(0.5,0.5,Width()-1,Height()-1);
    g.DrawBitmap(logo_,32,25,27,27);
    ui::Text(g,{wxS("原助理"),{70,24},18,ui::Ink,true});
    ui::Text(g,{wxS("安装向导"),{145,29},11,ui::Muted});
    g.StrokeLine(32,578,Width()-32,578);
    g.SetPen(*wxTRANSPARENT_PEN);g.SetBrush(wxBrush(ui::Accent));g.DrawEllipse(691,607,5,5);
    ui::Text(g,{wxS("Fusion 官方分发  /  稳定通道"),{706,599},11,ui::Muted});
}
void SetupWindow::PaintMark(wxGraphicsContext&){ }
void SetupWindow::PaintContent(wxGraphicsContext& g){
    if(snapshot_.phase==setup::Phase::Ready){if(configuring_)PaintConfigure(g);else PaintReady(g);}
    else if(Running())PaintProgress(g);else PaintResult(g);
}
void SetupWindow::PaintReady(wxGraphicsContext& g){
    ui::Text(g,{wxS("准备好，开启新可能"),{Left,123},12,ui::Accent,true});
    ui::Text(g,{wxS("让想法"),{Left,163},42,ui::Ink,true});
    ui::Text(g,{wxS("即刻发生。"),{Left,218},42,wxColour("#b2ccff"),true});
    ui::Text(g,{wxS("把原助理带到桌面，让 AI 与你一起完成。"),{Left,293},13,ui::Muted});
    ui::Text(g,{wxS("下载稳定版，自动准备所需环境。"),{Left,321},13,ui::Muted});
    g.SetPen(wxPen(ui::Line,1));g.StrokeLine(Left,355,Right,355);
    ui::Text(g,{wxS("支持升级、重装与版本回退"),{Left,502},11,ui::Muted});
    ui::Text(g,{wxS("安装前可以选择位置"),{Left,527},11,ui::Muted});
}
void SetupWindow::PaintConfigure(wxGraphicsContext& g){
    ui::Text(g,{wxS("安装设置"),{Left,111},12,ui::Accent,true});
    ui::Text(g,{wxS("给原助理一个位置"),{Left,145},26,ui::Ink,true});
    ui::Text(g,{wxS("应用与环境会在后台准备。"),{Left,191},13,ui::Muted});
    ui::Text(g,{wxS("安装位置"),{Left,224},12,ui::Ink,true});
    ui::Rounded(g,{Left,250,Right-Left,48},ui::Ground);
    g.SetPen(wxPen(wxColour("#3e424e"),1));g.StrokeLine(793,263,793,285);
    ui::Text(g,{wxS("沿用已有目录，可升级、重装或回退。"),{Left,313},11,ui::Muted});
    g.SetPen(wxPen(ui::Line,1));g.StrokeLine(Left,353,Right,353);
    if (configuring_ && updateRequest_) {
        ui::Text(g,{wxS("正在更新原助理"),{Left,373},14,ui::Ink,true});
        ui::Text(g,{wxString::Format(wxS("目标版本 %s  ·  %s 通道"),wxString::FromUTF8(updateRequest_->release.version),wxString::FromUTF8(updateRequest_->channel)),{Left,402},12,ui::Muted});
        ui::Text(g,{wxS("复用已校验更新包；环境按组件核对，只补齐缺项。"),{Left,429},12,ui::Muted});
    } else {
        ui::Text(g,{wxS("最新稳定版"),{Left,373},14,ui::Ink,true});
        ui::Text(g,{wxS("每次从 Fusion 实时获取匹配的安装包。"),{Left,402},12,ui::Muted});
        ui::Text(g,{wxS("下载校验后，由正式安装器完成环境配置。"),{Left,429},12,ui::Muted});
    }
    ui::Text(g,{wxS("建议预留至少 2 GiB 可用空间。"),{Left,469},11,ui::Muted});
}
wxString SetupWindow::PhaseTitle()const{
    if(Running())return wxString::FromUTF8(setup::StageName(snapshot_.stage));
    switch(snapshot_.phase){
    case setup::Phase::Complete:return wxS("一切就绪，开始使用。");
    case setup::Phase::Cancelled:return wxS("已取消本次安装");
    case setup::Phase::Failed:
        if(setup::diagnostics::Lower(snapshot_.detail).find("需要重启")!=std::string::npos)
            return wxS("重启电脑后即可继续。");
        return wxS("安装需要处理一下");
    default:return wxS("准备安装");}
}
void SetupWindow::PaintResult(wxGraphicsContext& g){
    const bool success=snapshot_.phase==setup::Phase::Complete;
    const bool cancelled=snapshot_.phase==setup::Phase::Cancelled;
    const bool restart=setup::diagnostics::Lower(snapshot_.detail).find("需要重启")!=std::string::npos;
    const auto colour=success?ui::Success:cancelled?ui::Muted:
        restart?ui::Accent:ui::Danger;
    const auto status = restart ? wxString(wxS("需要重启")) :
        success ? wxString(wxS("安装完成")) :
        cancelled ? wxString(wxS("已取消")) : wxString(wxS("暂未完成"));
    ui::Text(g,{status,{Left,111},12,colour,true});
    ui::Text(g,{PhaseTitle(),{Left,147},26,ui::Ink,true});
    const auto subtitle=success?wxS("原助理已经准备好，开始你的下一件事。"):
        cancelled?wxS("下载已停止，可以稍后重新开始。 "):
        restart?wxS("系统组件已准备好，重启电脑后再次运行即可继续。"):
        wxS("查看下面的原因，然后返回设置重试。");
    ui::Text(g,{subtitle,{Left,195},12,ui::Muted});
    ui::Rounded(g,{Left,242,Right-Left,218},ui::Ground);
    ui::Text(g,{success?wxS("原助理 ")+wxString::FromUTF8(snapshot_.version)+wxS(" · 稳定版"):
        wxString::FromUTF8(setup::StageName(snapshot_.stage)),{538,260},13,ui::Ink,true});
    ui::Text(g,{wxString::Format(wxS("已完成 %u / 8 步  ·  用时 %.1f 秒"),snapshot_.completedStages,snapshot_.elapsed),
        {538,293},11,ui::Muted});
    auto reason=wxString::FromUTF8(success||cancelled?snapshot_.detail:setup::diagnostics::Reason(snapshot_));
    reason.Replace("\r", " "); reason.Replace("\n", " ");
    if(reason.length()>81)reason=reason.Left(78)+wxS("…");
    for(std::size_t line=0;line<3&&line*27<reason.length();++line)
        ui::Text(g,{reason.Mid(line*27,27),{538,322+line*21.0},12,colour});
    if (!success && !cancelled) {
        const auto advice=wxString::FromUTF8(setup::diagnostics::Advice(snapshot_));
        for(std::size_t line=0;line<3&&line*27<advice.length();++line)
            ui::Text(g,{advice.Mid(line*27,27),{538,392+line*19.0},11,ui::Muted});
        return;
    }
    auto path=wxString::FromUTF8(snapshot_.destination.u8string());
    if(path.length()>45)path=path.Left(15)+wxS("…")+path.Right(28);
    ui::Text(g,{path,{538,425},10,ui::Muted});
    ui::Text(g,{success?wxS("点击下方按钮，启动原助理。"):wxS("修复上述问题后，可以返回设置重试。"),{Left,480},11,ui::Muted});
}
