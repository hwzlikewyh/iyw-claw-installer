#pragma once
#ifndef _WIN32
#include "release.h"
#include "command.h"
namespace setup {
void ExtractApplication(FileWork& work);
void VerifyMacBundle(const fs::path& bundle, const Release& release, const fs::path& log);
fs::path AppExecutable(const fs::path& root);
}
#endif
