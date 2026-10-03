#!/usr/bin/env bash
# =============================================================================
# probe-docx-table-parity.sh — DOCX 两模式表格列宽对齐回归门（γ 修复验证）
#
# 断言口径：同一 md 文件分别按 WYSIWYG / 源码模式走真实导出管线，产出的
# DOCX word/document.xml 的 tblGrid **逐列相等**（同机同环境逐列 0 容差，
# w2-gamma 定调报告裁决②；总和恒被钳到页内容宽、无判别力，不作判据）。
#
# 管线构成（全部产品代码，无行为等价重写）：
#   [WYSIWYG] 编辑器 getExportHtml() ──────────┐
#                                              ├→ TableColumnNormalizer::
#   [源码]    textarea MD → mdconv tohtml ─────┘   normalize（DOCX 基准 cfg，
#                                                  含 border:'0' 测量期清零）
#                                            → DocxExporter::exportToDocx
#                                            → 解包比对 tblGrid
# 归一+导出执行器 scripts/docx-parity-harness.cpp 在 /tmp 工程内编译，链接
# 仓库产品源文件（与 tests/test_docxexporter 同一组编译单元）——归一 cfg
# 与共享脚本 table-column-layout.js 始终取当前工作区源码，不依赖旧二进制。
#
# 骨架来源与出处：
#   - 无头采集骨架：scripts/srcmode-md-probe.js + scripts/probe-srcmode-md.sh
#     （其骨架又出自 scripts/mode-switch-probe.js / measure-mode-switch.sh 的
#     Wave 2 Task 1 CDP 无头驱动架构）
#   - 归一 harness 与方法：w2-gamma 取证报告《Wave 2 Task 6（γ 层）两模式
#     DOCX 表格列宽分歧——取证与定调》（2026-09-30，md-editor 优化专项 0927）
#     的第五节工件清单与复跑口径——/tmp 取证 harness 的入库提升版
#   - 病灶背景：Qt toHtml 的 border="1" 呈现属性让 UA 给单元格画 1px 折叠
#     边框、每列 min/max 测量 +1px，TipTap 片段无边框——归一器测量基准
#     须统一清零（border:'0' 同清 td/th 与 table 两处）
#
# 用法:
#   scripts/probe-docx-table-parity.sh <md文件> <输出目录>
# 产出:
#   <输出目录>/wysiwyg-path.html   WYSIWYG 路归一器输入（getExportHtml）
#   <输出目录>/source-md.txt       源码路 textarea MD（getSourceContent）
#   <输出目录>/src-path.html       源码路归一器输入（mdconv tohtml）
#   <输出目录>/wysiwyg.docx / src.docx   两路导出产物
#   <输出目录>/tblgrid.txt         逐表逐列比对明细
#   /tmp/md-editor-claude-<日期>/w3t1-parity/<名称>-<时间戳>/
#   （运行目录：隔离 HOME、编辑器 stderr 日志、harness 构建目录）
#
# 环境要求（缺失时报错退出而非挂死）：
#   - build/src/md-editor 与 build/tests/mdconv 已构建
#   - xvfb-run（Xvfb）、node >= 22（内建 WebSocket 走 CDP）、python3 + Xlib
#     （XSetInputFocus + XTEST 合成 Ctrl+\ 真实快捷键路径）、cmake + Qt6
#     （WebEngineWidgets，harness 编译）
#
# 确定性说明：两路链路均为纯函数（无编辑无计时），同一文件多次运行的
# tblGrid 应逐列一致；harness 以隔离 HOME 运行（页宽走 QSettings 默认
# A4 竖版 1.8cm = 9865 缇，宿主导出设置不污染两路基准——w2-gamma 同款决策）。
# =============================================================================
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO_ROOT=$(cd "$SCRIPT_DIR/.." && pwd)
PROBE="$SCRIPT_DIR/docx-table-parity-probe.js"
HARNESS_SRC="$SCRIPT_DIR/docx-parity-harness.cpp"
BINARY="${MD_EDITOR_BINARY:-$REPO_ROOT/build/src/md-editor}"
MDCONV="${MD_EDITOR_MDCONV:-$REPO_ROOT/build/tests/mdconv}"

MD_FILE=""
OUT_DIR=""

usage() {
    sed -n '2,50p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
    exit "${1:-0}"
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        -h|--help) usage 0 ;;
        -*)       echo "未知参数: $1" >&2; usage 1 ;;
        *)
            if [[ -z "$MD_FILE" ]]; then MD_FILE="$1"
            elif [[ -z "$OUT_DIR" ]]; then OUT_DIR="$1"
            else echo "多余参数: $1" >&2; usage 1
            fi
            shift ;;
    esac
