#ifdef _WIN32
#define NOMINMAX
#include <windows.h>

// 新版静态 STL 的 filesystem 引用 CreateFile2；Win7 使用相同参数的 CreateFileW。
extern "C" HANDLE WINAPI YuanCompatibleCreateFile2(LPCWSTR name, DWORD access, DWORD share,
    DWORD disposition, LPCREATEFILE2_EXTENDED_PARAMETERS parameters) {
    if (parameters && parameters->dwSize != sizeof(CREATEFILE2_EXTENDED_PARAMETERS)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return INVALID_HANDLE_VALUE;
    }
    const auto flags = parameters ? parameters->dwFileAttributes | parameters->dwFileFlags |
        parameters->dwSecurityQosFlags : FILE_ATTRIBUTE_NORMAL;
    return CreateFileW(name, access, share, parameters ? parameters->lpSecurityAttributes : nullptr,
        disposition, flags, parameters ? parameters->hTemplateFile : nullptr);
}
#endif
