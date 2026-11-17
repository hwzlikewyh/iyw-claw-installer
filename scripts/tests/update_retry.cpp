#include "update_retry.h"
#include "diagnostics.h"
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
    Require(setup::diagnostics::Report(state).find("原助理安装诊断") == 0, "Report is not UTF-8 Chinese text");
}
}

int main() {
    try { VerifyRetryPreservesSelectedRelease(); VerifyDiagnostics(); std::cout << "Update retry and UTF-8 diagnostics checks passed\n"; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
