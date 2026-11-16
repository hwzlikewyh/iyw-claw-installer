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
        if(found==items.end()&& !IsOptionalPreset(expected.id))throw std::runtime_error("环境助手缺少必需预置组件："+expected.id);
        if(found==items.end())continue;
        const auto& actual=found->at("component");
        if(actual.at("artifactId")!=expected.artifactId||actual.at("artifactSha256")!=expected.sha256||actual.at("version")!=expected.version)
            throw std::runtime_error("环境助手组件身份与所选计划不一致："+expected.id);
    }
}
}
void PrepareDirectEnvironment(const Release& release, FileWork& work) {
    if(!work.environmentTransaction.empty())return;
    BeginStage(work,Phase::Environment);
    const auto staged=work.deployment/"incoming/app/iyw-environment.exe";
    const auto helper=fs::is_regular_file(staged)?staged:EnvironmentHelper(work.progress.destination);
    VerifyEnvironmentBinary(helper);
    const auto output=work.staging/"environment-identity.json";
    if(RunCommand({helper,{"--identity"},output,15})!=0)throw std::runtime_error("环境助手无法运行。");
    const auto identity=nlohmann::json::parse(ReadOutput(output));
    if(identity.at("distributionTarget")!="windows"||identity.at("target")!="x86_64-pc-windows-msvc"||identity.value("installerProtocol",0)!=2)
        throw std::runtime_error("环境助手系统或架构不匹配。");
    work.progress.logPath=work.staging/"environment-prepare.log";
    const std::vector<std::string> args{"install","--phase","prepare","--app-version",release.version,"--initialize-defaults","--progress-file",(work.staging/"environment-progress.ini").u8string()};
    work.progress.detail="快速核对已有环境，只准备缺失或变更组件";work.job.Publish(work.progress);
    if(RunCommand({helper,args,work.progress.logPath,0},[&]{PollEnvironmentProgress(work);})!=0)
        throw std::runtime_error("运行环境准备失败，请查看环境准备日志。");
    // 使用此次助手进程的回执，避免其他环境操作覆盖全局 pending 指针。
    const auto log=ReadOutput(work.progress.logPath);
    const auto marker=log.rfind("prepared-transaction:");
    if(marker==std::string::npos)throw std::runtime_error("环境助手缺少准备回执。");
    const auto id=log.substr(marker+21,32);
    if(id.size()!=32||id.find_first_not_of("0123456789abcdef")!=std::string::npos)throw std::runtime_error("环境事务编号无效。");
    const auto prepared=UserHome()/".iyw-claw/staging/environment"/id/"prepared.json";
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
    SaveDeployment(work,"environment-prepared");
}
}
