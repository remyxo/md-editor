#!/bin/bash
# macOS 通用二进制打包：macdeployqt → 重新签名 → dmg
# 前置条件：已用 universal Qt (clang_64) 构建出 build/src/md-editor.app（同时含 arm64 + x86_64）
# 环境变量（由 workflow 注入）：
#   QT_PREFIX —— Qt 安装前缀（含 bin/macdeployqt）
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

APP="$PROJECT_DIR/build/src/md-editor.app"

# 1. macdeployqt（把 universal 的 Qt framework/插件/资源拷进 .app）
if [ -n "${QT_PREFIX:-}" ]; then
    "$QT_PREFIX/bin/macdeployqt" "$APP" -always-overwrite
else
    macdeployqt "$APP" -always-overwrite
fi

# 2. 重新 ad-hoc 签名（未公证，用户首次需「右键→打开」）
codesign --force --deep --sign - "$APP"

# 3. 版本号从 CMakeLists.txt 读取
VERSION="$(sed -n 's/.*project(md-editor VERSION \([0-9.]*\).*/\1/p' "$PROJECT_DIR/CMakeLists.txt")"
[ -n "$VERSION" ] || VERSION="0.0.0"

# 4. 生成 dmg
DMG="$PROJECT_DIR/md-editor-$VERSION-macos-universal.dmg"
rm -f "$DMG"
hdiutil create -volname "md-editor" -srcfolder "$APP" -ov -format UDZO "$DMG"

echo "已生成: $DMG"
