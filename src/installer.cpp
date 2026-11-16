#include "release.h"
#include "diagnostics.h"
#include <fstream>
#include <algorithm>
#include <stdexcept>
#include <utility>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include "payload.h"
#endif
namespace setup {
namespace {
void ValidateRoot(const fs::path& root, Snapshot& result) {
    if (root.empty() || !root.is_absolute() || root == root.root_path())
        throw std::runtime_error("请选择完整的应用文件夹路径，不要选择磁盘根目录。");
    std::error_code error;
    auto existing = root;
    while (!fs::exists(existing, error) && !error) existing = existing.parent_path();
    if (error || !fs::is_directory(existing)) throw std::runtime_error("安装位置无效，请选择文件夹。");
    result.availableBytes = fs::space(existing).available;
    result.destination = root;
}
void SaveFailure(const fs::path& directory, Snapshot& result) {
    if (directory.empty()) return;
    const auto path = directory / "installer-error.log";
    std::ofstream log(path, std::ios::binary);
    if (!log) return;
    result.logPath = path;
    log << diagnostics::Report(result);
}
void StageCachedPackage(const UpdateRequest& request, const Release& release, FileWork& work) {
    const auto destination = work.staging / "setup.exe";
    work.progress.phaseTotal = release.size;
    work.progress.phaseDone = 0;
    work.progress.detail = "正在复用已验证的应用更新缓存";
    std::error_code error;
    if (!fs::is_regular_file(request.packageCache, error) || error ||
        fs::file_size(request.packageCache, error) != release.size || error)
        throw std::runtime_error("应用更新缓存大小与发布记录不匹配，请重新检查更新。");
    fs::copy_file(request.packageCache, destination, fs::copy_options::overwrite_existing, error);
    if (error) throw std::runtime_error("无法暂存应用更新缓存：" + error.message());
    work.progress.phaseDone = release.size;
    work.progress.elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - work.started).count();
    work.job.Publish(work.progress);
    VerifyDownload(release, work);
    work.progress.reused[static_cast<unsigned>(StageIndex(Phase::Downloading))] = true;
    work.progress.events.push_back("复用应用更新缓存，SHA-256 与发布者签名重新校验通过。");
}
void Execute(FileWork& work, const std::optional<UpdateRequest>& request) {
    work.updateMode = request.has_value();
    work.silentUpdate = request && request->silent;
    work.acceptedHandoff = request && request->acceptedHandoff;
    if (request) {
        const auto& platform = CurrentPlatform();
        if (platform.target != "windows" || platform.arch != "x86_64")
            throw std::runtime_error("当前平台暂不支持统一更新交接。");
    }
    BeginStage(work, Phase::Checking);
    ValidateRoot(work.progress.destination, work.progress);
    const auto platform = CurrentPlatform();
    CheckSystem(work); CheckEnvironment(work);
    BeginStage(work, Phase::Resolving);
    const auto release = request ? request->release : ResolveRelease(platform, work.job);
#ifdef _WIN32
    work.directPayload=release.packageKind==PayloadKind;
    if(work.directPayload)LockDirectInstall(work);
    if(work.directPayload)work.needsElevation=DirectNeedsElevation(work.progress.destination);
#endif
    {std::ofstream identity(work.staging/"selected-release.txt",std::ios::binary);
        identity<<release.version<<'\n'<<release.sha256<<'\n';}
    work.progress.version = release.version;
    work.progress.events.push_back("Fusion 稳定版：" + release.version + "；平台：" + platform.target + "/" + platform.arch);
    work.job.Publish(work.progress);
    ResolvePresetEnvironment(release,work);
    constexpr std::uint64_t MinimumFree = 2ULL * 1024 * MiB;
    std::uint64_t presetBytes=0;for(const auto& component:work.progress.presets)presetBytes+=component.bytes;
    if (work.progress.availableBytes < std::max(MinimumFree, (release.size+presetBytes) * 3))
        throw std::runtime_error("目标磁盘空间不足，建议至少保留 2 GiB 供应用及环境安装。");
    if (fs::space(work.staging).available < release.size + 64 * MiB)
        throw std::runtime_error("临时下载目录空间不足。");
    const bool reused=!work.needsElevation&&ReuseApplication(release,work);
    const bool preparedByInstaller = !reused && !work.directPayload && platform.target.rfind("windows",0)==0;
    if(reused){work.progress.reused[2]=true;work.progress.reused[3]=true;work.progress.reused[5]=true;}
    if(!reused){
        if (request && !request->packageCache.empty()) {
            BeginStage(work, Phase::Downloading);
            work.progress.detail = "正在复用已校验的更新缓存";
            StageCachedPackage(*request, release, work);
        } else {
            DownloadPackage(release,work);VerifyDownload(release,work);
        }
    }
    PrefetchEnvironment(work);
#ifdef _WIN32
    if(work.needsElevation) {
        InstallDirectElevated(release,work,request?&*request:nullptr);
        return;
    }
    if(request && request->parentPid) {
        AwaitUpdateParent(*request);
        work.parentExited = true;
    }
    if(work.directPayload)RecoverDirectInstall(work);
#endif
    if(!reused)InstallPackage(release,work);
    PrepareEnvironment(release, work, preparedByInstaller); VerifyInstalled(release, work);
    FinishDeployment(work, true);
    work.progress.phase = Phase::Complete;
    work.progress.completedStages = StageCount;
    work.progress.cancellable = false;
    work.progress.events.push_back("安装器返回成功，主程序版本与签名检查通过。");
}
}
InstallJob::~InstallJob() { Cancel(); Join(); }
void InstallJob::Cancel() { cancelled_.store(true); }
void InstallJob::Join() { if (worker_.joinable()) worker_.join(); }
bool InstallJob::IsCancelled() const { return cancelled_.load(); }
Snapshot InstallJob::Read() const { std::lock_guard<std::mutex> guard(mutex_); return state_; }
void InstallJob::Publish(Snapshot value) { std::lock_guard<std::mutex> guard(mutex_); state_ = std::move(value); }
void InstallJob::Start(const fs::path& root) {
    Join(); cancelled_.store(false); Snapshot initial; initial.phase = Phase::Checking; initial.destination = root;
    Publish(initial);
    try { worker_ = std::thread(&InstallJob::Run, this, root, std::nullopt); }
    catch (const std::exception& error) { initial.phase = Phase::Failed; initial.detail = error.what(); Publish(initial); }
}
void InstallJob::Start(UpdateRequest request) {
    Join(); cancelled_.store(false); Snapshot initial; initial.phase = Phase::Checking; initial.destination = request.destination;
    initial.version = request.release.version;
    Publish(initial);
    // 先保留目标路径，不能在同一参数列表中同时读取和移动 request。
    const auto destination = request.destination;
    try { worker_ = std::thread(&InstallJob::Run, this, destination, std::optional<UpdateRequest>(std::move(request))); }
    catch (const std::exception& error) { initial.phase = Phase::Failed; initial.detail = error.what(); Publish(initial); }
}
void InstallJob::Run(fs::path root, std::optional<UpdateRequest> request) {
#ifdef _WIN32
    SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_BELOW_NORMAL);
