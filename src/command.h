#pragma once
#include "installer.h"
#include <functional>
namespace setup {
struct Command {
    fs::path executable;
    std::vector<std::string> arguments;
    fs::path output;
    unsigned timeoutSeconds = 30;
    fs::path directory;
    bool elevated = false;
};
struct WmfInstallRequest {
    fs::path wusa;
    fs::path package;
    fs::path installLog;
    fs::path serviceLog;
};
int RunCommand(const Command& command, const std::function<void()>& poll = {});
int RunWmfInstallerElevated(const WmfInstallRequest& request,
    const std::function<void()>& poll = {});
int RunWmfElevatedWorker(const fs::path& session);
fs::path WmfLiveDirectory(const fs::path& staging);
std::string ReadOutput(const fs::path& path);
}
