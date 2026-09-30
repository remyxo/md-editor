#!/usr/bin/env bash
# =============================================================================
# measure-mode-switch.sh — 模式切换三指标无头测量驱动
#
# 指标（字段名被 Wave 2 Task 3/4/5 作为 A/B 判据引用，不得改名）：
#   switch_ms       切入源码 + 切出回 WYSIWYG 一轮往返耗时（不含源码停留）
#   viewport_hl_ms  源码首帧 → 视口内全部代码块 .hljs-* 高亮完成的时延
#                   （探针以「空闲闸门」挂起高亮批次至切入武装时刻，保证切入时
#                   高亮尚在途——本机 4k 加载段高亮 ~2s 跑完，不闸则恒 ~0）
#   wrap_cache_hit  第 3 次切入源码时刻折行缓存命中（0/1，1=命中；探针优先消费
#                   editor.js 的 window.__wrapCacheState()，Task 4 起跨会话缓存
#                   保留，未编辑往返应为 1；4k 折行测量 >10min，翻面以 guide 为准）
#
# 用法:
#   scripts/measure-mode-switch.sh <md文件> <输出目录> [--rounds N] [--query 查询词]
# 产出:
#   <输出目录>/switch-metrics.json（N 轮中位数 + 逐轮样本 + 测量协议说明）
#   /tmp/md-editor-claude-<日期>/measure-mode-switch/<名称>-<时间戳>/round-*.json
#   （逐轮明细与编辑器 stderr 日志，排查失败用）
#
# 环境要求（缺失时报错退出而非挂死）：
#   - build/src/md-editor 已构建（cmake --build build --target md-editor）
#   - xvfb-run（Xvfb）、node >= 22（内建 WebSocket 走 CDP）、python3 + Xlib
#     （XSetInputFocus + XTEST 合成 Ctrl+\ 真实快捷键路径）
#
# 测量协议要点（详见 mode-switch-probe.js 文件头与产出 JSON 的 protocol 段）：
#   - 每轮全新应用实例 + 隔离 HOME（应用有会话恢复；QWebEngine 档案/字体缓存
#     轮间一致），Ctrl+\ 经 XTEST 注入走 MainWindow::onToggleSourceMode 真实编排
#   - 切入前光标置文档末尾（可视区锚定文档尾部代码块，最大化「视口优先高亮」
#     优化的 A/B 信号——文档顺序的空闲高亮批次最后才轮到尾部块）
#   - 加载后主动消费 warmup()（应用 1.5s 后的冷 JIT 预热不落入测量窗口）
# =============================================================================
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO_ROOT=$(cd "$SCRIPT_DIR/.." && pwd)
PROBE="$SCRIPT_DIR/mode-switch-probe.js"
BINARY="${MD_EDITOR_BINARY:-$REPO_ROOT/build/src/md-editor}"

MD_FILE=""
OUT_DIR=""
ROUNDS=5
QUERY=""

usage() {
    sed -n '2,36p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
    exit "${1:-0}"
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        -h|--help) usage 0 ;;
        --rounds) ROUNDS="$2"; shift 2 ;;
        --query)  QUERY="$2"; shift 2 ;;
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
[[ "$ROUNDS" -ge 1 ]] || { echo "错误: --rounds 须 >= 1" >&2; exit 1; }
# 默认搜索词：bench 生成文档每节标题都含「章节」；其他文档（如 markdown-guide.md）
# 建议 --query 显式传入一个確定有匹配的词（无匹配则折行指标记 -1）
[[ -n "$QUERY" ]] || QUERY="章节"

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
WORKDIR="/tmp/md-editor-claude-$DAY/measure-mode-switch/${BASE}-$(date +%H%M%S)"
mkdir -p "$WORKDIR"
echo "工作目录: $WORKDIR"
echo "待测文档: $MD_FILE （轮数=$ROUNDS 搜索词=$QUERY）"

# ---- 清理护栏：退出时兜底清掉本脚本拉起的编辑器（精确匹配本 worktree 二进制
#      绝对路径 + CDP 端口参数，不误杀主仓库/已安装版本；Xvfb 由 node 驱动经
#      进程组正常回收，此处不再 pkill Xvfb 以免误伤其他会话的显示号） ----
cleanup() {
    pkill -f -- "$BINARY --remote-debugging-port" 2>/dev/null || true
    wait 2>/dev/null || true
}
trap cleanup EXIT

# ---- 逐轮测量（每轮全新应用实例：冷档/JIT/高亮缓存状态轮间可比） ----
for i in $(seq 1 "$ROUNDS"); do
    echo "--- 第 $i/$ROUNDS 轮 ---"
    if ! node "$PROBE" --drive \
            --file "$MD_FILE" \
            --out "$WORKDIR/round-$i.json" \
            --round "$i" \
            --query "$QUERY" \
            --binary "$BINARY"; then
        echo "错误: 第 $i 轮失败（逐轮目录含 editor-stderr.log: $WORKDIR/round-$i/）" >&2
        exit 1
    fi
done

# ---- 汇总中位数并落盘 ----
node "$PROBE" --aggregate \
    --workdir "$WORKDIR" \
    --rounds "$ROUNDS" \
    --file "$MD_FILE" \
    --query "$QUERY" \
    --out "$OUT_DIR/switch-metrics.json"
