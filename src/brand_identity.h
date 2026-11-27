#pragma once
#include "release.h"
#include <nlohmann/json.hpp>
#include <fstream>

namespace setup {
inline bool MatchesInstalledBrand(const fs::path& app) {
    const auto directory = BrandConfigurationDirectory(app.parent_path());
    const auto metadata = directory/"branding.json";
    try {
        if(fs::is_symlink(app)||fs::is_symlink(directory)||fs::is_symlink(metadata)) return false;
        if(!fs::exists(metadata)) return !fs::exists(app) || setup::Brand().id=="iyw";
        if(fs::file_size(metadata)>MiB) return false;
        std::ifstream file(metadata);
        const auto brand=nlohmann::json::parse(file);
        return brand.at("brandId")==setup::Brand().id;
    } catch(const std::exception&) { return false; }
}
}
