#include "payload_elevation.h"
#include <fstream>
#include <stdexcept>
namespace setup {
namespace {
constexpr DWORD PollMs=150;
constexpr DWORD ParentTimeoutMs=90000;
std::vector<char> TokenUser(HANDLE process) {
    HANDLE raw=nullptr;if(!OpenProcessToken(process,TOKEN_QUERY,&raw))throw std::runtime_error("无法核对安装调用方账户。");
    Handle token(raw);DWORD size=0;GetTokenInformation(token.get(),::TokenUser,nullptr,0,&size);
    std::vector<char> bytes(size);
    if(!size||!GetTokenInformation(token.get(),::TokenUser,bytes.data(),size,&size))throw std::runtime_error("无法读取安装调用方账户。");
    return bytes;
}
UpdateRequest ReadRequest(const fs::path& message) {
    if(message.filename()!=L"elevated-request.json"||!message.is_absolute())throw std::runtime_error("安装权限请求路径无效。");
    const auto value=ReadElevatedMessage(message);UpdateRequest request;
    request.destination=fs::u8path(value.at("root").get<std::string>());
    request.packageCache=message.parent_path()/"setup.exe";
    request.release={value.at("version"),value.at("url"),value.at("sha256"),value.at("size"),value.at("signature"),PayloadKind,value.at("releaseId")};
    request.channel=value.value("channel",std::string("stable"));request.workerMode=true;
    if(request.channel!="stable"&&request.channel!="beta")throw std::runtime_error("更新通道无效。");
    request.release.channel=request.channel;
    std::string error;if(!ValidateRequestedRelease(request.release,error)||request.release.signature.empty())throw std::runtime_error("安装权限请求的载荷身份无效。");
    if(!request.destination.is_absolute()||request.destination==request.destination.root_path())throw std::runtime_error("安装权限请求的目标目录无效。");
    return request;
}
void WaitForParent(HANDLE parent,const fs::path& directory) {
    const auto deadline=GetTickCount64()+ParentTimeoutMs;
    while(GetTickCount64()<deadline) {
        if(WaitForSingleObject(parent,PollMs)==WAIT_OBJECT_0||fs::exists(directory/"elevated-cancel.json"))
            throw std::runtime_error("安装向导已取消，应用尚未被替换。");
        if(fs::exists(directory/"elevated-go.json")) {
            if(ReadElevatedMessage(directory/"elevated-go.json").at("pid")!=GetCurrentProcessId())
                throw std::runtime_error("安装权限请求的交接身份不匹配。");
            return;
        }
    }
    throw std::runtime_error("等待安装向导交接超时，应用尚未被替换。");
}
Snapshot ExecuteWorker(UpdateRequest request,const fs::path& directory) {
    InstallJob job;job.Start(std::move(request));
    for(;;) {
        const auto state=job.Read();
        WriteElevatedSnapshot(directory/"elevated-progress.json",state);
        if(state.phase==Phase::Complete||state.phase==Phase::Failed||state.phase==Phase::Cancelled) {
            job.Join();return state;
        }
        Sleep(PollMs);
    }
}
}
Handle VerifyInstallerParent(unsigned parentPid) {
    Handle parent(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,parentPid));
    if(!parent)throw std::runtime_error("无法验证安装向导调用方。");
    const auto current=TokenUser(GetCurrentProcess()),other=TokenUser(parent.get());
    if(!EqualSid(reinterpret_cast<const TOKEN_USER*>(current.data())->User.Sid,reinterpret_cast<const TOKEN_USER*>(other.data())->User.Sid))
        throw std::runtime_error("管理员授权切换了账户，已停止安装；请使用原账户授权。");
    wchar_t path[32768]{};DWORD size=32768;
    if(!QueryFullProcessImageNameW(parent.get(),0,path,&size)||!fs::equivalent(fs::path(path),CurrentInstaller()))
        throw std::runtime_error("安装权限助手只能由同一安装向导调用。");
    VerifySignature(fs::path(path));return parent;
}
void WriteElevatedSnapshot(const fs::path& path,const Snapshot& state) {
    WriteDurableJSON(path,{{"phase",static_cast<int>(state.phase)},{"done",state.phaseDone},{"total",state.phaseTotal},
        {"version",state.version},{"detail",state.detail},{"executable",state.executable.u8string()},
        {"logPath",state.logPath.u8string()},{"events",state.events},{"checks",state.checks}});
}
void ReadElevatedSnapshot(const fs::path& path,Snapshot& state) {
    const auto value=ReadElevatedMessage(path);const int phase=value.at("phase");
    if(phase<static_cast<int>(Phase::Checking)||phase>static_cast<int>(Phase::Failed))throw std::runtime_error("安装权限助手进度无效。");
    state.phase=static_cast<Phase>(phase);state.cancellable=false;
    const auto stage=StageIndex(state.phase);if(stage>=0)state.stage=static_cast<unsigned>(stage);
    state.phaseDone=value.at("done");state.phaseTotal=value.at("total");state.version=value.at("version");
    state.detail=value.at("detail");state.executable=fs::u8path(value.at("executable").get<std::string>());
    state.logPath=fs::u8path(value.at("logPath").get<std::string>());
    state.events=value.at("events").get<std::vector<std::string>>();state.checks=value.at("checks").get<std::vector<std::string>>();
    if(state.phase==Phase::Complete)state.completedStages=StageCount;
}
int RunElevatedInstall(const fs::path& message,unsigned parentPid) {
    // 在验证调用方之前不读取请求，也不向调用方提供的目录写入。
    auto parent=VerifyInstallerParent(parentPid);auto requestLock=OpenLocked(message);
    const auto value=ReadElevatedMessage(message);const bool uninstall=value.value("operation",std::string())=="uninstall";
    UpdateRequest request;
    if(uninstall)request.destination=fs::u8path(value.at("root").get<std::string>());
    else request=ReadRequest(message);
    if(!request.destination.is_absolute()||request.destination==request.destination.root_path())
        throw std::runtime_error("安装权限请求的目标目录无效。");
    LoadInstalledBrand(request.destination);
    const auto directory=message.parent_path();
    Snapshot state;state.destination=request.destination;state.version=request.release.version;
    try {
        InstallJob precheck;FileWork work{precheck,directory,state,std::chrono::steady_clock::now(),{}};
        LockDirectInstall(work);
        if(DirectNeedsElevation(request.destination))throw std::runtime_error("安装权限助手未取得管理员权限。");
        if(!uninstall)VerifyDownload(request.release,work);
        WriteDurableJSON(directory/"elevated-ready.json",{{"pid",GetCurrentProcessId()}});
        WaitForParent(parent.get(),directory);work.installLock.reset();work.packageLock.reset();
        if(uninstall){UninstallDirect(request.destination);state.phase=Phase::Complete;}
        else state=ExecuteWorker(request,directory);
    } catch(const std::exception& error) {state.phase=Phase::Failed;state.detail=error.what();}
    WriteElevatedSnapshot(directory/"elevated-result.json",state);
    return state.phase==Phase::Complete?0:1;
}
}
