#!/bin/bash
# 本地构建 deb 包
set -e

echo "=== 构建 md-editor deb 包 ==="

BUILD_DIR="build-deb"
rm -rf "$BUILD_DIR"
mkdir "$BUILD_DIR"
cd "$BUILD_DIR"

cmake .. \
    -DCMAKE_PREFIX_PATH=/home/daijd/qt6-local/usr \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_TESTS=OFF

make -j$(nproc)

echo "=== 打包 ==="
cpack -G DEB

echo "=== 完成 ==="
ls -lh *.deb
