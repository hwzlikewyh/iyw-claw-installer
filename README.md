# 原助理安装向导

C++17 + wxWidgets 3.2 原生安装入口。Windows 对外一个 `原助理安装向导.exe`，
入口采用 32 位构建，可在 32/64 位 Windows 启动，并按原生系统与架构选择 Fusion 制品。
产品名称统一为“原助理”。

0.2.9 修复应用内更新时向导自身锁住 app 目录，以及失败后重试复用已失效的父进程；
中文诊断日志显式写入 UTF-8 BOM。详见 [更新与重试修复](docs/update-lock-retry.md)。

0.2.8 接通 Win7 x64/x86 的统一更新交接，保留旧 NSIS 更新兼容；
微软前置组件和应用包准备完成后才通知旧应用退出。详见 [Win7 统一入口](docs/win7-unified-entry.md)。

独立签名修复包为 [0.2.8 原助理安装包](https://github.com/hwzlikewyh/iyw-claw-installer/releases/tag/v0.2.8)。
向导在线下载 Fusion 当前稳定版；有直接载荷时直接部署，历史版本沿用 NSIS 兼容路径。
2026-11-16 回读线上稳定版为 0.1.333，0.1.340 已撤回；本轮保持该状态。
详细范围和验收见 [安装兼容修复](docs/optional-browser-hotfix.md)。

源码仓库：[hwzlikewyh/iyw-claw-installer](https://github.com/hwzlikewyh/iyw-claw-installer)。
历史候选：[0.1.0 三端包](https://github.com/hwzlikewyh/iyw-claw-installer/releases/tag/v0.1.0)。
三端构建方式、产物格式和运行依赖见 [构建说明](docs/builds.md)。

## 使用

运行 `dist/原助理安装向导.exe` → 开始安装 → 确认位置 → 下载并安装 → 启动原助理。
这是接入正式分发的程序，确认安装会实际部署或更新应用。
每次重新查询 Fusion stable 通道，允许升级、同版本复用和回退；不阻止高版本回退到后台稳定版。
相同版本且签名有效的主程序会跳过下载与重新安装。

## 主包与预置环境

主包来自 Fusion 的当前系统稳定版选择接口；环境来自与该版本、系统、架构精确绑定的
`POST /app-updates/v1/environment/resolve`。不能把后台目录中的全部条目等同于当前平台已发布制品。
实际下载清单以服务器过滤 `installOnInitialize`、生命周期、制品状态及绑定后的结果为准。

当前 Windows x64 计划包括 Node、Git、uv、Chromix、agent-browser、OfficeCLI、Agent Reach、
电脑操作驱动。向导不根据本机浏览器开关再删减计划。环境助手通过明确的组件选择补齐缺项，
结束前再次比对每个组件的版本、制品 ID、摘要身份与入口。Chromix、agent-browser
缺失或准备失败只记录告警，安装仍完成，仅浏览器能力暂不可用，可在应用内修复。
Node、Git、uv 等必需组件缺失及应用签名校验失败仍阻止完成；未通过摘要校验的组件不激活。
后台关闭的本地嵌入模型不下载，不恢复已经停用的本地模型架构。

## 性能上限

- 主包最多 3 路 HTTP Range 下载；先探测支持，服务器不支持时顺序下载。
- 环境最多 2 个包同时下载；与主包阶段分开，不叠加成 5 路。
- 每路固定缓冲，直接写磁盘，不把整个安装包读入内存；UI 进度最多每 100 ms 更新一次。
- 环境摘要校验与解包串行。下载后放入现有环境助手的摘要缓存，助手直接复用。
- 版本、制品身份和入口状态匹配的已安装组件跳过下载、重装与全量内容扫描。
- 普通安装的轻量检查不保证发现非入口文件损坏或同大小内容变化；显式修复保留全量校验。
- Windows 后台安装线程和环境助手使用较低调度优先级，只允许同一用户运行一个向导实例。
- 动画以实际时间推进、缓存静态画面；最小化暂停，闲置约 8 秒后停止，交互时恢复。
- 安装中每 4.5 秒轮播一组使用短句，淡出淡入并轻微上移；关闭系统动画时固定显示，完成即退出轮播。

主包校验大小、SHA-256、Windows 发布者数字签名；macOS/Linux 校验固定公钥的 minisign 签名。
下载或校验可以取消。正式安装事务开始后不强杀进程，关闭请求显示页内等待进度，完成后退出。
没有安装说明弹窗。成功后才开放“启动原助理”。

安装失败时可在结果页查看完整诊断、打开日志或复制诊断信息。
向导保留正式安装器退出码及本次新增日志，并给出对应的处理建议；没有取得具体原因时会明确说明。
日志位置、支持范围和验证边界见 [安装失败诊断](docs/installation-diagnostics.md)。

## 平台与构建

Windows：Visual Studio C++ Build Tools、CMake、wxWidgets 3.2 源码。

```powershell
./scripts/build.ps1 -WxSource <wxWidgets源码目录>
```

默认构建 Win32 统一入口；`-Architecture x64` 可构建专用 x64 入口。
已检测 Windows 7 SP1 的 TLS 1.2；缺少 PowerShell 5.1 或其 .NET 前置组件时，
安装向导会下载微软官方签名包，校验后按本机架构补齐。系统要求重启时会暂停，
重启后重新运行向导即可继续。WebView2 由正式安装包处理。
Win7/ARM64/32 位系统仍需实机验收，缺少精确平台制品时明确失败，不退回其他系统的包。

macOS/Linux：开发机准备 CMake、C++17、pkg-config、libcurl、libarchive、libsodium、
wxWidgets 源码（Linux 还需 GTK 3 开发包），运行：

```sh
WX_SOURCE_DIR=/path/to/wxWidgets ./scripts/build-unix.sh
```

macOS 支持路径为 Intel / Apple Silicon，要求 macOS 13.5+；检查签名、应用标识和版本后部署应用。
Linux 检查 glibc 2.35+、GTK 3、WebKitGTK 4.1；从签名 AppImage 解包运行，避免目标机依赖 FUSE。
两端先在用户目录暂存，保留旧 app 备份，环境和应用检查失败时恢复旧 app。
系统库缺失时提供具体包名；Node、Git、uv 等受管组件由环境助手补齐。

当前已实测 Windows 11 安装复用；macOS 通用版及 Linux x64/ARM64 已完成原生编译、
打包和欢迎窗口启动检查。Win7、更多系统版本，以及 macOS/Linux 的完整安装、升级、
回退与环境补齐仍需实机验收，不能将构建成功等同于全部兼容性验收。
相邻 `iyw-claw` 环境助手已移除无条件排除 Open Computer Use 的过滤；
这项修复需随新版正式应用发布后，旧 Linux 制品才会获得相应行为。

源码与验证记录见 `docs/design.md`、`docs/verification.md`，最新截图为 `docs/final-*.png`。
安装动效演示见 `docs/install-motion.gif`，使用隔离模拟进度录制，正式程序仍按实际进度运行。
v0.1.0 的 Windows 下载文件已完成正式 Authenticode 签名和 DigiCert 时间戳验证。
macOS 应用和 DMG 已完成 Developer ID 签名及安全时间戳验证，Apple 公证尚未完成。
Windows 新程序仍可能因 SmartScreen 下载信誉不足而提示确认，正式签名不保证立即免提示。
当前提供构建候选；依赖许可证随打包目录附带。
签名流程与验证记录见 [签名说明](docs/signing.md)。
