#!/usr/bin/env bash
# 构建 tiptap-all.js（TipTap + ProseMirror + lowlight 单一 bundle）。
#
# 背景：TipTap v2 的官方 UMD 构建依赖 ProseMirror 全局对象（window.state 等），
# 且 StarterKit 内部又依赖 ~19 个子扩展 UMD，无法通过经典 <script> 逐个加载。
# 因此使用 esbuild 打包为单个自包含文件，供 editor.html 直接加载。
#
# 前置条件：在某个目录中执行过
#   npm install @tiptap/core@2.6.0 @tiptap/starter-kit@2.6.0 @tiptap/pm@2.6.0 \
#       <StarterKit 依赖的全部子扩展 @2.6.0，见下方 PKGS 列表> lowlight@3.1.0 highlight.js@11.9.0 esbuild
# esbuild 按入口文件所在目录解析 node_modules，因此本脚本会把入口复制到
# node_modules 所在目录后构建。若未设置 NODE_MODULES_PATH，则使用当前目录。
#
# 用法：
#   bash src/resources/web/build-tiptap.sh
# 或指定依赖目录：
#   NODE_MODULES_PATH=/path/to/dir bash src/resources/web/build-tiptap.sh
set -euo pipefail

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

if [[ -n "${NODE_MODULES_PATH:-}" ]]; then
    WORKDIR="$NODE_MODULES_PATH"
else
    WORKDIR="$(pwd)"
fi

if [[ ! -d "$WORKDIR/node_modules" ]]; then
    echo "错误：在 $WORKDIR 下未找到 node_modules，请先执行 npm install（见脚本头部说明）。" >&2
    exit 1
fi

cp "$DIR/tiptap-entry.js" "$WORKDIR/.entry-build.js"
trap 'rm -f "$WORKDIR/.entry-build.js"' EXIT

cd "$WORKDIR"
npx esbuild .entry-build.js \
    --bundle \
    --format=iife \
    --target=es2017 \
    --minify \
    --outfile="$DIR/tiptap-all.js"

echo "已生成 $DIR/tiptap-all.js ($(wc -c < "$DIR/tiptap-all.js") bytes)"
