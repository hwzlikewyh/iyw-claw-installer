#include "release.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <cctype>
#include <sstream>
#include <stdexcept>
namespace setup {
namespace {
constexpr std::uint64_t MaxPackageBytes = 1024 * MiB;

std::uint64_t PackageLimit(const nlohmann::json& data) {
    const auto found=data.find("max_artifact_bytes");
    // 旧后台未下发业务上限时仍允许已有主包，保留安全上限。
    if(found==data.end())return MaxPackageBytes;
    if(!found->is_number_integer() || *found<=0)
        throw std::runtime_error("Fusion 安装包大小上限无效。");
    return std::min(found->get<std::uint64_t>(),MaxPackageBytes);
}

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
        const auto channel = data.at("channel").get<std::string>();
        if (channel != "stable" && channel != "beta") throw std::runtime_error("Fusion 返回了无效发布通道。");
        if (channel == "beta" && std::string(setup::Brand().releaseId).empty())
            throw std::runtime_error("正式安装向导不能安装测试版。");
        const auto platform=CurrentPlatform();
        if(platform.target=="windows"&&platform.arch=="x86_64"&&data.contains("app_payload")) {
            const auto& payload=data.at("app_payload");
            const auto artifactId=payload.at("artifact_id").get<std::string>();
            const auto releaseId=data.at("release_id").get<std::string>();
            if(payload.at("schema_version")!=1||payload.at("package_kind")!="app_payload_zip"||
               payload.at("release_id")!=data.at("release_id")||payload.at("target")!="windows"||payload.at("arch")!="x86_64"||
               artifactId.empty()||artifactId[0]=='0'||artifactId.find_first_not_of("0123456789")!=std::string::npos||
               releaseId.empty()||releaseId[0]=='0'||releaseId.find_first_not_of("0123456789")!=std::string::npos||
               payload.at("url")!=std::string(FusionBase)+"/app-updates/artifacts/"+artifactId+"/download")
                throw std::runtime_error("应用载荷发布身份不匹配。");
            Release release{data.at("version"),payload.at("url"),payload.at("sha256"),payload.at("size"),payload.at("signature"),"app_payload_zip",data.at("release_id")};
            if(release.size>PackageLimit(data))throw std::runtime_error("应用载荷超过后台配置的下载上限。");
            std::string error;
            if(!ValidateRequestedRelease(release,error)||release.signature.empty())throw std::runtime_error("应用载荷元数据无效："+error);
            std::transform(release.sha256.begin(),release.sha256.end(),release.sha256.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
            release.channel = channel;
            return release;
        }
        Release release{data.at("version"), data.at("url"), data.at("sha256"), 0};
        Version(release.version);
        const auto size = data.at("size").get<std::int64_t>();
        if (size <= 0 || static_cast<std::uint64_t>(size) > PackageLimit(data) || !DownloadURL(release.url) || release.sha256.size() != 64 ||
            !std::all_of(release.sha256.begin(), release.sha256.end(), [](unsigned char c) { return std::isxdigit(c); }))
            throw std::runtime_error("Fusion 安装包的大小、下载地址或摘要无效。");
        release.size = static_cast<std::uint64_t>(size);
        release.signature=data.value("signature",std::string());
        std::transform(release.sha256.begin(), release.sha256.end(), release.sha256.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        release.channel = channel;
        release.releaseId = data.value("release_id", std::string());
        return release;
    } catch (const nlohmann::json::exception&) {
        throw std::runtime_error("Fusion 版本响应格式异常，请稍后重试。");
    }
}
bool ValidateRequestedRelease(const Release& release, std::string& error) {
    try { Version(release.version); }
    catch (const std::exception& exception) { error = exception.what(); return false; }
    if (!DownloadURL(release.url) || !ValidDigest(release.sha256) || release.size == 0 ||
        release.size > MaxPackageBytes) {
        error = "更新包的版本、下载地址、大小或摘要无效。";
        return false;
    }
    return true;
}
Release ResolveRelease(const Platform& platform, InstallJob& job) {
    return std::string(setup::Brand().releaseId).empty() ? ResolveStableRelease(platform, job) : ResolveCandidate(platform, job);
}
void RequireBrandRuntime(const Release& release) {
    constexpr std::array<unsigned,3> FirstRuntimeBrandVersion{0,1,367};
    if(Brand().id!="iyw" && Version(release.version)<FirstRuntimeBrandVersion)
        throw std::runtime_error("当前公共 PC 版本尚不支持安装品牌配置，请先发布 0.1.367 或更新版本的公共 iyw-claw 包。");
}
Release ResolveStableRelease(const Platform& platform, InstallJob& job) {
    return ResolveChannelRelease(platform,job,"stable");
}
Release ResolveChannelRelease(const Platform& platform, InstallJob& job,const std::string& channel) {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto url = std::string(FusionBase) + "/app-updates/v1/check/" + platform.target + "/" + platform.arch +
        "/0.0.0?product=iyw-claw&runtime=desktop&channel=" + channel + "&reason=manual&t=" + std::to_string(stamp);
    std::string body;
    ReadHttps(url, [&body](const char* bytes, std::size_t count) {
        if (body.size() + count > MiB) throw std::runtime_error("Fusion 版本响应过大。");
        body.append(bytes, count);
    }, job);
    return ParseRelease(body);
}
}
