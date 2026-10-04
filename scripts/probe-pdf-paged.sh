#!/usr/bin/env bash
# PDF paged 路径回退取证探针（W3-3 定型，诊断工具，零产品改动）
#
# 用途：给 paged.js 分页「不限时」的环境复刻 PdfExporter 的渲染链路，取证
#   回退根因——产线 pollPrintState 的 30s 轮询超时会先于指纹校验触发，
#   「超时回退（静默）/指纹不一致/paged.js 内部异常」三种表象在产线无法
#   区分，本探针把它们拆开：
#   · 不设 30s 上限 → 看 paged.js 到底需要多久、最终 verify 结果
#   · 包装 __verifyContent → 还原 body 前抓 pre/post 指纹双侧原文
#   · 包装 preview() → 拿异常 e.stack（产线只回传 e.message）
#   · teardownSec → 第 N 秒复刻产线超时的 __restoreBody（拆 DOM 竞态实验）
#
# 用法: probe-pdf-paged.sh <HF_DUMP_HTML产物> <输出目录> [maxSec] [teardownSec]
#   渲染 HTML 抓取（输入）：
#     HF_DUMP_HTML=1 xvfb-run -a build-release/src/md-editor --selftest export \
#       --file X.md --outdir DIR --out DIR/x.json   # 跑完取 /tmp/hf-full.html
#   注意：nativeHf 档（无脚注文档）抓到的是 nativeHf 版 HTML，本探针只吃
#   paged 版（脚注文档、或 nativeHf 各防线回退后的第二次构建会覆盖同一文件）
#
# 依赖：qmake6 + Qt6WebEngineWidgets 开发包（同项目构建环境）、xvfb-run
# 产物（<输出目录>）：
#   timeline.txt 分页时间线（状态/页数逐拍）   measured.txt 测量遍回报
#   probeDump.json 指纹双侧原文+verify 结果    err.json preview 异常 stack
#   ppInfo.txt 表格预分页自述                   summary.txt 一行结论
set -euo pipefail

HTML="$(readlink -f "$1")"; OUT="$2"; MAX="${3:-600}"; TD="${4:-0}"
ROOT="$(git rev-parse --show-toplevel)"
WORK=/tmp/md-editor-probe-pdf-paged
mkdir -p "$WORK/res" "$OUT"

# 资源布置：渲染 HTML 以相对 src 引用 qrc:/web/ 下的库与字体，file: 基址等价替换
for f in paged.polyfill.js pdf-split-prelines.js katex.min.js mermaid.min.js; do
  cp "$ROOT/src/resources/web/$f" "$WORK/res/"
done
cp -r "$ROOT/src/resources/web/fonts" "$WORK/res/"
: > "$WORK/res/probe-base.html"

# 构建（源或二进制陈旧才重编）
if [ ! -x "$WORK/probe-paged" ] || [ "$ROOT/scripts/pdf-paged-probe.cpp" -nt "$WORK/probe-paged" ]; then
  cat > "$WORK/probe-paged.pro" <<'EOF'
QT += webenginewidgets core gui
CONFIG += console
CONFIG -= app_bundle
TARGET = probe-paged
SOURCES += pdf-paged-probe.cpp
EOF
  cp "$ROOT/scripts/pdf-paged-probe.cpp" "$WORK/"
  (cd "$WORK" && qmake6 probe-paged.pro >/dev/null && make -j4 >/dev/null)
fi

env -u WAYLAND_DISPLAY QT_QPA_PLATFORM=xcb QT_LOGGING_RULES="js.*=true" \
  xvfb-run -a "$WORK/probe-paged" "$HTML" "$WORK/res" "$OUT" "$MAX" "$TD" \
  2> "$OUT/probe.stderr"
echo "--- summary ---"; cat "$OUT/summary.txt" 2>/dev/null || true
echo "--- err ---"; cat "$OUT/err.json" 2>/dev/null || true; echo
echo "--- timeline(tail) ---"; tail -8 "$OUT/timeline.txt" 2>/dev/null || true
