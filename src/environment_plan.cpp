#include "release.h"
#include <nlohmann/json.hpp>
#include <set>
#include <stdexcept>
#include <algorithm>
#include <cctype>
namespace setup {
void ResolvePresetEnvironment(const Release& release,FileWork& work){
    using Json=nlohmann::json;const auto platform=CurrentPlatform();
    const Json request={{"schemaVersion",1},{"installationId","yuan-installer-environment"},
        {"clientVersion",release.version},{"pcVersion",release.version},{"channel","stable"},
        {"runtime","desktop"},{"target",platform.target},{"arch",platform.arch},{"inventory",Json::array()}};
    std::string response;
    PostHttps(std::string(FusionBase)+"/app-updates/v1/environment/resolve",request.dump(),
        [&](const char* bytes,std::size_t count){if(response.size()+count>MiB)throw std::runtime_error("环境计划响应过大。");response.append(bytes,count);},work.job);
    try{
        const auto envelope=Json::parse(response);
        if(envelope.at("code")!=1)throw std::runtime_error("Fusion 尚未提供此应用版本与系统的预置环境计划。");
        const auto& plan=envelope.at("data");
        if(plan.at("pcVersion")!=release.version||plan.at("target")!=platform.target||plan.at("arch")!=platform.arch)
            throw std::runtime_error("预置环境计划与当前安装目标不匹配。");
        std::set<std::string> seen;work.progress.presets.clear();
        if(!plan.at("actions").is_array()||plan.at("actions").size()>32)throw std::runtime_error("预置环境清单格式无效。");
        for(const auto& action:plan.at("actions")){
            const auto& artifact=action.at("artifact");
            PresetComponent component{action.at("componentId"),action.at("version"),artifact.at("versionId"),
                artifact.at("artifactId"),artifact.at("sha256"),artifact.at("size")};
            if(component.id.empty()||component.id.size()>64||component.id.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789-")!=std::string::npos||
                !seen.insert(component.id).second||component.bytes>4ULL*1024*MiB||component.bytes==0||component.sha256.size()!=64||
                !std::all_of(component.sha256.begin(),component.sha256.end(),[](unsigned char c){return std::isxdigit(c);}))
                throw std::runtime_error("预置组件身份、大小或摘要无效。");
            work.progress.presets.push_back(std::move(component));
            work.componentURLs[action.at("componentId").get<std::string>()]=artifact.value("url",std::string());
        }
        if(platform.target=="windows7"&&seen.empty()){
            work.progress.events.push_back("Win7 最低可用模式：不下载外部运行环境组件，仅保留主程序对话能力。");
            work.job.Publish(work.progress);return;
        }
        for(const auto* required:{"node","git","uv"})if(!seen.count(required))throw std::runtime_error("环境计划缺少必要组件："+std::string(required));
        std::string names;for(const auto& item:work.progress.presets){if(!names.empty())names+=", ";names+=item.id;}
        work.progress.events.push_back("Fusion 预置组件："+names);
        work.progress.detail="已取得 "+std::to_string(work.progress.presets.size())+" 个预置组件的安装计划";
        work.job.Publish(work.progress);
    }catch(const Json::exception&){throw std::runtime_error("Fusion 预置环境计划响应无效。");}
}
}
