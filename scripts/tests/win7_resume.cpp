#define NOMINMAX
#include <windows.h>
#include "win7_restart.h"
#include "payload.h"
#include "command.h"
#include <fstream>
#include <iostream>

namespace setup {
fs::path testDirectory;
fs::path CurrentInstaller() { return testDirectory / "wizard.exe"; }
fs::path UserHome() { return testDirectory; }
void RequirePlainPath(const fs::path&) {}
void WriteDurableJSON(const fs::path& path, const nlohmann::json& value) { std::ofstream(path) << value; }
std::string ReadOutput(const fs::path& path) {
    std::ifstream input(path);
    return {std::istreambuf_iterator<char>(input), {}};
}
void BeginStage(FileWork& work, Phase phase) { work.progress.phase = phase; }
Platform CurrentPlatform() { return {"windows7", "x86_64"}; }
void ReadHttps(const std::string&, const Receive&, InstallJob&) { throw std::runtime_error("offline"); }
std::string HashFile(const fs::path&) { return {}; }
void DownloadPackage(const Release&, FileWork& work) { std::ofstream(work.staging / "setup.exe") << "test package"; }
void VerifyDownload(const Release&, FileWork&) {}
void StageCachedPackage(const UpdateRequest&, const Release&, FileWork&) { throw std::runtime_error("bad cache"); }
void ResolvePresetEnvironment(const Release&, FileWork&) { throw std::runtime_error("offline"); }
void PrefetchEnvironment(FileWork&) {}
InstallJob::~InstallJob() = default;
#include "../../src/win7_restart.inc"

void Check(bool result, const char* error) { if (!result) throw std::runtime_error(error); }
void VerifySession() {
    const auto cache = RestartCache();
    fs::create_directories(cache);
    std::ofstream(CurrentInstaller()) << "test executable";
    const Release release{"0.1.3",std::string(FusionBase)+"/app-updates/artifacts/3/download",std::string(64,'3'),10,{},"nsis"};
    UpdateRequest original;
    original.release = release; original.accessToken = "never-store-this"; original.parentPid = 14;
    original.readyFile = "expired-handoff"; original.silent = true;
    const auto destination = testDirectory / L"原助理";
    InstallJob job;
    Snapshot progress; progress.destination = destination;
    FileWork work{job, testDirectory, progress, std::chrono::steady_clock::now(), {}};
    PrepareRestart(work, original);
    Check(progress.restartRegistered && fs::exists(cache / "setup.exe") && !progress.events.empty(),
        "Offline preset preparation lost registered resume or downloaded package");
    fs::remove(cache / "setup.exe");
    const auto text = ReadOutput(cache / "session.json");
    Check(text.find(original.accessToken) == text.npos && text.find("expired-handoff") == text.npos, "Sensitive handoff persisted");
    wchar_t command[260]{}; DWORD bytes = sizeof(command);
    Check(RegGetValueW(HKEY_CURRENT_USER, RestartRunKey, RestartRunValue, RRF_RT_REG_SZ, nullptr, command, &bytes) == ERROR_SUCCESS,
        "Missing RunOnce entry");
    Check(command == L"\"" + (cache / "YuanSetup.exe").wstring() + L"\" --resume-after-restart", "Invalid RunOnce command");
    auto restored = LoadRestartSession();
    Check(restored.destination == destination && restored.selection && restored.selection->resumed &&
        restored.selection->packageCache.empty() && restored.selection->parentPid == 0 && !restored.selection->silent,
        "Resume should re-download missing cache and detach from the original parent");
    std::ofstream(cache / "setup.exe") << "test cache";
    Check(LoadRestartSession().selection->packageCache == cache / "setup.exe", "Existing cache unavailable on resume");
    ClearRestartSession(destination);
    Check(!fs::exists(cache / "session.json") && !fs::exists(cache / "setup.exe"), "Completed session left state behind");
    Check(RegGetValueW(HKEY_CURRENT_USER, RestartRunKey, RestartRunValue, RRF_RT_REG_SZ, nullptr, command, &bytes) == ERROR_FILE_NOT_FOUND,
        "Completed session still registered for startup");
}
}

int wmain(int argc, wchar_t** argv) {
    if (argc != 2) return 2;
    setup::testDirectory = setup::fs::absolute(argv[1]);
    const auto sandbox = L"Software\\YuanInstallerTest-" + std::to_wstring(GetCurrentProcessId());
    HKEY key = nullptr;
    DWORD disposition = 0;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, sandbox.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS,
        nullptr, &key, &disposition) != ERROR_SUCCESS || disposition != REG_CREATED_NEW_KEY) return 2;
    if (RegOverridePredefKey(HKEY_CURRENT_USER, key) != ERROR_SUCCESS) return 2;
    int result = 0;
    try { setup::VerifySession(); std::cout << "Isolated RunOnce and restart session round trip passed\n"; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; result = 1; }
    RegOverridePredefKey(HKEY_CURRENT_USER, nullptr);
    RegCloseKey(key); RegDeleteTreeW(HKEY_CURRENT_USER, sandbox.c_str());
    return result;
}
