"""输出构建矩阵，只接受与配置内容一致的渠道目录。"""
import json
from pathlib import Path
import re

root = Path(__file__).resolve().parents[1] / "brands"
ids = []
for path in sorted(root.glob("*/brand.json")):
    brand = json.loads(path.read_text(encoding="utf-8"))
    brand_id = path.parent.name
    if not re.fullmatch(r"[a-z][a-z0-9]{1,31}", brand_id) or brand_id != brand["brandId"]:
        raise ValueError(f"品牌目录与配置不符：{path}")
    ids.append(brand_id)
if not ids:
    raise ValueError("请至少配置一个安装品牌")
print("ids=" + json.dumps(ids))
