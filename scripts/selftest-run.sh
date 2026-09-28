#!/usr/bin/env bash
# selftest 统一入口：Xvfb 无头跑 perf，结果收集到指定目录
# 用法: selftest-run.sh <md文件> <结果目录>
set -euo pipefail
MD="$(readlink -f "$1")"; OUT="$2"
BIN="$(git rev-parse --show-toplevel)/build-release/src/md-editor"
mkdir -p "$OUT"
env -u WAYLAND_DISPLAY QT_QPA_PLATFORM=xcb xvfb-run -a "$BIN" \
  --selftest perf --file "$MD" --out "$OUT/perf-$(basename "$MD" .md).json"
