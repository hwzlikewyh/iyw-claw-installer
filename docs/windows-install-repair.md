# Windows 残留重装与身份修复

日期：2026-11-26。

## 问题

故障机安装 0.1.366 时出现两类错误：身份注册检查返回 1；重试时在“安装应用”阶段返回 `-2146885629 / 0x80092003 / CRYPT_E_FILE_ERROR`。

用户已确认 `D:\原助理\app` 只剩 Assets 和 PRI 文件，`iyw-claw.exe` 不存在。旧逻辑把非空 app 目录视为完整旧安装，尝试校验不存在的 EXE，导致残留目录阻断重新安装。第一次身份注册的具体系统错误尚未取得，不能据此断言其唯一根因。

## 处理方式

- 不完整或签名异常的旧 app 进入原有备份、替换、提交事务；不执行损坏的旧主程序。新载荷和新可执行文件仍须通过完整校验。
- 主程序身份先核对当前用户的包名称、发布者、版本、状态、外部位置和应用自带检查；不匹配时正常注册，仍失败再移除对应的当前用户身份包并重新安装一次。
- 使用 ForceUpdateFromAnyVersion 支持回退；使用 ForceTargetApplicationShutdown 处理目标包占用，不使用会关闭依赖包的 ForceApplicationShutdown，也不使用 AllUsers。
- 旧载荷注册脚本在独立的 Windows PowerShell 进程中运行，显式设置工作目录，隔离 exit，并同时读取输出及错误流。向导生成的脚本采用 UTF-8 BOM。
- 在现有部署目录保存 native-identity.json。文件回滚后恢复原身份；首次安装失败或原目录无主程序时清理对应残留身份。原位置不可读、旧文件损坏或 Windows 拒绝恢复时，保留日志并允许下次安装修复。
- 残留目录卸载不再校验不存在的主程序，也不会执行失去主程序签名校验前提的残留注册脚本。
- 签名失败显示具体文件及十六进制系统错误；身份失败将错误输出摘录到安装诊断，同时保留完整 stderr。原身份恢复使用独立日志，避免覆盖注册失败的原因。

这些变更针对应用文件和 Windows 身份，不删除 `.iyw-claw` 用户数据。

## 验证

| 范围 | 结果 |
| --- | --- |
| 原生部署事务 | 先复现缺 EXE 的失败；修复后残留、全新、正常旧版、损坏旧版和成功切换共 5 个场景通过，验证备份、回滚与旁侧用户文件保留 |
| Windows PowerShell 5.1 | 16 个隔离检查通过，包括健康复用、重建、换路径、版本回退、损坏状态、重试上限、发布者隔离、签名阻断、回滚、子进程 exit、中文空格路径、双流大输出和超时 |
| 完整脚本入口 | 通过实际 Windows PowerShell 启动完整脚本和子脚本；系统注册边界使用替身，未改动本机已安装应用 |
| 实际 Windows 验签 | 正式包签名通过；不存在文件返回含路径和 0x80092003 的诊断；篡改 EXE 被拒绝，共 3 项通过 |
| 已发布载荷 | 0.1.366 实际 MSIX 签名 Valid，发布者与 config.json 相符 |
| 构建与成品 | 原助理 Win32 Release 编译通过；协议 2 能力入口、PE 架构及成品内嵌脚本与最终源码一致性通过 |
| 候选签名 | 原发布者 Authenticode 签名和 DigiCert 时间戳验证通过 |

修复首版为 0.2.20，基于远程主分支 76a94e1 集成本次修复，保留 0.2.19 的目录切换和 WebView2 行为。0.2.20 公开回读发现 latest 只返回该版本的两个 Windows 制品，已暂停其 Fusion 分发。0.2.21 同时提供完整的 8 项制品：Windows 使用重新编号的修复包，macOS/Linux 明确复用 0.2.19 原文件。

发布分支再次通过 5 项原生事务场景、已有更新重试和安装位置识别回归、实际向导工作目录检查；Windows 身份脚本与完成 16 项检查的候选完全一致。Win32 Release 构建、发布者签名、RFC 3161 时间戳及 ZIP 内外一致性均验证通过。故障机完整安装仍需实际复测。

0.2.20 Windows EXE SHA-256：`789669db3fb408bca3aba5c8af0acdb263bce69b7506d1e7a1073bb55adb55e6`，大小 4,528,048 字节。

0.2.20 Windows ZIP SHA-256：`e106bd8127aa27a7d5a90bf00393f9018c071afab0c3020d8e36e9283a853b4f`。

0.2.21 Windows 构建和包内外签名检查通过。EXE SHA-256 为 `f0886b584ad5e79a93ea2315bec06048895ba1bcdb8ce79305265901b26ec9e2`；ZIP 为 `71767451242acc893cc02125642a97cefddc1c66414e3f6b2c3da7057199e584`。macOS/Linux 六项制品与 0.2.19 逐文件摘要一致。

## 接口依据

- [Add-AppxPackage](https://learn.microsoft.com/en-us/powershell/module/appx/add-appxpackage?view=windowsserver2025-ps)
- [Remove-AppxPackage](https://learn.microsoft.com/en-us/powershell/module/appx/remove-appxpackage?view=windowsserver2025-ps)

PreserveApplicationData 仅适用于开发态 loose-file 注册，正式 MSIX 重建不使用此参数。
