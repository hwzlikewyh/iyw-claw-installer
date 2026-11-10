#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
// 新版静态 C++ 运行库引用精确时钟；Win7 使用系统已有的 FILETIME 时钟。
extern "C" void WINAPI YuanCompatibleSystemTime(LPFILETIME time){
    using Query=void(WINAPI*)(LPFILETIME);
    static const auto precise=reinterpret_cast<Query>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"GetSystemTimePreciseAsFileTime"));
    if(precise)precise(time);else GetSystemTimeAsFileTime(time);
}
#endif
