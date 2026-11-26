#include "release.h"
#include "runtime-brand-assets.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <array>
#include <stdexcept>
#ifdef _WIN32
#include "payload.h"
#include <windows.h>
#endif

namespace setup {
void RequireBrandRuntime(const fs::path& executable) {
    if(Brand().id=="iyw")return;
    constexpr const char* Capability="IYW_INSTALLED_BRAND_V1";
    const std::string marker=Capability;
    std::ifstream stream(executable,std::ios::binary);
    std::array<char,64*1024> block{};
    std::string tail;
    while(stream) {
        stream.read(block.data(),block.size());
        const auto bytes=tail+std::string(block.data(),static_cast<std::size_t>(stream.gcount()));
        if(bytes.find(marker)!=std::string::npos)return;
        tail=bytes.substr(bytes.size()>marker.size()?bytes.size()-marker.size():0);
    }
    throw std::runtime_error("当前公共 PC 包尚不支持安装品牌配置，请发布兼容的公共 iyw-claw 包后重试。");
}

fs::path BrandConfigurationDirectory(const fs::path& root) {
#ifdef __APPLE__
    return root/fs::u8path(setup::Brand().name + ".app.branding");
#else
    return root/"config";
#endif
}

namespace {
void WriteBrandFile(const fs::path& path,const unsigned char* bytes,std::size_t count) {
    auto temporary=path;
    temporary += ".new";
#ifdef _WIN32
    RequirePlainPath(path);RequirePlainPath(temporary);
#else
    if(fs::is_symlink(path)||fs::is_symlink(temporary))throw std::runtime_error("品牌文件路径不能为链接。");
#endif
    std::ofstream stream(temporary,std::ios::binary|std::ios::trunc);
    stream.write(reinterpret_cast<const char*>(bytes),static_cast<std::streamsize>(count));
    stream.flush();
    if(!stream.good())throw std::runtime_error("品牌配置写入失败。");
    stream.close();
#ifdef _WIN32
    if(!MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("品牌配置保存失败，系统错误："+std::to_string(GetLastError()));
#else
    fs::rename(temporary,path);
#endif
}
}

void PrepareBrandAssets(const fs::path& root) {
    if(Brand().preserve)return;
    const auto directory=BrandConfigurationDirectory(root)/"branding";
#ifdef _WIN32
    RequirePlainPath(directory);
#endif
    fs::create_directories(directory);
    WriteBrandFile(directory/IYW_RUNTIME_LOGO_NAME,RuntimeBrandLogo,sizeof(RuntimeBrandLogo));
    WriteBrandFile(directory/IYW_RUNTIME_ICON_NAME,RuntimeBrandIcon,sizeof(RuntimeBrandIcon));
}

fs::path InstalledBrandIcon(const fs::path& root) {
    if(Brand().preserve)return Brand().icon;
    return BrandConfigurationDirectory(root)/"branding"/IYW_RUNTIME_ICON_NAME;
}

void SaveBrandConfiguration(FileWork& work) {
    if(Brand().preserve)return;
    const auto value=nlohmann::json{
        {"schemaVersion",1},{"brandId",setup::Brand().id},{"displayName",setup::Brand().name},
        {"productKey","iyw-claw"},{"identifier",setup::Brand().identifier},{"urlScheme",setup::Brand().scheme},
        {"logoFile","branding/" IYW_RUNTIME_LOGO_NAME},{"iconFile","branding/" IYW_RUNTIME_ICON_NAME},
        {"releaseId",setup::Brand().releaseId},{"promotionChannel",IYW_PROMOTION_CHANNEL}};
    const auto json=value.dump(2);
    WriteBrandFile(BrandConfigurationDirectory(work.progress.destination)/"branding.json",
        reinterpret_cast<const unsigned char*>(json.data()),json.size());
    work.progress.events.push_back("已保存安装品牌配置，主程序与后续更新共用 iyw-claw 发布包。");
}
}
