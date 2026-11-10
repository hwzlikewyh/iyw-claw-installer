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
};
int RunCommand(const Command& command, const std::function<void()>& poll = {});
std::string ReadOutput(const fs::path& path);
}
