#include "payload.h"
#include "windows_support.h"
#include "command.h"
#include <fstream>
#include <algorithm>
#include <stdexcept>
namespace setup {
namespace {
using Json=nlohmann::json;
Json ReadJSON(const fs::path& path) {
    RequirePlainPath(path);
    if(fs::file_size(path)>MiB)throw std::runtime_error("安装事务记录过大。");
    std::ifstream file(path);return Json::parse(file);
}
fs::path Journal(const FileWork& work){return work.progress.destination/"staging/direct-install.json";}
void ValidateCommitted(FileWork& work) {
    const auto app=work.progress.destination/"app";
    if(FileVersion(app/"iyw-claw.exe")!=work.progress.version)
        throw std::runtime_error("环境已提交但应用版本不符，保留备份等待恢复。");
    VerifySignature(app/"iyw-claw.exe");
    VerifyEnvironmentBinary(EnvironmentHelper(work.progress.destination));
    if(!MissingPresetComponents(work).empty())
        throw std::runtime_error("环境提交后仍有预置组件缺失，已保留备份与恢复记录。");
}
bool EnvironmentWasCommitted(FileWork& work) {
    if(work.environmentTransaction.empty())return false;
    if(work.environmentTransaction.size()!=32||work.environmentTransaction.find_first_not_of("0123456789abcdef")!=std::string::npos)
        throw std::runtime_error("环境事务编号无效。");
    const auto receipt=UserHome()/".iyw-claw/inventory/transactions"/(work.environmentTransaction+".json");
    if(fs::exists(receipt)) {
        const auto value=ReadJSON(receipt);
        if(value.value("schemaVersion",0)==2&&value.at("generation")==work.environmentTransaction&&value.at("state")=="committed")return true;
    }
    const auto current=UserHome()/".iyw-claw/inventory/environment-current.json";
    if(!fs::exists(current))return false;
    // 环境库存包含较多组件文件记录；仅流式解析，不重新扫描组件内容。
    if(fs::file_size(current)>64*MiB)throw std::runtime_error("环境清单超过读取上限。");
    std::ifstream file(current);const auto snapshot=Json::parse(file);
    return snapshot.at("generation")==work.environmentTransaction&&snapshot.at("pcVersion")==work.progress.version;
}
void RemoveOwnedTree(const fs::path& path) {
    RequirePlainPath(path);if(!fs::exists(path))return;
    for(const auto& entry:fs::recursive_directory_iterator(path))RequirePlainPath(entry.path());
    fs::remove_all(path);
}
void Rollback(FileWork& work) {
    const auto selected=ReadJSON(work.deployment/"selection.json");
    const auto app=work.progress.destination/"app",backup=work.deployment/"previous-app";
    if(!selected.value("reused",false)) {
        if(fs::exists(backup)) {
            if(fs::exists(app))fs::rename(app,work.deployment/"failed-app");
            fs::rename(backup,app);
        } else if(!selected.at("oldApp").get<bool>()&&fs::exists(app)) {
            fs::rename(app,work.deployment/"failed-app");
        }
    }
    RestoreDirectRegistration(work);
    fs::remove(Journal(work));
    work.installerStarted=false;
    work.progress.events.push_back("未提交的应用部署已恢复；暂存诊断保留。");
}
void ConfirmCommit(FileWork& work) {
    ValidateCommitted(work);
    work.environmentCommitted=true;SaveDeployment(work,"committed");
    const auto selection=ReadJSON(work.deployment/"selection.json");
    WriteDurableJSON(work.progress.destination/"installer/direct-current.json",{{"schemaVersion",1},
        {"version",work.progress.version},{"sha256",selection.at("sha256")},{"environmentTransaction",work.environmentTransaction}});
    fs::remove(Journal(work));
    // 回收失败不改变已经提交的结果，保留有明确所有权的旧 app 备份。
    try{RemoveOwnedTree(work.deployment/"previous-app");}
    catch(const std::exception&){work.progress.events.push_back("安装成功，旧应用备份暂留 staging。");}
}
void RecoverEnvironment(FileWork& work) {
    const auto helper=EnvironmentHelper(work.progress.destination);VerifyEnvironmentBinary(helper);
    if(work.staging.empty())work.staging=CreateStaging();
    if(work.environmentTransaction.size()!=32||work.environmentTransaction.find_first_not_of("0123456789abcdef")!=std::string::npos)
        throw std::runtime_error("恢复环境事务编号无效。");
    const auto code=RunCommand({helper,{"install","--phase","recover","--transaction-id",work.environmentTransaction},work.staging/"environment-recover.log",120});
    if(code!=0)throw std::runtime_error("环境事务恢复失败，已保留应用与环境备份。");
}
void LoadRecovery(FileWork& work) {
    const auto journal=ReadJSON(Journal(work));
    const auto name=journal.at("directory").get<std::string>();
    const auto state=journal.at("state").get<std::string>();
    const std::vector<std::string> states{"prepared","backed-up","swapped","environment-prepared","environment-committing","committed"};
    if(journal.at("schemaVersion")!=1||name.rfind("direct-yuan-setup-",0)!=0||name.size()>80||
       name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-{}")!=std::string::npos||
       std::find(states.begin(),states.end(),state)==states.end())
        throw std::runtime_error("上次安装事务记录无效，已保留现场。");
    work.deployment=work.progress.destination/"staging"/fs::u8path(name);
    const auto selection=ReadJSON(work.deployment/"selection.json");
    if(selection.at("version")!=journal.at("version")||
       fs::u8path(selection.at("root").get<std::string>())!=work.progress.destination.lexically_normal())
        throw std::runtime_error("安装恢复记录与当前目录或版本不一致。");
    selection.at("oldApp").get<bool>();selection.at("reused").get<bool>();
    work.environmentTransaction=journal.at("environmentTransaction");work.progress.version=journal.at("version");
    work.progress.presets.clear();
    for(const auto& item:selection.at("presets"))work.progress.presets.push_back({item.at("id"),item.at("version"),item.at("versionId"),item.at("artifactId"),item.at("sha256"),item.at("bytes")});
    RequirePlainPath(work.progress.destination/"app");
}
}
void WriteDurableJSON(const fs::path& path,const Json& value) {
    RequirePlainPath(path);fs::create_directories(path.parent_path());
    GUID id{};wchar_t textId[40]{};
    if(FAILED(CoCreateGuid(&id))||!StringFromGUID2(id,textId,40))throw std::runtime_error("无法创建事务写入标识。");
    const auto temporary=fs::path(path.wstring()+L"."+textId+L".new");RequirePlainPath(temporary);
    const auto text=value.dump();
    Handle file(CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr));
    DWORD count=0;
    if(file.get()==INVALID_HANDLE_VALUE||!WriteFile(file.get(),text.data(),static_cast<DWORD>(text.size()),&count,nullptr)||count!=text.size()||!FlushFileBuffers(file.get()))
        throw std::runtime_error("无法持久化安装事务。");
    file.reset();
    constexpr unsigned RetryCount=20;constexpr DWORD RetryDelayMs=15;
    for(unsigned attempt=0;attempt<RetryCount;++attempt) {
        if(MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))return;
        const auto error=GetLastError();
        if(error!=ERROR_SHARING_VIOLATION&&error!=ERROR_ACCESS_DENIED)break;
        Sleep(RetryDelayMs);
    }
    throw std::runtime_error("无法提交安装事务记录。");
}
void SaveDeployment(FileWork& work,const std::string& state) {
    WriteDurableJSON(Journal(work),{{"schemaVersion",1},{"directory",work.deployment.filename().u8string()},
        {"state",state},{"version",work.progress.version},{"environmentTransaction",work.environmentTransaction}});
}
void LockDirectInstall(FileWork& work) {
    if(work.installLock)return;
    RequirePlainPath(work.progress.destination);
    Handle lock(CreateMutexW(nullptr,FALSE,L"Global\\IywClawInstallerTransactionV1"));
    if(!lock||GetLastError()==ERROR_ALREADY_EXISTS)throw std::runtime_error("另一个安装或卸载正在进行，请稍后重试。");
    work.installLock=std::shared_ptr<void>(lock.release(),HandleClose{});
    if(fs::exists(work.progress.destination/"staging/installer-app-pending-v1"))throw std::runtime_error("旧安装器仍有未完成事务，请先完成旧安装恢复。");
}
void RecoverDirectInstall(FileWork& work) {
    LockDirectInstall(work);
    if(!fs::exists(Journal(work)))return;
    // 恢复使用独立上下文；失败时本次安装的异常清理不能覆盖旧事务。
    Snapshot previous=work.progress;
    FileWork recovery{work.job,work.staging,previous,work.started,{}};
    LoadRecovery(recovery);
    const auto state=ReadJSON(Journal(recovery)).at("state").get<std::string>();
    StopInstalledProcesses(previous.destination);
    if(state=="committed"||EnvironmentWasCommitted(recovery))ConfirmCommit(recovery);
    else if(state=="environment-committing") {
        RecoverEnvironment(recovery);
        if(EnvironmentWasCommitted(recovery))ConfirmCommit(recovery);else Rollback(recovery);
    } else Rollback(recovery);
    work.progress.events.push_back("上次中断的安装事务已恢复。");
}
void WriteDeploymentSelection(const Release& release,FileWork& work,bool reused) {
    Json presets=Json::array();
    for(const auto& item:work.progress.presets)presets.push_back({{"id",item.id},{"version",item.version},{"versionId",item.versionId},{"artifactId",item.artifactId},{"sha256",item.sha256},{"bytes",item.bytes}});
    WriteDurableJSON(work.deployment/"selection.json",{{"root",work.progress.destination.lexically_normal().u8string()},
        {"version",release.version},{"sha256",release.sha256},{"oldApp",fs::exists(work.progress.destination/"app")},{"reused",reused},{"presets",presets}});
}
void BeginDirectInstall(const Release& release,FileWork& work) {
    if(CurrentPlatform().target!="windows"||CurrentPlatform().arch!="x86_64")throw std::runtime_error("当前系统不支持该应用载荷。");
    const auto root=work.progress.destination;RequirePlainPath(root);
    work.deployment=root/"staging"/("direct-"+work.staging.filename().u8string());
    RequirePlainPath(work.deployment);fs::create_directories(work.deployment);
    BeginStage(work,Phase::Installing);
    ExtractPayload(work);ValidatePayloadTree(work.deployment/"incoming",release);EnsureWebView(work);
    CheckCancel(work.job);
    StopInstalledProcesses(root);
    const auto app=root/"app";
    if(fs::exists(app)&&!fs::is_empty(app))VerifySignature(app/"iyw-claw.exe");
    WriteDeploymentSelection(release,work,false);
    SaveDeployment(work,"prepared");
    if(fs::exists(app))fs::rename(app,work.deployment/"previous-app");
    SaveDeployment(work,"backed-up");
    fs::rename(work.deployment/"incoming/app",app);
    work.installerStarted=true;SaveDeployment(work,"swapped");
    work.progress.events.push_back("新向导直接部署应用文件，等待环境提交。");
}
void FinishDirectInstall(FileWork& work,bool success) {
    if(work.deployment.empty()||!fs::exists(Journal(work)))return;
    if(ReadJSON(Journal(work)).at("directory")!=work.deployment.filename().u8string())
        throw std::runtime_error("安装恢复记录不属于本次部署，保留现场。");
    if(!success) {
        if(EnvironmentWasCommitted(work)) {
            work.progress.events.push_back("环境已提交，保留应用备份与恢复记录，重新运行向导后校验恢复。");
        }
        else if(ReadJSON(Journal(work)).at("state")=="environment-committing") {
            RecoverEnvironment(work);
            if(!EnvironmentWasCommitted(work))Rollback(work);
        }
        else Rollback(work);
        return;
    }
    SaveDeployment(work,"environment-committing");
    const auto helper=EnvironmentHelper(work.progress.destination);
    const auto code=RunCommand({helper,{"install","--phase","commit","--transaction-id",work.environmentTransaction},work.staging/"environment-commit.log",0},[&]{Advance(work,0);});
    if(!EnvironmentWasCommitted(work)) {
        if(code!=0){RecoverEnvironment(work);SaveDeployment(work,"environment-prepared");throw std::runtime_error("环境提交失败，应用将恢复旧版本。");}
        throw std::runtime_error("环境助手未生成正确提交回执。");
    }
    ConfirmCommit(work);
}
}
