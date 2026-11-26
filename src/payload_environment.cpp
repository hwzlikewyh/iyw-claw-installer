#include "payload.h"
#include "windows_support.h"
#include "command.h"
#include <fstream>
#include <algorithm>
#include <stdexcept>
namespace setup {
namespace {
void ValidatePrepared(const nlohmann::json& state, FileWork& work) {
    if(state.at("pcVersion")!=work.progress.version||state.at("target")!="windows"||state.at("arch")!="x86_64")
        throw std::runtime_error("准备的环境事务与应用版本不匹配。");
    for(const auto& expected:work.progress.presets) {
        const auto& items=state.at("components");
        const auto found=std::find_if(items.begin(),items.end(),[&](const auto& item){return item.at("component").at("componentId")==expected.id;});
        if(found==items.end()&&!expected.optional&&!IsOptionalPreset(expected.id))throw std::runtime_error("环境助手缺少必需预置组件："+expected.id);
        if(found==items.end())continue;
        const auto& actual=found->at("component");
        const bool identityMatches=actual.at("artifactId")==expected.artifactId&&actual.at("artifactSha256")==expected.sha256&&actual.at("version")==expected.version;
        if(!identityMatches)
            throw std::runtime_error("环境助手组件身份与所选计划不一致："+expected.id);
    }
}
}
void PrepareDirectEnvironment(const Release& release, FileWork& work, bool beforeExit) {
    if(!work.environmentTransaction.empty())return;
    BeginStage(work,beforeExit?Phase::PresetDownload:Phase::Environment);
    const auto staged=work.deployment/"incoming/app/iyw-environment.exe";
    const auto helper=!work.deployment.empty()&&fs::is_regular_file(staged)?staged:EnvironmentHelper(work.progress.destination);
    VerifyEnvironmentBinary(helper);
    const auto output=work.staging/"environment-identity.json";
    if(RunCommand({helper,{"--identity"},output,15,work.staging})!=0)throw std::runtime_error("环境助手无法运行。");
    const auto identity=nlohmann::json::parse(ReadOutput(output));
    if(identity.at("distributionTarget")!="windows"||identity.at("target")!="x86_64-pc-windows-msvc"||identity.value("installerProtocol",0)!=2)
        throw std::runtime_error("环境助手系统或架构不匹配。");
    work.progress.logPath=work.staging/"environment-prepare.log";
    const auto progressFile = work.staging / "environment-progress.ini";
    const std::vector<std::string> args{"install","--phase","prepare","--app-version",release.version,"--progress-file",progressFile.u8string()};
    work.progress.detail="快速核对已有环境，只准备缺失或变更组件";work.job.Publish(work.progress);
    const auto code=RunCommand({helper,args,work.progress.logPath,0,work.staging},[&]{PollEnvironmentProgress(work);});
    if(code!=0)
        throw std::runtime_error("运行环境准备失败，退出码 "+std::to_string(code)+"。详见本次安装目录中的 environment-prepare.log.stderr。");
    // 与正式安装器共用本次助手的进度回执，避免读取全局 pending 指针。
    std::ifstream receipt(progressFile);
    std::string id, line;
    while(std::getline(receipt,line)){
        if(!line.empty()&&line.back()=='\r')line.pop_back();
        if(line.rfind("Transaction=",0)==0)id=line.substr(sizeof("Transaction=")-1);
    }
    if(id.size()!=32||id.find_first_not_of("0123456789abcdef")!=std::string::npos)throw std::runtime_error("环境事务编号无效。");
    const auto prepared=UserHome()/setup::Brand().userDirectory/"staging/environment"/id/"prepared.json";
    if(fs::file_size(prepared)>64*MiB)throw std::runtime_error("环境事务清单过大。");
    std::ifstream file(prepared);const auto state=nlohmann::json::parse(file);
    if(state.at("transactionId")!=id)throw std::runtime_error("环境事务编号不匹配。");
    ValidatePrepared(state,work);
    work.environmentTransaction=id;
    if(work.deployment.empty()) {
        const auto directory=work.progress.destination/"staging"/("direct-"+work.staging.filename().u8string());
        RequirePlainPath(directory);fs::create_directories(directory);work.deployment=directory;
        WriteDeploymentSelection(release,work,true);
    }
    // 这里只准备不可变组件；应用退出、旧事务恢复后才写全局安装事务。
}
}
