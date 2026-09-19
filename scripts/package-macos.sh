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

# 4. 生成「拖拽安装」式 dmg：staging（app + /Applications 符号链接 + 背景图）
#    → UDRW 挂载 → Finder AppleScript 布置窗口（app 左、Applications 右、
#    微渐变背景 + 小箭头 + 中文提示）→ 转压缩 UDZO。拖拽布局从 UX 上引导
#    用户把 app 拖入 Applications；用户仍双击图标直跑时，main.cpp 的
#    MacInstallCheck 硬阻断兜底（弹「请先拖曳…完成安装」后退出）——挂载卷
#    内直跑是 Launch Services 僵尸注册的标准来源（真机排查实录：.Trash 与
#    /Volumes 残留条目把推荐列表置灰）
DMG="$PROJECT_DIR/md-editor-$VERSION-macos-universal.dmg"
VOLNAME="md-editor"
STAGING="$PROJECT_DIR/dmg-staging"
RW_DMG="$PROJECT_DIR/md-editor-rw.dmg"
rm -rf "$STAGING" "$RW_DMG" "$DMG"
mkdir -p "$STAGING/.background"
cp -R "$APP" "$STAGING/md-editor.app"
ln -s /Applications "$STAGING/Applications"
# 背景图（@2x 144dpi，Finder 按 660×400 逻辑显示，含中文提示「拖动图标到
# Applications 以完成安装」）：产物已提交 scripts/dmg-background.png——CI 无
# PIL/中文字体，不现场生成；改设计本地跑 gen-dmg-background.py 重新生成并提交
BG_SRC="$SCRIPT_DIR/dmg-background.png"
[ -f "$BG_SRC" ] || { echo "错误: 缺少背景图 $BG_SRC" >&2; exit 1; }
cp "$BG_SRC" "$STAGING/.background/background.png"
hdiutil create -volname "$VOLNAME" -srcfolder "$STAGING" -ov -format UDRW "$RW_DMG" >/dev/null
MOUNT=$(hdiutil attach -readwrite -noverify -noautoopen "$RW_DMG" 2>/dev/null | sed -n 's/.*\(\/Volumes\/md-editor\).*/\1/p' | head -1)
[ -n "$MOUNT" ] || { echo "错误: UDRW 挂载失败" >&2; exit 1; }
# 窗口布置失败仅降级为默认视图（dmg 功能不受影响），不阻断打包。
# 视图选项须逐属性赋值——记录字面量 {icon size:…, arrangement: not arranged}
# 中枚举值不是合法记录成员，osascript 报 -2740 语法错（首轮 CI 实测）
style_window() {
    osascript <<APPLESCRIPT
tell application "Finder"
    tell disk "$VOLNAME"
        open
        delay 1
        set current view of container window to icon view
        set toolbar visible of container window to false
        set statusbar visible of container window to false
        set the bounds of container window to {0, 0, 660, 400}
        set theViewOptions to the icon view options of container window
        set arrangement of theViewOptions to not arranged
        set icon size of theViewOptions to 128
        set text size of theViewOptions to 12
        set background picture of theViewOptions to file ".background:background.png"
        repeat with f in (get every item of container window)
            set itemName to name of f
            if itemName begins with "md-editor" then
                set position of f to {185, 190}
                set extension hidden of f to true
            else if itemName is "Applications" then
                set position of f to {475, 190}
            end if
        end repeat
        close
        open
        update without registering applications
        delay 1
    end tell
end tell
APPLESCRIPT
}
style_window || { sleep 2; style_window; } \
    || echo "警告: Finder 窗口布置失败，dmg 保持默认视图" >&2
hdiutil detach "$MOUNT" >/dev/null 2>&1 || hdiutil detach -force "$MOUNT" >/dev/null 2>&1
hdiutil convert "$RW_DMG" -format UDZO -o "$DMG" >/dev/null
rm -rf "$STAGING" "$RW_DMG"

echo "已生成: $DMG"
