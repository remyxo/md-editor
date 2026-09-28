#!/usr/bin/env python3
"""golden 对账：features.json vs golden.json 深比较。
用法: export_parity_compare.py <features.json> <golden.json> [--update]"""
import json, sys
from pathlib import Path

def diff(a, b, path=""):
    out = []
    if isinstance(a, dict) and isinstance(b, dict):
        for k in set(a) | set(b):
            out += diff(a.get(k), b.get(k), f"{path}.{k}")
    elif isinstance(a, list) and isinstance(b, list):
        if len(a) != len(b):
            out.append(f"{path}: 长度 {len(a)} != {len(b)}")
        else:
            for i, (x, y) in enumerate(zip(a, b)):
                out += diff(x, y, f"{path}[{i}]")
    elif a != b:
        out.append(f"{path}: {a!r} != {b!r}")
    return out

feats, golden = (Path(sys.argv[1]), Path(sys.argv[2]))
update = "--update" in sys.argv
new = json.loads(feats.read_text(encoding="utf-8"))
if update or not golden.exists():
    golden.write_text(json.dumps(new, ensure_ascii=False, indent=2), encoding="utf-8")
    print("golden 已写入/更新:", golden); sys.exit(0)
old = json.loads(golden.read_text(encoding="utf-8"))
d = diff(old, new)
if d:
    print("跨格式特征漂移（%d 处）——若为预期变更请 --update 重建 golden：" % len(d))
    print("\n".join(d[:50])); sys.exit(1)
print("对账通过：与 golden 一致")
