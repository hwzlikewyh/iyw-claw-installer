constexpr const wchar_t* WmfLogs[] = {L"wmf-service.log", L"wmf-install.log", L"wmf-install.log.dpx",
    L"wmf-expand-output.log", L"wmf-expand-output.log.stderr", L"wmf-pkgmgr-output.log", L"wmf-pkgmgr-output.log.stderr"};
fs::path WmfWorkerDirectory(const fs::path& session) {
    const auto name = session.wstring();
    const std::wstring prefix = L"yuan-setup-";
    constexpr std::size_t GuidChars = 38;
    GUID id{};
    if (name.size() != prefix.size() + GuidChars || name.compare(0, prefix.size(), prefix) != 0 ||
        name[prefix.size()] != L'{' || name.back() != L'}' || FAILED(CLSIDFromString(name.c_str() + prefix.size(), &id)))
        throw std::runtime_error("WMF 安装会话标识无效。");
    std::array<wchar_t, MAX_PATH> windows{};
    const auto size = GetWindowsDirectoryW(windows.data(), static_cast<UINT>(windows.size()));
    if (!size || size >= windows.size()) throw std::runtime_error("无法定位系统临时目录。");
    return fs::path(windows.data()) / L"Temp" / (L"wmf-" + name);
}
bool CreateWmfDirectory(const fs::path& directory) {
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
        L"O:BAG:BAD:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;OICI;GRGX;;;AU)",
        SDDL_REVISION_1, &descriptor, nullptr)) return false;
    SECURITY_ATTRIBUTES attributes{sizeof(attributes), descriptor, FALSE};
    const bool created = CreateDirectoryW(directory.c_str(), &attributes) != FALSE;
    LocalFree(descriptor);
    return created;
}
void CopyVerifiedWmf(const fs::path& destination, bool x64) {
    const auto source = UserHome() / L".iyw-claw/cache/prerequisites" / WmfPackageName(x64);
    auto locked = OpenLocked(source);
    if (HashFile(source) != (x64 ? WmfX64SHA : WmfX86SHA)) throw std::runtime_error("微软 WMF 缓存摘要不匹配。");
    fs::copy_file(source, destination);
    if (HashFile(destination) != (x64 ? WmfX64SHA : WmfX86SHA)) throw std::runtime_error("管理员暂存的 WMF 包摘要不匹配。");
}
struct WmfWorkspaceCleanup {
    fs::path directory, package;
    ~WmfWorkspaceCleanup() {
        std::error_code error;
        fs::remove(package, error);
        fs::remove_all(directory / L"wmf-package-files", error);
        for (const auto* name : WmfLogs) {
            const auto path = directory / name;
            if (fs::exists(path, error)) MoveFileExW(path.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
        }
        MoveFileExW(directory.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
    }
};
