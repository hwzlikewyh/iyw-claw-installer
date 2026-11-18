#pragma once
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <memory>
#include <map>
#include <optional>
#include <array>
#include <string>
#include <thread>
#include <vector>

namespace setup {
namespace fs = std::filesystem;
constexpr std::uint64_t MiB = 1024 * 1024;
enum class Phase { Ready, Checking, Resolving, Downloading, Verifying, PresetDownload, Installing, Environment,
    Finalizing, Complete, Cancelled, Failed };
constexpr unsigned StageCount = 8;
struct Platform { std::string target; std::string arch; };
struct Release {
    std::string version;
    std::string url;
    std::string sha256;
    std::uint64_t size = 0;
    std::string signature;
    std::string packageKind;
    std::string releaseId;
};
struct UpdateRequest {
    fs::path destination;
    fs::path packageCache;
    std::string channel;
    std::string releaseId;
    std::string requestURL;
    std::string accessToken;
    bool silent = false;
    bool acceptedHandoff = false;
    Release release;
    unsigned parentPid = 0;
    fs::path readyFile;
    bool workerMode = false;
};
struct PresetComponent{
    std::string id,version,versionId,artifactId,sha256;
    bool optional=false;
    std::uint64_t bytes=0;
};
struct Snapshot {
    Phase phase = Phase::Ready;
    std::uint64_t phaseDone = 0;
    std::uint64_t phaseTotal = 0;
    std::uint64_t availableBytes = 0;
    double elapsed = 0;
    double stageStartedSeconds = 0;
    unsigned stage = 0;
    unsigned completedStages = 0;
    std::array<bool,StageCount> reused{};
    bool cancellable = true;
    std::string detail;
    std::string version;
    fs::path destination;
    fs::path executable;
    fs::path logPath;
    std::vector<std::string> events;
    std::vector<std::string> checks;
    std::vector<PresetComponent> presets;
};
std::string StageName(unsigned index);
int StageIndex(Phase phase);
double OverallProgress(const Snapshot& state);
class InstallJob {
public:
    ~InstallJob();
    void Start(const fs::path& parent);
    void Start(UpdateRequest request);
    void Cancel();
    void Join();
    Snapshot Read() const;
    bool IsCancelled() const;
    void Publish(Snapshot value);
private:
    void Run(fs::path parent, std::optional<UpdateRequest> request = std::nullopt);
    mutable std::mutex mutex_;
    Snapshot state_;
    std::atomic<bool> cancelled_{false};
    std::thread worker_;
};
struct FileWork {
    InstallJob& job;
    fs::path staging;
    Snapshot& progress;
    std::chrono::steady_clock::time_point started;
    std::shared_ptr<void> packageLock;
    fs::path backup;
    std::map<std::string,std::string> componentURLs;
    std::vector<std::shared_ptr<void>> retainedFiles;
    bool installerStarted = false;
    bool updateMode = false;
    bool silentUpdate = false;
    bool acceptedHandoff = false;
    bool directPayload = false;
    bool environmentCommitted = false;
    bool parentExited = false;
    bool needsElevation = false;
    std::string environmentTransaction;
    fs::path deployment;
    std::shared_ptr<void> installLock;
};
void CheckCancel(const InstallJob& job);
void BeginStage(FileWork& work, Phase phase);
void Advance(FileWork& work, std::uint64_t count);
}
