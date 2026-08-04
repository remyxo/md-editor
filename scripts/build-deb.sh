#!/bin/bash
# 本地构建 deb 包
set -e

PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$PROJECT_DIR/build-deb"

echo "=== 构建 md-editor deb 包 ==="

rm -rf "$BUILD_DIR"
mkdir "$BUILD_DIR"
cd "$BUILD_DIR"

cmake "$PROJECT_DIR" \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_TESTS=OFF

make -j$(nproc)

echo "=== 打包 ==="
cpack -G DEB

echo "=== 完成 ==="
ls -lh *.deb
