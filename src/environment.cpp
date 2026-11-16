#include "release.h"
#include "command.h"
#ifdef _WIN32
#include "payload.h"
#endif
#include <nlohmann/json.hpp>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <algorithm>
#include <map>
namespace setup {
using Json=nlohmann::json;
std::string ReadOutput(const fs::path& path){
    std::ifstream file(path,std::ios::binary);if(!file)return {};
    std::string text;char buffer[4096];
    while(file.read(buffer,sizeof(buffer))||file.gcount()){
        text.append(buffer,static_cast<std::size_t>(file.gcount()));
        if(text.size()>MiB)throw std::runtime_error("环境检查响应过大。");
    }
    return text;
}
namespace {
Json Inventory(){
    const auto path=UserHome()/".iyw-claw/inventory/environment-current.json";
    if(!fs::is_regular_file(path))return {};
    try{
        if(fs::file_size(path)>8*MiB)return {};
        std::ifstream file(path);auto value=Json::parse(file);
        if(!value.is_object()||!value.contains("components")||!value["components"].is_array()||
            !value.contains("schemaVersion")||!value["schemaVersion"].is_number_integer())return {};
        for(const auto* field:{"pcVersion","target","arch"})if(!value.contains(field)||!value[field].is_string())return {};
        for(const auto& item:value["components"])if(!item.is_object()||!item.contains("componentId")||!item["componentId"].is_string())return {};
        return value;
    }catch(const Json::exception&){return {};}
}
std::vector<std::string> RequiredMissing(const std::vector<std::string>& missing) {
    std::vector<std::string> required;
    for(const auto& id:missing)if(!IsOptionalPreset(id))required.push_back(id);
    return required;
}
bool Compatible(const Json& inventory,const Release& release){
    const auto platform=CurrentPlatform();
    if(!inventory.is_object()||inventory.value("schemaVersion",0)!=1||inventory.value("pcVersion","")!=release.version||
        inventory.value("target","")!=platform.target||inventory.value("arch","")!=platform.arch||
        !inventory.contains("components")||!inventory["components"].is_array())return false;
    for(const auto* name:{"node","git","uv"}){
        if(std::none_of(inventory["components"].begin(),inventory["components"].end(),
            [&](const Json& item){return item.is_object()&&item.value("componentId","")==name;}))return false;
    }
    return true;
}
}
void PollEnvironmentProgress(FileWork& work){
    std::ifstream file(work.staging/"environment-progress.ini");std::string line,phase;unsigned value=0;
    while(std::getline(file,line)){
        if(!line.empty()&&line.back()=='\r')line.pop_back();
        if(line.rfind("Phase=",0)==0)phase=line.substr(6);
        if(line.rfind("Value=",0)==0){try{value=static_cast<unsigned>(std::stoul(line.substr(6)));}catch(...){value=0;}}
    }
    static const std::map<std::string,std::string> names={{"checking","检查已有组件"},{"resolving","查询环境计划"},
        {"downloading","下载缺失或损坏的组件"},{"extracting","解压环境组件"},{"verifying","校验组件"},
        {"reusing","复用健康组件"},{"commit","提交运行环境"},{"committed","运行环境已就绪"}};
    const auto found=names.find(phase);work.progress.detail=found==names.end()?"环境助手正在处理组件":found->second;
    const auto log=work.progress.logPath;
    if(fs::is_regular_file(log)){
        std::ifstream output(log,std::ios::binary);const auto size=fs::file_size(log);
        if(size>8192)output.seekg(static_cast<std::streamoff>(size-8192));
        while(std::getline(output,line)){
            try{const auto event=Json::parse(line);const auto id=event.value("componentId",std::string());
                if(!id.empty()&&id!="environment")work.progress.detail=(found==names.end()?"准备组件":found->second)+" · "+id;
            }catch(const Json::exception&){}
        }
    }
    work.progress.phaseTotal=10000;work.progress.phaseDone=std::min(value,10000u);work.job.Publish(work.progress);
}
void CheckEnvironment(FileWork& work){
    const auto inventory=Inventory();
    if(!inventory.is_object())work.progress.checks.push_back("运行环境清单：未找到有效记录，安装后将补齐。");
    else{
        const auto components=inventory.value("components",Json::array());
        if(!components.is_array()){work.progress.checks.push_back("运行环境清单结构异常，将重新准备。");return;}
        for(const auto* name:{"node","git","uv"}){
            const bool found=std::any_of(components.begin(),components.end(),[&](const Json& item){return item.is_object()&&item.value("componentId","")==name;});
            work.progress.checks.push_back(std::string(name)+(found?"：已有受管记录，安装后检查健康状态。":"：缺少受管记录，将由环境助手补齐。"));
        }
    }
    work.job.Publish(work.progress);
}
void PrepareEnvironment(const Release& release,FileWork& work,bool preparedByInstaller){
#ifdef _WIN32
    if(work.directPayload){PrepareDirectEnvironment(release,work);return;}
#endif
    BeginStage(work,Phase::Environment);
    const auto helper=EnvironmentHelper(work.progress.destination);
    if(!fs::is_regular_file(helper))throw std::runtime_error("缺少应用自带的环境助手，请重新下载安装包。");
    if(work.retainedFiles.size()!=2)VerifyEnvironmentBinary(helper);
    const auto identity=work.staging/"helper-identity.json";
    if(RunCommand({helper,{"--identity"},identity,15})!=0)throw std::runtime_error("环境助手无法在当前系统运行。");
    try{
        const auto platform=CurrentPlatform();const auto actual=Json::parse(ReadOutput(identity));
        if(actual.value("distributionTarget","")!=platform.target)throw std::runtime_error("环境助手与当前系统不匹配。");
        const auto target=actual.value("target",std::string());
        const std::string expected=platform.arch=="aarch64"?"aarch64-":platform.arch=="i686"?"i686-":"x86_64-";
        if(target.rfind(expected,0)!=0)throw std::runtime_error("环境助手 CPU 架构与本机不匹配。");
    }catch(const Json::exception&){throw std::runtime_error("环境助手身份无效。");}
    const auto missing=MissingPresetComponents(work);
    const bool reusable=Compatible(Inventory(),release)&&missing.empty();
    work.progress.reused[static_cast<unsigned>(StageIndex(Phase::Environment))]=reusable;
    if(!reusable){
        if (preparedByInstaller) {
            const auto requiredMissing=RequiredMissing(missing);
            if(requiredMissing.empty()) {
                if(!missing.empty())work.progress.events.push_back("可选浏览器组件暂未就绪，原助理仍可使用；需要浏览器能力时可在应用内修复。");
                return;
            }
            std::string names;
            for (const auto& item : requiredMissing) { if (!names.empty()) names += ", "; names += item; }
            throw std::runtime_error("正式安装器返回后，目标运行环境缺少组件或版本不匹配：" + names + "。请查看环境助手日志。");
        }
        work.progress.detail="正在补齐缺失或损坏的运行环境";work.job.Publish(work.progress);
        work.progress.logPath=work.staging/"environment-repair.log";
        std::string selected;
        for(const auto& id:missing){if(!selected.empty())selected+=",";selected+=id;}
        if(selected.empty())for(const auto& component:work.progress.presets){if(!selected.empty())selected+=",";selected+=component.id;}
        std::vector<std::string> args{"repair","--app-version",release.version,"--progress-file",(work.staging/"environment-progress.ini").u8string()};
        if(!selected.empty()){args.push_back("--components");args.push_back(selected);}
        const auto code=RunCommand({helper,args,work.progress.logPath,0},[&]{PollEnvironmentProgress(work);});
        if(code!=0)throw std::runtime_error("环境组件修复失败，退出码："+std::to_string(code)+"，日志："+work.progress.logPath.u8string());
        const auto remaining=MissingPresetComponents(work);
        const auto requiredRemaining=RequiredMissing(remaining);
        if(!requiredRemaining.empty()){
            std::string names;for(const auto& item:requiredRemaining){if(!names.empty())names+=", ";names+=item;}
            throw std::runtime_error("预置组件仍有缺项或版本不符："+names+"。已停止启动原助理。");
        }
        if(!Compatible(Inventory(),release)||!RequiredMissing(MissingPresetComponents(work)).empty())throw std::runtime_error("运行环境基础组件仍未完整就绪，已停止启动应用。");
    }
    work.progress.events.push_back(reusable?"全部预置组件匹配，轻量检查通过，跳过重新安装和全量摘要扫描。":"缺失或损坏的组件已修复并通过诊断。");
}
}