done
[[ -n "$MD_FILE" && -n "$OUT_DIR" ]] || usage 1
MD_FILE=$(readlink -f "$MD_FILE")
[[ -f "$MD_FILE" ]] || { echo "错误: 待测文档不存在: $MD_FILE" >&2; exit 1; }
DOC_DIR=$(dirname "$MD_FILE")

# ---- 环境预检（缺啥报啥，不挂死） ----
[[ -f "$PROBE" ]] || { echo "错误: 缺少探针脚本: $PROBE" >&2; exit 1; }
[[ -f "$HARNESS_SRC" ]] || { echo "错误: 缺少 harness 源码: $HARNESS_SRC" >&2; exit 1; }
[[ -x "$BINARY" ]] || { echo "错误: 编辑器二进制不存在或不可执行: $BINARY" >&2
    echo "  先构建: cmake --build build --target md-editor（或用 MD_EDITOR_BINARY 指定路径）" >&2; exit 1; }
[[ -x "$MDCONV" ]] || { echo "错误: mdconv 不存在或不可执行: $MDCONV" >&2
    echo "  先构建: cmake --build build --target mdconv（或用 MD_EDITOR_MDCONV 指定路径）" >&2; exit 1; }
command -v xvfb-run >/dev/null || { echo "错误: 缺少 xvfb-run（apt install xvfb）" >&2; exit 1; }
command -v node >/dev/null || { echo "错误: 缺少 node（需 >= 22，内建 WebSocket）" >&2; exit 1; }
node -e 'if (typeof WebSocket !== "function") process.exit(1)' \
    || { echo "错误: node 内建 WebSocket 不可用（需 Node >= 22）" >&2; exit 1; }
command -v python3 >/dev/null || { echo "错误: 缺少 python3" >&2; exit 1; }
python3 -c 'import Xlib' 2>/dev/null \
    || { echo "错误: 缺少 python3-xlib（apt install python3-xlib，XTEST 注入快捷键用）" >&2; exit 1; }
command -v cmake >/dev/null || { echo "错误: 缺少 cmake（harness 编译）" >&2; exit 1; }
command -v Xvfb >/dev/null || { echo "错误: 缺少 Xvfb（apt install xvfb）" >&2; exit 1; }

mkdir -p "$OUT_DIR"

# ---- 工作目录（/tmp 专属目录，避免多 harness 并行同名互写） ----
DAY=$(date +%Y%m%d)
BASE=$(basename "$MD_FILE" .md)
WORKDIR="/tmp/md-editor-claude-$DAY/w3t1-parity/${BASE}-$(date +%H%M%S)"
mkdir -p "$WORKDIR"
HARNESS_DIR="$WORKDIR/harness"
HARNESS_HOME="$WORKDIR/home"   # 隔离 HOME：页宽 QSettings 取默认 9865 缇
mkdir -p "$HARNESS_DIR" "$HARNESS_HOME"
echo "工作目录: $WORKDIR"
echo "待测文档: $MD_FILE"

# ---- 清理护栏：退出时兜底清掉本脚本拉起的编辑器（精确匹配本 worktree 二进制
#      绝对路径 + CDP 端口参数，不误杀主仓库/已安装版本；Xvfb 由 node 驱动经
#      进程组正常回收，不再 pkill Xvfb 以免误伤其他会话的显示号） ----
cleanup() {
    pkill -f -- "$BINARY --remote-debugging-port" 2>/dev/null || true
    wait 2>/dev/null || true
}
trap cleanup EXIT

# ---- Step 1: 无头采集两模式归一器输入（单次启动，文档状态同源） ----
node "$PROBE" \
    --file "$MD_FILE" \
    --out-export-html "$OUT_DIR/wysiwyg-path.html" \
    --out-source-md "$OUT_DIR/source-md.txt" \
    --workdir "$WORKDIR" \
    --binary "$BINARY"

# ---- Step 2: 源码路输入 = textarea MD 经 mdconv tohtml（与 ExportHtmlAsync
#      源码分支的 MarkdownHandler::toHtml 同一转换，产品代码零重写） ----
"$MDCONV" tohtml < "$OUT_DIR/source-md.txt" > "$OUT_DIR/src-path.html"

