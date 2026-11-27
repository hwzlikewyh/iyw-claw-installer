"""从 Fusion 导出一个已绑定品牌的候选，供三端 CI 使用同一输入。"""
import argparse
import base64
import hashlib
import json
import os
from pathlib import Path
import re
import sys
import urllib.parse
import urllib.request

DEFAULT_SERVER = "https://gateway.iyw.cn/iyw-fusion-api"
MAX_LOGO_BYTES = 5 * 1024 * 1024


def read_json(server, path):
    headers = {
        "token": os.environ.get("IYW_GATEWAY_TOKEN", ""),
        "admin-token": os.environ.get("IYW_ADMIN_TOKEN", ""),
    }
    request = urllib.request.Request(server + path, headers=headers)
    with urllib.request.urlopen(request, timeout=30) as response:
        envelope = json.load(response)
    if envelope.get("code") != 1:
        raise ValueError("Fusion 未返回有效的候选或品牌修订")
    return envelope["data"]


def load_candidate(options):
    if options.config:
        source = Path(options.config).resolve()
        value = json.loads(source.read_text(encoding="utf-8"))
        value["version"] = options.version or value.get("version", "")
        value["releaseId"] = options.release_id or value.get("releaseId", "")
        if "logoData" in value:
            encoded = value.pop("logoData")
            if not isinstance(encoded, str) or len(encoded) > 4 * ((MAX_LOGO_BYTES + 2) // 3):
                raise ValueError("内嵌 PNG 超过大小限制")
            logo = base64.b64decode(encoded, validate=True)
        else:
            with (source.parent / value["logo"]).open("rb") as stream:
                logo = stream.read(MAX_LOGO_BYTES + 1)
        value.setdefault("logoSha256", hashlib.sha256(logo).hexdigest())
        return value, logo
    if not re.fullmatch(r"[1-9][0-9]{0,18}", options.release_id or ""):
        raise ValueError("需要十进制 --release-id 或 --config")
    detail = read_json(options.server, "/admin/api/app-releases/" + options.release_id)
    release = detail["release"]
    if release["status"] != "draft":
        raise ValueError("仅可从草稿创建候选制品")
    revision = options.brand_revision_id or release.get("brandRevisionId")
    if not revision:
        raise ValueError("候选尚未绑定后台品牌修订")
    brand = read_json(options.server, "/admin/api/app-brands/revision?revisionId=" + revision)["brand"]
    if release["productKey"] != "iyw-claw":
        raise ValueError("请选择公共 PC 主包的发布记录")
    brand.update(releaseId=options.release_id, version=release["version"], logo="logo.png")
    logo_url = urllib.parse.urljoin(options.server + "/", brand["logoUrl"])
    if urllib.parse.urlsplit(logo_url).scheme != "https":
        raise ValueError("Logo 下载要求 HTTPS")
    with urllib.request.urlopen(logo_url, timeout=30) as response:
        logo = response.read(MAX_LOGO_BYTES + 1)
    return brand, logo


def validate(brand, logo):
    brand_id = brand["brandId"]
    if not re.fullmatch(r"[a-z][a-z0-9]{1,31}", brand_id):
        raise ValueError("渠道标识无效")
    suffix = "" if brand_id == "iyw" else "-" + brand_id
    identifier = "app.iywclaw" + ("" if brand_id == "iyw" else "." + brand_id)
    if (brand["identifier"], brand["urlScheme"]) != (identifier, "iyw-assistant" + suffix):
        raise ValueError("品牌的应用标识或协议不匹配")
    brand["productKey"] = "iyw-claw"
    if not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+", brand["version"]):
        raise ValueError("可直接晋级候选须使用最终 SemVer")
    if brand.get("promotionChannel", "stable") not in ("stable", "beta"):
        raise ValueError("晋级后的通道无效")
    name = brand["displayName"]
    if not isinstance(name, str) or not name.strip() or name != name.strip() or len(name) > 64 or re.search(r'[\x00-\x1f<>:"/\\|?*;]', name) or name.endswith(".") or name in (".", "..") or re.match(r"^(con|prn|aux|nul|com[1-9]|lpt[1-9])(?:\.|$)", name, re.I):
        raise ValueError("应用名称包含无效字符")
    if brand["releaseId"] and not re.fullmatch(r"[1-9][0-9]{0,18}", brand["releaseId"]):
        raise ValueError("候选发布 ID 无效")
    if len(logo) > MAX_LOGO_BYTES or hashlib.sha256(logo).hexdigest() != brand["logoSha256"]:
        raise ValueError("Logo 长度或摘要不一致")


def write_assets(brand, logo, output):
    from PIL import Image
    import io
    output.mkdir(parents=True, exist_ok=True)
    image = Image.open(io.BytesIO(logo))
    if image.format != "PNG" or max(image.size) > 2048:
        raise ValueError("Logo 须为宽高不超过 2048 像素的 PNG")
    image = image.convert("RGBA")
    canvas = Image.new("RGBA", (1024, 1024))
    scale = min(1024 / image.width, 1024 / image.height)
    image = image.resize((max(1,round(image.width*scale)),max(1,round(image.height*scale))), Image.Resampling.LANCZOS)
    canvas.alpha_composite(image, ((1024-image.width)//2, (1024-image.height)//2))
    (output / "logo.png").write_bytes(logo)
    canvas.save(output / "icon.png")
    canvas.save(output / "icon.ico", sizes=[(16,16),(32,32),(48,48),(64,64),(128,128),(256,256)])
    canvas.save(output / "icon.icns")
    for size in (32, 128, 256):
        canvas.resize((size,size), Image.Resampling.LANCZOS).save(output / f"icon-{size}.png")
    brand["logo"] = "logo.png"
    brand.pop("logoUrl", None)
    (output / "brand.json").write_text(json.dumps(brand, ensure_ascii=False, indent=2), encoding="utf-8")


def write_environment(brand, output):
    values = {
        "IYW_BRAND_ID": brand["brandId"],
        "IYW_BRAND_NAME": brand["displayName"], "IYW_BRAND_PRODUCT": brand["productKey"],
        "IYW_BRAND_IDENTIFIER": brand["identifier"], "IYW_BRAND_SCHEME": brand["urlScheme"],
        "IYW_RELEASE_ID": brand["releaseId"],
        "IYW_PROMOTION_CHANNEL": brand.get("promotionChannel", "stable"),
        "NEXT_PUBLIC_IYW_BRAND_NAME": brand["displayName"],
        "NEXT_PUBLIC_IYW_BRAND_LOGO": "/icon-128x128.png",
        "NEXT_PUBLIC_IYW_BRAND_SCHEME": brand["urlScheme"],
    }
    (output / "build-env.json").write_text(json.dumps(values, ensure_ascii=False, indent=2), encoding="utf-8")
    if os.environ.get("GITHUB_ENV"):
        with open(os.environ["GITHUB_ENV"], "a", encoding="utf-8") as stream:
            for key, value in values.items():
                stream.write(f"{key}={value}\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--server", default=DEFAULT_SERVER)
    parser.add_argument("--release-id")
    parser.add_argument("--brand-revision-id")
    parser.add_argument("--version")
    parser.add_argument("--config")
    parser.add_argument("--output", required=True)
    options = parser.parse_args()
    options.server = options.server.rstrip("/")
    if urllib.parse.urlsplit(options.server).scheme != "https":
        raise ValueError("Fusion 构建元数据要求 HTTPS")
    brand, logo = load_candidate(options)
    validate(brand, logo)
    output = Path(options.output).resolve()
    write_assets(brand, logo, output)
    write_environment(brand, output)
    print(f"品牌 {brand['displayName']}，候选 {brand['releaseId']}，资源已写入 {output}")


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8")
    main()
