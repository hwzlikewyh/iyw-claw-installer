"""获取构建所需的官方源码，并验证固定 SHA-256。"""
import argparse
import hashlib
from pathlib import Path
import tarfile
import urllib.request

ROOT = Path(__file__).resolve().parent.parent
CACHE = ROOT / ".cache"
DEPENDENCIES = {
    "wxWidgets-3.2.12": (
        "https://github.com/wxWidgets/wxWidgets/releases/download/v3.2.12/wxWidgets-3.2.12.tar.bz2",
        "a62719bb5e1dcc41c1a6fc1eecd499c7ee8521f70402c7b1bb3342a1e16344e0",
    ),
    "libsodium-1.0.22": (
        "https://github.com/jedisct1/libsodium/releases/download/1.0.22-RELEASE/libsodium-1.0.22.tar.gz",
        "adbdd8f16149e81ac6078a03aca6fc03b592b89ef7b5ed83841c086191be3349",
    ),
    "libarchive-3.8.11": (
        "https://github.com/libarchive/libarchive/releases/download/v3.8.11/libarchive-3.8.11.tar.gz",
        "900fe9fc7237c2b04f090f3c3648de8fe597525771f5035016537ce2d667d12b",
    ),
}


def fetch(name):
    url, expected = DEPENDENCIES[name]
    archive = CACHE / url.rsplit("/", 1)[-1]
    if not archive.exists():
        request = urllib.request.Request(url, headers={"User-Agent": "yuan-installer-build"})
        with urllib.request.urlopen(request, timeout=120) as response, archive.open("wb") as output:
            while block := response.read(1024 * 1024):
                output.write(block)
    with archive.open("rb") as source:
        checksum = hashlib.sha256()
        while block := source.read(1024 * 1024):
            checksum.update(block)
        digest = checksum.hexdigest()
    if digest != expected:
        raise RuntimeError(f"Source digest mismatch: {archive.name}")
    if not (CACHE / name).is_dir():
        with tarfile.open(archive) as source:
            source.extractall(CACHE, filter="data")
    print(f"Verified {name}: {digest}")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--macos", action="store_true")
    options = parser.parse_args()
    CACHE.mkdir(exist_ok=True)
    fetch("wxWidgets-3.2.12")
    if options.macos:
        fetch("libsodium-1.0.22")
        fetch("libarchive-3.8.11")


if __name__ == "__main__":
    main()
