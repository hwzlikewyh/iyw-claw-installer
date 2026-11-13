#include "release.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <cctype>
#include <sstream>
#include <stdexcept>
namespace setup {
namespace {
std::array<unsigned, 3> Version(const std::string& value) {
    std::array<unsigned, 3> parts{};
    std::istringstream stream(value);
    char first = 0, second = 0;
    if (!(stream >> parts[0] >> first >> parts[1] >> second >> parts[2]) || first != '.' || second != '.' ||
        stream.peek() != EOF || value.find_first_not_of("0123456789.") != std::string::npos)
        throw std::runtime_error("Fusion 返回的稳定版版本号无效。");
    return parts;
}
bool DownloadURL(const std::string& value) {
    const auto prefix = std::string(FusionBase) + "/app-updates/artifacts/";
    constexpr const char* suffix = "/download";
    if (value.rfind(prefix, 0) != 0 || value.size() <= prefix.size() + 9 ||
        value.substr(value.size() - 9) != suffix) return false;
    const auto id = value.substr(prefix.size(), value.size() - prefix.size() - 9);
    return !id.empty() && std::all_of(id.begin(), id.end(), [](unsigned char c) { return std::isdigit(c); });
}
bool ValidDigest(const std::string& value) {
    return value.size() == 64 && std::all_of(value.begin(), value.end(),
        [](unsigned char c) { return std::isxdigit(c); });
}
}
Release ParseRelease(const std::string& body) {
    try {
        const auto envelope = nlohmann::json::parse(body);
        if (envelope.at("code") != 1) throw std::runtime_error("Fusion 拒绝版本查询，请稍后重试。");
        const auto& data = envelope.at("data");
        if (!data.is_object() || !data.contains("version"))
            throw std::runtime_error("Fusion 暂未发布此系统与架构的稳定版安装包。");
        if (data.at("channel") != "stable") throw std::runtime_error("Fusion 返回了非稳定通道，已停止安装。");
        Release release{data.at("version"), data.at("url"), data.at("sha256"), 0};
        Version(release.version);
        const auto size = data.at("size").get<std::int64_t>();
        constexpr std::int64_t MaxPackage = 4LL * 1024 * MiB;
        if (size <= 0 || size > MaxPackage || !DownloadURL(release.url) || release.sha256.size() != 64 ||
            !std::all_of(release.sha256.begin(), release.sha256.end(), [](unsigned char c) { return std::isxdigit(c); }))
            throw std::runtime_error("Fusion 安装包的大小、下载地址或摘要无效。");
        release.size = static_cast<std::uint64_t>(size);
        release.signature=data.value("signature",std::string());
        std::transform(release.sha256.begin(), release.sha256.end(), release.sha256.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return release;
    } catch (const nlohmann::json::exception&) {
        throw std::runtime_error("Fusion 版本响应格式异常，请稍后重试。");
    }
}
bool ValidateRequestedRelease(const Release& release, std::string& error) {
    try { Version(release.version); }
    catch (const std::exception& exception) { error = exception.what(); return false; }
    if (!DownloadURL(release.url) || !ValidDigest(release.sha256) || release.size == 0 ||
        release.size > 4ULL * 1024 * MiB) {
        error = "更新包的版本、下载地址、大小或摘要无效。";
        return false;
    }
    return true;
}
Release ResolveRelease(const Platform& platform, InstallJob& job) {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto url = std::string(FusionBase) + "/app-updates/v1/check/" + platform.target + "/" + platform.arch +
        "/0.0.0?product=iyw-claw&runtime=desktop&channel=stable&reason=manual&t=" + std::to_string(stamp);
    std::string body;
    ReadHttps(url, [&body](const char* bytes, std::size_t count) {
        if (body.size() + count > MiB) throw std::runtime_error("Fusion 版本响应过大。");
        body.append(bytes, count);
    }, job);
    return ParseRelease(body);
}
}
