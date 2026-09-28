#!/usr/bin/env python3
"""跨格式可比特征提取：pdftotext -bbox-layout（PDF）× unzip document.xml（DOCX）× 直读（HTML）。
用法: export_parity_features.py <导出目录>   → 该目录写 features.json"""
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
    out = {}
    for stem, fn in (("pdf", pdf_features), ("docx", docx_features), ("html", html_features)):
        matches = sorted(d.glob(f"*.{stem}"))
        if matches:
            out[stem] = fn(matches[0])
    (d / "features.json").write_text(
        json.dumps(out, ensure_ascii=False, indent=2), encoding="utf-8")
    print(f"features.json 已写入（键: {', '.join(out)}）")

if __name__ == "__main__":
    main()
