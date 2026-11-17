#pragma once
#include "installer.h"

namespace setup {
// 用户显式重试成为新的前台更新；保留所选发布与缓存，旧父进程握手只使用一次。
inline UpdateRequest ManualUpdateRetry(UpdateRequest request) {
    request.parentPid = 0;
    request.readyFile.clear();
    request.silent = false;
    request.acceptedHandoff = false;
    return request;
}
}
