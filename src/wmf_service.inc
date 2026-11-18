DWORD ServiceState(SC_HANDLE service) {
    SERVICE_STATUS_PROCESS status{};
    DWORD size = 0;
    if (!QueryServiceStatusEx(service, SC_STATUS_PROCESS_INFO,
        reinterpret_cast<BYTE*>(&status), sizeof(status), &size))
        throw std::runtime_error("无法读取 Windows Update 服务状态，Windows 错误：" +
            std::to_string(GetLastError()));
    return status.dwCurrentState;
}
class ServiceHandle {
    SC_HANDLE value_ = nullptr;
public:
    ~ServiceHandle() { if (value_) CloseServiceHandle(value_); }
    SC_HANDLE get() const { return value_; }
    explicit operator bool() const { return value_ != nullptr; }
    void reset(SC_HANDLE value) {
        if (value_) CloseServiceHandle(value_);
        value_ = value;
    }
};
void WaitForServiceState(SC_HANDLE service, DWORD expected) {
    const auto deadline = GetTickCount64() + ServiceWaitMs;
    while (GetTickCount64() < deadline) {
        if (ServiceState(service) == expected) return;
        Sleep(ServicePollMs);
    }
    throw std::runtime_error("等待 Windows Update 服务切换状态超时。");
}
class WindowsUpdateServiceLease {
    ServiceHandle manager_;
    ServiceHandle service_;
    DWORD startType_ = SERVICE_NO_CHANGE;
    DWORD initialState_ = SERVICE_STOPPED;
    bool changed_ = false;
    bool restored_ = false;
    std::ostream& log_;
public:
    explicit WindowsUpdateServiceLease(std::ostream& log) : log_(log) {
        manager_.reset(OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT));
        if (!manager_) throw std::runtime_error("无法打开 Windows 服务管理器，Windows 错误：" +
            std::to_string(GetLastError()));
        service_.reset(OpenServiceW(manager_.get(), L"wuauserv", SERVICE_QUERY_CONFIG | SERVICE_QUERY_STATUS));
        if (!service_) throw std::runtime_error("无法读取或调整 Windows Update 服务，Windows 错误：" +
            std::to_string(GetLastError()));
        DWORD size = 0;
        QueryServiceConfigW(service_.get(), nullptr, 0, &size);
        if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || !size)
            throw std::runtime_error("无法读取 Windows Update 服务启动类型。");
        std::vector<BYTE> buffer(size);
        auto* config = reinterpret_cast<QUERY_SERVICE_CONFIGW*>(buffer.data());
        if (!QueryServiceConfigW(service_.get(), config, size, &size))
            throw std::runtime_error("无法读取 Windows Update 服务启动类型，Windows 错误：" +
                std::to_string(GetLastError()));
        startType_ = config->dwStartType;
        initialState_ = ServiceState(service_.get());
    }
    ~WindowsUpdateServiceLease() { if (!restored_) Restore(); }
    bool Disabled() const { return startType_ == SERVICE_DISABLED; }
    void Prepare() {
        if (!Disabled()) {
            log_ << "Windows Update 服务未禁用，无需临时调整。\n";
            return;
        }
        service_.reset(OpenServiceW(manager_.get(), L"wuauserv", SERVICE_QUERY_CONFIG | SERVICE_QUERY_STATUS |
            SERVICE_CHANGE_CONFIG | SERVICE_START | SERVICE_STOP));
        if (!service_) throw std::runtime_error("无法临时调整被禁用的 Windows Update 服务，Windows 错误：" +
            std::to_string(GetLastError()));
        if (!ChangeServiceConfigW(service_.get(), SERVICE_NO_CHANGE, SERVICE_DEMAND_START,
            SERVICE_NO_CHANGE, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr))
            throw std::runtime_error("无法临时启用 Windows Update 服务，Windows 错误：" +
                std::to_string(GetLastError()));
        changed_ = true;
        log_ << "已将 Windows Update 服务临时设为手动启动。\n";
        if (initialState_ == SERVICE_STOPPED) {
            if (!StartServiceW(service_.get(), 0, nullptr) && GetLastError() != ERROR_SERVICE_ALREADY_RUNNING)
                throw std::runtime_error("无法启动 Windows Update 服务，Windows 错误：" +
                    std::to_string(GetLastError()));
            WaitForServiceState(service_.get(), SERVICE_RUNNING);
        } else if (initialState_ != SERVICE_RUNNING) {
            throw std::runtime_error("Windows Update 服务当前正在切换状态，暂时无法安全安装。");
        }
        log_ << "Windows Update 服务已启动，开始安装 WMF 5.1。\n";
    }
    bool Restore() noexcept {
        if (restored_) return true;
        restored_ = true;
        if (!changed_) return true;
        bool success = true;
        try {
            const DWORD expected = initialState_ == SERVICE_RUNNING ? SERVICE_RUNNING : SERVICE_STOPPED;
            const DWORD current = ServiceState(service_.get());
            if (current != expected) {
                if (expected == SERVICE_RUNNING) {
                    if (!StartServiceW(service_.get(), 0, nullptr) && GetLastError() != ERROR_SERVICE_ALREADY_RUNNING)
                        throw std::runtime_error("无法恢复 Windows Update 服务的运行状态。");
                } else {
                    SERVICE_STATUS status{};
                    if (!ControlService(service_.get(), SERVICE_CONTROL_STOP, &status) &&
                        GetLastError() != ERROR_SERVICE_NOT_ACTIVE)
                        throw std::runtime_error("无法恢复 Windows Update 服务的停止状态。");
                }
                WaitForServiceState(service_.get(), expected);
            }
            log_ << (expected == SERVICE_RUNNING ? "Windows Update 服务原本正在运行，已恢复运行状态。\n" :
                "Windows Update 服务原本已停止，已恢复停止状态。\n");
        } catch (const std::exception& error) {
            success = false;
            log_ << "恢复 Windows Update 服务运行状态失败：" << error.what() << '\n';
        }
        if (!ChangeServiceConfigW(service_.get(), SERVICE_NO_CHANGE, startType_, SERVICE_NO_CHANGE,
            nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr)) {
            success = false;
            log_ << "恢复 Windows Update 服务启动类型失败，Windows 错误：" << GetLastError() << '\n';
        } else {
            log_ << "Windows Update 服务启动类型已恢复为原设置。\n";
        }
        return success;
    }
};
