#!/bin/bash
# macOS 通用二进制打包：lipo 合并 arm64 + x86_64 → macdeployqt → 重新签名 → dmg
# 前置条件：已分别用 arm64 与 x86_64 的 Qt 构建出
#   build-arm64/src/md-editor.app 与 build-x86_64/src/md-editor.app
# 环境变量（由 workflow 注入）：
#   ARM64_PREFIX / X86_PREFIX —— 各自 Qt 的安装前缀（含 bin/macdeployqt）
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

ARM_APP="$PROJECT_DIR/build-arm64/src/md-editor.app"
X86_APP="$PROJECT_DIR/build-x86_64/src/md-editor.app"
UNIVERSAL_APP="$PROJECT_DIR/build/src/md-editor.app"

# 1. 各自 macdeployqt（把各自架构的 Qt framework/插件/资源拷进 .app）
if [ -n "${ARM64_PREFIX:-}" ]; then
    "$ARM64_PREFIX/bin/macdeployqt" "$ARM_APP" -always-overwrite
else
    macdeployqt "$ARM_APP" -always-overwrite
fi
if [ -n "${X86_PREFIX:-}" ]; then
    "$X86_PREFIX/bin/macdeployqt" "$X86_APP" -always-overwrite
else
    macdeployqt "$X86_APP" -always-overwrite
fi

# 2. 以 arm64 版本为基础，lipo 合并所有 Mach-O 二进制（主程序 + framework + 插件 + WebEngine 辅助进程）
rm -rf "$UNIVERSAL_APP"
cp -R "$ARM_APP" "$UNIVERSAL_APP"

while IFS= read -r -d '' rel; do
    a="$ARM_APP/$rel"
    b="$X86_APP/$rel"
    out="$UNIVERSAL_APP/$rel"
    [ -L "$a" ] && continue       # 跳过符号链接（Versions/Current 等）
    [ -f "$b" ] || continue       # 另一架构不存在则跳过
    if file -b "$a" | grep -q 'Mach-O'; then
        lipo -create "$a" "$b" -output "$out"
    fi
done < <(cd "$ARM_APP" && find . -type f -print0)

# 3. lipo 合并会使原签名失效，重新 ad-hoc 签名（未公证，用户首次需「右键→打开」）
codesign --force --deep --sign - "$UNIVERSAL_APP"

# 4. 版本号从 CMakeLists.txt 读取
VERSION="$(sed -n 's/.*project(md-editor VERSION \([0-9.]*\)).*/\1/p' "$PROJECT_DIR/CMakeLists.txt")"
[ -n "$VERSION" ] || VERSION="0.0.0"

# 5. 生成 dmg
DMG="$PROJECT_DIR/md-editor-$VERSION-macos-universal.dmg"
rm -f "$DMG"
hdiutil create -volname "md-editor" -srcfolder "$UNIVERSAL_APP" -ov -format UDZO "$DMG"

echo "已生成: $DMG"
