"""在隔离的 CI 检出中应用已校验的候选资源。"""
import argparse
import json
from pathlib import Path
import shutil


def byte_array(name, data):
    rows = [",".join(str(value) for value in data[index:index+1024]) for index in range(0,len(data),1024)]
    return f"inline const unsigned char {name}[] = {{\n" + ",\n".join(rows) + "\n};\n"


def apply(directory, resources):
    from PIL import Image
    brand = json.loads((directory / "brand.json").read_text(encoding="utf-8"))
    resources.mkdir(parents=True, exist_ok=True)
    shutil.copy2(directory / "icon.ico", resources / "icon.ico")
    shutil.copy2(directory / "icon.icns", resources / "icon.icns")
    shutil.copy2(directory / "icon-256.png", resources / "icon.png")
    image = Image.open(directory / "icon.png").convert("RGBA").resize((256,256), Image.Resampling.LANCZOS)
    # wxWidgets 构建裁掉了 PNG 解码器，直接嵌入像素，保留原始透明度。
    header = "#pragma once\n#define IYW_RGBA_LOGO 1\n"
    header += byte_array("LogoRGB", image.convert("RGB").tobytes())
    header += byte_array("LogoAlpha", image.getchannel("A").tobytes())
    (resources / "logo_data.h").write_text(header, encoding="utf-8")
    (resources / "brand-build.json").write_text(json.dumps(brand,ensure_ascii=False,indent=2),encoding="utf-8")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory")
    parser.add_argument("--root", default=str(Path(__file__).resolve().parent.parent))
    parser.add_argument("--output")
    args = parser.parse_args()
    output = Path(args.output).resolve() if args.output else Path(args.root).resolve() / "resources"
    apply(Path(args.directory).resolve(), output)
