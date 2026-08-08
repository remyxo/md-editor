#!/usr/bin/env bash
# 构建 mermaid.min.js（mermaid 单一 bundle，挂到 window.mermaid）。
#
# 背景：mermaid 官方 dist/mermaid.min.js 的全局包装器存在缺陷
# （详见 mermaid-entry.js 头部说明），无法直接以 <script> 加载。
# 因此使用与 tiptap-all.js 相同的 esbuild 工具链，自行打包为自包含 IIFE。
#
# 前置条件：在 node_modules 所在目录执行过
#   npm install mermaid esbuild
# esbuild 按入口文件所在目录解析 node_modules，因此本脚本会把入口复制到
# node_modules 所在目录后构建。若未设置 NODE_MODULES_PATH，则使用当前目录。
#
# 用法：
#   bash src/resources/web/build-mermaid.sh
# 或指定依赖目录：
#   NODE_MODULES_PATH=/path/to/dir bash src/resources/web/build-mermaid.sh
set -euo pipefail

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

if [[ -n "${NODE_MODULES_PATH:-}" ]]; then
    WORKDIR="$NODE_MODULES_PATH"
else
    WORKDIR="$(pwd)"
fi

if [[ ! -d "$WORKDIR/node_modules" ]]; then
    echo "错误：在 $WORKDIR 下未找到 node_modules，请先执行 npm install mermaid esbuild。" >&2
    exit 1
fi

cp "$DIR/mermaid-entry.js" "$WORKDIR/.mermaid-entry-build.js"
trap 'rm -f "$WORKDIR/.mermaid-entry-build.js"' EXIT

cd "$WORKDIR"
npx esbuild .mermaid-entry-build.js \
    --bundle \
    --format=iife \
    --target=es2017 \
    --minify \
    --outfile="$DIR/mermaid.min.js"

echo "已生成 $DIR/mermaid.min.js ($(wc -c < "$DIR/mermaid.min.js") bytes)"
