#ifndef _WIN32
#include "unix_support.h"
#include <sys/stat.h>
#include <stdexcept>
#include <fstream>
namespace setup {
namespace {
fs::path AppDirectory(const fs::path& root){
#ifdef __APPLE__
    return root/fs::u8path(setup::Brand().name + ".app");
#else
    return root/"app";
#endif
}
void RequireNotRunning(FileWork& work){
    const auto executable=AppExecutable(work.progress.destination);
    if(!fs::exists(executable))return;
    std::string escaped;
    for(const auto c:executable.string()){
        if(std::string(".[](){}*+?^$|\\").find(c)!=std::string::npos)escaped+='\\';escaped+=c;
    }
    const fs::path pgrep="/usr/bin/pgrep";
    if(!fs::exists(pgrep))throw std::runtime_error("缺少 pgrep，无法确认旧应用是否已经退出。");
    const auto status=RunCommand({pgrep,{"-f","^"+escaped+"( |$)"},work.staging/"running-check.log",10});
    if(status==0)throw std::runtime_error("请先退出正在运行的原助理，再开始安装。");
    if(status!=1)throw std::runtime_error("无法确认旧应用进程状态。");
}
void SwapApp(const fs::path& source,FileWork& work){
    const auto root=work.progress.destination;fs::create_directories(root);
    const auto destination=AppDirectory(root);
    const auto incoming=root/(".yuan-incoming-"+work.staging.filename().string());
    if(fs::exists(incoming))throw std::runtime_error("安装暂存目录冲突。");
    try{fs::copy(source,incoming,fs::copy_options::recursive|fs::copy_options::copy_symlinks);}
    catch(...){std::error_code error;fs::remove_all(incoming,error);throw;}
    if(fs::exists(destination)){
        work.backup=root/(".yuan-backup-"+work.staging.filename().string());
        if(fs::exists(work.backup))throw std::runtime_error("备份目录冲突。");
        fs::rename(destination,work.backup);
    }
    try{fs::rename(incoming,destination);}catch(...){
        if(!work.backup.empty())fs::rename(work.backup,destination);work.backup.clear();
        std::error_code error;fs::remove_all(incoming,error);throw;
    }
    work.progress.events.push_back("应用文件已部署，等待环境与启动文件检查。");
}
}
void VerifyMacBundle(const fs::path& bundle,const Release& release,const fs::path& log){
#ifdef __APPLE__
    if(RunCommand({"/usr/bin/codesign",{"--verify","--deep","--strict","-R",
        "identifier \"app.iywclaw\" and anchor apple generic",bundle.string()},log,60})!=0)
        throw std::runtime_error("macOS 应用签名或应用标识校验失败。");
    const auto plist=bundle/"Contents/Info.plist";
    if(RunCommand({"/usr/libexec/PlistBuddy",{"-c","Print :CFBundleShortVersionString",plist.string()},log,10})!=0)
        throw std::runtime_error("无法读取 macOS 应用版本。");
    auto version=ReadOutput(log);while(!version.empty()&&(version.back()=='\n'||version.back()=='\r'))version.pop_back();
    if(version!=release.version)throw std::runtime_error("macOS 应用版本与发布版本不匹配。");
    RequireBrandRuntime(bundle/"Contents/MacOS/iyw-claw");
#else
    (void)bundle;(void)release;(void)log;
#endif
}
void InstallPackage(const Release& release,FileWork& work){
    CheckCancel(work.job);RequireNotRunning(work);BeginStage(work,Phase::Installing);
#ifdef __APPLE__
    ExtractApplication(work);fs::path source;
    for(const auto& entry:fs::directory_iterator(work.staging/"unpacked")){
        if(entry.path().extension()==".app"&&entry.is_directory()&&!entry.is_symlink()){
            if(!source.empty())throw std::runtime_error("归档包含多个应用。");source=entry.path();
        }
    }
    if(source.empty())throw std::runtime_error("安装包缺少 macOS 应用。");
    VerifyMacBundle(source,release,work.staging/"codesign-check.log");SwapApp(source,work);
#else
    const auto package=work.staging/"setup.exe";chmod(package.c_str(),0700);
    // AppImage 已通过固定发布密钥签名验证，使用其标准解包入口避免 FUSE 依赖。
    if(RunCommand({package,{"--appimage-extract"},work.staging/"extract.log",0,work.staging},[&]{Advance(work,0);})!=0)
        throw std::runtime_error("AppImage 解压失败，请查看日志。");
    const auto source=work.staging/"squashfs-root";
    if(!fs::is_regular_file(source/"AppRun")||!fs::is_regular_file(source/"usr/bin/iyw-environment"))
        throw std::runtime_error("安装包缺少应用或环境助手。");
    RequireBrandRuntime(source/"usr/bin/iyw-claw");
    SwapApp(source,work);
#endif
}
void VerifyInstalled(const Release& release,FileWork& work){
    BeginStage(work,Phase::Finalizing);const auto executable=AppExecutable(work.progress.destination);
    if(!fs::is_regular_file(executable)||!fs::is_regular_file(EnvironmentHelper(work.progress.destination)))
        throw std::runtime_error("安装后的应用文件不完整。");
    VerifyMacBundle(AppDirectory(work.progress.destination),release,work.staging/"installed-check.log");
    work.progress.executable=executable;work.progress.detail="原助理 "+release.version+" 已就绪。";
}
bool ReuseApplication(const Release& release,FileWork& work){
#ifdef __APPLE__
    const auto bundle=AppDirectory(work.progress.destination);
    if(!fs::is_directory(bundle))return false;
    try{VerifyMacBundle(bundle,release,work.staging/"reuse-check.log");}
    catch(const std::exception&){return false;}
#else
    const auto receipt=work.progress.destination/"installed-release.json";
    if(!fs::is_regular_file(receipt)||!fs::is_regular_file(AppExecutable(work.progress.destination))||
        !fs::is_regular_file(EnvironmentHelper(work.progress.destination)))return false;
    const auto saved=ReadOutput(receipt);
    if(saved!=release.version+"\n"+release.sha256+"\n")return false;
    try{RequireBrandRuntime(work.progress.destination/"app/usr/bin/iyw-claw");}
    catch(const std::exception&){return false;}
#endif
    work.progress.events.push_back("已安装同一稳定版，跳过主程序下载与重装。");return true;
}
void FinishDeployment(FileWork& work,bool success){
    if(success){
#ifndef __APPLE__
        std::ofstream receipt(work.progress.destination/"installed-release.json",std::ios::binary);
        if(!receipt)throw std::runtime_error("无法保存应用安装身份记录。");
        // 记录来自当前经过签名验证的部署，而不是应用运行产生的数据。
        std::ifstream selected(work.staging/"selected-release.txt",std::ios::binary);
        if(selected)receipt<<selected.rdbuf();
        receipt.close();
#endif
        if(!work.backup.empty()){
            std::error_code error;fs::remove_all(work.backup,error);
            if(error)work.progress.events.push_back("安装成功；旧版本备份暂留："+work.backup.string());
        }
        work.backup.clear();return;
    }
    if(!work.backup.empty()&&fs::exists(work.backup)){
        const auto current=AppDirectory(work.progress.destination);
        const auto failed=work.progress.destination/(".yuan-failed-"+work.staging.filename().string());
        if(fs::exists(current))fs::rename(current,failed);
        fs::rename(work.backup,current);work.backup.clear();work.progress.events.push_back("旧应用目录已恢复。");
    }
}
}
#endif
