#!/bin/bash
PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
# 静默 Qt WebEngine 6.10+ 的无害噪音日志（Skia Graphite 后端探测失败回退 Ganesh，
# 各平台通用，Chromium 子进程级别 --disable-logging 压不住，需 --log-level=3）
export QTWEBENGINE_CHROMIUM_FLAGS="--log-level=3 ${QTWEBENGINE_CHROMIUM_FLAGS:-}"
# macOS 打 .app bundle，可执行文件在 Contents/MacOS 下
if [ -f "$PROJECT_DIR/build/src/md-editor.app/Contents/MacOS/md-editor" ]; then
    exec "$PROJECT_DIR/build/src/md-editor.app/Contents/MacOS/md-editor" "$@"
fi
exec "$PROJECT_DIR/build/src/md-editor" "$@"
