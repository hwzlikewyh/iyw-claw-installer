#ifndef _WIN32
#include "unix_support.h"
#include <pwd.h>
#include <unistd.h>
#include <sys/utsname.h>
#include <dlfcn.h>
#include <spawn.h>
#include <sys/wait.h>
#include <fstream>
#include <stdexcept>
#ifdef __APPLE__
#include <sys/sysctl.h>
#else
#include <gnu/libc-version.h>
#endif
extern char** environ;
namespace setup {
void InstallUnifiedInstaller(const fs::path&) {}
void LaunchExistingForUpdate(const fs::path&) {}
fs::path UserHome(){
    struct passwd user{},*result=nullptr;std::vector<char> buffer(16384);
    if(getpwuid_r(getuid(),&user,buffer.data(),buffer.size(),&result)||!result||!user.pw_dir)
        throw std::runtime_error("无法读取系统用户目录。");return fs::path(user.pw_dir);
}
Platform CurrentPlatform(){
    struct utsname system{};if(uname(&system))throw std::runtime_error("无法读取系统架构。");
    std::string arch=system.machine;
#ifdef __APPLE__
    int arm=0;size_t bytes=sizeof(arm);sysctlbyname("hw.optional.arm64",&arm,&bytes,nullptr,0);
    if(arm)arch="arm64";
#endif
    arch=arch=="arm64"||arch=="aarch64"?"aarch64":arch=="x86_64"?"x86_64":"";
    if(arch.empty())throw std::runtime_error("此安装向导仅支持 x64 与 ARM64 系统。");
#ifdef __APPLE__
    return {"darwin",arch};
#else
    return {"linux",arch};
#endif
}
fs::path DefaultInstallRoot(){
#ifdef __APPLE__
    return UserHome()/"Applications";
#else
    return UserHome()/".local/share/iyw-claw";
#endif
}
fs::path CreateStaging(){
    auto pattern=(fs::temp_directory_path()/"yuan-setup-XXXXXX").string();
    if(!mkdtemp(pattern.data()))throw std::runtime_error("无法创建安全安装缓存。");return fs::path(pattern);
}
fs::path AppExecutable(const fs::path& root){
#ifdef __APPLE__
    return root/fs::u8path("原助理.app/Contents/MacOS/iyw-claw");
#else
    return root/"app/AppRun";
#endif
}
fs::path EnvironmentHelper(const fs::path& root){
#ifdef __APPLE__
    return root/fs::u8path("原助理.app/Contents/MacOS/iyw-environment");
#else
    return root/"app/usr/bin/iyw-environment";
#endif
}
void VerifyEnvironmentBinary(const fs::path& helper){
    if(access(helper.c_str(),X_OK)!=0)throw std::runtime_error("环境助手不可执行，请检查应用文件权限。");
}
void CheckSystem(FileWork& work){
    if(geteuid()==0)throw std::runtime_error("请用普通用户运行安装向导，避免将运行环境安装到 root 账户。");
    const auto platform=CurrentPlatform();work.progress.checks.push_back("系统："+platform.target+" / "+platform.arch);
#ifdef __APPLE__
    char version[128]{};size_t count=sizeof(version);
    if(sysctlbyname("kern.osproductversion",version,&count,nullptr,0))throw std::runtime_error("无法识别 macOS 版本。");
    unsigned major=0,minor=0;sscanf(version,"%u.%u",&major,&minor);
    if(major<13||(major==13&&minor<5))throw std::runtime_error("原助理当前运行环境需要 macOS 13.5 或更新版本。");
    work.progress.checks.push_back(std::string("macOS ")+version+"：版本检查通过；系统自带 WKWebView。");
#else
    unsigned major=0,minor=0;sscanf(gnu_get_libc_version(),"%u.%u",&major,&minor);
    if(major<2||(major==2&&minor<35))throw std::runtime_error("Linux 需要 glibc 2.35+，请使用 Ubuntu 22.04+ / Debian 12+ 或兼容系统。");
    std::string missing;
    for(const auto* library:{"libgtk-3.so.0","libwebkit2gtk-4.1.so.0"}){
        void* handle=dlopen(library,RTLD_LAZY|RTLD_LOCAL);
        if(handle)dlclose(handle);else missing+=std::string(library)+" ";
    }
    if(!missing.empty())throw std::runtime_error("缺少 Linux 系统图形库："+missing+
        "。Ubuntu/Debian 请安装 libgtk-3-0、libwebkit2gtk-4.1-0 后重试。");
    work.progress.checks.push_back("glibc、GTK 3、WebKitGTK 4.1：检查通过。");
#endif
    work.job.Publish(work.progress);
}
void LaunchInstalled(const Snapshot& state,bool restoreSession){
    (void)restoreSession;
    if(state.phase!=Phase::Complete||!fs::is_regular_file(state.executable))throw std::runtime_error("安装尚未完成。");
#ifdef __APPLE__
    std::vector<std::string> words{"/usr/bin/open",(state.destination/fs::u8path("原助理.app")).string()};
#else
    std::vector<std::string> words{state.executable.string()};
#endif
    std::vector<char*> args;for(auto& word:words)args.push_back(word.data());args.push_back(nullptr);
    pid_t child=0;if(posix_spawn(&child,words[0].c_str(),nullptr,nullptr,args.data(),environ))throw std::runtime_error("无法启动原助理。");
    std::thread([child]{int code;while(waitpid(child,&code,0)<0&&errno==EINTR){}}).detach();
}
}
#endif
