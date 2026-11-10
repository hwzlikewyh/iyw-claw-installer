#ifndef _WIN32
#include "unix_support.h"
#include <archive.h>
#include <archive_entry.h>
#include <memory>
#include <stdexcept>
namespace setup {
namespace {
fs::path SafeRelative(const char* value){
    if(!value||!*value)throw std::runtime_error("安装包路径为空。");
    fs::path path(value);if(path.is_absolute())throw std::runtime_error("安装包含绝对路径。");
    for(const auto& part:path)if(part=="..")throw std::runtime_error("安装包含越界路径。");
    return path;
}
void ValidateEntry(archive_entry* entry,const fs::path& root){
    const auto relative=SafeRelative(archive_entry_pathname(entry));
    const auto type=archive_entry_filetype(entry);
    if(type!=AE_IFREG&&type!=AE_IFDIR&&type!=AE_IFLNK)throw std::runtime_error("安装包包含特殊设备文件。");
    if(const auto* link=archive_entry_symlink(entry)){
        fs::path target(link);if(target.is_absolute())throw std::runtime_error("安装包链接指向外部位置。");
        const auto resolved=(relative.parent_path()/target).lexically_normal();
        for(const auto& part:resolved)if(part=="..")throw std::runtime_error("安装包链接越界。");
    }
    if(const auto* hard=archive_entry_hardlink(entry)){
        const auto target=root/SafeRelative(hard);archive_entry_set_hardlink(entry,target.c_str());
    }
    const auto absolute=root/relative;archive_entry_set_pathname(entry,absolute.c_str());
    archive_entry_set_perm(entry,archive_entry_perm(entry)&0777);
}
}
void ExtractApplication(FileWork& work){
    const auto root=work.staging/"unpacked";fs::create_directory(root);
    std::unique_ptr<archive,decltype(&archive_read_free)> reader(archive_read_new(),archive_read_free);
    std::unique_ptr<archive,decltype(&archive_write_free)> writer(archive_write_disk_new(),archive_write_free);
    archive_read_support_filter_gzip(reader.get());archive_read_support_format_tar(reader.get());
    archive_write_disk_set_options(writer.get(),ARCHIVE_EXTRACT_TIME|ARCHIVE_EXTRACT_PERM|
        ARCHIVE_EXTRACT_SECURE_SYMLINKS|ARCHIVE_EXTRACT_SECURE_NODOTDOT|ARCHIVE_EXTRACT_SAFE_WRITES);
    if(archive_read_open_filename(reader.get(),(work.staging/"setup.exe").c_str(),64*1024)!=ARCHIVE_OK)
        throw std::runtime_error("无法读取应用归档。");
    archive_entry* entry=nullptr;std::uint64_t total=0;unsigned files=0;int status;
    constexpr std::uint64_t MaxExpanded=4ULL*1024*MiB;
    while((status=archive_read_next_header(reader.get(),&entry))==ARCHIVE_OK){
        if(++files>100000)throw std::runtime_error("安装包文件数量异常。");
        const auto size=archive_entry_size(entry);
        if(size<0||static_cast<std::uint64_t>(size)>MaxExpanded-total)throw std::runtime_error("安装包解压体积异常。");
        total+=size;ValidateEntry(entry,root);
        if(archive_write_header(writer.get(),entry)<ARCHIVE_OK)throw std::runtime_error("无法创建应用文件。");
        const void* data=nullptr;size_t count=0;la_int64_t offset=0;
        int block;
        while((block=archive_read_data_block(reader.get(),&data,&count,&offset))==ARCHIVE_OK)
            if(archive_write_data_block(writer.get(),data,count,offset)<ARCHIVE_OK)throw std::runtime_error("解压写入失败。");
        if(block!=ARCHIVE_EOF||archive_write_finish_entry(writer.get())<ARCHIVE_OK)throw std::runtime_error("应用归档损坏。");
        work.progress.detail="正在解压应用文件："+std::to_string(files);Advance(work,0);
    }
    if(status!=ARCHIVE_EOF)throw std::runtime_error("应用归档读取中断。");
}
}
#endif
