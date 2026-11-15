#pragma once
#include "payload.h"
#include "windows_support.h"
namespace setup {
nlohmann::json ReadElevatedMessage(const fs::path& path);
void WriteElevatedSnapshot(const fs::path& path, const Snapshot& snapshot);
void ReadElevatedSnapshot(const fs::path& path, Snapshot& snapshot);
Handle VerifyInstallerParent(unsigned parentPid);
}
