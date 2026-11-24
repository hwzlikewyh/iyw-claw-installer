#pragma once
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include "release.h"
#include <memory>
namespace setup {
struct HandleClose { void operator()(void* handle) const { if (handle && handle != INVALID_HANDLE_VALUE) CloseHandle(handle); } };
using Handle = std::unique_ptr<void, HandleClose>;
const OSVERSIONINFOEXW& NativeWindowsVersion();
DWORD WindowsProductEdition();
Handle OpenLocked(const fs::path& path);
std::string FileVersion(const fs::path& path);
void RequireAppVersion(const fs::path& path, const std::string& expected, FileWork& work);
void VerifySignature(const fs::path& path);
void VerifyMicrosoftSignature(const fs::path& path);
void ReadWebViewStandalone(const Receive& receive, InstallJob& job);
void VerifyHash(const Release& release, FileWork& work);
void CheckWindowsPowerShell(const Platform& platform, FileWork& work);
}
#endif
