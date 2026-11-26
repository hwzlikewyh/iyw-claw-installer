#include "payload.h"
#include "windows_support.h"
#include "command.h"
#include <shlobj.h>
#include <propkey.h>
#include <propsys.h>
#include <fstream>
#include <stdexcept>
namespace setup {
namespace {
using Json=nlohmann::json;



constexpr PROPERTYKEY AppUserModelIdKey=INIT_PKEY_AppUserModel_ID;
template<class Interface>
struct ComRelease{void operator()(Interface* value)const noexcept{if(value)value->Release();}};
struct KeyClose{void operator()(HKEY key)const{if(key)RegCloseKey(key);}};
using Key=std::unique_ptr<std::remove_pointer_t<HKEY>,KeyClose>;
Key OpenKey(const std::wstring& path){HKEY key=nullptr;if(RegCreateKeyExW(HKEY_CURRENT_USER,path.c_str(),0,nullptr,0,KEY_READ|KEY_WRITE,nullptr,&key,nullptr)!=ERROR_SUCCESS)throw std::runtime_error("无法写入应用注册信息。");return Key(key);}
void Set(const std::wstring& key,const std::wstring& name,const std::wstring& value){auto handle=OpenKey(key);if(RegSetValueExW(handle.get(),name.c_str(),0,REG_SZ,reinterpret_cast<const BYTE*>(value.c_str()),static_cast<DWORD>((value.size()+1)*sizeof(wchar_t)))!=ERROR_SUCCESS)throw std::runtime_error("无法保存安装信息。");}
std::optional<std::wstring> ReadValue(const std::wstring& key,const std::wstring& name){
    wchar_t value[32768]{};DWORD size=sizeof(value);
    const auto result=RegGetValueW(HKEY_CURRENT_USER,key.c_str(),name.c_str(),RRF_RT_REG_SZ,nullptr,value,&size);
    if(result==ERROR_FILE_NOT_FOUND)return std::nullopt;
    if(result!=ERROR_SUCCESS)throw std::runtime_error("无法读取已有安装注册信息。");
    return std::wstring(value);
}
std::wstring Read(const std::wstring& key,const std::wstring& name){return ReadValue(key,name).value_or(L"");}
fs::path UserFolder(int id){wchar_t path[MAX_PATH]{};if(FAILED(SHGetFolderPathW(nullptr,id,nullptr,SHGFP_TYPE_CURRENT,path)))throw std::runtime_error("无法读取快捷方式目录。");return path;}
fs::path PowerShell(){
    wchar_t windows[MAX_PATH]{};const auto size=GetWindowsDirectoryW(windows,MAX_PATH);
    if(!size||size>=MAX_PATH)throw std::runtime_error("无法定位 Windows PowerShell。");
    BOOL wow64=FALSE;IsWow64Process(GetCurrentProcess(),&wow64);
    return fs::path(windows)/(wow64?L"Sysnative":L"System32")/L"WindowsPowerShell/v1.0/powershell.exe";
}
std::vector<fs::path> Shortcuts(){return {UserFolder(CSIDL_DESKTOPDIRECTORY)/(setup::Brand().nameW + L".lnk"),UserFolder(CSIDL_PROGRAMS)/setup::Brand().nameW/(setup::Brand().nameW + L".lnk")};}
std::vector<std::pair<std::wstring,std::wstring>> Values(){return {{setup::Brand().installKey.c_str(),L"InstallRoot"},{setup::Brand().installKey.c_str(),L""},{setup::Brand().uninstallKey.c_str(),L"DisplayName"},{setup::Brand().uninstallKey.c_str(),L"DisplayVersion"},{setup::Brand().uninstallKey.c_str(),L"Publisher"},{setup::Brand().uninstallKey.c_str(),L"InstallLocation"},{setup::Brand().uninstallKey.c_str(),L"UninstallString"},{setup::Brand().uninstallKey.c_str(),L"DisplayIcon"},{L"Software\\Classes\\" + setup::Brand().schemeW,L""},{L"Software\\Classes\\" + setup::Brand().schemeW,L"URL Protocol"},{L"Software\\Classes\\" + setup::Brand().schemeW + L"\\shell\\open\\command",L""}};}
void BackupRegistration(FileWork& work){
    const auto path=work.deployment/"registration.json";if(fs::exists(path))return;
    Json values=Json::array();for(const auto& entry:Values()){
        const auto value=ReadValue(entry.first,entry.second);
        values.push_back({{"exists",value.has_value()},{"value",value?Json(fs::path(*value).u8string()):Json(nullptr)}});
    }
    unsigned index=0;Json shortcuts=Json::array();
    for(const auto& target:Shortcuts()){RequirePlainPath(target);const bool exists=fs::is_regular_file(target);shortcuts.push_back(exists);if(exists)fs::copy_file(target,work.deployment/("shortcut-"+std::to_string(index)+".lnk"));++index;}
    WriteDurableJSON(path,{{"values",values},{"shortcuts",shortcuts}});
}
fs::path InstallDirectWizard(FileWork& work){
    const auto source=CurrentInstaller();
    auto lock=OpenLocked(source);VerifySignature(source);
    const auto digest=HashFile(source);
    const auto destination=work.progress.destination/"installer"/fs::u8path(std::string(setup::Brand().installerName + "-")+digest+".exe");
    RequirePlainPath(destination);fs::create_directories(destination.parent_path());
    // 按内容存放，可从已安装向导更新自身；保留旧向导供事务回滚使用。
    if(fs::exists(destination)) {
        if(HashFile(destination)!=digest)throw std::runtime_error("已保存向导摘要不匹配。");
        return destination;
    }
    const auto temporary=work.deployment/"installer.new";
    fs::copy_file(source,temporary);VerifySignature(temporary);
    if(HashFile(temporary)!=digest)throw std::runtime_error("安装向导复制校验失败。");
    fs::rename(temporary,destination);
    return destination;
}
void Shortcut(const fs::path& path,const fs::path& executable,const fs::path& icon){
    RequirePlainPath(path);fs::create_directories(path.parent_path());
    IShellLinkW* rawLink=nullptr;
    if(FAILED(CoCreateInstance(CLSID_ShellLink,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&rawLink))))
        throw std::runtime_error("无法创建快捷方式。");
    std::unique_ptr<IShellLinkW,ComRelease<IShellLinkW>> link(rawLink);
    if(FAILED(link->SetPath(executable.c_str()))||
       FAILED(link->SetWorkingDirectory(executable.parent_path().c_str()))||
       FAILED(link->SetIconLocation(icon.c_str(),0)))
        throw std::runtime_error("无法设置快捷方式目标或图标。");
    IPropertyStore* rawProperties=nullptr;
    if(FAILED(link->QueryInterface(IID_PPV_ARGS(&rawProperties))))
        throw std::runtime_error("无法设置快捷方式应用标识。");
    std::unique_ptr<IPropertyStore,ComRelease<IPropertyStore>> properties(rawProperties);
    PROPVARIANT appId{};appId.vt=VT_LPWSTR;appId.pwszVal=const_cast<PWSTR>(setup::Brand().identifierW.c_str());
    if(FAILED(properties->SetValue(AppUserModelIdKey,appId))||
       FAILED(properties->Commit()))
        throw std::runtime_error("无法保存快捷方式应用标识。");
    IPersistFile* rawFile=nullptr;
    if(FAILED(link->QueryInterface(IID_PPV_ARGS(&rawFile))))
        throw std::runtime_error("无法保存快捷方式。");
    std::unique_ptr<IPersistFile,ComRelease<IPersistFile>> file(rawFile);
    if(FAILED(file->Save(path.c_str(),TRUE)))
        throw std::runtime_error("保存快捷方式失败。");
}
bool CopyShellResource(const fs::path& source,const fs::path& destination){
    RequirePlainPath(source);RequirePlainPath(destination);
    const auto digest=HashFile(source);
    if(fs::is_regular_file(destination)&&HashFile(destination)==digest)return false;
    fs::create_directories(destination.parent_path());
    fs::copy_file(source,destination,fs::copy_options::overwrite_existing);
    if(HashFile(destination)!=digest)throw std::runtime_error("任务栏图标资源复制校验失败。");
    return true;
}
bool RestoreShellResources(FileWork& work){
    const auto app=work.progress.destination/"app";const auto source=app/"native-share";
    const auto configPath=source/"config.json";RequirePlainPath(configPath);
    if(!fs::exists(configPath))return false;
    if(fs::file_size(configPath)>MiB)throw std::runtime_error("系统分享配置过大。");
    std::ifstream file(configPath);const auto config=Json::parse(file);
    if(!config.value("signed",false)||config.value("receiver",std::string{})!="main")return false;
    // 兼容旧应用包：身份包图标从 app 根目录解析，而旧注册脚本只检查 native-share。
    for(const auto* name:{"resources.pri","Assets/Square44x44Logo.png","Assets/Square150x150Logo.png"}){
        RequirePlainPath(source/name);
        if(!fs::is_regular_file(source/name))throw std::runtime_error("系统分享图标资源缺失。");
    }
    unsigned copied=0;
    for(const auto& entry:fs::directory_iterator(source)){
        const auto name=entry.path().filename();
        if(name.wstring().rfind(L"resources",0)==0&&name.extension()==L".pri")
            copied+=CopyShellResource(entry.path(),app/name);
    }
    for(const auto& entry:fs::directory_iterator(source/"Assets")){
        const auto name=entry.path().filename();
        if(name.extension()==L".png")copied+=CopyShellResource(entry.path(),app/"Assets"/name);
    }
    if(copied){
        SHChangeNotify(SHCNE_ASSOCCHANGED,SHCNF_IDLIST,nullptr,nullptr);
        work.progress.events.push_back("已补齐主程序目录的任务栏图标资源，文件数="+std::to_string(copied));
    }
    return true;
}
constexpr char RegisterMainIdentity[]=
#include "native_identity_script.inc"
;
std::string IdentityFailure(const fs::path& log,int code){
    constexpr std::size_t MaxErrorBytes=8192;
    std::ifstream errors(fs::path(log.wstring()+L".stderr"),std::ios::binary);
    std::string detail(MaxErrorBytes,'\0');errors.read(detail.data(),detail.size());
    detail.resize(static_cast<std::size_t>(errors.gcount()));
    if(detail.size()==MaxErrorBytes){const auto end=detail.find_last_of('\n');detail.resize(end==std::string::npos?0:end);}
    return "Windows 主程序身份修复或启动检查失败。退出码："+std::to_string(code)+
        "；日志："+log.filename().u8string()+".stderr"+(detail.empty()?"":"\n"+detail);
}
int RunMainIdentity(FileWork& work,const fs::path& log,bool restore){
    if(work.staging.empty())work.staging=CreateStaging();
    const auto script=work.staging/"native-identity.ps1",state=work.deployment/"native-identity.json";
    RequirePlainPath(script);RequirePlainPath(state);RequirePlainPath(fs::path(state.wstring()+L".new"));
    if(fs::exists(state)&&fs::file_size(state)>MiB)throw std::runtime_error("应用身份备份记录过大。");
    {std::ofstream file(script,std::ios::binary);file<<"\xEF\xBB\xBF"<<RegisterMainIdentity;
        file.close();if(!file)throw std::runtime_error("无法准备应用身份修复脚本。");}
    std::vector<std::string> args{"-NoLogo","-NoProfile","-NonInteractive","-ExecutionPolicy","Bypass",
        "-File",script.u8string(),"-AppDirectory",(work.progress.destination/"app").u8string(),"-StateFile",state.u8string()};
    if(restore)args.push_back("-Restore");
    return RunCommand({PowerShell(),args,log,180,work.staging});
}
void RegisterNativeShare(FileWork& work){
    if(Brand().id!="iyw")return;
    const auto script=work.progress.destination/fs::u8path("app/native-share/register.ps1");
    const bool required=RestoreShellResources(work);
    const auto log=work.staging/"native-share-register.log";
    try{
        RequirePlainPath(script);
        if(!required&&!fs::is_regular_file(script))return;
        constexpr int unsupportedIdentity=23;
        const auto code=required?RunMainIdentity(work,log,false):RunCommand({PowerShell(),
            {"-NoLogo","-NoProfile","-NonInteractive","-ExecutionPolicy","Bypass","-File",script.u8string()},log,180,script.parent_path()});
        if(required&&code==unsupportedIdentity){work.progress.events.push_back("此 Windows 版本不支持系统分享身份，已跳过。");return;}
        if(required&&code!=0)throw std::runtime_error(IdentityFailure(log,code));
        work.progress.events.push_back(code==0?"系统分享入口已为当前用户注册。":"系统分享入口暂未注册，原助理启动时会自动检查并重试。注册退出码="+std::to_string(code));
    }catch(const std::exception& error){if(required)throw;work.progress.events.push_back(std::string("系统分享入口暂未注册，原助理启动时会自动检查并重试：")+error.what());}
}
}
fs::path CurrentInstaller(){wchar_t path[32768]{};const auto size=GetModuleFileNameW(nullptr,path,32768);if(!size||size>=32768)throw std::runtime_error("无法定位安装向导。");return fs::path(path);}
void RegisterDirectInstall(const Release& release,FileWork& work){
    BackupRegistration(work);
    const auto root=work.progress.destination;const auto exe=root/"app/iyw-claw.exe";
    const auto uninstall=InstallDirectWizard(work);
    Set(setup::Brand().installKey.c_str(),L"InstallRoot",root.wstring());Set(setup::Brand().installKey.c_str(),L"",root.wstring());
    Set(setup::Brand().uninstallKey.c_str(),L"DisplayName",setup::Brand().nameW);Set(setup::Brand().uninstallKey.c_str(),L"DisplayVersion",fs::u8path(release.version).wstring());
    Set(setup::Brand().uninstallKey.c_str(),L"Publisher",L"厦门市虹约产品设计有限公司");Set(setup::Brand().uninstallKey.c_str(),L"InstallLocation",root.wstring());
    Set(setup::Brand().uninstallKey.c_str(),L"UninstallString",L"\""+uninstall.wstring()+L"\" --uninstall-root \""+root.wstring()+L"\"");
    const auto icon=InstalledBrandIcon(root);
    Set(setup::Brand().uninstallKey.c_str(),L"DisplayIcon",icon.wstring());
    Set(L"Software\\Classes\\" + setup::Brand().schemeW,L"",L"URL:原助理");Set(L"Software\\Classes\\" + setup::Brand().schemeW,L"URL Protocol",L"");
    Set(L"Software\\Classes\\" + setup::Brand().schemeW + L"\\shell\\open\\command",L"",L"\""+exe.wstring()+L"\" \"%1\"");
    const auto status=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    if(FAILED(status))throw std::runtime_error("无法初始化快捷方式服务。");
    try{for(const auto& target:Shortcuts())Shortcut(target,exe,icon);}catch(...){CoUninitialize();throw;}
    CoUninitialize();
    RegisterNativeShare(work);
}
void RestoreDirectRegistration(FileWork& work){
    const auto path=work.deployment/"registration.json";if(!fs::exists(path))return;
    RequirePlainPath(path);
    if(fs::file_size(path)>MiB)throw std::runtime_error("安装注册备份过大。");
    std::ifstream file(path);const auto saved=Json::parse(file);const auto names=Values();
    if(saved.at("values").size()!=names.size())throw std::runtime_error("安装注册备份无效。");
    for(std::size_t i=0;i<names.size();++i){const auto& item=saved.at("values").at(i);
        if(item.at("exists").get<bool>())Set(names[i].first,names[i].second,fs::u8path(item.at("value").get<std::string>()).wstring());
        else{auto key=OpenKey(names[i].first);const auto status=RegDeleteValueW(key.get(),names[i].second.c_str());
            if(status!=ERROR_SUCCESS&&status!=ERROR_FILE_NOT_FOUND)throw std::runtime_error("恢复安装注册信息失败。");}}
    unsigned index=0;for(const auto& target:Shortcuts()){
        RequirePlainPath(target);const auto backup=work.deployment/("shortcut-"+std::to_string(index)+".lnk");RequirePlainPath(backup);
        if(saved.at("shortcuts").at(index).get<bool>())fs::copy_file(backup,target,fs::copy_options::overwrite_existing);
        else fs::remove(target);++index;
    }
    if(fs::exists(work.deployment/"native-identity.json")){
        try{
            if(work.staging.empty())work.staging=CreateStaging();
            const auto log=work.staging/"native-share-restore.log";
            const auto code=RunMainIdentity(work,log,true);
            if(code!=0)throw std::runtime_error(IdentityFailure(log,code));
            work.progress.events.push_back("原 Windows 应用身份已恢复。");
        }catch(const std::exception& error){
            work.progress.events.push_back(std::string("应用文件已回滚，原身份暂未恢复；重装时将再次修复：")+error.what());
        }
    }
}
void UninstallDirect(const fs::path& root){
    if(root.empty()||!root.is_absolute()||root==root.root_path()||fs::path(Read(setup::Brand().installKey.c_str(),L"InstallRoot"))!=root)throw std::runtime_error("卸载目录与已登记应用不匹配。");
    if(DirectNeedsElevation(root)){UninstallDirectElevated(root);return;}
    RequirePlainPath(root);InstallJob job;Snapshot state;state.destination=root;
    FileWork work{job,{},state,std::chrono::steady_clock::now(),{}};
    RecoverDirectInstall(work);StopInstalledProcesses(root);
    const bool hasMain=fs::is_regular_file(root/"app/iyw-claw.exe");
    if(hasMain)VerifySignature(root/"app/iyw-claw.exe");
    const auto script=root/"app/native-share/register.ps1";
    if(Brand().id=="iyw"&&hasMain&&fs::is_regular_file(script)){
        work.staging=CreateStaging();
        if(RunCommand({PowerShell(),{"-NoLogo","-NoProfile","-NonInteractive","-ExecutionPolicy","Bypass","-File",script.u8string(),"-Unregister"},work.staging/"unregister.log",120})!=0)throw std::runtime_error("系统分享入口未移除，卸载已停止。");
    }
    if(fs::exists(root/"app")){
        for(const auto& entry:fs::recursive_directory_iterator(root/"app"))RequirePlainPath(entry.path());
        fs::remove_all(root/"app");
    }
    for(const auto& path:Shortcuts()){RequirePlainPath(path);fs::remove(path);}
    RegDeleteTreeW(HKEY_CURRENT_USER,setup::Brand().uninstallKey.c_str());RegDeleteTreeW(HKEY_CURRENT_USER,(L"Software\\Classes\\" + setup::Brand().schemeW).c_str());
    auto key=OpenKey(setup::Brand().installKey.c_str());RegDeleteValueW(key.get(),L"InstallRoot");RegDeleteValueW(key.get(),L"");
    // 保留当前运行的安装向导和用户环境，用户可再次安装；不递归删除安装根目录。
}
}
