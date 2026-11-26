#pragma once
#include "installer.h"
#include <functional>

namespace setup {
inline constexpr char FusionBase[] = "https://gateway.iyw.cn/iyw-fusion-api";
using Receive = std::function<void(const char*, std::size_t)>;
inline bool IsOptionalPreset(const std::string& id) {
    return id == "chromix" || id == "agent-browser" || id == "officecli" ||
        id == "agent-reach" || id == "cua-driver";
}
Platform CurrentPlatform();
fs::path DefaultInstallRoot();
fs::path CreateStaging();
Release ParseRelease(const std::string& body);
bool ValidateRequestedRelease(const Release& release, std::string& error);
Release ResolveRelease(const Platform& platform, InstallJob& job);
void RequireBrandRuntime(const fs::path& executable);
Release ResolveCandidate(const Platform& platform, InstallJob& job);
Release ResolveChannelRelease(const Platform& platform, InstallJob& job,const std::string& channel);
Release ResolveStableRelease(const Platform& platform, InstallJob& job);
fs::path BrandConfigurationDirectory(const fs::path& root);
fs::path InstalledBrandIcon(const fs::path& root);
void PrepareBrandAssets(const fs::path& root);
void SaveBrandConfiguration(FileWork& work);
void ReadHttps(const std::string& url, const Receive& receive, InstallJob& job);
void PostHttps(const std::string& url,const std::string& body,const Receive& receive,InstallJob& job);
bool ReadHttpsRange(const std::string& url, const std::pair<std::uint64_t,std::uint64_t>& range,
    const Receive& receive, InstallJob& job);
void DownloadPackage(const Release& release, FileWork& work);
void VerifyDownload(const Release& release, FileWork& work);
void InstallPackage(const Release& release, FileWork& work);
void VerifyInstalled(const Release& release, FileWork& work);
void InstallUnifiedInstaller(const fs::path& root);
void LaunchInstalled(const Snapshot& state, bool restoreSession = false);
void LaunchExistingForUpdate(const fs::path& root);
void CheckSystem(FileWork& work);
fs::path UserHome();
fs::path EnvironmentHelper(const fs::path& root);
void VerifyEnvironmentBinary(const fs::path& helper);
void CheckEnvironment(FileWork& work);
void ResolvePresetEnvironment(const Release& release,FileWork& work);
bool ReuseApplication(const Release& release,FileWork& work);
std::vector<std::string> MissingPresetComponents(const FileWork& work);
void PrefetchEnvironment(FileWork& work);
std::string HashFile(const fs::path& path);
void PrepareEnvironment(const Release& release, FileWork& work, bool preparedByInstaller);
void FinishDeployment(FileWork& work, bool success);
}
