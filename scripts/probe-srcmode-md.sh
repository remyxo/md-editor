#!/usr/bin/env bash
# =============================================================================
# probe-srcmode-md.sh — 源码模式 textarea MD 再生取证驱动（α 层诊断工具）
#
# 骨架来源：scripts/measure-mode-switch.sh + scripts/mode-switch-probe.js
# （Wave 2 Task 1 无头驱动架构）——抽取其中「应用启动 / 等加载稳定 /
# XTEST 合成 Ctrl+\ 切源码 / CDP evaluate 采集」四段；测量轮次、页面探针注入
# （monkey-patch editorApi）与空闲闸门均裁掉——本工具不做计时，只做取值。
#
# 用途（零产品代码改动的诊断工具）：
#   切源码时 C++ 侧 MarkdownHandler::fromHtml(TipTap getHTML) 再生出 textarea MD；
#   本探针把该 MD 全文原样落盘，供宿主侧与文件原 MD 做 diff——α 层
#   （源码模式导出字面量污染）的退化形态直接取证。取证协议：
#     开文件 → 等加载稳定 → 合成 Ctrl+\ 切源码 → 等切入完成
#     → Runtime.evaluate('window.editorApi.getSourceContent()')
#     → 落盘 <输出目录>/source-md.txt → 宿主侧 diff 文件原 MD 与 source-md.txt
#
# 用法:
#   scripts/probe-srcmode-md.sh <md文件> <输出目录>
# 产出:
#   <输出目录>/source-md.txt    textarea 实际 MD（getSourceContent 原样）
#   <输出目录>/source-md.diff   文件原 MD vs source-md.txt 的 unified diff
#   /tmp/md-editor-claude-<日期>/w2-alpha/<名称>-<时间戳>/
#   （运行目录：隔离 HOME 与编辑器 stderr 日志，排查失败用）
#
# 环境要求（缺失时报错退出而非挂死）：
#   - build/src/md-editor 已构建（cmake --build build --target md-editor）
#   - xvfb-run（Xvfb）、node >= 22（内建 WebSocket 走 CDP）、python3 + Xlib
#     （XSetInputFocus + XTEST 合成 Ctrl+\ 真实快捷键路径）
#
# 确定性说明：取证链路为纯函数（load→toHtml→TipTap→getHTML→fromHtml），
# 无编辑无计时，同一文件多次运行的 source-md.txt 应逐字节一致——
# 复跑对比（cmp）即确定性证据。
# =============================================================================
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO_ROOT=$(cd "$SCRIPT_DIR/.." && pwd)
PROBE="$SCRIPT_DIR/srcmode-md-probe.js"
BINARY="${MD_EDITOR_BINARY:-$REPO_ROOT/build/src/md-editor}"

MD_FILE=""
OUT_DIR=""

usage() {
    sed -n '2,34p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
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

# ---- 环境预检（缺啥报啥，不挂死） ----
[[ -f "$PROBE" ]] || { echo "错误: 缺少探针脚本: $PROBE" >&2; exit 1; }
[[ -x "$BINARY" ]] || { echo "错误: 编辑器二进制不存在或不可执行: $BINARY" >&2
    echo "  先构建: cmake --build build --target md-editor（或用 MD_EDITOR_BINARY 指定路径）" >&2; exit 1; }
command -v xvfb-run >/dev/null || { echo "错误: 缺少 xvfb-run（apt install xvfb）" >&2; exit 1; }
command -v node >/dev/null || { echo "错误: 缺少 node（需 >= 22，内建 WebSocket）" >&2; exit 1; }
command -v python3 >/dev/null || { echo "错误: 缺少 python3" >&2; exit 1; }
node -e 'if (typeof WebSocket !== "function") process.exit(1)' \
    || { echo "错误: node 内建 WebSocket 不可用（需 Node >= 22）" >&2; exit 1; }
python3 -c 'import Xlib' 2>/dev/null \
    || { echo "错误: 缺少 python3-xlib（apt install python3-xlib，XTEST 注入快捷键用）" >&2; exit 1; }
command -v Xvfb >/dev/null || { echo "错误: 缺少 Xvfb（apt install xvfb）" >&2; exit 1; }

mkdir -p "$OUT_DIR"

# ---- 工作目录（/tmp 专属目录，避免多 harness 并行同名互写） ----
DAY=$(date +%Y%m%d)
BASE=$(basename "$MD_FILE" .md)
WORKDIR="/tmp/md-editor-claude-$DAY/w2-alpha/${BASE}-$(date +%H%M%S)"
mkdir -p "$WORKDIR"
echo "工作目录: $WORKDIR"
echo "待取证文档: $MD_FILE"

# ---- 清理护栏：退出时兜底清掉本脚本拉起的编辑器（精确匹配本 worktree 二进制
#      绝对路径 + CDP 端口参数，不误杀主仓库/已安装版本；Xvfb 由 node 驱动经
#      进程组正常回收，此处不再 pkill Xvfb 以免误伤其他会话的显示号） ----
cleanup() {
    pkill -f -- "$BINARY --remote-debugging-port" 2>/dev/null || true
    wait 2>/dev/null || true
}
trap cleanup EXIT

# ---- 单次取证（全新应用实例 + 隔离 HOME；无测量轮次） ----
node "$PROBE" \
    --file "$MD_FILE" \
    --out "$OUT_DIR/source-md.txt" \
    --workdir "$WORKDIR" \
    --binary "$BINARY"

# ---- 宿主侧 diff：文件原 MD vs textarea 再生 MD（diff 有差异返回 1，不算失败） ----
diff -u --label "文件原MD:$MD_FILE" --label "textarea再生MD:$OUT_DIR/source-md.txt" \
    "$MD_FILE" "$OUT_DIR/source-md.txt" > "$OUT_DIR/source-md.diff" || true

ORIG_LINES=$(wc -l < "$MD_FILE")
REGEN_LINES=$(wc -l < "$OUT_DIR/source-md.txt")
DIFF_LINES=$(wc -l < "$OUT_DIR/source-md.diff")
echo "文件原MD: $ORIG_LINES 行 · textarea再生MD: $REGEN_LINES 行 · diff: $DIFF_LINES 行"
if [[ "$DIFF_LINES" -le 0 ]]; then
    echo "结果: 再生 MD 与文件原 MD 逐字节一致（无退化）"
else
    echo "结果: 再生 MD 存在差异（diff 全文见 $OUT_DIR/source-md.diff）"
fi
