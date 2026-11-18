#pragma once
namespace setup {
inline constexpr char WmfX64SHA[] = "c51abfc65dff5f6858c0b9f6420caa0c166df7a6d8b484f8a3c68ea99f0032b0";
inline constexpr char WmfX86SHA[] = "4575c47bedf732c7a215a5dd9e184ac2f06f10c0f23fcef38eec96b0ff3bd121";
inline const wchar_t* WmfPackageName(bool x64) {
    return x64 ? L"Win7AndW2K8R2-KB3191566-x64.msu" : L"Win7-KB3191566-x86.msu";
}
}
