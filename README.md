# 原助理安装向导

C++17 + wxWidgets 3.2 原生安装入口。Windows 对外一个 `原助理安装向导.exe`，
入口采用 32 位构建，可在 32/64 位 Windows 启动，并按原生系统与架构选择 Fusion 制品。
产品名称统一为“原助理”。

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
结束前再次比对每个组件的版本、制品 ID、摘要身份与入口，默认组件缺失时不报成功。
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

## 平台与构建

Windows：Visual Studio C++ Build Tools、CMake、wxWidgets 3.2 源码。

```powershell
./scripts/build.ps1 -WxSource <wxWidgets源码目录>
```

默认构建 Win32 统一入口；`-Architecture x64` 可构建专用 x64 入口。
已检测 Windows 7 SP1 的 TLS 1.2、PowerShell 5.1 前置条件；WebView2 由正式安装包处理。
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

当前已实测 Windows 11；Linux 平台代码通过真实目标头文件的编译语法检查，
macOS/Linux/Win7 仍需原生构建、安装、升级和运行验收，不能据此声称已全部支持。
相邻 `iyw-claw` 环境助手已移除无条件排除 Open Computer Use 的过滤；
这项修复需随新版正式应用发布后，旧 Linux 制品才会获得相应行为。

源码与验证记录见 `docs/design.md`、`docs/verification.md`，最新截图为 `docs/final-*.png`。
安装动效演示见 `docs/install-motion.gif`，使用隔离模拟进度录制，正式程序仍按实际进度运行。
向导自身未签名，当前提供本地候选；依赖许可证随 dist 附带。
