#include "command.h"
#include "windows_support.h"
#ifdef _WIN32
#include <fstream>
#include <stdexcept>
namespace setup {
namespace {
std::wstring Quote(const std::wstring& text) {
    std::wstring out=L"\"";unsigned slashes=0;
    for(auto c:text){
        if(c==L'\\'){++slashes;continue;}
        out.append(c==L'"'?slashes*2+1:slashes,L'\\');slashes=0;out+=c;
    }
    out.append(slashes*2,L'\\');return out+L'"';
}
Handle OutputFile(const fs::path& path){
    SECURITY_ATTRIBUTES security{sizeof(security),nullptr,TRUE};
    Handle file(CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ,&security,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr));
    if(file.get()==INVALID_HANDLE_VALUE)throw std::runtime_error("无法创建环境检查日志。");return file;
}
int RunElevatedCommand(const Command& command,const std::function<void()>& poll){
    std::wstring arguments=Quote(command.executable.wstring());
    for(const auto& arg:command.arguments)arguments+=L" "+Quote(fs::u8path(arg).wstring());
    const auto executable=Quote(command.executable.wstring());
    const auto parameters=arguments.substr(executable.size()+1);
    const auto directory=command.directory.empty()?command.executable.parent_path():command.directory;
    SHELLEXECUTEINFOW launch{};launch.cbSize=sizeof(launch);
    launch.fMask=SEE_MASK_NOCLOSEPROCESS|SEE_MASK_NOASYNC|SEE_MASK_FLAG_NO_UI;
    launch.lpVerb=L"runas";launch.lpFile=command.executable.c_str();
    launch.lpParameters=parameters.c_str();launch.lpDirectory=directory.c_str();launch.nShow=SW_HIDE;
    if(!ShellExecuteExW(&launch)){
        const auto error=GetLastError();
        if(error==ERROR_CANCELLED)throw std::runtime_error("已取消系统组件的管理员授权。");
        throw std::runtime_error("无法启动系统组件安装程序。系统错误："+std::to_string(error));
    }
    Handle process(launch.hProcess);
    for(;;){
        const auto status=WaitForSingleObject(process.get(),150);
        if(status==WAIT_OBJECT_0)break;
        if(status!=WAIT_TIMEOUT)throw std::runtime_error("等待系统组件安装结果失败。");
        if(poll)poll();
    }
    DWORD code=1;
    if(!GetExitCodeProcess(process.get(),&code))throw std::runtime_error("无法读取系统组件安装结果。");
    return static_cast<int>(code);
}
}
int RunCommand(const Command& command,const std::function<void()>& poll){
    if(command.elevated)return RunElevatedCommand(command,poll);
    std::wstring arguments=Quote(command.executable.wstring());
    for(const auto& arg:command.arguments)arguments+=L" "+Quote(fs::u8path(arg).wstring());
    auto out=OutputFile(command.output),err=OutputFile(command.output.wstring()+L".stderr");
    STARTUPINFOW start{};start.cb=sizeof(start);start.dwFlags=STARTF_USESTDHANDLES;
    start.hStdOutput=out.get();start.hStdError=err.get();start.hStdInput=GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION child{};
    const auto directory=command.directory.empty()?command.executable.parent_path():command.directory;
    if(!CreateProcessW(command.executable.c_str(),arguments.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW|BELOW_NORMAL_PRIORITY_CLASS,nullptr,
        directory.c_str(),&start,&child))throw std::runtime_error("无法启动环境助手。系统错误："+std::to_string(GetLastError()));
    Handle process(child.hProcess),thread(child.hThread);
    const auto started=std::chrono::steady_clock::now();
    for(;;){
        const auto status=WaitForSingleObject(process.get(),150);
        if(status==WAIT_OBJECT_0)break;
        if(status!=WAIT_TIMEOUT)throw std::runtime_error("等待环境助手失败。");
        if(poll)poll();
        if(command.timeoutSeconds&&std::chrono::steady_clock::now()-started>std::chrono::seconds(command.timeoutSeconds)){
            TerminateProcess(process.get(),1);WaitForSingleObject(process.get(),5000);
            throw std::runtime_error("环境只读检查超时。");
        }
    }
    DWORD code=1;if(!GetExitCodeProcess(process.get(),&code))throw std::runtime_error("无法读取环境助手退出码。");
    return static_cast<int>(code);
}
}
#endif
