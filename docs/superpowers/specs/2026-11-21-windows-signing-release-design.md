# Windows 安装包正式签名发布设计

## 目标

Windows 构建完成后，只有经过正式 Authenticode 签名、时间戳和发布者校验的安装包才作为最终流水线产物上传。中间构建包只用于签名机交接，并使用短期保留时间。

## 方案

Windows 构建继续在 GitHub 托管 runner 上完成。构建 job 生成安装向导和 ZIP，上传名为 `windows-signing-input` 的短期中间产物。

新增 Windows 签名 job 在已有的 `iyw-signing` 自托管 runner 上执行。该 job 使用 `iyw-claw` 仓库中现有的 `sign-staged-windows.mjs` 和 SafeNet 签名配置，签名身份通过 `WINDOWS_SIGN_THUMBPRINT` 和 `IYW_CLAW_SAFENET_PIN` 注入，私钥与 PIN 不进入仓库。

签名 job 先签名独立 EXE，再把同一份签名文件写回 ZIP，重新生成 ZIP 和 `SHA256SUMS.txt`。最后校验 Authenticode 状态、证书指纹、RFC 3161 时间戳、ZIP 内外 EXE 一致性和摘要清单，校验成功后上传 `windows-universal` 最终产物。

签名工具从固定的 `iyw-claw` 提交检出，避免构建时无约束地跟随远程最新脚本。升级签名脚本时显式更新该提交引用。

Windows PE 版本资源补充正式公司名和原始文件名，使文件属性、签名发布者和分发包身份保持一致。

签名 job 沿用现有签名机的本地代理访问 GitHub 和 DigiCert 时间戳服务。

## 失败处理

- 签名机、证书指纹、SafeNet PIN 或时间戳服务不可用时，签名 job 失败。
- 签名或校验失败时不上传最终 `windows-universal` 产物。
- 中间 `windows-signing-input` 仅保留一天，避免把未签名包作为长期下载入口。
- 签名阶段不修改源代码和发布密钥，只在工作目录中更新待发布包。

## 验证

- 使用现有签名包运行本地 ZIP 重打包和摘要更新流程。
- 静态检查 GitHub Actions 的 job 依赖、runner 标签、环境变量和产物名称。
- 在具备签名机和证书的环境中，验证最终包的发布者、时间戳、ZIP 内外 EXE 和 SHA-256 清单。
