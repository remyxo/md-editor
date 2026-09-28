#!/usr/bin/env python3
"""跨格式可比特征提取：pdftotext -bbox-layout（PDF）× unzip document.xml（DOCX）× 直读（HTML）。
用法: export_parity_features.py <导出目录> [basename]   → 该目录写 features.json
basename=导出 md 的基名：按 <base>.<ext> 精确取产物，工作目录有他名残留文件时不参与"""
import json, re, subprocess, sys, zipfile
from pathlib import Path

def pdf_features(p: Path):
    # 注：-bbox 只出 <word>（本机 poppler 25.03 实证无 <line> 元素，lines_per_page
    # 恒为全 0 的死特征）；-bbox-layout 才有 flow/block/line/word 层级，行数真实
    xml = subprocess.run(["pdftotext", "-bbox-layout", str(p), "-"],
                         capture_output=True, text=True, check=True).stdout
    pages = re.findall(r'<page width="([\d.]+)" height="([\d.]+)"', xml)
    lines_per_page = [len(re.findall(r"<line", m))
                      for m in re.findall(r"<page.*?</page>", xml, re.S)]
    return {"page_count": len(pages),
            "page_size": pages[0] if pages else None,
            "lines_per_page": lines_per_page}

def docx_features(p: Path):
    with zipfile.ZipFile(p) as z:
        doc = z.read("word/document.xml").decode("utf-8")
    tables = []
    for tbl in re.findall(r"<w:tbl>.*?</w:tbl>", doc, re.S):
        tables.append({"cols": len(re.findall(r"<w:gridCol", tbl)),
                       "rows": len(re.findall(r"<w:tr[ >]", tbl))})
    return {"table_count": len(tables), "tables": tables,
            "paragraph_count": len(re.findall(r"<w:p[ >]", doc))}

def html_features(p: Path):
    t = p.read_text(encoding="utf-8")
    tables = [{"cols": len(re.findall(r"<t[hd]", first_row)),
               "rows": len(re.findall(r"<tr[ >]", tbl))}
              for tbl in re.findall(r"<table.*?</table>", t, re.S)
              for first_row in [re.search(r"<tr.*?</tr>", tbl, re.S).group(0)
                                if re.search(r"<tr.*?</tr>", tbl, re.S) else ""]]
    return {"table_count": len(re.findall(r"<table", t)), "tables": tables,
            "paragraph_count": len(re.findall(r"<p[ >]", t))}

def main():
    d = Path(sys.argv[1])
    # 可选 basename：按 <base>.<ext> 精确取（is_file 直判，base 含 glob 元字符
    # 也安全）；未给时按通配。两种方式命中数都必须恰为 1——0=缺产物、
    # >1=歧义（他名残留混入，旧版 sorted()[0] 会静默取错文件），均报错退出
    base = sys.argv[2] if len(sys.argv) > 2 else None
    out = {}
    for stem, fn in (("pdf", pdf_features), ("docx", docx_features), ("html", html_features)):
        if base:
            exact = d / f"{base}.{stem}"
            matches = [exact] if exact.is_file() else []
        else:
            matches = sorted(d.glob(f"*.{stem}"))
        if not matches:
            expect = str(exact) if base else f"{d}/*.{stem}"
            sys.exit(f"错误: {stem} 产物缺失（期望 {expect}）")
        if len(matches) > 1:
            sys.exit(f"错误: {stem} 命中 {len(matches)} 个文件（"
                     + ", ".join(str(m) for m in matches)
                     + "）——工作目录有他名残留，清理后重跑")
        out[stem] = fn(matches[0])
    (d / "features.json").write_text(
        json.dumps(out, ensure_ascii=False, indent=2), encoding="utf-8")
    print(f"features.json 已写入（键: {', '.join(out)}）")

if __name__ == "__main__":
    main()
