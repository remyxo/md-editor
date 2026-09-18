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

# 1.5 注入 Markdown 文档类型声明：LSItemContentTypes 指向系统 UTI
#     net.daringfireball.markdown——「打开方式」推荐列表按 UTI 匹配，只声明扩展名
#     会被灰掉（v0.16.0 及之前的现象，需切「全部应用」才能选）。Typora/MacDown/
#     Obsidian 均声明该系统 UTI。CFBundleTypeExtensions 保留作老系统
#     （macOS 11- 无内建 md UTI）的扩展名兜底。不再注入自定义
#     UTImportedTypeDeclarations（com.md-editor.markdown 与系统 UTI 争抢同一
#     扩展名，类型解析与图标绑定不稳定）。
#     icon 在 Contents/Resources/text-markdown.icns（src/CMakeLists.txt 打包）。
#     CFBundleTypeRole=Editor + LSHandlerRank=Owner：声明本应用为 md 默认处理者
#     （最终默认与否仍由用户「打开方式」决定，macOS 按此机制授权）。
PLIST="$APP/Contents/Info.plist"
PLBUDDY="/usr/libexec/PlistBuddy"
# 先删后加保证幂等（重复打包不报 key 已存在；Delete UTImportedTypeDeclarations
# 亦防御旧版产物残留）
"$PLBUDDY" -c "Delete :CFBundleDocumentTypes" "$PLIST" 2>/dev/null || true
"$PLBUDDY" -c "Delete :UTImportedTypeDeclarations" "$PLIST" 2>/dev/null || true
"$PLBUDDY" -c "Add :CFBundleDocumentTypes array" "$PLIST"
"$PLBUDDY" -c "Add :CFBundleDocumentTypes:0 dict" "$PLIST"
"$PLBUDDY" -c "Add :CFBundleDocumentTypes:0:CFBundleTypeName string 'Markdown 文档'" "$PLIST"
"$PLBUDDY" -c "Add :CFBundleDocumentTypes:0:CFBundleTypeRole string Editor" "$PLIST"
"$PLBUDDY" -c "Add :CFBundleDocumentTypes:0:LSHandlerRank string Owner" "$PLIST"
"$PLBUDDY" -c "Add :CFBundleDocumentTypes:0:LSItemContentTypes array" "$PLIST"
"$PLBUDDY" -c "Add :CFBundleDocumentTypes:0:LSItemContentTypes:0 string net.daringfireball.markdown" "$PLIST"
"$PLBUDDY" -c "Add :CFBundleDocumentTypes:0:CFBundleTypeExtensions array" "$PLIST"
"$PLBUDDY" -c "Add :CFBundleDocumentTypes:0:CFBundleTypeExtensions:0 string md" "$PLIST"
"$PLBUDDY" -c "Add :CFBundleDocumentTypes:0:CFBundleTypeExtensions:1 string markdown" "$PLIST"
"$PLBUDDY" -c "Add :CFBundleDocumentTypes:0:CFBundleTypeIconFile string text-markdown" "$PLIST"
# 断言注入成功（验 UTI 值而非仅 key 存在——数组为空或拼错也应阻断；管道任一环
# 非零退出时 pipefail + set -e 阻断打包）
"$PLBUDDY" -c "Print :CFBundleDocumentTypes:0:LSItemContentTypes" "$PLIST" | grep -q 'net\.daringfireball\.markdown'

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
