#pragma once
#include <filesystem>
#include <stdexcept>
#include <string>

namespace setup {
inline std::string BindWmfSources(std::string manifest, const std::filesystem::path& folder) {
    if (!folder.is_absolute()) throw std::runtime_error("WMF 更新源目录无效。");
    std::string prefix;
    for (const char byte : folder.u8string()) {
        if (byte == '&') prefix += "&amp;";
        else if (byte == '"') prefix += "&quot;";
        else if (byte == '<') prefix += "&lt;";
        else prefix += byte;
    }
    const std::string location = "<source location=\"";
    const std::string token = "%configsetroot%\\";
    std::size_t cursor = 0, count = 0;
    while ((cursor = manifest.find(location, cursor)) != std::string::npos) {
        cursor += location.size();
        if (manifest.compare(cursor, token.size(), token) != 0) throw std::runtime_error("无法识别 WMF 清单源位置。");
        const auto nameStart = cursor + token.size(), end = manifest.find('"', nameStart);
        const auto name = manifest.substr(nameStart, end - nameStart);
        if (end == std::string::npos || name.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789.-") != std::string::npos ||
            std::filesystem::path(name).extension() != ".cab" || !std::filesystem::is_regular_file(folder / name))
            throw std::runtime_error("WMF 安装清单所需 CAB 文件缺失或名称无效。");
        manifest.replace(cursor, token.size(), prefix + "\\");
        cursor += prefix.size() + 1 + name.size();
        ++count;
    }
    if (!count) throw std::runtime_error("WMF 清单缺少本地安装源。");
    return manifest;
}
}
