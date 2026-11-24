# 原助理安装向导

C++17 + wxWidgets 3.2 原生安装入口。Windows 对外一个 `原助理安装向导.exe`，
入口采用 32 位构建，可在 32/64 位 Windows 启动，并按原生系统与架构选择 Fusion 制品。
产品名称统一为“原助理”。

0.2.18 恢复小体积向导，WebView2 改为安装时按需获取；主程序包遵从后台下载大小策略。

Windows 向导不内嵌 WebView2 独立运行库，构建产物必须小于 32 MiB，以兼容旧客户端。
已有可用 WebView2 时直接复用；缺失时先运行应用载荷中的微软在线引导器，由微软选择架构。
在线补装失败后，仅在支持该运行库的 x64 Windows 上下载微软独立安装包。安装前验证
Microsoft Authenticode 签名，安装后核对当前用户的注册项和运行库文件。
主程序包遵循 Fusion 更新响应的 `max_artifact_bytes`，并保留 1 GiB 安全上限；旧后台
未返回该字段时仍接受 1 GiB 内的主包。已下载更新的交接由客户端先检查后台上限。

0.2.13 统一 Windows 正式签名发布链路，补充安装器发布者元数据，并改进跨平台解包失败诊断。

0.2.12 支持在 Windows 直接安装时恢复旧安装器中断的事务。向导会校验旧备份、保留未完成的应用文件并继续安装。
前台应用更新等待旧进程退出 60 秒后，会结束目标安装目录中的旧进程并继续切换。

0.2.11 在启动时检查已有 Windows 安装，兼容当前用户、所有用户及 32/64 位注册表。
识别到旧版后显示版本和原安装位置，按钮切换为更新；旧记录中的 app 子目录会还原到安装根目录，失效记录会跳过。

Windows 更新后提示“无法打开此应用”的本机根因、修复记录和下载流量核验见[启动修复记录](docs/windows-launch-repair.md)。

0.2.10 修复 Windows 7 WMF 5.1 本地安装、重启后续装及更新时的系统 API 兼容；
同时保留更新期间的文件释放和应用身份检查。详见 [Win7 统一入口](docs/win7-unified-entry.md) 与
[Windows 更新锁与重试](docs/update-lock-retry.md)。

0.2.9 修复应用内更新时向导自身锁住 app 目录，以及失败后重试复用已失效的父进程；
中文诊断日志显式写入 UTF-8 BOM。详见 [更新与重试修复](docs/update-lock-retry.md)。

0.2.8 接通 Win7 x64/x86 的统一更新交接，保留旧 NSIS 更新兼容；
微软前置组件和应用包准备完成后才通知旧应用退出。详见 [Win7 统一入口](docs/win7-unified-entry.md)。

独立签名安装包为 [0.2.10 原助理安装包](https://github.com/hwzlikewyh/iyw-claw-installer/releases/tag/v0.2.10)。
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
结束前再次比对每个组件的版本、制品 ID、摘要身份与入口。可选组件（包括 OfficeCLI、
Agent Reach、电脑操作驱动和浏览器组件）缺失或准备失败只记录告警，安装仍完成；
预览使用应用内嵌能力，其他按需能力可在应用内修复。Node、Git、uv 等必需组件缺失及应用签名校验失败仍阻止完成；未通过摘要校验的组件不激活。
后台关闭的本地嵌入模型不下载，不恢复已经停用的本地模型架构。

## 性能上限

- 主包最多 3 路 HTTP Range 下载；先探测支持，服务器不支持时顺序下载。
- 向导预取最多 2 个包同时下载；环境助手准备阶段按 CPU 核数最多 4 个 worker 并行下载、解压和校验，与主包阶段分开。
- 每路固定缓冲，直接写磁盘，不把整个安装包读入内存；UI 进度最多每 100 ms 更新一次。
- 组件激活和环境清单提交保持串行；健康组件直接复用，不重复下载、解包或全量扫描。
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
当前支持 Windows 10、11。Windows 7（含 SP1）打开安装包会提示“暂不支持 Win7 系统。”，确认后退出。
Windows ARM64 和 32 位系统仍需实机验收，缺少精确平台制品时明确失败，不退回其他系统的包。

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
打包和欢迎窗口启动检查。更多受支持的 Windows 版本，以及 macOS/Linux 的完整安装、升级、
回退与环境补齐仍需实机验收，不能将构建成功等同于全部兼容性验收。
相邻 `iyw-claw` 环境助手已移除无条件排除 Open Computer Use 的过滤；
这项修复需随新版正式应用发布后，旧 Linux 制品才会获得相应行为。

源码与验证记录见 `docs/design.md`、`docs/verification.md`，最新截图为 `docs/final-*.png`。
安装动效演示见 `docs/install-motion.gif`，使用隔离模拟进度录制，正式程序仍按实际进度运行。
v0.1.0 的 Windows 下载文件已完成正式 Authenticode 签名和 DigiCert 时间戳验证。
macOS 应用和 DMG 已完成 Developer ID 签名及安全时间戳验证，Apple 公证尚未完成。
Windows 新程序仍可能因 SmartScreen 下载信誉不足而提示确认，正式签名不保证立即免提示。
Windows 构建先生成短期签名输入包，再由 `iyw-signing` 自托管签名机完成正式签名、时间戳和摘要校验；只有校验通过的 `windows-universal` 才作为最终产物上传。
当前提供构建候选；依赖许可证随打包目录附带。
签名流程与验证记录见 [签名说明](docs/signing.md)。
