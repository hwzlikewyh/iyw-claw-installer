#ifndef _WIN32
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "command.h"
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>
#include <stdexcept>
#include <cerrno>
extern char** environ;
namespace setup {
int RunCommand(const Command& command,const std::function<void()>& poll){
    posix_spawn_file_actions_t actions;posix_spawn_file_actions_init(&actions);
    const auto output=command.output.string(),errors=output+".stderr";
    int status=posix_spawn_file_actions_addopen(&actions,STDOUT_FILENO,output.c_str(),O_CREAT|O_TRUNC|O_WRONLY,0600);
    status|=posix_spawn_file_actions_addopen(&actions,STDERR_FILENO,errors.c_str(),O_CREAT|O_TRUNC|O_WRONLY,0600);
    status|=posix_spawn_file_actions_addopen(&actions,STDIN_FILENO,"/dev/null",O_RDONLY,0);
    if(!command.directory.empty())status|=posix_spawn_file_actions_addchdir_np(&actions,command.directory.c_str());
    std::vector<std::string> words{command.executable.string()};
    words.insert(words.end(),command.arguments.begin(),command.arguments.end());
    std::vector<char*> argv;for(auto& word:words)argv.push_back(word.data());argv.push_back(nullptr);
    pid_t pid=0;
    if(!status)status=posix_spawn(&pid,command.executable.c_str(),&actions,nullptr,argv.data(),environ);
    posix_spawn_file_actions_destroy(&actions);
    if(status)throw std::runtime_error("无法启动系统组件。错误："+std::to_string(status));
    const auto start=std::chrono::steady_clock::now();
    for(;;){
        const auto waited=waitpid(pid,&status,WNOHANG);
        if(waited==pid)break;
        if(waited<0&&errno!=EINTR)throw std::runtime_error("读取子进程状态失败。");
        if(poll)poll();
        if(command.timeoutSeconds&&std::chrono::steady_clock::now()-start>std::chrono::seconds(command.timeoutSeconds)){
            kill(pid,SIGKILL);while(waitpid(pid,&status,0)<0&&errno==EINTR){};
            throw std::runtime_error("系统组件只读检查超时。");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
    }
    return WIFEXITED(status)?WEXITSTATUS(status):128+WTERMSIG(status);
}
}
#endif
