#pragma once
#include "release.h"
#include <nlohmann/json.hpp>
namespace setup {
inline constexpr char PayloadKind[] = "app_payload_zip";
inline constexpr std::uint64_t MaxPayloadBytes = 4ULL * 1024 * MiB;
inline constexpr unsigned MaxPayloadFiles = 100000;
void VerifyPayload(const Release& release, FileWork& work);
fs::path PayloadRelative(const std::string& value);
void RequirePlainPath(const fs::path& path);
void ExtractPayload(FileWork& work);
void ValidatePayloadTree(const fs::path& root, const Release& release);
void BeginDirectInstall(const Release& release, FileWork& work);
void PrepareDirectPayload(const Release& release, FileWork& work);
void FinishDirectInstall(FileWork& work, bool success);
void PrepareDirectEnvironment(const Release& release, FileWork& work, bool beforeExit = false);
void RegisterDirectInstall(const Release& release, FileWork& work);
void StopInstalledProcesses(const fs::path& root);
void EnsureWebView(FileWork& work);
void RecoverDirectInstall(FileWork& work);
void LockDirectInstall(FileWork& work);
void WriteDeploymentSelection(const Release& release, FileWork& work, bool reused);
void PollEnvironmentProgress(FileWork& work);
bool DirectNeedsElevation(const fs::path& root);
void InstallDirectElevated(const Release& release, FileWork& work, const UpdateRequest* request);
int RunElevatedInstall(const fs::path& request, unsigned parentPid);
void UninstallDirectElevated(const fs::path& root);
void SaveDeployment(FileWork& work, const std::string& state);
void WriteDurableJSON(const fs::path& path, const nlohmann::json& value);
fs::path CurrentInstaller();
void UninstallDirect(const fs::path& root);
void AwaitUpdateParent(const UpdateRequest& request);
void ReportUpdateFailure(const UpdateRequest& request, const std::string& detail);
void RestoreDirectRegistration(FileWork& work);
}
