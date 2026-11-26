#include "release.h"
#include <fstream>
#include <future>
#include <array>
#include <set>
#include <stdexcept>
namespace setup {
namespace {
constexpr unsigned MaxComponentWorkers=2;
struct Queue{
    FileWork& work;std::vector<const PresetComponent*> pending;
    std::atomic<std::size_t> next{0};std::atomic<bool> failed{false};std::mutex mutex;
    std::set<std::string> skipped;
    std::chrono::steady_clock::time_point published=std::chrono::steady_clock::now();
};
fs::path Partial(const FileWork& work,const PresetComponent& component){return work.staging/("env-"+component.id+".part");}
fs::path Cached(const PresetComponent& component){return UserHome()/setup::Brand().userDirectory/"runtime/cache/downloads"/component.sha256/"artifact";}
void Fetch(Queue& queue,const PresetComponent& component){
    std::ofstream output(Partial(queue.work,component),std::ios::binary|std::ios::trunc);
    if(!output)throw std::runtime_error("无法创建预置组件下载缓存。");std::uint64_t done=0;
    const auto url=queue.work.componentURLs.at(component.id);
    if(url.empty())throw std::runtime_error("组件缺少下载地址："+component.id);
    ReadHttps(url,[&](const char* bytes,std::size_t count){
        if(queue.failed.load())throw std::runtime_error("预置环境下载已停止。");
        if(done+count>component.bytes)throw std::runtime_error("组件下载大小超出发布记录。");
        output.write(bytes,static_cast<std::streamsize>(count));if(!output)throw std::runtime_error("预置环境磁盘写入失败。");done+=count;
        std::lock_guard<std::mutex> guard(queue.mutex);queue.work.progress.phaseDone+=count;
        const auto now=std::chrono::steady_clock::now();
        if(now-queue.published>=std::chrono::milliseconds(100)){
            queue.published=now;queue.work.progress.detail="预置环境："+component.id+" · 最多 2 路下载";Advance(queue.work,0);
        }
    },queue.work.job);
    output.flush();if(!output||done!=component.bytes)throw std::runtime_error("组件下载未完成："+component.id);
}
bool SkipOptional(Queue& queue,const PresetComponent& component,const std::exception& error){
    if(!IsOptionalPreset(component.id)||queue.work.job.IsCancelled()||queue.failed.load())return false;
    std::lock_guard<std::mutex> guard(queue.mutex);
    queue.skipped.insert(component.id);
    queue.work.progress.events.push_back("可选浏览器组件 "+component.id+" 预下载未完成，将继续安装原助理："+error.what());
    queue.work.job.Publish(queue.work.progress);
    return true;
}
void Worker(Queue& queue){
    while(!queue.failed.load()){
        const auto index=queue.next.fetch_add(1);if(index>=queue.pending.size())return;
        try{Fetch(queue,*queue.pending[index]);}
        catch(const std::exception& error){if(!SkipOptional(queue,*queue.pending[index],error)){queue.failed.store(true);throw;}}
    }
}
void Activate(const fs::path& source,const fs::path& target){
    fs::create_directories(target.parent_path());
    if(fs::exists(target)){
        if(fs::file_size(target)==fs::file_size(source)&&HashFile(target)==HashFile(source)){fs::remove(source);return;}
        // 只替换摘要目录中的损坏缓存，源文件已通过完整 SHA-256 校验。
    }
    std::error_code error;fs::rename(source,target,error);
    if(!error)return;
    const auto temporary=target.parent_path()/("yuan-"+source.parent_path().filename().string()+".part");
    fs::copy_file(source,temporary,fs::copy_options::overwrite_existing);fs::rename(temporary,target);fs::remove(source);
}
}
void PrefetchEnvironment(FileWork& work){
    BeginStage(work,Phase::PresetDownload);
    const auto missing=MissingPresetComponents(work);const std::set<std::string> selected(missing.begin(),missing.end());
    Queue queue{work,{}};work.progress.phaseTotal=0;
    for(const auto& component:work.progress.presets){
        if(!selected.count(component.id))continue;
        if(component.optional||IsOptionalPreset(component.id))continue;
        const auto cache=Cached(component);
        if(fs::is_regular_file(cache)&&fs::file_size(cache)==component.bytes&&HashFile(cache)==component.sha256)continue;
        queue.pending.push_back(&component);work.progress.phaseTotal+=component.bytes;
    }
    if(queue.pending.empty()){
        work.progress.reused[static_cast<unsigned>(StageIndex(Phase::PresetDownload))]=true;
        work.progress.events.push_back("预置组件已安装或缓存完整，跳过环境下载。");return;
    }
    if(fs::space(work.staging).available<work.progress.phaseTotal+64*MiB)throw std::runtime_error("预置组件下载缓存空间不足。");
    work.progress.detail="正在下载 "+std::to_string(queue.pending.size())+" 个缺失或待更新的预置组件";work.job.Publish(work.progress);
    std::array<std::future<void>,MaxComponentWorkers> workers;
    for(auto& worker:workers)worker=std::async(std::launch::async,[&]{Worker(queue);});
    std::exception_ptr failure;for(auto& worker:workers){try{worker.get();}catch(...){if(!failure)failure=std::current_exception();}}
    if(failure)std::rethrow_exception(failure);
    for(const auto* component:queue.pending){
        if(queue.skipped.count(component->id))continue;
        CheckCancel(work.job);work.progress.detail="校验预置组件："+component->id;work.job.Publish(work.progress);
        const auto partial=Partial(work,*component);
        try{
            if(HashFile(partial)!=component->sha256)throw std::runtime_error("预置组件摘要不匹配："+component->id);
            Activate(partial,Cached(*component));
        }catch(const std::exception& error){if(!SkipOptional(queue,*component,error))throw;}
    }
    work.progress.events.push_back(queue.skipped.empty()?"缺失组件下载完成并通过摘要校验；由环境助手串行解包安装。":
        "必需组件下载完成；部分可选浏览器组件未缓存，安装完成后可在应用内修复。");
}
}
