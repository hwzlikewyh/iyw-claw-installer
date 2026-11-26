#include "windows_support.h"
#include "payload.h"
#ifdef _WIN32
#include <bcrypt.h>
#include <softpub.h>
#include <wintrust.h>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <vector>
namespace setup {
namespace {
void CheckCrypto(NTSTATUS result) {
    if (result < 0) throw std::runtime_error("无法计算安装包 SHA-256。错误：" + std::to_string(result));
}
struct Algorithm {
    BCRYPT_ALG_HANDLE value = nullptr;
    Algorithm() { CheckCrypto(BCryptOpenAlgorithmProvider(&value, BCRYPT_SHA256_ALGORITHM, nullptr, 0)); }
    ~Algorithm() { if (value) BCryptCloseAlgorithmProvider(value, 0); }
};
struct Hash {
    BCRYPT_HASH_HANDLE value = nullptr;
    std::vector<unsigned char> buffer;
    explicit Hash(BCRYPT_ALG_HANDLE algorithm) {
        DWORD size = 0, copied = 0;
        CheckCrypto(BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&size), sizeof(size), &copied, 0));
        buffer.resize(size);
        CheckCrypto(BCryptCreateHash(algorithm, &value, buffer.data(), size, nullptr, 0, 0));
    }
    ~Hash() { if (value) BCryptDestroyHash(value); }
};
}
void VerifyHash(const Release& release, FileWork& work) {
    auto handle = static_cast<HANDLE>(work.packageLock.get());
    LARGE_INTEGER position{};
    if (!SetFilePointerEx(handle, position, nullptr, FILE_BEGIN)) throw std::runtime_error("无法读取校验文件。");
    Algorithm algorithm; Hash hash(algorithm.value);
    std::vector<unsigned char> bytes(MiB);
    work.progress.phaseTotal = release.size; work.progress.phaseDone = 0;
    for (;;) {
        CheckCancel(work.job);
        DWORD count = 0;
        if (!ReadFile(handle, bytes.data(), static_cast<DWORD>(bytes.size()), &count, nullptr))
            throw std::runtime_error("读取安装包校验数据失败。");
        if (!count) break;
        CheckCrypto(BCryptHashData(hash.value, bytes.data(), count, 0)); Advance(work, count);
    }
    unsigned char digest[32]{}; CheckCrypto(BCryptFinishHash(hash.value, digest, sizeof(digest), 0));
    std::ostringstream text;
    for (const auto byte : digest) text << std::hex << std::setfill('0') << std::setw(2) << static_cast<unsigned>(byte);
    if (work.progress.phaseDone != release.size || text.str() != release.sha256)
        throw std::runtime_error("安装包 SHA-256 与 Fusion 发布摘要不一致，已停止安装。");
}
std::string HashFile(const fs::path& path){
    auto file=OpenLocked(path);Algorithm algorithm;Hash hash(algorithm.value);
    std::vector<unsigned char> bytes(MiB);DWORD count=0;
    for(;;){
        if(!ReadFile(file.get(),bytes.data(),static_cast<DWORD>(bytes.size()),&count,nullptr))throw std::runtime_error("缓存校验读取失败。");
        if(!count)break;CheckCrypto(BCryptHashData(hash.value,bytes.data(),count,0));
    }
    unsigned char digest[32]{};CheckCrypto(BCryptFinishHash(hash.value,digest,sizeof(digest),0));
    std::ostringstream text;for(auto byte:digest)text<<std::hex<<std::setfill('0')<<std::setw(2)<<static_cast<unsigned>(byte);return text.str();
}
namespace {
void VerifyPublisher(const fs::path& path, const wchar_t* publisher,
    const char* mismatch) {
    WINTRUST_FILE_INFO file{}; file.cbStruct = sizeof(file); file.pcwszFilePath = path.c_str();
    WINTRUST_DATA trust{}; trust.cbStruct = sizeof(trust); trust.dwUIChoice = WTD_UI_NONE;
    trust.dwUnionChoice = WTD_CHOICE_FILE; trust.pFile = &file; trust.dwStateAction = WTD_STATEACTION_VERIFY;
    trust.fdwRevocationChecks = WTD_REVOKE_WHOLECHAIN;
    trust.dwProvFlags = WTD_REVOCATION_CHECK_CHAIN_EXCLUDE_ROOT;
    GUID action = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    const auto result = WinVerifyTrust(nullptr, &action, &trust);
    bool publisherMatches = false;
    if (result == ERROR_SUCCESS) {
        const auto provider = WTHelperProvDataFromStateData(trust.hWVTStateData);
        const auto signer = provider ? WTHelperGetProvSignerFromChain(provider, 0, FALSE, 0) : nullptr;
        if (signer && signer->csCertChain) {
            wchar_t name[256]{};
            CertGetNameStringW(signer->pasCertChain[0].pCert, CERT_NAME_SIMPLE_DISPLAY_TYPE, 0, nullptr, name, 256);
            publisherMatches = std::wstring(name) == publisher;
        }
    }
    trust.dwStateAction = WTD_STATEACTION_CLOSE; WinVerifyTrust(nullptr, &action, &trust);
    if (result != ERROR_SUCCESS) {
        std::ostringstream detail;
        detail << "文件数字签名校验失败：" << path.u8string() << "；系统错误：" << result
            << "（0x" << std::hex << std::uppercase << static_cast<unsigned long>(result) << "）";
        if(result==CRYPT_E_FILE_ERROR)detail << "；Windows 无法读取或写入待校验文件。";
        throw std::runtime_error(detail.str());
    }
    if (!publisherMatches) throw std::runtime_error(mismatch);
}
}
void VerifySignature(const fs::path& path) {
    VerifyPublisher(path, L"厦门市虹约产品设计有限公司", "安装包签名发布者与原助理不匹配。");
}
void VerifyMicrosoftSignature(const fs::path& path) {
    VerifyPublisher(path, L"Microsoft Corporation", "系统组件签名发布者不是 Microsoft。");
}
void VerifyDownload(const Release& release, FileWork& work) {
    BeginStage(work, Phase::Verifying);
    auto lock = OpenLocked(work.staging / "setup.exe");
    work.packageLock = std::shared_ptr<void>(lock.release(), HandleClose{});
    work.progress.detail = "校验 SHA-256 与发布者数字签名";
    VerifyHash(release, work);
    CheckCancel(work.job);
    if(release.packageKind==PayloadKind){VerifyPayload(release,work);return;}
    VerifySignature(work.staging / "setup.exe");
    work.progress.events.push_back("SHA-256 与原助理发布者数字签名均通过。");
}
}
#endif
