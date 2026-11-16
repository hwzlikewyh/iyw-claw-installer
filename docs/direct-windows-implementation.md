# Windows 直接安装与更新

2026-11-16 最新状态：Fusion 0.1.340 已撤回，当前 stable 回到 0.1.333。
用户选择保留该版本并先使用独立签名修复包 0.2.6；以下 0.1.340 发布验收内容为历史记录。
可选组件兼容及预准备验证见 [本轮修复](optional-browser-hotfix.md)。

## 分发结构

面向 Windows 10/11 x64，官网 Latest 下载的是独立安装向导 [原助理安装向导 0.2.3](https://github.com/hwzlikewyh/iyw-claw-installer/releases/tag/v0.2.3)，约 4.4 MB。它从 Fusion 查询当前平台的应用版本，下载并校验同一 Release 的 `app_payload_zip`，然后直接部署 `app/` 文件。

独立向导不包含应用本体，也不放进应用载荷。应用 Release 保留三种单独制品：NSIS、bootstrapper 和 app payload。旧版 Claw 继续读取原 Tauri 顶层更新字段（NSIS）；新版先下载独立 bootstrapper，再用它部署 payload。历史 Release 缺少 payload 时，新版安装器也沿 NSIS 兼容路径更新。

Fusion 下载页根据 `/app-updates/v1/latest` 返回的 Windows 制品 URL 自动选 bootstrapper，无需改写前端固定下载地址或文件名。

## 已完成验证

- 三仓库实现已提交到远程主分支，当前工作区干净。Fusion 直接制品契约已部署并在 Swagger 中可见。
- 公开 Fusion latest 对 Windows x64 返回 `bootstrapper` 制品。版本 0.1.340 的 check 响应同时包含同一 Release 的 `app_payload_zip` 和 `bootstrapper`；顶层仍是 NSIS。
- 以安装 ID 查询旧版 0.1.332 和 0.1.333 时仍收到 NSIS 更新 URL；0.1.340 对当前版本检查返回空更新。
- 0.1.340 发布策略在 Fusion 回读为 `stable / optional / manual / 1%`。
- 隔离 GitHub Windows runner 的验收任务 [43809448193](https://github.com/hwzlikewyh/iyw-claw/actions/runs/43809448193) 通过：空白 profile 用独立向导全新安装应用并检查环境完整性；另一干净 runner 先安装旧 NSIS，再用新向导迁移到 0.1.340，用户数据哨兵保持不变。
- 新 NSIS 包内主程序的 Authenticode 签名和时间戳有效；独立安装器在本机验证发布者、时间戳、协议能力及窗口启动/关闭正常。
- 原应用构建 [43679471534](https://github.com/hwzlikewyh/iyw-claw/actions/runs/43679471534) 生成 0.1.340。重签任务复用同一 staging，没有重新编译应用；随后通过 [43809448193](https://github.com/hwzlikewyh/iyw-claw/actions/runs/43809448193) 验收。
- installer 仓库 Latest 为 v0.2.3，独立 EXE SHA-256：`433db35b33811ef94ca810c9c746cba21cd1cca8b68bc6bee358fa120412cf80`。

## 兼容边界

- 旧 Tauri 客户端继续下载 Fusion 顶层 NSIS 地址；不会要求它们识别新增字段。
- macOS/Linux 安装格式未改；Win7 与 x86 Windows 暂不使用直接载荷。
- Windows x64 的 UAC 切换账户、受保护目录授权、卸载交互和断电级故障恢复未做完整实机验收，不在本次已通过范围内。
