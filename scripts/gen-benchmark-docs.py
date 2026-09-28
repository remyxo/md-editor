#!/usr/bin/env python3
"""基准文档生成器：确定性（seed 固定）混合内容 md，供性能基线与回归对比。
内容池：标题/中英段落/8×5 表格/js+python 代码块/引用/列表/任务列表/脚注/行内公式/长行/hr"""
import argparse, pathlib, random

PARAS = [
    "所见即所得编辑器的往返保真要求 MD→HTML→MD 不丢失语义信息。",
    "ProseMirror uses marks to style inline content; sets cannot nest.",
    "表格列宽在 PDF 与 DOCX 两条渲染链路间需要单一来源的决策。",
    "模式切换的感知停顿主要来自代码块高亮的异步补全阶段。",
]
CODE_JS = "function wrap(a) {\n  const [x, y] = a;\n  return `${x}-${y}`;\n}\n"
CODE_PY = "def bench(fn, n=100):\n    import time\n    t0 = time.perf_counter()\n    for _ in range(n):\n        fn()\n    return (time.perf_counter() - t0) * 1000\n"

def gen(n_lines: int, rng: random.Random) -> str:
    out, i = [], 0
    while i < n_lines:
        r = rng.random()
        if r < 0.10:
            lvl = rng.randint(1, 4)
            out.append("#" * lvl + f" 章节 {i}")
            out.append("")
        elif r < 0.16:
            out.append("| 列A | 列B | 列C | 列D | 列E |")
            out.append("|---|---|:---:|---:|---|")
            for _ in range(5):
                out.append(f"| 甲{i} | beta{i} | 中心{i} | {rng.randint(0,999)} | 尾{i} |")
            out.append("")
        elif r < 0.24:
            out.append("```javascript"); out.append(CODE_JS); out.append("```"); out.append("")
        elif r < 0.30:
            out.append("```python"); out.append(CODE_PY); out.append("```"); out.append("")
        elif r < 0.36:
            out.append("> 引用块：跨平台发布为当前重心，Windows 与 macOS 都要验证。"); out.append("")
        elif r < 0.44:
            for _ in range(4):
                out.append(f"- 列表项 {rng.randint(0,9999)}：转换层占位符按类型分域")
            out.append("")
        elif r < 0.50:
            out.append("- [ ] 待办：验证收敛基线")
            out.append("- [x] 已办：外部语料接入")
            out.append("")
        elif r < 0.55:
            out.append(f"行内公式 $E=mc^{i}$ 与脚注[^fn{i}]。")
            out.append("")
            out.append(f"[^fn{i}]: 脚注定义 {i}。")
            out.append("")
        elif r < 0.58:
            out.append("-" * 40); out.append("")
        elif r < 0.62:
            out.append("长行折行点验证：" + "甲乙丙丁戊己庚辛壬癸" * 12 + "。"); out.append("")
        else:
            for _ in range(3):
                out.append(rng.choice(PARAS) + (" 行内 `code` 与 **加粗** 混排。" if rng.random() < 0.3 else ""))
            out.append("")
        i = len(out)
    return "\n".join(out) + "\n"

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--sizes", default="500,2000,4000,8000")
    ap.add_argument("--seed", type=int, default=42)
    ap.add_argument("--outdir", required=True)
    a = ap.parse_args()
    d = pathlib.Path(a.outdir); d.mkdir(parents=True, exist_ok=True)
    for n in [int(x) for x in a.sizes.split(",")]:
        (d / f"bench-{n}lines.md").write_text(gen(n, random.Random(a.seed + n)), encoding="utf-8")
        print(f"bench-{n}lines.md 已生成")

if __name__ == "__main__":
    main()
