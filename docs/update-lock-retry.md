# Windows 更新目录占用与重试修复

0.2.9 修复 0.2.8 更新 0.1.350 时的两个问题：

- 向导由旧应用启动时继承 `app` 工作目录。Windows 进程当前目录会阻止目录重命名，
  即使旧应用退出，向导自身仍锁着待替换目录。启动时在任何工作线程创建前切换到系统临时目录。
- 用户点击“返回设置并重试”时复用了旧父进程 PID 和握手文件。首轮失败后旧应用可能已恢复启动，
  原 PID 已退出或被复用。显式重试清除旧握手与静默/已接受标志，保留目标版本、摘要、签名和缓存，
  沿原安装路径重新验签、准备环境、停止本安装目录进程并事务化部署。

首次交接继续校验父进程用户、路径和发布者签名；不会因 PID 校验失败自动跳过身份检查。
仅用户显式点击重试后才转入独立前台更新。诊断日志增加 UTF-8 BOM，便于 Windows 文本查看器
正确识别中文；目录占用和调用方身份错误有对应建议。

验收：隔离目录复现旧程序自身占用 → 新构建在同场景下成功移动目录 → 重试请求保留缓存与
签名并清除旧 PID → Windows Release 编译、打包、签名与启动验证 → Fusion 新版本分发。

初始复现：从新建的 `.cache/update-lock-probe-*/app` 目录启动 0.2.8 向导，仅打开欢迎窗口，
目录改名即报 “The process cannot access the file because it is being used by another process”。
关闭向导后测试目录可清理；未触碰用户应用安装目录。

修复验证：Windows Win32 Release 编译通过；对新构建执行
`scripts/verify-update-working-directory.ps1`，向导运行时可将继承的 app 目录改名并恢复。
原生 C++ 回归验证重试清除旧握手、保留发布身份/缓存/签名和中文诊断；全部通过。

官方依据：Microsoft SetCurrentDirectory 文档明确说明当前目录在进程执行时被锁定，阻止
删除、移动或改名。https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-setcurrentdirectory
