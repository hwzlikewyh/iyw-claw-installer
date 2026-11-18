fs::path ExtractWmfPackage(const WmfInstallRequest& request) {
    const auto directory = request.serviceLog.parent_path();
    const auto packages = directory / L"wmf-package-files";
    std::error_code error;
    if (!fs::create_directory(packages, error) || error)
        throw std::runtime_error("无法创建 WMF 更新包解压目录。");
    const auto expand = NativeSystemDirectory() / L"expand.exe";
    const auto output = directory / L"wmf-expand-output.log";
    const auto code = RunCommand({expand,
        {"-F:*", request.package.u8string(), packages.u8string()}, output, 0, packages}, {});
    if (code != 0)
        throw std::runtime_error("expand.exe 解包 WMF 更新包失败，退出码：" + std::to_string(code) +
            "；日志：wmf-expand-output.log");
    fs::path manifest;
    for (const auto& entry : fs::directory_iterator(packages)) {
        if (!entry.is_regular_file() || entry.path().extension() != L".xml" ||
            entry.path().filename().u8string().find("3191566") == std::string::npos) continue;
        if (!manifest.empty()) throw std::runtime_error("WMF 更新包中存在多个安装清单。");
        manifest = entry.path();
    }
    if (manifest.empty()) throw std::runtime_error("WMF 更新包解包后缺少 KB3191566 安装清单。");
    const auto local = packages / L"wmf-local.xml";
    std::ofstream outputManifest(local, std::ios::binary);
    outputManifest << BindWmfSources(ReadOutput(manifest), packages);
    outputManifest.close();
    if (!outputManifest) throw std::runtime_error("无法保存 WMF 本地源安装清单。");
    return local;
}

DWORD RunPkgmgrWmfInstaller(const WmfInstallRequest& request) {
    const auto manifest = ExtractWmfPackage(request);
    const auto pkgmgr = NativeSystemDirectory() / L"pkgmgr.exe";
    const auto output = request.serviceLog.parent_path() / L"wmf-pkgmgr-output.log";
    const auto code = RunCommand({pkgmgr,
        {"/n:" + manifest.u8string(), "/quiet", "/norestart"}, output, 0, manifest.parent_path()}, {});
    return static_cast<DWORD>(code);
}
