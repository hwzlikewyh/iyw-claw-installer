#include <shldisp.h>
#include <thread>

namespace setup {
namespace {
struct WmfCom {
    HRESULT status = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    ~WmfCom() { if (SUCCEEDED(status)) CoUninitialize(); }
};
struct WmfRelease { template<class T> void operator()(T* value) const { if (value) value->Release(); } };
struct WmfVariant {
    VARIANT value{};
    explicit WmfVariant(const fs::path& path) {
        auto native = path; native.make_preferred();
        value.vt = VT_BSTR; value.bstrVal = SysAllocString(native.c_str());
        if (!value.bstrVal) throw std::runtime_error("无法准备 Windows 更新包路径。");
    }
    ~WmfVariant() { VariantClear(&value); }
};
void CheckWmfCom(HRESULT status) {
    if (FAILED(status)) throw std::runtime_error("无法打开微软 ZIP 更新包。系统错误：" + std::to_string(status));
}
bool WmfPackageMatches(const fs::path& package, std::uint64_t size, const std::string& hash) {
    std::error_code error;
    if (!fs::is_regular_file(package, error) || error || fs::file_size(package, error) != size || error) return false;
    try { return HashFile(package) == hash; } catch (const std::exception&) { return false; }
}
void ExtractWmfArchive(const fs::path& archive, const fs::path& package, FileWork& work) {
    WmfCom apartment; CheckWmfCom(apartment.status);
    IShellDispatch* rawShell = nullptr;
    CheckWmfCom(CoCreateInstance(CLSID_Shell, nullptr, CLSCTX_INPROC_SERVER,
        IID_IShellDispatch, reinterpret_cast<void**>(&rawShell)));
    std::unique_ptr<IShellDispatch, WmfRelease> shell(rawShell);
    WmfVariant sourcePath(archive), targetPath(package.parent_path());
    Folder *rawSource = nullptr, *rawTarget = nullptr;
    CheckWmfCom(shell->NameSpace(sourcePath.value, &rawSource));
    std::unique_ptr<Folder, WmfRelease> source(rawSource);
    CheckWmfCom(shell->NameSpace(targetPath.value, &rawTarget));
    std::unique_ptr<Folder, WmfRelease> target(rawTarget);
    if (!source || !target) throw std::runtime_error("系统无法读取微软 ZIP 更新包或目标目录。");
    WmfVariant name(package.filename()); FolderItem* rawEntry = nullptr;
    CheckWmfCom(source->ParseName(name.value.bstrVal, &rawEntry));
    std::unique_ptr<FolderItem, WmfRelease> entry(rawEntry);
    if (!entry) throw std::runtime_error("微软 ZIP 更新包中缺少当前架构的 MSU 文件。");
    VARIANT item{}, flags{}; item.vt = VT_DISPATCH; item.pdispVal = entry.get();
    constexpr long SilentCopy = 4 | 16 | 512 | 1024;
    flags.vt = VT_I4; flags.lVal = SilentCopy;
    CheckWmfCom(target->CopyHere(item, flags));
    work.progress.detail = "正在解压并校验 Windows PowerShell 更新包";
    work.progress.phaseDone = 0; work.progress.phaseTotal = 0; work.job.Publish(work.progress);
}
void WaitForWmfPackage(const fs::path& package, std::uint64_t size, const std::string& hash, FileWork& work) {
    constexpr auto ExtractTimeout = std::chrono::seconds(120);
    constexpr auto ExtractPoll = std::chrono::milliseconds(250);
    const auto started = std::chrono::steady_clock::now();
    while (!WmfPackageMatches(package, size, hash)) {
        CheckCancel(work.job);
        if (std::chrono::steady_clock::now() - started > ExtractTimeout)
            throw std::runtime_error("微软更新包解压或摘要校验未通过，请检查磁盘空间与目录权限后重试。");
        Advance(work, 0); std::this_thread::sleep_for(ExtractPoll);
    }
}
}
}
