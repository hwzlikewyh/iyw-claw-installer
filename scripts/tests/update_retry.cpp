#include "update_retry.h"
#include "diagnostics.h"
#include "win7_restart.h"
#include "wmf_sources.h"
#include <fstream>
#include <stdexcept>
#include <iostream>

namespace setup {
std::string StageName(unsigned) { return "安装应用"; }
}

namespace {
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void VerifyRetryPreservesSelectedRelease() {
    setup::UpdateRequest original;
    original.destination = setup::fs::u8path("D:/software/原助理");
    original.packageCache = setup::fs::u8path("D:/cache/selected-payload.zip");
    original.release = {"0.1.350", "https://gateway.iyw.cn/iyw-fusion-api/app-updates/artifacts/123/download",
        std::string(64, 'a'), 12345, "verified-signature", "app_payload_zip", "123"};
    original.channel = "stable"; original.releaseId = "123";
    original.requestURL = original.release.url;
    original.parentPid = 1234; original.readyFile = setup::fs::u8path("D:/handoff/ready.json");
    original.silent = true; original.acceptedHandoff = true;
    const auto retry = setup::ManualUpdateRetry(original);
    Require(retry.parentPid == 0 && retry.readyFile.empty(), "Retry reused the expired parent handshake");
    Require(!retry.silent && !retry.acceptedHandoff, "Retry kept automatic consent flags");
    Require(retry.destination == original.destination && retry.packageCache == original.packageCache, "Retry changed installation or cache path");
    Require(retry.release.version == original.release.version && retry.release.releaseId == original.release.releaseId, "Retry selected another release");
    Require(retry.release.signature == original.release.signature && retry.release.sha256 == original.release.sha256 &&
        retry.release.size == original.release.size && retry.release.url == original.release.url &&
        retry.release.packageKind == original.release.packageKind, "Retry lost verification metadata");
    Require(retry.channel == original.channel && retry.releaseId == original.releaseId && retry.requestURL == original.requestURL,
        "Retry changed selection identity");
    Require(original.parentPid == 1234 && original.silent, "Retry mutated the original attempt");
}

void VerifyDiagnostics() {
    setup::Snapshot state;
    state.detail = "rename: The process cannot access the file because it is being used by another process.";
    Require(setup::diagnostics::Reason(state) == "应用目录仍被进程占用，暂时无法替换。", "Missing directory lock advice");
    state.detail = "更新调用方不是当前安装的原助理。";
    Require(setup::diagnostics::Advice(state).find("返回设置并重试") != std::string::npos, "Missing retry advice");
    state.detail = "Windows Management Framework 5.1 安装失败：Windows 服务被禁用（错误 1058）。";
    Require(setup::diagnostics::Reason(state).find("服务被禁用") != std::string::npos,
        "Missing disabled-service diagnosis");
    Require(setup::diagnostics::Advice(state).find("TrustedInstaller") != std::string::npos,
        "Missing Windows servicing service advice");
    state.detail = "Windows Management Framework 5.1 安装失败：Windows Update Agent 返回不支持操作错误（0x80240037）。";
    Require(setup::diagnostics::Reason(state).find("不支持") != std::string::npos,
        "Missing Windows Update unsupported-operation diagnosis");
    Require(setup::diagnostics::Advice(state).find("Package Manager 本地安装") != std::string::npos,
        "Missing automatic local servicing fallback advice");
    Require(setup::diagnostics::Advice(state).find("手动启用 Windows Update") != std::string::npos,
        "Unsupported-operation advice still asks users to change service settings");
    state.detail = "本地安装 0x800F081F，先前 WUSA 返回 0x80240037";
    Require(setup::diagnostics::Reason(state).find("安装源") != std::string::npos, "Lost final CBS error");
    Require(setup::RequiresSystemRestart(3010) && setup::RequiresSystemRestart(1641) &&
        !setup::RequiresSystemRestart(0x800f081f) && !setup::RequiresSystemRestart(1603) && !setup::RequiresSystemRestart(0),
        "Servicing failures and successful reboots must be distinguished");
    Require(setup::diagnostics::Report(state).find("原助理安装诊断") == 0, "Report is not UTF-8 Chinese text");
}
void VerifyLocalSources(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    const std::string xml{std::istreambuf_iterator<char>(input), {}};
    const auto local = setup::BindWmfSources(xml, path.parent_path());
    Require(local.find("%configsetroot%") == std::string::npos && local != xml, "CAB paths not bound");
    Require(local.find("KB3191566") != std::string::npos, "Missing WMF package");
    Require(local.find("&amp;") != std::string::npos || path.parent_path().u8string().find('&') == std::string::npos,
        "XML source directory was not escaped");
    try { setup::BindWmfSources("<source location=\"%configsetroot%\\missing.cab\" />", path.parent_path()); }
    catch (const std::runtime_error&) { return; }
    throw std::runtime_error("Missing CAB accepted");
}
}

int main(int argc, char** argv) {
    try { VerifyRetryPreservesSelectedRelease(); VerifyDiagnostics();
        for (int index = 1; index < argc; ++index) VerifyLocalSources(std::filesystem::absolute(argv[index]));
        std::cout << "Update retry, reboot and WMF diagnostics checks passed\n"; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
