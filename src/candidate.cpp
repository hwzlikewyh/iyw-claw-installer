#include "release.h"
#include <stdexcept>

namespace setup {
Release ResolveCandidate(const Platform& platform, InstallJob& job) {
    // 同一查询同时允许 beta 和 stable，已晋级的发布仍返回同一 releaseId。
    const auto release=ResolveChannelRelease(platform,job,"beta");
    if(release.releaseId==Brand().releaseId)return release;
    // 候选入口固定发布身份；不把后来创建的测试版静默当成本次候选。
    const auto stable=ResolveStableRelease(platform,job);
    if(stable.releaseId==Brand().releaseId)return stable;
    throw std::runtime_error("此测试候选已变更或不可用，请从后台取得当前安装向导。");
}
}
