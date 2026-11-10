#include "installer.h"
#include <algorithm>
#include <iomanip>
#include <sstream>
#include <stdexcept>
namespace setup {
std::string StageName(unsigned index) {
    static const char* names[] = {"检查系统与安装位置", "查询 Fusion 最新稳定版", "下载安装包",
        "校验安装包与签名", "下载预置环境", "安装应用", "补齐并检查环境", "确认安装结果"};
    return index < StageCount ? names[index] : "准备安装";
}
int StageIndex(Phase phase) {
    return phase >= Phase::Checking && phase <= Phase::Finalizing ?
        static_cast<int>(phase) - static_cast<int>(Phase::Checking) : -1;
}
double OverallProgress(const Snapshot& state) {
    if (state.phase == Phase::Complete) return 1;
    if (!state.phaseTotal) return 0;
    return std::min(1.0, state.phaseDone / double(state.phaseTotal));
}
void CheckCancel(const InstallJob& job) {
    if (job.IsCancelled()) throw std::runtime_error("操作已取消。");
}
void BeginStage(FileWork& work, Phase phase) {
    if (work.progress.cancellable) CheckCancel(work.job);
    work.progress.phase = phase;
    work.progress.stage = static_cast<unsigned>(StageIndex(phase));
    work.progress.completedStages = work.progress.stage;
    work.progress.phaseDone = 0; work.progress.phaseTotal = 0;
    work.progress.cancellable = phase < Phase::Installing;
    const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - work.started).count();
    work.progress.stageStartedSeconds = elapsed;
    std::ostringstream event;
    event << std::fixed << std::setprecision(2) << elapsed << " s  " << StageName(work.progress.stage);
    work.progress.events.push_back(event.str());
    work.progress.detail = StageName(work.progress.stage);
    work.job.Publish(work.progress);
}
void Advance(FileWork& work, std::uint64_t count) {
    work.progress.phaseDone += count;
    work.progress.elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - work.started).count();
    work.job.Publish(work.progress);
}
}
