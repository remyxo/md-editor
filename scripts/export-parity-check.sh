#!/usr/bin/env bash
# 跨格式导出对账：无头导出三格式 → 提取特征 → golden 对账
# 用法: export-parity-check.sh <md文件> <工作目录> [golden.json|--update]
set -euo pipefail
MD="$(readlink -f "$1")"; WORK="$2"; ARGS=("${@:3}")
BIN="$(git rev-parse --show-toplevel)/build/src/md-editor"
mkdir -p "$WORK"
# 注：golden 路径与 --update 分开收集——原稿把 --update 混进 GOLDEN_ARG 数组，
# 单独传 --update 时 ${GOLDEN_ARG[0]} 会取到 "--update" 当路径用（写出名为
# --update 的文件），对账 golden 指向错误
GOLDEN=""; UPDATE=""
for a in "${ARGS[@]:-}"; do
  if [[ "$a" == "--update" ]]; then UPDATE="--update"; else GOLDEN="$a"; fi
done
GOLDEN="${GOLDEN:-$WORK/golden.json}"
env -u WAYLAND_DISPLAY QT_QPA_PLATFORM=xcb xvfb-run -a "$BIN" \
  --selftest export --file "$MD" --outdir "$WORK" --out "$WORK/export.json"
# 传 basename 让特征提取按 <base>.<ext> 精确取本 md 的产物——工作目录有
# 先前其他 md 的残留文件时不参与（selftest 只清当前 base 的三个产物）
python3 "$(git rev-parse --show-toplevel)/scripts/export_parity_features.py" \
  "$WORK" "$(basename "$MD" .md)"
python3 "$(git rev-parse --show-toplevel)/scripts/export_parity_compare.py" \
  "$WORK/features.json" "$GOLDEN" $UPDATE
