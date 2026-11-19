#pragma once
#include "installer.h"

namespace setup {
#ifdef _WIN32
fs::path FindExistingInstallRoot();
fs::path NormalizeExistingInstallRoot(const fs::path& candidate);
std::string ExistingInstallVersion(const fs::path& root);
#endif
}
