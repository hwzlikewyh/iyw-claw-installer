#include "existing_install.h"
#include "windows_support.h"
#include <array>

namespace setup {
namespace {
std::wstring RegistryPath(HKEY hive, REGSAM view, const std::pair<const wchar_t*, const wchar_t*>& entry) {
    HKEY opened = nullptr;
    if (RegOpenKeyExW(hive, entry.first, 0, KEY_QUERY_VALUE | view, &opened) != ERROR_SUCCESS) return {};
    std::array<wchar_t, 32768> value{};
    DWORD bytes = static_cast<DWORD>(sizeof(value)), type = 0;
    const auto result = RegQueryValueExW(opened, entry.second, nullptr, &type, reinterpret_cast<BYTE*>(value.data()), &bytes);
    RegCloseKey(opened);
    if (result != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ) || bytes >= sizeof(value)) return {};
    if (type == REG_EXPAND_SZ) {
        std::array<wchar_t, 32768> expanded{};
        const auto count = ExpandEnvironmentStringsW(value.data(), expanded.data(), static_cast<DWORD>(expanded.size()));
        if (!count || count > expanded.size()) return {};
        return expanded.data();
    }
    return value.data();
}
}
fs::path NormalizeExistingInstallRoot(const fs::path& candidate) {
    auto text = candidate.wstring();
    if (text.size() >= 2 && text.front() == L'"' && text.back() == L'"') text = text.substr(1, text.size() - 2);
    auto root = fs::path(text).lexically_normal();
    if (root.filename().empty()) root = root.parent_path();
    if (!root.is_absolute() || root == root.root_path()) return {};
    std::error_code error;
    if (fs::is_regular_file(root / "app/iyw-claw.exe", error)) return root;
    if (_wcsicmp(root.filename().c_str(), L"app") == 0 &&
        fs::is_regular_file(root / "iyw-claw.exe", error)) return root.parent_path();
    return {};
}
fs::path FindExistingInstallRoot() {
    constexpr const wchar_t* install = L"Software\\iywclaw\\iyw-claw";
    constexpr const wchar_t* uninstall = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\原助理";
    const std::array<std::pair<const wchar_t*, const wchar_t*>, 3> values{{
        {install, L"InstallRoot"}, {install, L""}, {uninstall, L"InstallLocation"}
    }};
    for (auto hive : {HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE}) {
        for (auto view : {KEY_WOW64_64KEY, KEY_WOW64_32KEY}) {
            for (const auto& value : values) {
                const auto root = NormalizeExistingInstallRoot(RegistryPath(hive, view, value));
                if (!root.empty()) return root;
            }
        }
    }
    return {};
}
std::string ExistingInstallVersion(const fs::path& root) {
    const auto existing = NormalizeExistingInstallRoot(root);
    if (existing.empty()) return {};
    const auto version = FileVersion(existing / "app/iyw-claw.exe");
    return version.empty() ? "版本未知" : version;
}
}
