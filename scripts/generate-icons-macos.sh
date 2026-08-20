#!/bin/bash
# 从 SVG 生成 macOS 应用图标 .icns（CI 内使用：macos-latest）
# 依赖：librsvg（rsvg-convert，SVG→PNG）、iconutil（macOS 自带，PNG→icns）
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
SVG="$PROJECT_DIR/src/resources/icons/md-editor.svg"
OUT="$PROJECT_DIR/src/resources/icons/md-editor.icns"
ICONSET_DIR="$(mktemp -d)/md-editor.iconset"

mkdir -p "$ICONSET_DIR"

# 生成 iconset 所需各尺寸 PNG（16/32/128/256/512 及各自 @2x）
for size in 16 32 128 256 512; do
    double=$((size * 2))
    rsvg-convert -w "$size"   -h "$size"   "$SVG" -o "$ICONSET_DIR/icon_${size}x${size}.png"
    rsvg-convert -w "$double" -h "$double" "$SVG" -o "$ICONSET_DIR/icon_${size}x${size}@2x.png"
done

# 打包为 .icns
iconutil -c icns "$ICONSET_DIR" -o "$OUT"

echo "已生成: $OUT"
rm -rf "$(dirname "$ICONSET_DIR")"
