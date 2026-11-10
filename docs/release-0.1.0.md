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
Windows 未做发布证书签名；macOS 仅作 ad-hoc 签名，尚无 Developer ID 和 Apple 公证。

本版不改变 Fusion 当前 stable 选择策略，安装向导版本与应用版本独立。
