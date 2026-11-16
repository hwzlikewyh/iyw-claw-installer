"""打包各平台安装向导，检查架构和依赖，并生成 SHA-256 清单。"""
import argparse
import hashlib
import os
from pathlib import Path
import platform
import plistlib
import re
import shutil
import struct
import subprocess
import sys
import tarfile
import zipfile

ROOT = Path(__file__).resolve().parent.parent
PACKAGES = ROOT / "packages"
STAGING = ROOT / "build-package"
APP_NAME = "原助理安装向导"
VERSION = "0.2.6"
WX_SOURCE = ROOT / ".cache/wxWidgets-3.2.12"


def run(*args):
    result = subprocess.run([str(arg) for arg in args], check=True, text=True,
                            encoding="utf-8", errors="replace",
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    print(result.stdout, end="")
    return result.stdout


def notices(destination):
    destination.mkdir(parents=True, exist_ok=True)
    sources = {
        ROOT / "vendor/nlohmann/LICENSE.MIT": "nlohmann-json-LICENSE.txt",
        WX_SOURCE / "docs/licence.txt": "wxWidgets-LICENSE.txt",
        WX_SOURCE / "docs/lgpl.txt": "wxWidgets-LGPL.txt",
    }
    if platform.system() == "Windows":
        sources[ROOT / ".cache/libsodium-1.0.22/LICENSE"] = "libsodium-LICENSE.txt"
        sources[ROOT / ".cache/libarchive-3.8.11/COPYING"] = "libarchive-LICENSE.txt"
        sources[WX_SOURCE / "src/zlib/README"] = "zlib-LICENSE.txt"
    if platform.system() == "Darwin":
        sources[ROOT / ".cache/libsodium-1.0.22/LICENSE"] = "libsodium-LICENSE.txt"
        sources[ROOT / ".cache/libarchive-3.8.11/COPYING"] = "libarchive-LICENSE.txt"
    for source, name in sources.items():
        shutil.copy2(source, destination / name)
    (destination / "使用说明.txt").write_text(
        "原助理在线安装向导\n\n运行向导后选择安装位置，按实际进度下载最新稳定版和预置环境。\n"
        "安装完成后点击“启动原助理”。已安装且健康的组件会复用。\n"
        "Windows 发布包的签名状态以发布页 SHA256SUMS 和系统签名验证为准。\n"
        "Windows: Win7 SP1/10/11；Win7 需 TLS 1.2 和 PowerShell 5.1。\n"
        "macOS: 13.5+，Intel/Apple Silicon 通用包；首次打开受 Gatekeeper 管理。\n"
        "Linux: Ubuntu 22.04+ / Debian 12+；DEB 自动声明 GTK/curl/archive/sodium/WebKitGTK 依赖。\n"
        "源码及构建记录：https://github.com/hwzlikewyh/iyw-claw-installer\n",
        encoding="utf-8",
    )


def zip_folder(source, output):
    with zipfile.ZipFile(output, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        for path in sorted(source.rglob("*")):
            if path.is_file():
                archive.write(path, path.relative_to(source))


def windows():
    executable = ROOT / f"build-Win32/bin/{APP_NAME}.exe"
    data = executable.read_bytes()
    pe_offset = struct.unpack_from("<I", data, 0x3C)[0]
    if struct.unpack_from("<H", data, pe_offset + 4)[0] != 0x14C:
        raise RuntimeError("Windows entry point must be i386")
    vswhere = Path(os.environ["ProgramFiles(x86)"]) / "Microsoft Visual Studio/Installer/vswhere.exe"
    visual_studio = run(vswhere, "-latest", "-products", "*", "-property", "installationPath").strip()
    dumpbin = sorted(Path(visual_studio).glob("VC/Tools/MSVC/*/bin/Hostx64/x86/dumpbin.exe"))[-1]
    dependencies = run(dumpbin, "/DEPENDENTS", executable)
    if re.search(r"(?:VCRUNTIME|MSVCP|wxmsw).*\.dll", dependencies, re.IGNORECASE):
        raise RuntimeError("Unexpected external GUI or CRT dependency")
    imports = run(dumpbin, "/IMPORTS", executable)
    for name in ("GetSystemTimePreciseAsFileTime", "ProcessPrng", "CreatePseudoConsole"):
        if name in imports:
            raise RuntimeError(f"Unsupported Win7 import: {name}")
    folder = STAGING / "windows"
    folder.mkdir(parents=True, exist_ok=True)
    shutil.copy2(executable, folder / executable.name)
    notices(folder / "licenses")
    shutil.copy2(executable, PACKAGES / "YuanAssistant-Setup-windows.exe")
    zip_folder(folder, PACKAGES / "YuanAssistant-Setup-windows.zip")


def macos():
    source = ROOT / f"build-native/bin/{APP_NAME}.app"
    folder = STAGING / "macos"
    folder.mkdir(parents=True, exist_ok=True)
    bundle = folder / source.name
    shutil.copytree(source, bundle, dirs_exist_ok=True, symlinks=True)
    executable = bundle / f"Contents/MacOS/{APP_NAME}"
    archs = run("lipo", "-archs", executable).split()
    if set(archs) != {"arm64", "x86_64"}:
        raise RuntimeError(f"Expected universal app, got {archs}")
    linked = run("otool", "-L", executable)
    for line in linked.splitlines():
        if "compatibility version" in line and not line.strip().startswith(("/usr/lib/", "/System/Library/")):
            raise RuntimeError(f"Unbundled macOS dependency: {line.strip()}")
    plist_path = bundle / "Contents/Info.plist"
    with plist_path.open("rb") as source_plist:
        plist = plistlib.load(source_plist)
    plist["LSMinimumSystemVersion"] = "13.5"
    with plist_path.open("wb") as output:
        plistlib.dump(plist, output)
    notices(bundle / "Contents/Resources/licenses")
    run("codesign", "--force", "--deep", "--sign", "-", bundle)
    run("codesign", "--verify", "--deep", "--strict", bundle)
    run("ditto", "-c", "-k", "--sequesterRsrc", "--keepParent", bundle,
        PACKAGES / "YuanAssistant-Setup-macos-universal.zip")
    run("hdiutil", "create", "-volname", APP_NAME, "-srcfolder", folder,
        "-ov", "-format", "UDZO", PACKAGES / "YuanAssistant-Setup-macos-universal.dmg")


def linux():
    arch = platform.machine()
    deb_arch = {"x86_64": "amd64", "aarch64": "arm64"}[arch]
    executable = ROOT / f"build-native/bin/{APP_NAME}"
    linked = run("ldd", executable)
    if "not found" in linked:
        raise RuntimeError("Missing Linux runtime dependency")
    run("file", executable)
    folder = STAGING / f"linux-{arch}"
    application = folder / "opt/yuan-assistant-installer"
    application.mkdir(parents=True, exist_ok=True)
    shutil.copy2(executable, application / "yuan-assistant-installer")
    os.chmod(application / "yuan-assistant-installer", 0o755)
    notices(application / "licenses")
    desktop = folder / "usr/share/applications"
    desktop.mkdir(parents=True, exist_ok=True)
    (desktop / "yuan-assistant-installer.desktop").write_text(
        "[Desktop Entry]\nType=Application\nName=原助理安装向导\n"
        "Exec=/opt/yuan-assistant-installer/yuan-assistant-installer\n"
        "Icon=system-software-install\nTerminal=false\nCategories=Utility;\n", encoding="utf-8")
    metadata = folder / "DEBIAN"
    metadata.mkdir(parents=True, exist_ok=True)
    (metadata / "control").write_text(
        f"Package: yuan-assistant-installer\nVersion: {VERSION}\nArchitecture: {deb_arch}\n"
        "Maintainer: IYW <hwzlikewyh@users.noreply.github.com>\nSection: utils\nPriority: optional\n"
        "Depends: libc6 (>= 2.35), libstdc++6 (>= 11), libgtk-3-0, libsm6, libcurl4, libarchive13, libsodium23, libwebkit2gtk-4.1-0\n"
        "Recommends: fonts-noto-cjk\nDescription: Yuan Assistant online setup wizard\n",
        encoding="utf-8")
    run("dpkg-deb", "--root-owner-group", "--build", folder,
        PACKAGES / f"YuanAssistant-Setup-linux-{arch}.deb")
    with tarfile.open(PACKAGES / f"YuanAssistant-Setup-linux-{arch}.tar.gz", "w:gz") as archive:
        archive.add(application, arcname="yuan-assistant-installer")


def checksums():
    rows = []
    for artifact in sorted(PACKAGES.iterdir()):
        if artifact.suffix == ".txt":
            continue
        digest = hashlib.sha256()
        with artifact.open("rb") as source:
            while block := source.read(1024 * 1024):
                digest.update(block)
        rows.append(f"{digest.hexdigest()}  {artifact.name}")
    (PACKAGES / "SHA256SUMS.txt").write_text("\n".join(rows) + "\n", encoding="utf-8")
    print("\n".join(rows))


def main():
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    parser = argparse.ArgumentParser()
    parser.add_argument("target", choices=("windows", "macos", "linux"))
    options = parser.parse_args()
    PACKAGES.mkdir(exist_ok=True)
    {"windows": windows, "macos": macos, "linux": linux}[options.target]()
    checksums()


if __name__ == "__main__":
    main()
