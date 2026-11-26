#include "payload_elevation.h"
#include <shellapi.h>
#include <fstream>
#include <stdexcept>
namespace setup {
namespace {
constexpr DWORD WorkerReadyTimeout=60000;
constexpr DWORD WorkerPollMs=150;
bool IsElevated() {
    HANDLE raw=nullptr;if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&raw))throw std::runtime_error("无法检查安装权限。");
    Handle token(raw);TOKEN_ELEVATION value{};DWORD size=0;
    if(!GetTokenInformation(token.get(),TokenElevation,&value,sizeof(value),&size))throw std::runtime_error("无法检查安装权限。");
    return value.TokenIsElevated!=0;
}
void ProbeDirectory(const fs::path& directory) {
    RequirePlainPath(directory);fs::create_directories(directory);
    GUID id{};wchar_t name[40]{};
    if(FAILED(CoCreateGuid(&id))||!StringFromGUID2(id,name,40))throw std::runtime_error("无法创建权限检查标识。");
    const auto probe=directory/(std::wstring(L".iyw-access-")+name);
    Handle file(CreateFileW(probe.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_TEMPORARY|FILE_FLAG_DELETE_ON_CLOSE,nullptr));
    if(file.get()==INVALID_HANDLE_VALUE)throw fs::filesystem_error("安装目录写入检查失败",probe,std::error_code(GetLastError(),std::system_category()));
}
Handle StartWorker(const fs::path& request) {
    const auto executable=CurrentInstaller();VerifySignature(executable);
    const auto arguments=L"--elevated-worker \""+request.wstring()+L"\" "+std::to_wstring(GetCurrentProcessId());
    SHELLEXECUTEINFOW launch{};launch.cbSize=sizeof(launch);
    launch.fMask=SEE_MASK_NOCLOSEPROCESS|SEE_MASK_NOASYNC|SEE_MASK_FLAG_NO_UI;
    launch.lpVerb=L"runas";launch.lpFile=executable.c_str();launch.lpParameters=arguments.c_str();launch.nShow=SW_HIDE;
    if(!ShellExecuteExW(&launch)||!launch.hProcess) {
        if(GetLastError()==ERROR_CANCELLED)throw std::runtime_error("已取消管理员授权，旧应用尚未被替换。");
        throw std::runtime_error("无法启动安装权限助手。");
    }
    return Handle(launch.hProcess);
}
void WaitWorkerReady(HANDLE worker,const fs::path& directory) {
    const auto deadline=GetTickCount64()+WorkerReadyTimeout;
    while(GetTickCount64()<deadline) {
        if(fs::exists(directory/"elevated-ready.json")) {
            if(ReadElevatedMessage(directory/"elevated-ready.json").at("pid")!=GetProcessId(worker))
                throw std::runtime_error("安装权限助手身份不匹配。");
            return;
        }
        if(WaitForSingleObject(worker,WorkerPollMs)==WAIT_OBJECT_0) {
            if(!fs::exists(directory/"elevated-result.json"))
                throw std::runtime_error("安装权限助手身份检查失败；请用原登录账户授权。");
            const auto result=ReadElevatedMessage(directory/"elevated-result.json");
            throw std::runtime_error(result.value("detail",std::string("安装权限助手预检失败。")));
        }
    }
    throw std::runtime_error("安装权限助手准备超时，旧应用尚未被替换。");
}
}
bool DirectNeedsElevation(const fs::path& root) {
    try {
        ProbeDirectory(root);ProbeDirectory(root/"staging");
        if(fs::exists(root/"app"))ProbeDirectory(root/"app");
        ProbeDirectory(UserHome()/setup::Brand().userDirectory/"inventory");
        return false;
    } catch(const fs::filesystem_error& error) {
        if(error.code().value()!=ERROR_ACCESS_DENIED)throw;
        if(IsElevated())throw std::runtime_error("管理员权限仍无法写入安装或环境目录，请检查目录权限。");
        return true;
    }
}
nlohmann::json ReadElevatedMessage(const fs::path& path) {
    RequirePlainPath(path);
    if(fs::file_size(path)>MiB)throw std::runtime_error("安装权限助手消息过大。");
    std::ifstream file(path);return nlohmann::json::parse(file);
}
void InstallDirectElevated(const Release& release,FileWork& work,const UpdateRequest* request) {
    if(work.silentUpdate)throw std::runtime_error("此安装目录需要管理员授权，请手动点击安装更新。");
    const auto message=work.staging/"elevated-request.json";
    WriteDurableJSON(message,{{"root",work.progress.destination.u8string()},{"version",release.version},
        {"url",release.url},{"sha256",release.sha256},{"size",release.size},{"signature",release.signature},{"releaseId",release.releaseId},{"channel",release.channel}});
    auto messageLock=OpenLocked(message);
    work.installLock.reset();
    auto worker=StartWorker(message);
    try {
        WaitWorkerReady(worker.get(),work.staging);
        if(request&&request->parentPid) {AwaitUpdateParent(*request);work.parentExited=true;}
        BeginStage(work,Phase::Installing);work.installerStarted=true;
        WriteDurableJSON(work.staging/"elevated-go.json",{{"pid",GetProcessId(worker.get())}});
        while(WaitForSingleObject(worker.get(),WorkerPollMs)==WAIT_TIMEOUT) {
            if(fs::exists(work.staging/"elevated-progress.json"))ReadElevatedSnapshot(work.staging/"elevated-progress.json",work.progress);
            work.job.Publish(work.progress);
        }
        ReadElevatedSnapshot(work.staging/"elevated-result.json",work.progress);
        DWORD code=1;if(!GetExitCodeProcess(worker.get(),&code)||code||work.progress.phase!=Phase::Complete)
            throw std::runtime_error(work.progress.detail);
    } catch(...) {
        // go 之前只终止等待；go 之后由助手完成提交/回滚，不能强杀部署事务。
        if(!work.installerStarted)WriteDurableJSON(work.staging/"elevated-cancel.json",{{"cancel",true}});
        else while(WaitForSingleObject(worker.get(),WorkerPollMs)==WAIT_TIMEOUT) {
            work.progress.detail="等待安装权限助手完成提交或恢复";work.job.Publish(work.progress);
        }
        if(!fs::exists(work.progress.destination/"staging/direct-install.json"))work.installerStarted=false;
        throw;
    }
}
void UninstallDirectElevated(const fs::path& root) {
    const auto directory=CreateStaging();const auto message=directory/"elevated-request.json";
    WriteDurableJSON(message,{{"operation","uninstall"},{"root",root.u8string()}});
    auto lock=OpenLocked(message);auto worker=StartWorker(message);
    WaitWorkerReady(worker.get(),directory);
    WriteDurableJSON(directory/"elevated-go.json",{{"pid",GetProcessId(worker.get())}});
    while(WaitForSingleObject(worker.get(),WorkerPollMs)==WAIT_TIMEOUT){}
    const auto result=ReadElevatedMessage(directory/"elevated-result.json");
    DWORD code=1;
    if(!GetExitCodeProcess(worker.get(),&code)||code)
        throw std::runtime_error(result.value("detail",std::string("卸载权限助手失败。")));
}
}
