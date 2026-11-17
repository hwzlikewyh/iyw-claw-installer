# Windows 任务栏图标资源

主程序为了接收微信分享带有稀疏 MSIX 包身份，Windows Shell 根据包清单的
`VisualElements` 读取图标；这些路径相对于主程序安装位置 `app/`。
旧应用只在 `app/native-share/` 和 MSIX 内交付了 `Assets/` 和 PRI，导致普通 EXE
图标正常，而按包身份读取的任务栏图标缺失。快捷方式的 AppUserModelID 与图标
位置修复并不能补齐这些资源。

直接载荷安装和同版本复用的最终注册阶段先执行 `RestoreShellResources`：

- 仅对 `signed: true`、`receiver: main` 的分享配置补齐外部资源。
- 从已有 `native-share/` 复制 PNG 与 PRI 到 `app/`，复用摘要一致的文件。
- 沿用 `RequirePlainPath` 拒绝链接路径，复制后核对摘要。
- 有文件变化时通知 Shell 重新读取，并记录复制数量。
- 随后运行主包的注册脚本；新版脚本自行验证外部资源，旧版脚本同样能受益。

主程序的 NSIS 安装器和启动检查复用主包内修正后的 `register.ps1`。
2026-11-17 在运行中的 0.1.350 上补齐外部资源并通知 Shell 后，任务栏已恢复蓝色
Logo，进程 PID 保持不变。新增 C++ 注册代码通过 MSVC v145 的 Win32 编译器语法检查。
本次未做完整安装事务、签名与发布验收；源码编译不等同于新安装包已交付。

官方说明：
https://learn.microsoft.com/en-us/windows/apps/desktop/modernize/grant-identity-to-nonpackaged-apps#localization-and-visual-assets
