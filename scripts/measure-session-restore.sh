#!/usr/bin/env bash
# =============================================================================
# measure-session-restore.sh — 会话恢复启动耗时无头 A/B 测量（W4-2 延迟读盘）
#
# 指标（restoreSession 末尾的 session-restore 日志行为完成标志）：
#   sync  restoreSession 函数体内耗时——启动期阻塞 GUI 线程的同步恢复段
#         （读盘 + 编码检测 + 建 tab；W4-2 后非活动标签只存路径，激活才读）
#   wall  进程拉起 → 日志行出现（含进程/Qt/WebEngine 初始化，A/B 两边同量）
#
# 为什么以日志行而非常规「窗口出现」为完成标志：裸 Xvfb 无 DRI3/Present，
# Mesa loader_dri3_get_buffers 会死等 present 事件，主线程卡在首帧 flush、
# 窗口永不映射（show 本身没问题，是上屏死等）——窗口出现在这台机器上不可
# 作为标志；selftest 同理全程不 show。
#
# 用法:
#   scripts/measure-session-restore.sh [--tabs N] [--size KB] [--rounds R] [--bin 路径]
#   A/B：分别对两个构建各跑一次本脚本（例：改前 commit 构建于 build-a/），
#        比较 sync 中位数与 eager_reads（改前 = N，改后 = 1）
#
# 夹具约定：
#   - 文档默认 1900KB：贴近 2MB 软警告线以下（≥2MB 会话恢复会弹「文件较大」
#     确认框，无头模式下阻塞等输入）——单文件读盘成本最大化且不触弹窗
#   - HOME 隔离在 /tmp 工作目录（应用有会话恢复/最近文件等 QSettings 副作用）
set -euo pipefail

TABS=6; SIZE_KB=1900; ROUNDS=6
BIN="$(git rev-parse --show-toplevel)/build/src/md-editor"
while [ $# -gt 0 ]; do
  case "$1" in
    --tabs) TABS="$2"; shift 2 ;;
    --size) SIZE_KB="$2"; shift 2 ;;
    --rounds) ROUNDS="$2"; shift 2 ;;
    --bin) BIN="$2"; shift 2 ;;
    *) echo "未知参数: $1" >&2; exit 1 ;;
  esac
done

command -v Xvfb >/dev/null || { echo "错误: 缺少 Xvfb（apt install xvfb）" >&2; exit 1; }
[ -x "$BIN" ] || { echo "错误: 二进制不存在或不可执行: $BIN（先 cmake --build build）" >&2; exit 1; }

WORK="/tmp/md-editor-claude-$(date +%F)/measure-session-restore"
DOCS="$WORK/docs"; HOME_DIR="$WORK/home"
rm -rf "$WORK"; mkdir -p "$DOCS" "$HOME_DIR/.config/md-editor"

python3 - "$DOCS" "$TABS" "$SIZE_KB" <<'EOF'
import sys, os
docs, tabs, kb = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
block = ("## 小节 {i}\n\n第 {i} 段正文，包含 **加粗**、`行内代码`。\n\n"
         "- 列表项一\n- 列表项二\n\n```js\nconst v = {i};\n```\n\n")
paths = []
for f in range(1, tabs + 1):
    parts = [f"# 基准文档 {f}\n\n"]; size = len(parts[0].encode()); i = 0
    while size < kb * 1024:
        i += 1; p = block.format(i=i); parts.append(p); size += len(p.encode())
    path = os.path.join(docs, f"doc{f}.md")
    open(path, "w").write("".join(parts)); paths.append(path)
active = min(2, tabs - 1)   # 第 3 个为活动标签（tab 数不足时取末位）
with open(os.path.join(os.path.dirname(docs), "home/.config/md-editor/md-editor.conf"), "w") as cf:
    cf.write("[session]\nopenFiles=" + ", ".join(paths) + f"\nactiveTab={active}\nrestore=true\n")
EOF

# 跑一轮：等 session-restore 日志行（10s 兜底——正常 <2s；未出现视为失败）
run_once() {
  local disp=":$((96 + RANDOM % 800))"
  Xvfb "$disp" -screen 0 1280x800x24 >/dev/null 2>&1 &
  local xpid=$!
  sleep 0.3
  rm -f "$WORK/run.log"
  local start end line
  start=$(date +%s%N)
  HOME="$HOME_DIR" QT_QPA_PLATFORM=xcb "$BIN" >"$WORK/run.log" 2>&1 &
  local pid=$!
  line=""
  for _ in $(seq 1 2000); do
    line=$(grep -m1 "session-restore:" "$WORK/run.log" 2>/dev/null || true)
    [ -n "$line" ] && break
    kill -0 "$pid" 2>/dev/null || break
    sleep 0.005
  done
  end=$(date +%s%N)
  kill "$pid" 2>/dev/null || true
  sleep 0.2; kill "$xpid" 2>/dev/null || true
  wait "$pid" 2>/dev/null || true; wait "$xpid" 2>/dev/null || true
  if [ -z "$line" ]; then echo "FAIL -1 -1"; return; fi
  local sync reads
  sync=$(echo "$line" | grep -o 'sync=[0-9]*ms' | grep -o '[0-9]\+')
  reads=$(echo "$line" | grep -o 'eager_reads=[0-9]*' | grep -o '[0-9]\+')
  echo "$(( (end - start) / 1000000 )) ${sync:--1} ${reads:--1}"
}

echo "二进制: $BIN（tabs=$TABS size=${SIZE_KB}KB rounds=$ROUNDS）"
echo "warmup: $(run_once)"
vals=()
for r in $(seq 1 "$ROUNDS"); do
  out=$(run_once)
  echo "run$r: wall=$(echo "$out" | awk '{print $1}')ms sync=$(echo "$out" | awk '{print $2}')ms eager_reads=$(echo "$out" | awk '{print $3}')"
  vals+=("$(echo "$out" | awk '{print $2}')")
done
median=$(printf '%s\n' "${vals[@]}" | sort -n | awk '{a[NR]=$1} END {print a[int((NR+1)/2)]}')
echo "sync 中位数: ${median}ms（对照口径：改前 eager_reads=tab 数、改后=1；"
echo "单次读盘成本 ≈ (改前 sync − 改后 sync) / (tab 数 − 1)，随文件大小/冷缓存放大）"
