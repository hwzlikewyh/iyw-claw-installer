# 跨平台安装向导构建

源码仓库：https://github.com/hwzlikewyh/iyw-claw-installer（私有）。
构建入口：GitHub Actions → Build installers → Run workflow。
推送 main 分支中的源码、资源或构建脚本变更也会启动构建。

## 交付矩阵

| 平台 | 构建机 | 产物 | 目标系统 |
| --- | --- | --- | --- |
| Windows | windows-2022 | 单 EXE、带许可证 ZIP | Win32 引导入口；按系统识别应用包 |
| macOS | macos-15 | Universal `.app` ZIP、DMG | macOS 13.5+，Intel 与 Apple Silicon |
| Linux x64 | ubuntu-22.04 | DEB、tar.gz | glibc 2.35+，Ubuntu 22.04+ / Debian 12+ |
| Linux ARM64 | ubuntu-22.04-arm | DEB、tar.gz | 同上，aarch64 |

向导版本为 0.1.0；它安装的应用版本由 Fusion stable 通道实时决定，两者版本独立。
Windows 的单 EXE 和 ZIP 包含相同程序，ZIP 额外附带依赖许可证。
macOS 包静态链接 wxWidgets、sodium 和 archive，其余库来自系统，不需要安装 Homebrew。
Linux 的 DEB 声明 GTK、curl、archive、sodium 和 WebKitGTK 4.1 运行依赖。
tar.gz 使用相同可执行程序，适合已有这些系统依赖的电脑，解压后运行目录内的程序。

## 本机构建

需 Python 3.10+、CMake 3.24+ 及对应 C++17 工具链。源码下载会验证固定 SHA-256。

Windows（Visual Studio C++ Build Tools）：

```powershell
python scripts/fetch-deps.py
./scripts/build.ps1 -WxSource .cache/wxWidgets-3.2.12
python scripts/package.py windows
```

macOS（Xcode 命令行工具、CMake、Ninja、pkg-config）：

```sh
python3 scripts/fetch-deps.py --macos
bash scripts/build-macos.sh
python3 scripts/package.py macos
```

Linux（Debian/Ubuntu 示例）：

```sh
sudo apt-get install build-essential cmake pkg-config libgtk-3-dev \
  libcurl4-openssl-dev libarchive-dev libsodium-dev
python3 scripts/fetch-deps.py
WX_SOURCE_DIR="$PWD/.cache/wxWidgets-3.2.12" sh scripts/build-unix.sh
python3 scripts/package.py linux
```

产物保存到 `packages/`；`SHA256SUMS.txt` 记录当前打包目录内各产物的摘要。
Windows/macOS/Linux 必须在各自平台构建，Linux 包还需使用对应 CPU 架构。

## 自动检查与发布边界

Windows 检查 PE 为 i386、无外部 wx/VC Runtime DLL，以及已知 Win7 不支持的导入项；
macOS 检查 arm64/x86_64 双架构、仅引用系统动态库、应用包 ad-hoc 签名有效；
Linux 使用 ldd 检查没有缺失动态库，并生成声明依赖的 DEB。
每个平台会打开欢迎窗口做启动检查，Linux 在 Xvfb 中运行。
这些检查不执行真实安装，也不能代替目标系统上的安装、升级、回退和环境补齐验收。

Windows 向导未做发布证书签名。macOS 仅作 ad-hoc 签名，没有 Developer ID 签名及 Apple 公证；
Gatekeeper 仍会将互联网下载的文件视为未经公证的应用。当前包应作为候选体验版。

## 一手构建资料

- GitHub runner 标签：https://github.com/actions/runner-images
- GitHub runner 规格：https://docs.github.com/en/actions/reference/runners/github-hosted-runners
- wxWidgets 源码：https://github.com/wxWidgets/wxWidgets/releases/tag/v3.2.12
- sodium 源码：https://github.com/jedisct1/libsodium/releases/tag/1.0.22-RELEASE
