#include "payload.h"
#include "windows_support.h"
#include <tlhelp32.h>
#include <fstream>
#include <map>
#include <stdexcept>
namespace setup {
namespace {
std::vector<char> UserToken(HANDLE process) {
    HANDLE raw=nullptr;if(!OpenProcessToken(process,TOKEN_QUERY,&raw))throw std::runtime_error("无法核对应用进程所属用户。");
    Handle token(raw);DWORD size=0;GetTokenInformation(token.get(),TokenUser,nullptr,0,&size);
    std::vector<char> bytes(size);
    if(!size||!GetTokenInformation(token.get(),TokenUser,bytes.data(),size,&size))throw std::runtime_error("无法核对进程身份。");
    return bytes;
}
bool SameUser(HANDLE process) {
    const auto current=UserToken(GetCurrentProcess()),other=UserToken(process);
    return EqualSid(reinterpret_cast<const TOKEN_USER*>(current.data())->User.Sid,reinterpret_cast<const TOKEN_USER*>(other.data())->User.Sid)!=FALSE;
}
BOOL CALLBACK CloseWindowForProcess(HWND window,LPARAM processId) {
    DWORD owner=0;GetWindowThreadProcessId(window,&owner);
    if(owner==static_cast<DWORD>(processId))PostMessageW(window,WM_CLOSE,0,0);
    return TRUE;
}
bool UnderApp(const std::wstring& path,const fs::path& root) {
    auto prefix=fs::weakly_canonical(root/"app").wstring()+L"\\";
    return path.size()>prefix.size()&&CompareStringOrdinal(path.data(),static_cast<int>(prefix.size()),prefix.data(),static_cast<int>(prefix.size()),TRUE)==CSTR_EQUAL;
}
bool UnderRuntime(const std::wstring& path) {
    const auto prefix=(UserHome()/".iyw-claw/runtime").wstring()+L"\\";
    return path.size()>prefix.size()&&CompareStringOrdinal(path.data(),static_cast<int>(prefix.size()),prefix.data(),static_cast<int>(prefix.size()),TRUE)==CSTR_EQUAL;
}
struct Process {
    DWORD id=0,parent=0;
    Handle handle;
    FILETIME created{};
    bool selected=false;
};
std::vector<Process> FindProcesses(const fs::path& root) {
    Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0));
    if(snapshot.get()==INVALID_HANDLE_VALUE)throw std::runtime_error("无法枚举应用进程。");
    PROCESSENTRY32W entry{};entry.dwSize=sizeof(entry);std::vector<Process> targets;
    if(!Process32FirstW(snapshot.get(),&entry))throw std::runtime_error("无法读取进程列表。");
    do {
        if(entry.th32ProcessID==GetCurrentProcessId())continue;
        Handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,entry.th32ProcessID));
        if(!process)continue;
        wchar_t path[32768]{};DWORD size=32768;
        if(!QueryFullProcessImageNameW(process.get(),0,path,&size))continue;
        const bool owned=UnderApp(std::wstring(path,size),root);
        if(!owned&&!UnderRuntime(std::wstring(path,size)))continue;
        if(!SameUser(process.get())) {
            if(owned)throw std::runtime_error("其他用户正在运行此目录中的原助理，请先退出。");
            continue;
        }
        FILETIME created{},exit{},kernel{},user{};
        if(!GetProcessTimes(process.get(),&created,&exit,&kernel,&user))throw std::runtime_error("无法确认应用进程创建时间。");
        targets.push_back({entry.th32ProcessID,entry.th32ParentProcessID,std::move(process),created,owned});
    } while(Process32NextW(snapshot.get(),&entry));
    if(GetLastError()!=ERROR_NO_MORE_FILES)throw std::runtime_error("应用进程枚举未完成。");
    return targets;
}
void SelectChildren(std::vector<Process>& targets) {
    std::map<DWORD,Process*> byId;for(auto& target:targets)byId.emplace(target.id,&target);
    bool changed=true;
    while(changed) {
        changed=false;
        for(auto& child:targets) {
            const auto found=byId.find(child.parent);
            if(child.selected||found==byId.end()||!found->second->selected)continue;
            if(CompareFileTime(&child.created,&found->second->created)<0)continue;
            child.selected=true;changed=true;
        }
    }
}
void TerminateSelected(Process& process) {
    if(WaitForSingleObject(process.handle.get(),0)==WAIT_OBJECT_0)return;
    Handle control(OpenProcess(PROCESS_TERMINATE|SYNCHRONIZE,FALSE,process.id));
    // 原始句柄仍有效且尚未退出，PID 不能在重新开句柄期间被复用。
    if(WaitForSingleObject(process.handle.get(),0)==WAIT_OBJECT_0)return;
    if(!control||!TerminateProcess(control.get(),0)||WaitForSingleObject(control.get(),5000)!=WAIT_OBJECT_0)
        throw std::runtime_error("原助理文件仍被占用，请关闭相关任务后重试。");
}
}
void StopInstalledProcesses(const fs::path& root) {
    auto targets=FindProcesses(root);SelectChildren(targets);
    for(const auto& process:targets)if(process.selected)EnumWindows(CloseWindowForProcess,process.id);
    const auto deadline=GetTickCount64()+15000;
    for(auto& process:targets) {
        if(!process.selected)continue;
        const auto now=GetTickCount64();const auto timeout=now<deadline?static_cast<DWORD>(deadline-now):0;
        if(WaitForSingleObject(process.handle.get(),timeout)!=WAIT_OBJECT_0)TerminateSelected(process);
    }
    // 退出过程中重新启动的 app 子进程不能被当成已经停止。
    for(const auto& process:FindProcesses(root))if(process.selected)
        throw std::runtime_error("应用后台进程重新启动，请退出后重试。");
}
namespace {
Handle UpdateParent(const UpdateRequest& request) {
    Handle parent(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,request.parentPid));
    if(!parent||!SameUser(parent.get()))throw std::runtime_error("更新调用方身份无效。");
    wchar_t path[32768]{};DWORD size=32768;
    if(!QueryFullProcessImageNameW(parent.get(),0,path,&size)||!UnderApp(std::wstring(path,size),request.destination)||
       fs::path(path).filename()!=L"iyw-claw.exe")throw std::runtime_error("更新调用方不是当前安装的原助理。");
    VerifySignature(fs::path(path));RequirePlainPath(request.readyFile);
    if(request.readyFile.empty()||!request.readyFile.is_absolute())throw std::runtime_error("更新握手路径无效。");
    return parent;
}
}
void ReportUpdateFailure(const UpdateRequest& request, const std::string& detail) {
    try {
        auto parent = UpdateParent(request);
        if (WaitForSingleObject(parent.get(), 0) != WAIT_TIMEOUT) return;
        constexpr std::size_t MaxErrorBytes = 8192;
        const auto error = detail.size() <= MaxErrorBytes ? detail : "更新准备失败，请查看安装向导日志。";
        // 失败不写就绪 pid，避免旧客户端将错误回执误认成可以退出。
        WriteDurableJSON(request.readyFile, {{"error_pid", GetCurrentProcessId()}, {"protocol", 2}, {"error", error}});
    } catch (const std::exception&) { }
}
void AwaitUpdateParent(const UpdateRequest& request) {
    auto parent = UpdateParent(request);
    WriteDurableJSON(request.readyFile,{{"pid",GetCurrentProcessId()},{"protocol",2}});
    if(WaitForSingleObject(parent.get(),60000)!=WAIT_OBJECT_0)throw std::runtime_error("等待原助理退出超时，更新未开始。");
}
}
