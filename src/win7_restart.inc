namespace {
constexpr wchar_t RestartRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\RunOnce";
constexpr wchar_t RestartRunValue[] = L"YuanAssistantSetupResume";
fs::path RestartCache() { return UserHome() / L".iyw-claw/cache/win7-resume"; }

void RegisterRestartEntry(const fs::path& directory) {
    const auto current = CurrentInstaller();
    const auto runner = directory / L"YuanSetup.exe";
    RequirePlainPath(runner);
    std::error_code error;
    if (!fs::equivalent(current, runner, error)) {
        const auto temporary = directory / L"YuanSetup.new";
        RequirePlainPath(temporary);
        fs::copy_file(current, temporary, fs::copy_options::overwrite_existing);
        if (!MoveFileExW(temporary.c_str(), runner.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("无法保存重启后使用的向导，请重启后手动打开本安装向导。");
    }
    const std::wstring command = L"\"" + runner.wstring() + L"\" --resume-after-restart";
    constexpr size_t RunOnceLimit = 260;
    if (command.size() >= RunOnceLimit || RegSetKeyValueW(HKEY_CURRENT_USER, RestartRunKey, RestartRunValue,
        REG_SZ, command.c_str(), static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t))) != ERROR_SUCCESS)
        throw std::runtime_error("无法登记登录后续装，请重启后手动打开本安装向导。");
}

void SaveRestartSession(const fs::path& directory, const RestartSession& session) {
    nlohmann::json plan{{"destination", session.destination.u8string()}};
    if (session.selection) {
        const auto& selected = *session.selection;
        const auto& release = selected.release;
        plan["selection"] = {{"version",release.version},{"url",release.url},{"size",release.size},
            {"sha256",release.sha256},{"kind",release.packageKind},{"fresh",selected.resumedFresh}};
    }
    WriteDurableJSON(directory / L"session.json", plan);
}

void WarmRestartCache(FileWork& work, RestartSession& session) {
    const auto directory = RestartCache();
    BeginStage(work, Phase::Resolving);
    UpdateRequest selected = session.selection.value_or(UpdateRequest{});
    if (!session.selection) {
        selected.release = ResolveRelease(CurrentPlatform(), work.job);
        selected.resumedFresh = true;
    }
    work.progress.version = selected.release.version;
    session.selection = selected;
    SaveRestartSession(directory, session);
    const auto cached = directory / L"setup.exe";
    RequirePlainPath(cached);
    BeginStage(work, Phase::Downloading);
    std::error_code error;
    if (selected.packageCache.empty() && fs::is_regular_file(cached, error) &&
        fs::file_size(cached, error) == selected.release.size && HashFile(cached) == selected.release.sha256)
        selected.packageCache = cached;
    if (!selected.packageCache.empty()) StageCachedPackage(selected, selected.release, work);
    else {
        if (fs::space(work.staging).available < selected.release.size + 64 * MiB)
            throw std::runtime_error("下载磁盘空间不足，稍后继续。");
        DownloadPackage(selected.release, work);
        VerifyDownload(selected.release, work);
    }
    work.packageLock.reset();
    if (!MoveFileExW((work.staging / L"setup.exe").c_str(), cached.c_str(),
        MOVEFILE_COPY_ALLOWED | MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("主包暂存失败，登录后将重新准备。");
    work.progress.events.push_back("重启前已缓存并校验主包。");
    ResolvePresetEnvironment(selected.release, work);
    PrefetchEnvironment(work);
}
}

void PrepareRestart(FileWork& work, const std::optional<UpdateRequest>& request) {
    const auto directory = RestartCache();
    RequirePlainPath(directory);
    fs::create_directories(directory);
    RestartSession session{work.progress.destination, request};
    SaveRestartSession(directory, session);
    RegisterRestartEntry(directory);
    work.progress.restartRegistered = true;
    try { WarmRestartCache(work, session); }
    catch (const std::exception& error) {
        work.progress.events.push_back(std::string("剩余准备工作将在登录后继续。") + error.what());
    }
}

RestartSession LoadRestartSession() {
    const auto path = RestartCache() / L"session.json";
    RequirePlainPath(path);
    const auto plan = nlohmann::json::parse(ReadOutput(path));
    RestartSession session{fs::u8path(plan.at("destination").get<std::string>()), {}};
    if (!session.destination.is_absolute() || session.destination == session.destination.root_path())
        throw std::runtime_error("续装记录中的安装位置无效，请重新打开向导选择位置。");
    if (!plan.contains("selection")) return session;
    const auto& record = plan.at("selection");
    UpdateRequest selected;
    selected.destination = session.destination;
    selected.resumed = true;
    selected.resumedFresh = record.at("fresh");
    selected.release = {record.at("version"),record.at("url"),record.at("sha256"),record.at("size"),{},record.at("kind")};
    std::string reason;
    if ((!selected.release.packageKind.empty() && selected.release.packageKind != "nsis") ||
        !ValidateRequestedRelease(selected.release, reason)) throw std::runtime_error("Win7 续装记录中的发布信息无效。");
    selected.channel = "stable";
    selected.requestURL = selected.release.url;
    const auto cached = RestartCache() / L"setup.exe";
    RequirePlainPath(cached);
    std::error_code error;
    if (fs::is_regular_file(cached, error)) selected.packageCache = cached;
    session.selection = std::move(selected);
    return session;
}

void ClearRestartSession(const fs::path& destination) {
    try {
        const auto directory = RestartCache();
        if (!fs::exists(directory / L"session.json") || LoadRestartSession().destination != destination) return;
        RegDeleteKeyValueW(HKEY_CURRENT_USER, RestartRunKey, RestartRunValue);
        for (const auto* name : {L"session.json", L"setup.exe", L"YuanSetup.exe"}) {
            std::error_code error;
            fs::remove(directory / name, error);
        }
    } catch (const std::exception&) { }
}