#endif
    Snapshot result; result.destination = std::move(root);
    FileWork work{*this, {}, result, std::chrono::steady_clock::now(), {}};
    try {
        work.staging = CreateStaging(); Execute(work, request);
    } catch (const std::exception& error) {
        try { FinishDeployment(work, false); } catch (const std::exception& rollback) {
            result.events.push_back(std::string("应用恢复失败，保留现场：") + rollback.what());
        }
        result.phase = IsCancelled() && result.cancellable ? Phase::Cancelled : Phase::Failed;
        result.detail = result.phase == Phase::Cancelled ? "下载或校验已取消，尚未启动安装。" : error.what();
        result.elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - work.started).count();
        result.events.push_back(result.detail); SaveFailure(work.staging, result);
        work.retainedFiles.clear();
        work.installLock.reset();
        if (request && !request->workerMode && (!request->parentPid || work.parentExited) && !work.installerStarted) {
            try { LaunchExistingForUpdate(result.destination); }
            catch (const std::exception& restart) {
                result.events.push_back(std::string("更新未启动，重新打开旧版本失败：") + restart.what());
            }
        }
    }
    if(!work.staging.empty()){
        try{
            const auto logs=UserHome()/".iyw-claw/logs/installer"/work.staging.filename();fs::create_directories(logs);
            for(const auto& entry:fs::directory_iterator(work.staging)){
                if(!entry.is_regular_file())continue;const auto name=entry.path().filename().string();
                if(name.find(".log")!=std::string::npos||name.find(".stderr")!=std::string::npos)
                    fs::copy_file(entry.path(),logs/entry.path().filename(),fs::copy_options::overwrite_existing);
            }
            if(!result.logPath.empty()&&result.logPath.parent_path()==work.staging)result.logPath=logs/result.logPath.filename();
            if(result.phase==Phase::Failed||result.phase==Phase::Cancelled)SaveFailure(logs,result);
        }catch(const std::exception&){result.events.push_back("安装日志暂留下载缓存目录。");}
    }
    work.packageLock.reset();
    std::error_code error;
    if (!work.staging.empty() && result.phase == Phase::Complete) fs::remove_all(work.staging, error);
    else if (!work.staging.empty()) {
        fs::remove(work.staging / "setup.part", error); fs::remove(work.staging / "setup.exe", error);
        for(const auto& entry:fs::directory_iterator(work.staging,error)){
            const auto name=entry.path().filename().string();
            if(entry.is_regular_file()&&name.rfind("env-",0)==0&&entry.path().extension()==".part")fs::remove(entry.path(),error);
        }
    }
    result.elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - work.started).count();
    Publish(std::move(result));
}
}
