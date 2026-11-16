#include "payload.h"
#include "windows_support.h"
#include <archive.h>
#include <archive_entry.h>
#include <fstream>
#include <set>
#include <algorithm>
#include <cwctype>
#include <cstring>
#include <sstream>
#include <stdexcept>
namespace setup {
namespace {
std::wstring Fold(const fs::path& path) {
    auto text=path.generic_wstring();
    std::transform(text.begin(),text.end(),text.begin(),[](wchar_t c){return static_cast<wchar_t>(std::towlower(c));});
    return text;
}
void WriteEntry(archive* reader, archive_entry* entry, const fs::path& path) {
    RequirePlainPath(path);fs::create_directories(path.parent_path());RequirePlainPath(path);
    if(fs::exists(path))throw std::runtime_error("载荷包含重复文件或目录冲突。");
    std::ofstream file(path,std::ios::binary);
    if(!file)throw std::runtime_error("无法写入载荷文件。");
    std::vector<char> bytes(256*1024);std::uint64_t total=0;la_ssize_t count;
    while((count=archive_read_data(reader,bytes.data(),bytes.size()))>0) {
        total+=count;if(total>static_cast<std::uint64_t>(archive_entry_size(entry)))throw std::runtime_error("载荷文件超出声明大小。");
        file.write(bytes.data(),count);if(!file)throw std::runtime_error("载荷磁盘写入失败。");
    }
    file.flush();
    if(count<0||!file||total!=static_cast<std::uint64_t>(archive_entry_size(entry)))throw std::runtime_error("载荷文件解压不完整。");
}
void CheckExecutable(const fs::path& file, const Release& release) {
    std::ifstream stream(file,std::ios::binary);std::array<unsigned char,64> header{};
    stream.read(reinterpret_cast<char*>(header.data()),header.size());
    if(!stream||header[0]!='M'||header[1]!='Z')throw std::runtime_error("应用入口不是有效 Windows 程序。");
    std::uint32_t offset=0;std::memcpy(&offset,header.data()+60,sizeof(offset));
    stream.seekg(offset);std::array<unsigned char,6> pe{};stream.read(reinterpret_cast<char*>(pe.data()),pe.size());
    if(!stream||pe[0]!='P'||pe[1]!='E'||pe[2]||pe[3]||pe[4]!=0x64||pe[5]!=0x86)
        throw std::runtime_error("应用载荷 CPU 架构不匹配。");
    if(FileVersion(file)!=release.version)throw std::runtime_error("应用入口版本与更新版本不一致。");
    VerifySignature(file);
}
}
fs::path PayloadRelative(const std::string& value) {
    if(value.empty()||value.size()>1024||value.find_first_of("\\:\"<>|?*")!=std::string::npos)
        throw std::runtime_error("应用载荷包含非法路径。");
    for(const auto ch:value)if(static_cast<unsigned char>(ch)<32)throw std::runtime_error("载荷路径含控制字符。");
    const auto path=fs::u8path(value);
    if(path.is_absolute()||path.has_root_path())throw std::runtime_error("载荷不允许绝对路径。");
    std::string part;std::istringstream parts(value);
    while(std::getline(parts,part,'/')) {
        if(part.empty()||part=="."||part==".."||part.back()=='.'||part.back()==' ')throw std::runtime_error("载荷路径越界或含歧义。");
        auto device=part.substr(0,part.find('.'));
        std::transform(device.begin(),device.end(),device.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
        if(device=="con"||device=="prn"||device=="aux"||device=="nul"||
           (device.size()==4&&(device.rfind("com",0)==0||device.rfind("lpt",0)==0)&&device[3]>='1'&&device[3]<='9'))
            throw std::runtime_error("载荷不允许 Windows 设备路径。");
    }
    if(value.back()=='/'||(value!="payload-manifest.json"&&value.rfind("app/",0)!=0))throw std::runtime_error("载荷文件必须位于 app 目录。");
    return path;
}
void RequirePlainPath(const fs::path& path) {
    const auto absolute=fs::absolute(path).lexically_normal();fs::path current;
    for(const auto& part:absolute) {
        current/=part;const auto attrs=GetFileAttributesW(current.c_str());
        if(attrs!=INVALID_FILE_ATTRIBUTES&&(attrs&FILE_ATTRIBUTE_REPARSE_POINT))throw std::runtime_error("安装目录不允许符号链接或联接点。");
        if(attrs==INVALID_FILE_ATTRIBUTES&&GetLastError()!=ERROR_FILE_NOT_FOUND&&GetLastError()!=ERROR_PATH_NOT_FOUND)
            throw std::runtime_error("无法验证安装目录权限。");
    }
}
void ExtractPayload(FileWork& work) {
    const auto root=work.deployment/"incoming";RequirePlainPath(root);
    if(!fs::create_directory(root))throw std::runtime_error("载荷暂存目录已存在。");
    std::unique_ptr<archive,decltype(&archive_read_free)> reader(archive_read_new(),archive_read_free);
    archive_read_support_format_zip(reader.get());
    if(archive_read_open_filename_w(reader.get(),(work.staging/"setup.exe").c_str(),256*1024)!=ARCHIVE_OK)
        throw std::runtime_error("无法打开应用 ZIP 载荷。");
    archive_entry* entry=nullptr;std::uint64_t total=0;std::set<std::wstring> paths;int status;
    auto published=std::chrono::steady_clock::now();
    while((status=archive_read_next_header(reader.get(),&entry))==ARCHIVE_OK) {
        CheckCancel(work.job);
        const auto* name=archive_entry_pathname_utf8(entry);
        if(!name||archive_entry_filetype(entry)!=AE_IFREG||archive_entry_symlink(entry)||archive_entry_hardlink(entry)||archive_entry_is_encrypted(entry))
            throw std::runtime_error("载荷仅允许普通、未加密文件。");
        const auto path=PayloadRelative(name);const auto size=archive_entry_size(entry);
        if(!paths.insert(Fold(path)).second||paths.size()>MaxPayloadFiles+1||size<0||static_cast<std::uint64_t>(size)>MaxPayloadBytes-total)
            throw std::runtime_error("载荷存在重复文件或超过解压上限。");
        total+=size;WriteEntry(reader.get(),entry,root/path);
        const auto now=std::chrono::steady_clock::now();
        if(now-published>=std::chrono::milliseconds(100)) {
            work.progress.detail="正在展开应用文件，已处理 "+std::to_string(total/MiB)+" MiB";
            Advance(work,0);published=now;
        }
    }
    if(status!=ARCHIVE_EOF)throw std::runtime_error("应用载荷读取失败。");
}
void ValidatePayloadTree(const fs::path& root, const Release& release) {
    const auto path=root/"payload-manifest.json";RequirePlainPath(path);
    if(fs::file_size(path)>32*MiB)throw std::runtime_error("载荷清单过大。");
    std::ifstream file(path);const auto manifest=nlohmann::json::parse(file);
    const auto commit=manifest.value("sourceCommit",std::string());
    if(commit.size()!=40||commit.find_first_not_of("0123456789abcdef")!=std::string::npos)
        throw std::runtime_error("应用载荷缺少有效的构建来源。");
    if(manifest.at("schemaVersion")!=1||manifest.at("product")!="iyw-claw"||manifest.at("version")!=release.version||
       manifest.at("target")!="windows"||manifest.at("arch")!="x86_64"||!manifest.at("files").is_array()||manifest.at("files").size()>MaxPayloadFiles)
        throw std::runtime_error("应用载荷身份与当前发布不一致。");
    std::set<std::wstring> declared;
    for(const auto& item:manifest.at("files")) {
        const auto relative=PayloadRelative(item.at("path").get<std::string>());const auto target=root/relative;
        if(relative=="payload-manifest.json"||!declared.insert(Fold(relative)).second)throw std::runtime_error("载荷清单重复。");
        RequirePlainPath(target);
        if(!fs::is_regular_file(target)||fs::file_size(target)!=item.at("size").get<std::uint64_t>()||HashFile(target)!=item.at("sha256"))
            throw std::runtime_error("应用载荷文件摘要不匹配。");
    }
    for(const auto& entry:fs::recursive_directory_iterator(root)) {
        RequirePlainPath(entry.path());
        if(entry.is_regular_file()&&entry.path()!=path&&!declared.count(Fold(fs::relative(entry.path(),root))))throw std::runtime_error("载荷存在未声明文件。");
    }
    CheckExecutable(root/"app/iyw-claw.exe",release);
    VerifyEnvironmentBinary(root/"app/iyw-environment.exe");
    for(const auto* required:{"app/iyw-environment.exe","app/resources/bundle/index.html","app/xinghe-resources/runtime.json","app/native-share/config.json","app/native-share/register.ps1","app/native-share/share-resources.ps1","app/native-share/IywAssistant.ShareTarget.msix","app/installer/webview2-setup.exe"})
        if(!declared.count(Fold(fs::u8path(required))))throw std::runtime_error("应用载荷缺少必要资源。");
}
}