# ---- Step 3: 编译归一+导出 harness（链接当前工作区产品源文件） ----
cat > "$HARNESS_DIR/CMakeLists.txt" <<EOF
cmake_minimum_required(VERSION 3.16)
# 语言须含 C：miniz.c 是 C 源（只声明 CXX 会让 .c 不参与编译 → mz_* 链接缺失）
project(docx-parity-harness C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
# AUTORCC 必开：app.qrc 资源（table-column-layout.js/mermaid.min.js 等）靠它
# 编译注册，缺了则 readResource 返回空 → 归一脚本缺 __measureTableColumns
set(CMAKE_AUTORCC ON)
find_package(Qt6 REQUIRED COMPONENTS Gui Widgets WebEngineWidgets)
add_executable(docx-parity-harness "$HARNESS_SRC"
    "$REPO_ROOT/src/core/DocxExporter.cpp"
    "$REPO_ROOT/src/core/miniz/miniz.c"
    "$REPO_ROOT/src/core/MermaidRenderer.cpp"
    "$REPO_ROOT/src/core/MathMlRenderer.cpp"
    "$REPO_ROOT/src/core/MathMlConverter.cpp"
    "$REPO_ROOT/src/core/ExportCommon.cpp"
    "$REPO_ROOT/src/core/TableColumnNormalizer.cpp"
    "$REPO_ROOT/src/core/HtmlExporter.cpp"
    "$REPO_ROOT/src/resources/app.qrc")
target_include_directories(docx-parity-harness PRIVATE "$REPO_ROOT/src")
target_link_libraries(docx-parity-harness PRIVATE Qt6::Gui Qt6::Widgets Qt6::WebEngineWidgets)
EOF
cmake -S "$HARNESS_DIR" -B "$HARNESS_DIR/build" > "$WORKDIR/harness-cmake.log" 2>&1 \
    || { echo "错误: harness cmake 配置失败（见 $WORKDIR/harness-cmake.log）" >&2; exit 1; }
cmake --build "$HARNESS_DIR/build" -j"$(nproc)" > "$WORKDIR/harness-build.log" 2>&1 \
    || { echo "错误: harness 编译失败（见 $WORKDIR/harness-build.log）" >&2; exit 1; }
HARNESS_BIN="$HARNESS_DIR/build/docx-parity-harness"

# ---- Step 4: 两路各跑归一+导出（隔离 HOME + 无头；docDir 传 md 所在目录，
#      与 MainWindow 两模式导出同款相对图片解析基准） ----
run_harness() {
    local in="$1" out="$2"
    env -u WAYLAND_DISPLAY HOME="$HARNESS_HOME" \
        xvfb-run -a "$HARNESS_BIN" "$in" "$out" "$DOC_DIR" \
        > "$WORKDIR/harness-$(basename "$out" .docx).log" 2>&1
}
echo "[WYSIWYG] 归一+导出…"
run_harness "$OUT_DIR/wysiwyg-path.html" "$OUT_DIR/wysiwyg.docx"
echo "[源码]   归一+导出…"
run_harness "$OUT_DIR/src-path.html" "$OUT_DIR/src.docx"

# ---- Step 5: 解包两路 document.xml，tblGrid 逐表逐列比对 ----
#（set -e 下用 || 捕获退出码：比对失败要走自定义判词与退出码 1，不在 heredoc 处直接断）
PY_RC=0
python3 - "$OUT_DIR/wysiwyg.docx" "$OUT_DIR/src.docx" "$OUT_DIR/tblgrid.txt" <<'PYEOF' || PY_RC=$?
import re
import sys
import zipfile


def grids(path):
    with zipfile.ZipFile(path) as z:
        xml = z.read('word/document.xml').decode('utf-8')
    return [[int(w) for w in re.findall(r'<w:gridCol w:w="(\d+)"', g)]
            for g in re.findall(r'<w:tblGrid>(.*?)</w:tblGrid>', xml, re.S)]


wys, src, out_path = sys.argv[1], sys.argv[2], sys.argv[3]
a, b = grids(wys), grids(src)
lines = []
ok = True
if len(a) != len(b):
    ok = False
    lines.append(f'表数不一致: WYSIWYG={len(a)} 源码={len(b)}')
for i in range(min(len(a), len(b))):
    if len(a[i]) != len(b[i]):
        ok = False
        lines.append(f'表{i + 1}: 列数不一致 WYSIWYG={len(a[i])} 源码={len(b[i])}')
        continue
    diffs = [j for j in range(len(a[i])) if a[i][j] != b[i][j]]
    tag = '相等' if not diffs else f'差列 {",".join(str(j + 1) for j in diffs)}'
    lines.append(f'表{i + 1}（{len(a[i])} 列）: {tag}')
    lines.append(f'  WYSIWYG: {" ".join(map(str, a[i]))}')
    lines.append(f'  源码:    {" ".join(map(str, b[i]))}')
    if diffs:
        ok = False
report = '\n'.join(lines)
with open(out_path, 'w', encoding='utf-8') as f:
    f.write(report + '\n')
print(report)
sys.exit(0 if ok else 1)
PYEOF

if [[ "$PY_RC" -eq 0 ]]; then
    echo "结果: 两模式 DOCX tblGrid 逐列相等（测量基准对齐）"
else
    echo "结果: tblGrid 存在逐列差异（测量基准未对齐，明细见 $OUT_DIR/tblgrid.txt）" >&2
    exit 1
fi
