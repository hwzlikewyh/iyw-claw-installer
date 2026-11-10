#pragma once
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include "release.h"
#include <memory>
namespace setup {
struct HandleClose { void operator()(void* handle) const { if (handle && handle != INVALID_HANDLE_VALUE) CloseHandle(handle); } };
using Handle = std::unique_ptr<void, HandleClose>;
Handle OpenLocked(const fs::path& path);
std::string FileVersion(const fs::path& path);
void VerifySignature(const fs::path& path);
void VerifyHash(const Release& release, FileWork& work);
}
#endif
