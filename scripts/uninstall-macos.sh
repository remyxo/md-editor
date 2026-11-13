#!/bin/bash
# macOS 卸载：注销 Launch Services 注册 → 退出实例 → 移入废纸篓
# 关联清理说明：CFBundleDocumentTypes 由 package-macos.sh 注入（LSHandlerRank=Owner），
# lsregister -u 注销后系统不再列出本应用；.md 默认打开权自动回退给其余声明了
# 该 UTI（net.daringfireball.markdown）的应用，无需也无法指定「回退到谁」。
# 用户数据（会话恢复/最近文件等，~/Library/Application Support/md-editor）
# 默认保留，如需一并清除加 --purge。
set -euo pipefail

APP="/Applications/md-editor.app"
PURGE=0
[ "${1:-}" = "--purge" ] && PURGE=1

if [ ! -d "${APP}" ]; then
    echo "未找到 ${APP}（可能已卸载）" >&2
    exit 1
fi

# 1. 注销 Launch Services 注册（先注销再删除，避免残留打开方式条目）
LSREGISTER="/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister"
"$LSREGISTER" -u "${APP}" && echo "✅ 已注销 Launch Services 注册"

# 2. 退出运行中的实例
osascript -e 'quit app "md-editor"' 2>/dev/null || true
sleep 1
pgrep -x md-editor >/dev/null && { pkill -x md-editor; echo "⚠️ 实例未响应，已强制结束"; }

# 3. 移入废纸篓（可后悔；rm -rf 请自行斟酌）
mv "${APP}" ~/.Trash/ && echo "✅ ${APP} 已移入废纸篓"

# 4. 可选：清除用户数据
if [ "$PURGE" -eq 1 ]; then
    rm -rf ~/Library/"Application Support"/md-editor && echo "✅ 用户数据已清除"
fi

echo "完成：.md 文件关联已交由系统重新选择默认应用"
