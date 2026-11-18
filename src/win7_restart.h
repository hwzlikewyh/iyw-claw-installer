#pragma once
#include "installer.h"
#include <stdexcept>

namespace setup {
inline constexpr unsigned SuccessRestartRequired = 3010, SuccessRestartInitiated = 1641;
inline bool RequiresSystemRestart(unsigned code) { return code == SuccessRestartRequired || code == SuccessRestartInitiated; }
struct SystemRestart : std::runtime_error { using std::runtime_error::runtime_error; };
struct RestartSession {
    fs::path destination;
    std::optional<UpdateRequest> selection;
};
#ifdef _WIN32
void PrepareRestart(FileWork& work, const std::optional<UpdateRequest>& request);
RestartSession LoadRestartSession();
void ClearRestartSession(const fs::path& destination);
#endif
}
