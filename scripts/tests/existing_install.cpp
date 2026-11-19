#include "existing_install.h"
#include "windows_support.h"
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace setup {
std::string FileVersion(const fs::path&) { return "0.1.350"; }
}

namespace {
void Require(bool passed, const char* message) {
    if (!passed) throw std::runtime_error(message);
}
struct RegistryFixture {
    std::wstring name = L"Software\\iyw-installer-detection-test-" + std::to_wstring(GetCurrentProcessId());
    HKEY root = nullptr, user = nullptr, machine = nullptr;
    RegistryFixture() {
        DWORD disposition = 0;
        Require(RegCreateKeyExW(HKEY_CURRENT_USER, name.c_str(), 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &root, &disposition) == ERROR_SUCCESS &&
            disposition == REG_CREATED_NEW_KEY, "cannot isolate registry test");
        Require(RegCreateKeyExW(root, L"user", 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &user, nullptr) == ERROR_SUCCESS, "create user fixture");
        Require(RegCreateKeyExW(root, L"machine", 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &machine, nullptr) == ERROR_SUCCESS, "create machine fixture");
        Require(RegOverridePredefKey(HKEY_CURRENT_USER, user) == ERROR_SUCCESS, "redirect user fixture");
        Require(RegOverridePredefKey(HKEY_LOCAL_MACHINE, machine) == ERROR_SUCCESS, "redirect machine fixture");
    }
    ~RegistryFixture() {
        RegOverridePredefKey(HKEY_CURRENT_USER, nullptr);
        RegOverridePredefKey(HKEY_LOCAL_MACHINE, nullptr);
        RegCloseKey(user); RegCloseKey(machine); RegCloseKey(root);
        RegDeleteTreeW(HKEY_CURRENT_USER, name.c_str());
    }
};
void StoreRoot(HKEY hive, const wchar_t* name, const setup::fs::path& path) {
    HKEY key = nullptr;
    Require(RegCreateKeyExW(hive, L"Software\\iywclaw\\iyw-claw", 0, nullptr, 0, KEY_WRITE, nullptr, &key, nullptr) == ERROR_SUCCESS, "create install key");
    const auto value = path.wstring();
    const auto result = RegSetValueExW(key, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()), static_cast<DWORD>((value.size()+1)*sizeof(wchar_t)));
    RegCloseKey(key);
    Require(result == ERROR_SUCCESS, "write fixture path");
}
void CheckDetection(const setup::fs::path& root) {
    RegistryFixture registry;
    const auto userRoot = root / L"用户 安装";
    const auto machineRoot = root / L"所有用户";
    for (const auto& path : {userRoot, machineRoot}) {
        setup::fs::create_directories(path / "app");
        std::ofstream(path / "app/iyw-claw.exe") << "test";
    }
    Require(setup::FindExistingInstallRoot().empty(), "fresh user was detected as installed");
    StoreRoot(HKEY_CURRENT_USER, L"InstallRoot", root / "missing");
    StoreRoot(HKEY_LOCAL_MACHINE, L"InstallRoot", machineRoot / "app");
    Require(setup::FindExistingInstallRoot() == machineRoot, "machine or legacy app root missed");
    StoreRoot(HKEY_CURRENT_USER, L"", userRoot);
    Require(setup::FindExistingInstallRoot() == userRoot, "stale primary record hid user installation");
    Require(setup::NormalizeExistingInstallRoot(userRoot / "app/") == userRoot, "trailing app path was not normalized");
    Require(setup::ExistingInstallVersion(userRoot) == "0.1.350", "existing version missing");
    Require(setup::NormalizeExistingInstallRoot(root / "missing").empty(), "missing installation accepted");
}
}
int main() {
    const auto root = setup::fs::temp_directory_path() / ("iyw-existing-install-" + std::to_string(GetCurrentProcessId()));
    if (!setup::fs::create_directory(root)) return 1;
    try {
        CheckDetection(root);
        setup::fs::remove_all(root);
        std::cout << "existing installation detection passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        setup::fs::remove_all(root);
        return 1;
    }
}
