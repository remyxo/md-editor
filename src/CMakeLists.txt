set(SOURCES
    main.cpp
    app/Application.cpp
    app/LanguageManager.cpp
    app/SystemThemeWatcher.cpp
    app/ThemeManager.cpp
    core/EditorBridge.cpp
    core/FileManager.cpp
    core/ImageManager.cpp
    core/LocalImageSchemeHandler.cpp
    core/MarkdownHandler.cpp
    core/PdfExporter.cpp
    core/DocxExporter.cpp
    core/miniz/miniz.c
    core/MermaidRenderer.cpp
    core/MathMlRenderer.cpp
    core/MathMlConverter.cpp
    core/HtmlExporter.cpp
    ui/EditorTab.cpp
    ui/TabWidget.cpp
    ui/ToolBar.cpp
    ui/MenuBar.cpp
    ui/FileTree.cpp
    ui/OutlineTree.cpp
    ui/SideBar.cpp
    ui/CollapsibleSection.cpp
    ui/StatusBar.cpp
    ui/SearchBar.cpp
    ui/SettingsDialog.cpp
    ui/TableGridPopup.cpp
    ui/AboutDialog.cpp
    ui/MainWindow.cpp
)

set(HEADERS
    app/Application.h
    app/LanguageManager.h
    app/SystemThemeWatcher.h
    app/ThemeManager.h
    core/EditorBridge.h
    core/FileManager.h
    core/ImageManager.h
    core/LocalImageSchemeHandler.h
    core/MarkdownHandler.h
    core/PdfExporter.h
    core/DocxExporter.h
    core/MermaidRenderer.h
    core/MathMlRenderer.h
    core/MathMlConverter.h
    core/HtmlExporter.h
    ui/EditorTab.h
    ui/TabWidget.h
    ui/ToolBar.h
    ui/MenuBar.h
    ui/FileTree.h
    ui/OutlineTree.h
    ui/SideBar.h
    ui/CollapsibleSection.h
    ui/StatusBar.h
    ui/SearchBar.h
    ui/SettingsDialog.h
    ui/TableGridPopup.h
    ui/AboutDialog.h
    ui/MainWindow.h
)

qt_add_resources(RESOURCES resources/app.qrc)

if(APPLE)
    # macOS 打包为 .app bundle（供 macdeployqt / dmg 使用）
    qt_add_executable(${PROJECT_NAME} MACOSX_BUNDLE ${SOURCES} ${HEADERS} ${RESOURCES})
    set_target_properties(${PROJECT_NAME} PROPERTIES
        MACOSX_BUNDLE_BUNDLE_NAME "md-editor"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.md-editor.app"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    )
    # 应用图标（.icns 由 CI 的 generate-icons-macos.sh 生成；不存在则用系统默认图标）
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/md-editor.icns")
        set_target_properties(${PROJECT_NAME} PROPERTIES
            MACOSX_BUNDLE_ICON_FILE "md-editor.icns")
        set_source_files_properties(
            "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/md-editor.icns"
            PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
        target_sources(${PROJECT_NAME} PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/md-editor.icns")
    endif()
    # 文档类型图标（.md/.markdown 的 Finder 图标，区别于应用图标；.icns 由 CI 的
    # generate-icons-macos.sh 生成，Info.plist 的 CFBundleDocumentTypes 由
    # package-macos.sh 用 PlistBuddy 注入引用）
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/text-markdown.icns")
        set_source_files_properties(
            "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/text-markdown.icns"
            PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
        target_sources(${PROJECT_NAME} PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/text-markdown.icns")
    endif()
else()
    # WIN32：链接为 GUI 子系统 exe（WinMain 入口由 Qt6 入口点库自动提供，main() 不用改）。
    # 缺此标志是控制台子系统——Windows 双击启动会附带一个黑色空控制台窗口。
    # 该属性仅 Windows 生效，Linux 构建不受影响；package-windows.ps1 有 PE 子系统断言防回归
    add_executable(${PROJECT_NAME} WIN32 ${SOURCES} ${HEADERS} ${RESOURCES})
    # Windows 程序图标（.ico 由 CI 的 generate-icons-win.ps1 生成；.rc 引用它）
    if(WIN32 AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/md-editor.ico")
        target_sources(${PROJECT_NAME} PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/resources/md-editor.rc")
        # CMake/Ninja 不解析 .rc 内部对 ico 的引用，显式挂依赖：
        # ico 变了（mtime 更新）才重编 .rc 并重链，否则 exe 内嵌图标一直是旧版
        set_source_files_properties(
            "${CMAKE_CURRENT_SOURCE_DIR}/resources/md-editor.rc"
            PROPERTIES OBJECT_DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/md-editor.ico")
    endif()
endif()

target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt6::Widgets
    Qt6::WebEngineWidgets
    Qt6::WebChannel
)

# 编译翻译资源并打入 qrc：生成 :/i18n/md-editor_en_US.qm
qt6_add_translations(${PROJECT_NAME}
    TS_FILES resources/i18n/md-editor_en_US.ts
    RESOURCE_PREFIX "/i18n"
)
