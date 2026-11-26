"""把同一候选配置传给三端现有构建入口，在发布机或 CI 中执行。"""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys


def application_environment():
    environment = dict(os.environ)
    for name in ["IYW_BRAND_ID", "IYW_BRAND_NAME", "IYW_BRAND_PRODUCT",
                 "IYW_BRAND_IDENTIFIER", "IYW_BRAND_SCHEME", "IYW_RELEASE_ID", "IYW_PROMOTION_CHANNEL",
                 "NEXT_PUBLIC_IYW_BRAND_NAME", "NEXT_PUBLIC_IYW_BRAND_LOGO",
                 "NEXT_PUBLIC_IYW_BRAND_SCHEME", "IYW_BRAND_ASSETS"]:
        environment.pop(name, None)
    return environment


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--brand-directory")
    parser.add_argument("--repository", required=True)
    parser.add_argument("--kind", choices=["application", "installer"], required=True)
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    command = args.command[1:] if args.command[:1] == ["--"] else args.command
    if not command:
        raise ValueError("请在 -- 后提供该平台现有的构建命令")
    repository = Path(args.repository).resolve()
    if args.kind == "application":
        subprocess.run(command, cwd=repository, env=application_environment(), check=True)
        return
    if not args.brand_directory:
        raise ValueError("安装向导需要 --brand-directory")
    directory = Path(args.brand_directory).resolve()
    brand = json.loads((directory / "brand.json").read_text(encoding="utf-8"))
    # 配置文件也通过与后台导出相同的校验，不执行清单中的命令。
    import importlib.util
    spec = importlib.util.spec_from_file_location("prepare_brand", Path(__file__).with_name("prepare-brand.py"))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    module.validate(brand, (directory / brand["logo"]).read_bytes())
    environment = dict(os.environ)
    environment.update({
        "IYW_BRAND_ID": brand["brandId"],
        "IYW_BRAND_NAME": brand["displayName"], "IYW_BRAND_PRODUCT": brand["productKey"],
        "IYW_BRAND_IDENTIFIER": brand["identifier"], "IYW_BRAND_SCHEME": brand["urlScheme"],
        "IYW_RELEASE_ID": brand["releaseId"], "IYW_PROMOTION_CHANNEL": brand.get("promotionChannel", "stable"),
        "NEXT_PUBLIC_IYW_BRAND_NAME": brand["displayName"],
        "NEXT_PUBLIC_IYW_BRAND_LOGO": "/icon-128x128.png",
        "NEXT_PUBLIC_IYW_BRAND_SCHEME": brand["urlScheme"],
    })
    environment["IYW_BRAND_ASSETS"] = str(directory / "installer-resources")
    apply = [sys.executable, str(Path(__file__).with_name("apply-brand.py")), str(directory),
             "--output", environment["IYW_BRAND_ASSETS"]]
    subprocess.run(apply, cwd=repository, env=environment, check=True)
    subprocess.run(command, cwd=repository, env=environment, check=True)


if __name__ == "__main__":
    main()
