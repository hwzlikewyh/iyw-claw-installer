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
`0c9dffa6bc5b27947f3accb7d1d4a02fe7d883ade964dde942cb0b4a4bbb76f2`。
Windows ZIP SHA-256：
`484ebd4fdae1fbd979267c5a966b3cb1b5da510a091e9e4f638ae3031d6f1835`。

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

当前状态：已找到用户指定 p12，空密码无法打开，正等待导出密码；尚未导入 GitHub Secret，
尚未执行此 macOS 签名工作流，下载页上的 Mac 包暂未替换。

官方流程依据：
https://developer.apple.com/documentation/security/customizing-the-notarization-workflow
