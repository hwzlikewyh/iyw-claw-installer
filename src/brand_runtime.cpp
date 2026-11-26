#include "installer.h"
#include "release.h"
#include <nlohmann/json.hpp>
#include <wx/utils.h>
#include <fstream>
#include <regex>
#include <stdexcept>

namespace setup {
namespace {
BrandSettings& MutableBrand(){
    static BrandSettings settings{IYW_BRAND_ID,IYW_APP_NAME,IYW_INSTALLER_NAME,IYW_IDENTIFIER,
    IYW_SCHEME,IYW_USER_DIRECTORY,IYW_PRODUCT,IYW_RELEASE_ID,IYW_APP_NAME_W,
    IYW_IDENTIFIER_W,IYW_SCHEME_W,IYW_INSTALL_REGISTRY_W,IYW_UNINSTALL_REGISTRY_W};
    return settings;
}

void Validate(const std::string& id,const std::string& name,const std::string& release) {
    if(!std::regex_match(id,std::regex("[a-z][a-z0-9]{1,31}")) || name.empty() || name.size()>256 ||
        name.find_first_of("<>:\"/\\|?*;\r\n")!=std::string::npos || name.back()=='.' || name.back()==' ')
        throw std::runtime_error("已安装品牌的标识或名称无效。");
    for(const unsigned char value:name)if(value<32)throw std::runtime_error("已安装品牌名称包含控制字符。");
    if(release.size()>19||release.find_first_not_of("0123456789")!=std::string::npos)
        throw std::runtime_error("已安装品牌的候选发布编号无效。");
}
}

const BrandSettings& Brand(){return MutableBrand();}

void PrepareBrandEnvironment(){
    const auto& settings=Brand();
    wxString inherited;
    if(wxGetEnv("IYW_CLAW_BRAND_ID",&inherited)&&inherited!=wxString::FromUTF8(settings.id)) {
        for(const auto* key:{"IYW_CLAW_HOME","IYW_CLAW_DATA_DIR","IYW_CLAW_USER_MEMORY_DIR",
            "IYW_CLAW_LOG_DIR","IYW_CLAW_INSTALL_ROOT","IYW_CLAW_BRAND_IDENTIFIER"})wxUnsetEnv(key);
    }
    wxSetEnv("IYW_CLAW_BRAND_HOME",wxString::FromUTF8((UserHome()/settings.userDirectory).u8string()));
    wxSetEnv("IYW_CLAW_BRAND_ID",wxString::FromUTF8(settings.id));
}

void LoadInstalledBrand(const fs::path& root){
    const auto directory=BrandConfigurationDirectory(root),path=directory/"branding.json";
    if(!fs::exists(path))return;
    if(fs::is_symlink(path)||fs::file_size(path)>64*1024)throw std::runtime_error("已安装品牌配置无效。");
    std::ifstream stream(path);
    const auto value=nlohmann::json::parse(stream);
    const auto id=value.at("brandId").get<std::string>(),name=value.at("displayName").get<std::string>();
    const auto release=value.value("releaseId",std::string());
    Validate(id,name,release);
    const auto suffix=id=="iyw"?std::string():"-"+id;
    BrandSettings loaded;
    loaded.id=id;loaded.name=name;loaded.nameW=fs::u8path(name).wstring();
    loaded.installerName=name+"安装向导";loaded.identifier="app.iywclaw"+(id=="iyw"?std::string():"."+id);
    loaded.identifierW=fs::u8path(loaded.identifier).wstring();loaded.scheme="iyw-assistant"+suffix;
    loaded.schemeW=fs::u8path(loaded.scheme).wstring();loaded.product="iyw-claw"+suffix;
    loaded.userDirectory=".iyw-claw"+suffix;loaded.releaseId=release;
    loaded.installKey=L"Software\\iywclaw\\"+fs::u8path(loaded.product).wstring();
    loaded.uninstallKey=L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\"+
        (id=="iyw"?std::wstring(L"原助理"):loaded.identifierW);
    const auto icon=fs::u8path(value.at("iconFile").get<std::string>());
    if(icon.empty()||icon.is_absolute())throw std::runtime_error("已安装品牌图标路径无效。");
    for(const auto& part:icon)if(part==".."||part==".")throw std::runtime_error("已安装品牌图标路径越界。");
    loaded.icon=directory/icon;loaded.preserve=true;
    if(!fs::is_regular_file(loaded.icon))throw std::runtime_error("已安装品牌图标缺失。");
    MutableBrand()=std::move(loaded);
    PrepareBrandEnvironment();
}
}
