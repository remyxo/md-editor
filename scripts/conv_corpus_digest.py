#!/usr/bin/env python3
"""转换字节指纹：gfm-corpus.md 全文 + commonmark 652 例逐例，经 mdconv
tohtml/fromhtml 双向，输出「输入sha|输出html sha|输出md sha」三列清单。
等价重构（正则 static 化/indexOf 化/globalReplaceWith 单遍化）改前改后
diff 必须为空。"""
import hashlib, json, subprocess, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MDCONV = sys.argv[2] if len(sys.argv) > 2 else str(ROOT / "build/tests/mdconv")

def conv(mode: str, data: bytes) -> bytes:
    r = subprocess.run([MDCONV, mode], input=data, capture_output=True, timeout=120)
    if r.returncode != 0:
        raise SystemExit(f"mdconv {mode} 失败: {r.stderr.decode(errors='replace')[:200]}")
    return r.stdout

def digest(b: bytes) -> str:
    return hashlib.sha256(b).hexdigest()[:16]

def main():
    out = []
    corpus = (ROOT / "tests/fixtures/gfm-corpus.md").read_bytes()
    h = conv("tohtml", corpus)
    m = conv("fromhtml", h)
    out.append(f"gfm-corpus|{digest(corpus)}|{digest(h)}|{digest(m)}")
    spec = json.loads((ROOT / "tests/fixtures/commonmark/spec-0.31.2.json").read_text(encoding="utf-8"))
    for ex in spec:
        md = ex["markdown"].encode("utf-8")
        h = conv("tohtml", md)
        m = conv("fromhtml", h)
        out.append(f"cm-{ex['example']}|{digest(md)}|{digest(h)}|{digest(m)}")
    Path(sys.argv[1]).write_text("\n".join(out) + "\n", encoding="utf-8")
    print(f"{len(out)} 行指纹已写入 {sys.argv[1]}")

if __name__ == "__main__":
    main()
