#!/bin/bash
# 从 SVG 生成 macOS 图标 .icns（CI 内使用：macos-latest）
# 依赖：librsvg（rsvg-convert，SVG→PNG）、iconutil（macOS 自带，PNG→icns）
# 产出：md-editor.icns（应用图标）+ text-markdown.icns（.md 文档类型图标，
#       Info.plist 由 package-macos.sh 注入 CFBundleDocumentTypes 引用）
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

gen_icns() {
    local svg="$1" out="$2"
    local iconset_dir
    # iconutil 要求 iconset 目录以 .iconset 结尾，否则报 "Invalid Iconset"
    iconset_dir="$(mktemp -d)/icon.iconset"
    mkdir -p "$iconset_dir"

    # 生成 iconset 所需各尺寸 PNG（16/32/128/256/512 及各自 @2x）
    for size in 16 32 128 256 512; do
        double=$((size * 2))
        rsvg-convert -w "$size"   -h "$size"   "$svg" -o "$iconset_dir/icon_${size}x${size}.png"
        rsvg-convert -w "$double" -h "$double" "$svg" -o "$iconset_dir/icon_${size}x${size}@2x.png"
    done

    # 打包为 .icns
    iconutil -c icns "$iconset_dir" -o "$out"

    echo "已生成: $out"
    rm -rf "$iconset_dir"
}

gen_icns "$PROJECT_DIR/src/resources/icons/md-editor.svg" "$PROJECT_DIR/src/resources/icons/md-editor.icns"
gen_icns "$PROJECT_DIR/src/resources/icons/text-markdown.svg" "$PROJECT_DIR/src/resources/icons/text-markdown.icns"
