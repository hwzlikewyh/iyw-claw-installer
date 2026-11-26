"""为现有构建入口导出渠道配置，PC 制品始终使用 iyw-claw。"""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parent.parent


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("brand", choices=["iyw", "yun"])
    args = parser.parse_args()
    version = re.search(r"VERSION ([0-9.]+) LANGUAGES", (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")).group(1)
    directory = ROOT / ".cache" / ("ci-brand-" + args.brand)
    subprocess.run([sys.executable, str(ROOT / "scripts/prepare-brand.py"),
                    "--config", str(ROOT / "brands" / args.brand / "brand.json"),
                    "--version", version, "--output", str(directory)], check=True)
    resources = directory / "installer-resources"
    subprocess.run([sys.executable, str(ROOT / "scripts/apply-brand.py"),
                    str(directory), "--output", str(resources)], check=True)
    values = json.loads((directory / "build-env.json").read_text(encoding="utf-8"))
    values.update(IYW_BRAND_ASSETS=str(resources),
                  IYW_PACKAGE_DIR="packages" if args.brand == "iyw" else "packages/" + args.brand,
                  IYW_ARTIFACT_PREFIX="YuanAssistant-Setup" if args.brand == "iyw" else args.brand + "-Setup")
    with open(os.environ["GITHUB_ENV"], "a", encoding="utf-8") as stream:
        for name, value in values.items():
            stream.write(f"{name}={value}\n")


if __name__ == "__main__":
    main()
