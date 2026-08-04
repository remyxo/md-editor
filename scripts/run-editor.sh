#!/bin/bash
export LD_LIBRARY_PATH=/home/daijd/qt6-local/usr/lib/x86_64-linux-gnu
export QTWEBENGINEPROCESS_PATH=/home/daijd/qt6-local/usr/lib/qt6/libexec/QtWebEngineProcess
export QTWEBENGINE_RESOURCES_PATH=/home/daijd/qt6-local/usr/share/qt6/resources
export QTWEBENGINE_LOCALES_PATH=/home/daijd/qt6-local/usr/share/qt6/translations/qtwebengine_locales
exec "$(dirname "$0")/../build/src/md-editor" "$@"
