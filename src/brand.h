#pragma once
#if __has_include("brand.generated.h")
#include "brand.generated.h"
#else
#define IYW_APP_NAME "原助理"
#define IYW_APP_NAME_W L"原助理"
#define IYW_INSTALLER_NAME "原助理安装向导"
#define IYW_RELEASE_ID ""
#define IYW_BRAND_ID "iyw"
#define IYW_PRODUCT "iyw-claw"
#define IYW_PRODUCT_W L"iyw-claw"
#define IYW_INSTALL_PRODUCT "iyw-claw"
#define IYW_IDENTIFIER "app.iywclaw"
#define IYW_IDENTIFIER_W L"app.iywclaw"
#define IYW_SCHEME "iyw-assistant"
#define IYW_SCHEME_W L"iyw-assistant"
#define IYW_USER_DIRECTORY ".iyw-claw"
#define IYW_INSTALL_REGISTRY_W L"Software\\iywclaw\\iyw-claw"
#define IYW_UNINSTALL_REGISTRY_W L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\原助理"
#endif
#include <filesystem>
#include <string>

namespace setup {
struct BrandSettings {
    std::string id, name, installerName, identifier, scheme, userDirectory, product, releaseId;
    std::wstring nameW, identifierW, schemeW, installKey, uninstallKey;
    std::filesystem::path icon;
    bool preserve = false;
};
const BrandSettings& Brand();
void LoadInstalledBrand(const std::filesystem::path& root);
void PrepareBrandEnvironment();
}

#ifndef IYW_PROMOTION_CHANNEL
#define IYW_PROMOTION_CHANNEL "stable"
#endif
