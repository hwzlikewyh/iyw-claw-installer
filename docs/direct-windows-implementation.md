# Windows 直接安装实施记录

用户已于 2026-11-14 批准书面设计，授权跨项目实现和定向安装、更新验收。

## 验收标准

- Windows x64 有载荷的新安装和更新均不启动 NSIS；历史版本和旧客户端保持原包。
- 同一 release 绑定应用载荷和向导，验证固定签名、摘要、版本、架构和目录。
- 健康环境复用，缺项补齐；更新失败或进程中断可恢复，不删除用户数据。
- 新向导接管快捷方式、协议注册和保留用户数据的卸载；签名后回验发布包。

## 顺序计划（步骤 → 验证）

1. 已实现并通过本地验证：载荷契约与打包。Fusion 增加同发布 app_payload/bootstrapper 元数据、公开 latest 优先向导；Claw 从 Tauri 资源映射生成 app/ ZIP 和 manifest。→ Go 定向回归/vet/build、三协议 smoke、确定性 ZIP 检查通过。
2. **进行中：部署与交接收尾验收。** 向导验签、受控 ZIP 解包、持久恢复、WebView2、环境 prepare/commit、注册与卸载已实现；权限助手与签名产物仍需实测。→ Win32 Release、隔离故障检查、实际 Windows 信任链检查。
3. 已实现，等待签名流程整体验收：Claw 预下载载荷、校验并锁定向导、准备后握手、退出前重检空闲门禁。→ 桌面 cargo check --lib 通过；不据此宣称完整安装通过。
4. 待执行：三仓库集成、签名产物、发布绑定和旧客户端迁移。→ 新装、旧 NSIS 升级、载荷升级、缺项、损坏缓存、失败恢复；报告未完成的平台验收。

Claw 与 Fusion 在 `.worktrees/direct-installer-claw`、`.worktrees/direct-installer-fusion` 独立工作树实施，保留主工作区既有修改。
旧工作区 CodeGraph 索引已用于定位；当前远程源码较索引新，行为以独立工作树实际文件为准。

## 2026-11-15 本地验证与待验收边界

- 新向导 `--capabilities` 返回协议 2，退出码 0；Win32 Release 编译通过。
- 29 项隔离检查覆盖 ZIP 路径、重复文件、真实历史发布签名及篡改拒绝、应用切换中断恢复、环境提交后失败保留备份、无效日志拒绝。签名历史样本为 0.1.282；不包含新包的完整部署。
- 实际环境助手在独立测试根验证备份前、备份后、激活后、恢复中断及重复恢复；危险目标被拒绝，用户数据保留。
- Rust Windows 原生信任链检查接受已签名的 0.1.0 向导，拒绝当前未签名候选。
- 官网下载页按 target/url 取 Fusion latest，没有固定过滤 nsis。当前线上仍为 0.1.333 NSIS，尚未切换。
- 独立安装器 0.2.1。签名工作流使用固定源提交和草稿 release，不覆盖旧版本。
- 权限助手仅接受同一个已签名向导、同一 Windows 账户；普通 UI 保持运行，提权结果回传后由普通 UI 启动应用。静默更新需要提权时转为手动操作。
- 环境事务使用 stdout 专属编号；提交回执 v2 在 current snapshot 持久化后才写 committed，避免后来环境操作造成误回滚。
- 直接安装的卸载入口保存在 root/installer 内按内容摘要命名的签名 EXE，旧副本保留供回滚；成功回执为 installer/direct-current.json。
- 尚未执行：新签名包真实新装、旧版本升级、UAC 同账户/取消/切换账户、卸载与真实断电恢复；macOS/Linux 回归及 Win7/i686 兼容验收。完成这些前不宣称已正式发布。

## 签名与集成进展

- 安装器代码 `aff4365` 已合并远程 main，Fusion `f95054f` 已合并远程 master，Claw `7a2e686b` 已合并 GitHub 与内网 main。
- 签名工作流 `43678232264` 成功。0.2.0 发布为非 Latest 配套制品，暂不替换官网下载；源提交固定 `aff43659615085fb06f52efc966a931d9efdabc6`。
- 已从 GitHub 下载回验：EXE 4,391,296 字节，SHA-256 `e282fe758bce338a08bd61e49b639f2f1c4d00da0f89175d5ad155bc5c6ad96a`，ZIP SHA-256 `2e651102f1eefcb19f607fbba8fce6648f0c72def315b815ecca8ae9d4478be2`；ZIP 内外 EXE 完全相同。
- 发布者为厦门市虹约产品设计有限公司，证书指纹 `E0F7DCA01BBA77A2DF1CB40A3B3459F37D4D4248`，DigiCert 时间戳有效；签名版协议检查退出 0、GUI 实际启动和正常关闭通过。签名文件已复制到 dist 与 packages。
- 0.1.340 应用正式发布任务 `43679471534` 已启动，包含直接载荷与旧 NSIS，必须通过干净环境直接安装和旧安装迁移验证。
- Fusion 线上 Swagger 仍无直接载荷契约，本机 SSH 无服务器登录权限；已向用户询问 Jenkins 任务地址或由用户触发部署。合并代码不代表已部署。
- 早期一次性测试目录的递归删除被自动审查拒绝，已移入忽略的 `.cache`，未进入提交或签名包；没有绕过删除限制。

## 2026-11-16 用户范围修正

- 对外下载独立安装向导；向导再像旧版一样从 Fusion 在线下载应用。
- 向导不塞进 NSIS，也不塞进 `app_payload_zip`。NSIS 继续作为旧客户端兼容制品；bootstrapper 与应用 ZIP 是同一 release 的分离下载制品。
- 原 0.2.0 曾签名发布，但把向导复制进应用包。构建 0.2.1 替代，待回验签名后设为唯一 Latest；0.2.0 不设 Latest。
- Fusion 新契约已部署。使用登录会话检查确认 Swagger 含 `DirectWindowsArtifact`；接口对 `0.1.332` 仍返回 NSIS 更新，对当前 `0.1.333` 和 `0.1.340` 返回无更新，旧接口字段未改变。
- 待办：发布并设 0.2.1 为 Latest；验证 0.1.340 的签名 NSIS、独立 bootstrapper、应用 ZIP，发布 Fusion 后确认 latest 为 bootstrapper、旧版本 check 顶层仍为 NSIS。
- 原验收 CI 上传大体积中间 artifact 超时。改由隔离 Windows runner 从同一草稿下载签名制品，测试新装和旧 NSIS 升级；复用原 staging，不重编应用。
