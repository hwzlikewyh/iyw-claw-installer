#include "release.h"
#include <nlohmann/json.hpp>
#include <fstream>
namespace setup {
using Json=nlohmann::json;
namespace {
bool Committed(const Json& inventory){
    if(!inventory.contains("selectedComponents")||inventory["selectedComponents"].is_null())return true;
    const auto generation=inventory.value("generation",std::string());
    if(generation.empty()||generation.size()>128||generation=="."||generation==".."||
        generation.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_")!=std::string::npos)return false;
    const auto path=UserHome()/".iyw-claw/inventory/transactions"/(generation+".json");
    if(!fs::is_regular_file(path)||fs::file_size(path)>4096)return false;
    std::ifstream file(path);Json receipt;file>>receipt;
    return receipt.is_object()&&receipt.value("state",std::string())=="committed"&&receipt.value("generation",std::string())==generation;
}
fs::path RuntimePath(const std::string& value){
    const auto relative=fs::u8path(value);
    if(relative.empty()||relative.is_absolute())return {};
    for(const auto& part:relative)if(part=="..")return {};
    return UserHome()/".iyw-claw"/relative;
}
bool QuickEntries(const Json& component){
    if(!component.contains("relativePath")||!component["relativePath"].is_string()||
        !component.contains("entrypoints")||!component["entrypoints"].is_object()||component["entrypoints"].empty()||
        !component.contains("files")||!component["files"].is_array())return false;
    const auto root=RuntimePath(component["relativePath"]);if(root.empty()||!fs::is_directory(root))return false;
    for(const auto& entry:component["entrypoints"].items()){
        if(!entry.value().is_string())return false;
        const auto relative=fs::u8path(entry.value().get<std::string>());
        if(relative.is_absolute())return false;for(const auto& part:relative)if(part=="..")return false;
        const auto path=root/relative;std::error_code error;
        if(!fs::is_regular_file(path,error)||error)return false;
        const auto name=relative.generic_u8string();const auto size=fs::file_size(path,error);
        if(error)return false;bool recorded=false;
        for(const auto& record:component["files"]){
            if(!record.is_object()||record.value("path",std::string())!=name)continue;
            if(record.contains("linkTarget")&&record["linkTarget"].is_string()){
                if(!fs::is_symlink(path)||fs::read_symlink(path).generic_u8string()!=record["linkTarget"].get<std::string>())return false;
            }else if(!record.contains("size")||record["size"].get<std::uint64_t>()!=size)return false;
            recorded=true;break;
        }
        if(!recorded)return false;
    }
    return true;
}
}
std::vector<std::string> MissingPresetComponents(const FileWork& work){
    std::vector<std::string> missing;
    const auto path=UserHome()/".iyw-claw/inventory/environment-current.json";
    Json inventory;
    try{
        if(fs::is_regular_file(path)&&fs::file_size(path)<=8*MiB){std::ifstream file(path);file>>inventory;}
        const auto platform=CurrentPlatform();
        if(!inventory.is_object()||inventory.value("target",std::string())!=platform.target||
            inventory.value("arch",std::string())!=platform.arch||!inventory.contains("components")||!inventory["components"].is_array()||
            !Committed(inventory))inventory=Json();
    }catch(const Json::exception&){inventory=Json();}
    for(const auto& expected:work.progress.presets){
        bool valid=false;
        if(inventory.is_object())for(const auto& actual:inventory["components"]){
            try{
                if(!actual.is_object()||actual.value("componentId",std::string())!=expected.id)continue;
                valid=actual.value("version",std::string())==expected.version&&actual.value("versionId",std::string())==expected.versionId&&
                    actual.value("artifactId",std::string())==expected.artifactId&&actual.value("artifactSha256",std::string())==expected.sha256&&QuickEntries(actual);
            }catch(const std::exception&){valid=false;}
        }
        if(!valid)missing.push_back(expected.id);
    }
    return missing;
}
}
