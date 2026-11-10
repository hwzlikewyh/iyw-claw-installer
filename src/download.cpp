#include "release.h"
#include <array>
#include <fstream>
#include <future>
#include <stdexcept>
namespace setup {
namespace {
constexpr unsigned MaxDownloadWorkers=3;
constexpr std::uint64_t ParallelThreshold=16*MiB;
struct ParallelDownload {
    const Release& release;FileWork& work;
    std::mutex mutex;std::atomic<bool> failed{false};
    std::chrono::steady_clock::time_point lastPublish=std::chrono::steady_clock::now();
};
void Sequential(const Release& release,FileWork& work){
    std::ofstream file(work.staging/"setup.part",std::ios::binary|std::ios::trunc);
    if(!file)throw std::runtime_error("无法创建下载缓存。");
    ReadHttps(release.url,[&](const char* bytes,std::size_t count){
        if(work.progress.phaseDone+count>release.size)throw std::runtime_error("下载大小超过发布记录。");
        file.write(bytes,static_cast<std::streamsize>(count));if(!file)throw std::runtime_error("写入安装包失败。");
        Advance(work,count);
    },work.job);
    file.flush();if(!file||work.progress.phaseDone!=release.size)throw std::runtime_error("安装包下载不完整。");
}
void Segment(ParallelDownload& download,unsigned index){
    const auto start=download.release.size*index/MaxDownloadWorkers;
    const auto end=download.release.size*(index+1)/MaxDownloadWorkers-1;
    std::fstream file(download.work.staging/"setup.part",std::ios::binary|std::ios::in|std::ios::out);
    if(!file)throw std::runtime_error("无法打开分段下载缓存。");
    file.seekp(static_cast<std::streamoff>(start));std::uint64_t received=0;
    const auto ranged=ReadHttpsRange(download.release.url,{start,end},[&](const char* bytes,std::size_t count){
        if(download.failed.load())throw std::runtime_error("其他下载分段失败。");
        if(received+count>end-start+1)throw std::runtime_error("分段内容超出预期。");
        file.write(bytes,static_cast<std::streamsize>(count));if(!file)throw std::runtime_error("分段写入失败。");
        received+=count;
        std::lock_guard<std::mutex> guard(download.mutex);download.work.progress.phaseDone+=count;
        const auto now=std::chrono::steady_clock::now();
        if(now-download.lastPublish>=std::chrono::milliseconds(100)){
            download.lastPublish=now;Advance(download.work,0);
        }
    },download.work.job);
    file.flush();if(!ranged||!file||received!=end-start+1)throw std::runtime_error("分段下载未完整返回。");
}
void Parallel(const Release& release,FileWork& work){
    {std::ofstream file(work.staging/"setup.part",std::ios::binary);if(!file)throw std::runtime_error("无法创建下载文件。");}
    fs::resize_file(work.staging/"setup.part",release.size);
    ParallelDownload download{release,work};std::array<std::future<void>,MaxDownloadWorkers> tasks;
    for(unsigned index=0;index<MaxDownloadWorkers;++index)tasks[index]=std::async(std::launch::async,[&,index]{
        try{Segment(download,index);}catch(...){download.failed.store(true);throw;}
    });
    std::exception_ptr failure;
    for(auto& task:tasks){try{task.get();}catch(...){if(!failure)failure=std::current_exception();}}
    if(failure)std::rethrow_exception(failure);
    if(work.progress.phaseDone!=release.size)throw std::runtime_error("分段下载大小不匹配。");
    Advance(work,0);
}
}
void DownloadPackage(const Release& release,FileWork& work){
    BeginStage(work,Phase::Downloading);work.progress.phaseTotal=release.size;
    work.progress.detail="正在下载原助理 "+release.version+" 稳定版";
    std::uint64_t probeBytes=0;
    const bool parallel=release.size>=ParallelThreshold&&ReadHttpsRange(release.url,{0,0},[&](const char*,std::size_t count){
        probeBytes+=count;if(probeBytes>1)throw std::runtime_error("分段探测响应异常。");
    },work.job);
    if(parallel&&probeBytes!=1)throw std::runtime_error("分段探测响应不完整。");
    work.progress.detail+=parallel?" · 3 路并发":" · 顺序下载";work.job.Publish(work.progress);
    if(parallel)Parallel(release,work);else Sequential(release,work);
    fs::rename(work.staging/"setup.part",work.staging/"setup.exe");
}
}
