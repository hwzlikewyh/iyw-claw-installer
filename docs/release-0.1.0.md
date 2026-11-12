# 原助理安装向导 0.1.0 候选版

这是在线安装向导；启动后从 Fusion 查询目标系统的最新 stable 应用及预置环境。
向导支持同版本复用、缺失组件补齐、真实安装进度及安装期间的短句轮播。

| 系统 | 推荐下载 | 说明 |
| --- | --- | --- |
| Windows | YuanAssistant-Setup-windows.exe | 统一 Win32 入口，识别系统版本与架构 |
| macOS | YuanAssistant-Setup-macos-universal.dmg | macOS 13.5+，Intel / Apple Silicon 通用 |
| Linux x64 | YuanAssistant-Setup-linux-x86_64.deb | Ubuntu 22.04+ / Debian 12+ |
| Linux ARM64 | YuanAssistant-Setup-linux-aarch64.deb | 同上，ARM64 |

Windows ZIP 额外附带依赖许可证；macOS ZIP 和 DMG 包含同一应用；Linux tar.gz
供已准备 GTK、curl、archive、sodium、libSM 和 WebKitGTK 4.1 的环境直接解压运行。
所有文件的校验值见附件 `SHA256SUMS.txt`。

完成原生 Release 构建、架构/动态库检查和欢迎窗口启动检查。
这些检查不会执行正式应用安装，Win7 和各系统的完整安装/升级/回退仍需实机验收。
Windows EXE（含 ZIP 内程序）已用“厦门市虹约产品设计有限公司”的正式证书完成
Authenticode 签名和 DigiCert RFC 3161 时间戳，发布者与信任链校验通过。
macOS 应用与 DMG 已用 Xiamen Hongyue Product Design Co.,Ltd. 的 Developer ID Application
证书正式签名，含安全时间戳，签名验证和启动检查通过。Apple 公证尚未完成。
附件 `macos-signing-report.json` 记录了签名和公证状态。

Windows 新程序即使签名有效，也可能显示 SmartScreen“无法识别的应用”提醒。
这项检查还依据文件和发布者信誉；签名不会立即建立下载信誉。请确认下载来源与发行者后操作。

2026-11-12，Windows 统一安装程序已补齐 Win7 PowerShell 5.1 / .NET 前置检查及微软官方组件修复。
v0.1.0 预发布页 Windows EXE、带许可证 ZIP 和 SHA256SUMS 已用同一正式签名身份重新签名、
替换并从 Release 下载回验。新 EXE SHA-256：
`9586f7327845a0335a84af0291631ca380098e71a948da315e76ee89fa67927f`。

本版不改变 Fusion 当前 stable 选择策略，安装向导版本与应用版本独立。
