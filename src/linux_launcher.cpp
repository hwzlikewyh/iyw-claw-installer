#include "unix_support.h"
#include "runtime-brand-assets.h"
#include <glib.h>
#include <memory>
#include <stdexcept>

namespace setup {
namespace {
std::string DesktopExec(const fs::path& executable) {
    std::string quoted = "\"";
    for (char ch : executable.string()) {
        if (ch == '"' || ch == '\\' || ch == '$' || ch == '`') quoted += '\\';
        if (ch == '%') quoted += '%';
        quoted += ch;
    }
    return quoted + "\"";
}
}

void RegisterLinuxLauncher(const fs::path& root) {
    fs::path data = g_get_user_data_dir();
    if (!data.is_absolute()) data = UserHome()/".local/share";
    const auto applications = data/"applications";
    fs::create_directories(applications);
    const auto target = applications/(Brand().identifier + ".desktop");
    const auto logo = BrandConfigurationDirectory(root)/"branding"/IYW_RUNTIME_LOGO_NAME;
    std::unique_ptr<GKeyFile, decltype(&g_key_file_unref)> entry(g_key_file_new(), g_key_file_unref);
    const auto set = [&](const char* key, const std::string& value) {
        g_key_file_set_string(entry.get(), "Desktop Entry", key, value.c_str());
    };
    set("Type", "Application");
    set("Name", Brand().name);
    set("Icon", logo.string());
    set("Exec", DesktopExec(AppExecutable(root)));
    set("Categories", "Utility;");
    GError* error = nullptr;
    if (!g_key_file_save_to_file(entry.get(), target.c_str(), &error)) {
        const std::string message = error->message;
        g_error_free(error);
        throw std::runtime_error("无法保存应用菜单快捷方式：" + message);
    }
}
}
