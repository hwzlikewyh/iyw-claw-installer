# 安装向导发布签名

2026-11-10，用户要求 Windows 复用相邻 `iyw-claw` 项目签名，macOS 使用指定目录的 p12。
签名材料、密码与私钥不进入此仓库。

## Windows

调用既有 `iyw-claw/src-tauri/scripts/sign-staged-windows.mjs`，不复制或重写硬件令牌实现。
复用 CurrentUser/My 中证书，发布者为“厦门市虹约产品设计有限公司”，指纹为
`E0F7DCA01BBA77A2DF1CB40A3B3459F37D4D4248`。
环境变量：`IYW_CLAW_SIGN_MODE=signtool`、`IYW_CLAW_SIGN_REQUIRED=1`、
`IYW_CLAW_SIGN_THUMBPRINT=<指纹>`。无人值守时沿用项目的 `IYW_CLAW_SAFENET_PIN` Secret。

本次 v0.1.0 实际签名验证结果：

- Authenticode 状态 `Valid`，发布者指纹匹配。
- DigiCert RFC 3161 时间戳存在，时间戳及签名信任链检查通过。
- 排除 PE 校验和、证书目录和签名附加区后，程序字节与已构建版本一致。
- ZIP 中可执行文件与单独 EXE 完全一致，签名后重新计算 SHA-256。
- 已替换 v0.1.0 的 EXE、ZIP 和汇总 SHA256SUMS.txt。

Windows EXE SHA-256：
`9586f7327845a0335a84af0291631ca380098e71a948da315e76ee89fa67927f`。
Windows ZIP SHA-256：
`a6fbccb5a1766271bf340b518c2e9579df73beef0e94d7038db77990eb2da0bb`。

### Windows 7 前置组件修复候选（2026-11-12）

来自 iyw-claw-installer main 提交 `853bbcd84bb25c533523113ca2ecdccd7bedb51c` 的
Win32 Release 程序通过相同 Authenticode 身份签名；发布者、证书指纹与 DigiCert 时间戳验证通过。
签名候选复用了现有 v0.1.0 文件名，并已替换该预发布页中的 Windows EXE、ZIP 和摘要清单。
下载后已核对发布者、时间戳、整包 SHA-256 和 ZIP 内 EXE 一致性。

- EXE SHA-256：`9586f7327845a0335a84af0291631ca380098e71a948da315e76ee89fa67927f`
- ZIP SHA-256：`a6fbccb5a1766271bf340b518c2e9579df73beef0e94d7038db77990eb2da0bb`
- ZIP 中 EXE 的 SHA-256 与独立签名 EXE 一致；随包许可证文件保留。

### SmartScreen 提示核查

用户反馈“Windows 已保护你的电脑”，发行者已正确显示公司名称。
实际检查 Downloads 中的 `YuanAssistant-Setup-windows (1).exe`：签名状态 Valid、时间戳存在，
SHA-256 与上述正式发布 EXE 完全一致。

微软官方文档明确，SmartScreen 同时评估发布者信誉和文件摘要信誉；新二进制即使使用
OV/EV 证书仍可能提示“无法识别的应用”。EV 不再提供首次下载免提示保证。
普通消费者端没有手动提交文件以获得 SmartScreen 信誉审核的入口；信誉随真实下载和使用积累，
没有固定通过次数或时间。保持同一签名身份、稳定的分发文件，避免仅为消除提示反复重签。
此提示不等同于 Defender 已检出恶意软件；若出现明确威胁名称，应另走检测误报核查流程。

官方依据：https://learn.microsoft.com/windows/apps/package-and-deploy/smartscreen-reputation

## macOS

工作流 `Sign macOS installer` 对已有 universal ZIP 做签名，不重新编译应用。
输入 `release_tag` 和该 ZIP 的精确 `source_sha256`；下载后先校验摘要，再进行私钥操作。

需要仓库 Secret：

| 名称 | 用途 |
| --- | --- |
| APPLE_CERTIFICATE | Developer ID Application p12 的 base64 |
| APPLE_CERTIFICATE_PASSWORD | p12 导出密码 |
| APPLE_ID | 可选公证账户，与后两项一起配置 |
| APPLE_PASSWORD | Apple app-specific password，仅供公证 |
| APPLE_TEAM_ID | Apple Developer Team ID，仅供公证 |

p12 导入临时 keychain，签名结束无论成功或失败都会清理证书副本和 keychain，恢复搜索列表。
只接受有效 Developer ID Application 身份。应用启用 hardened runtime，应用和 DMG 都加安全时间戳，
随后检查 Developer ID 发布者、时间戳、双架构和代码签名；签名后重新打包 ZIP/DMG并计算摘要。
工作流随后启动签名后的应用进行检查，上传安装文件与 `macos-signing-report.json`。

如果提供完整公证凭据，使用 notarytool 等待 Apple Accepted，为 app 和 DMG 分别 stapling，
并对 app 做 Gatekeeper 检查。未提供凭据时报告 `not-submitted`，不得声称已经公证。
部分提供公证凭据、签名验证失败或公证不被接受都会使任务失败，不降级签名后上传。

2026-11-10 已使用用户重新导出的 p12 完成签名。材料通过 GitHub 加密 Secret 注入，
未写入源码；工作流 [42740685939](https://github.com/hwzlikewyh/iyw-claw-installer/actions/runs/42740685939)
的签名和启动步骤全部成功，工作流源码提交为 `4230692776429650ff0a3063e4ca089f29913699`。

签名身份：Developer ID Application: Xiamen Hongyue Product Design Co.,Ltd. (393H57RCN6)。
证书有效期至 2027-02-02；Team ID 为 `393H57RCN6`。
应用和 DMG 的 Developer ID、Apple Root CA 链、安全时间戳及 hardened runtime 标志均已验证；
Intel/ARM64 两个 Mach-O 分片均包含嵌入式签名。下载后的 ZIP、DMG 摘要验证通过，
已替换 v0.1.0 下载文件、同步本机 `dist/release-0.1.0/` 并更新汇总摘要和签名报告。

macOS DMG SHA-256：
`0f5377d7a29c626061862e29711a8e25a00ce6493457afe552dffca4419c3a99`。
macOS ZIP SHA-256：
`97092953d17471aa8fd5274e14a648b70b4f27cfbe9f9fabc190aa9619bef803`。

未提供 Apple 公证账户凭据，本次公证状态为 `not-submitted`，不能声称 Gatekeeper 公证通过。

官方流程依据：
https://developer.apple.com/documentation/security/customizing-the-notarization-workflow
