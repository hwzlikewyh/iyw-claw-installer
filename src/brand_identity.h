#pragma once
#include "installer.h"
#include <nlohmann/json.hpp>
#include <fstream>

namespace setup {
inline bool MatchesInstalledBrand(const fs::path& app) {
    const auto metadata = app.parent_path()/"config/branding.json";
    if(!fs::exists(metadata)) return !fs::exists(app) || setup::Brand().id=="iyw";
    try {
        if(fs::is_symlink(metadata)||fs::file_size(metadata)>MiB) return false;
        std::ifstream file(metadata);
        const auto brand=nlohmann::json::parse(file);
        return brand.at("brandId")==setup::Brand().id;
    } catch(const std::exception&) { return false; }
}
}
