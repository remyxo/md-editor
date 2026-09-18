find_package(Qt6 REQUIRED COMPONENTS Test Gui)

# vendored QtMarkdownWriter 与 QTextDocument::toMarkdown() 的等价性回归
# （上游同步防线，详见 docs/vendor-qt-markdown-writer.md）
add_executable(test_qtmarkdownwriter test_qtmarkdownwriter.cpp
    ../src/core/qt/QtMarkdownWriter.cpp)
target_include_directories(test_qtmarkdownwriter PRIVATE ../src)
target_link_libraries(test_qtmarkdownwriter PRIVATE Qt6::Test Qt6::Gui)
add_test(NAME test_qtmarkdownwriter COMMAND test_qtmarkdownwriter)

add_executable(test_editorbridge test_editorbridge.cpp ../src/core/EditorBridge.cpp)
target_include_directories(test_editorbridge PRIVATE ../src)
target_link_libraries(test_editorbridge PRIVATE Qt6::Test Qt6::Gui)
add_test(NAME test_editorbridge COMMAND test_editorbridge)

add_executable(test_filemanager test_filemanager.cpp ../src/core/FileManager.cpp)
target_include_directories(test_filemanager PRIVATE ../src)
target_link_libraries(test_filemanager PRIVATE Qt6::Test)
add_test(NAME test_filemanager COMMAND test_filemanager)

add_executable(test_markdownhandler test_markdownhandler.cpp
    ../src/core/MarkdownHandler.cpp ../src/core/qt/QtMarkdownWriter.cpp)
target_compile_definitions(test_markdownhandler PRIVATE
    GUIDE_MD_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../src/resources/web/markdown-guide.md")
target_include_directories(test_markdownhandler PRIVATE ../src)
target_link_libraries(test_markdownhandler PRIVATE Qt6::Test Qt6::Gui)
add_test(NAME test_markdownhandler COMMAND test_markdownhandler)

add_executable(test_imagemanager test_imagemanager.cpp ../src/core/ImageManager.cpp)
target_include_directories(test_imagemanager PRIVATE ../src)
target_link_libraries(test_imagemanager PRIVATE Qt6::Test)
add_test(NAME test_imagemanager COMMAND test_imagemanager)

add_executable(test_htmlexporter test_htmlexporter.cpp ../src/core/HtmlExporter.cpp)
target_include_directories(test_htmlexporter PRIVATE ../src)
target_link_libraries(test_htmlexporter PRIVATE Qt6::Test)
add_test(NAME test_htmlexporter COMMAND test_htmlexporter)

add_executable(test_docxexporter test_docxexporter.cpp ../src/core/DocxExporter.cpp ../src/core/miniz/miniz.c ../src/core/MermaidRenderer.cpp ../src/core/MathMlRenderer.cpp ../src/core/MathMlConverter.cpp)
target_include_directories(test_docxexporter PRIVATE ../src)
# DocxExporter.cpp 用到 QImageReader（QtGui），必须链接 Qt6::Gui；
# MermaidRenderer.cpp/MathMlRenderer.cpp 用 QWebEngine，须链接 WebEngineWidgets。
# 测试只用非 mermaid/非公式 HTML（extractMermaidSources/extractMathSources 返回空 → 不构造
# 渲染器），链接 WebEngine 不触发无头渲染；MathMlConverter 为纯函数，可直接单测。
target_link_libraries(test_docxexporter PRIVATE Qt6::Test Qt6::Gui Qt6::WebEngineWidgets)
add_test(NAME test_docxexporter COMMAND test_docxexporter)

add_executable(test_pdfexporter test_pdfexporter.cpp ../src/core/PdfExporter.cpp ../src/core/HtmlExporter.cpp)
target_include_directories(test_pdfexporter PRIVATE ../src)
# PdfExporter.cpp 用到 QWebEngineView/Page/Settings，缺 WebEngineWidgets 会链接失败；
# 测试只调用静态 buildRenderHtml，从不构造 PdfExporter，不会触发无头渲染
target_link_libraries(test_pdfexporter PRIVATE Qt6::Test Qt6::Gui Qt6::WebEngineWidgets Qt6::Qml)
add_test(NAME test_pdfexporter COMMAND test_pdfexporter)

add_executable(test_systemthemewatcher test_systemthemewatcher.cpp
    ../src/app/SystemThemeWatcher.cpp)
target_include_directories(test_systemthemewatcher PRIVATE ../src)
target_link_libraries(test_systemthemewatcher PRIVATE Qt6::Test Qt6::Gui)
add_test(NAME test_systemthemewatcher COMMAND test_systemthemewatcher)

add_executable(test_languagemanager test_languagemanager.cpp
    ../src/app/LanguageManager.cpp)
target_include_directories(test_languagemanager PRIVATE ../src)
# LanguageManager.cpp 用到 QApplication（QTranslator 装载），必须链接 Qt6::Widgets
target_link_libraries(test_languagemanager PRIVATE Qt6::Test Qt6::Widgets)
add_test(NAME test_languagemanager COMMAND test_languagemanager)

add_executable(test_sidebar test_sidebar.cpp
    ../src/ui/SideBar.cpp ../src/ui/FileTree.cpp ../src/ui/OutlineTree.cpp
    ../src/ui/CollapsibleSection.cpp)
target_include_directories(test_sidebar PRIVATE ../src)
target_link_libraries(test_sidebar PRIVATE Qt6::Test Qt6::Widgets)
add_test(NAME test_sidebar COMMAND test_sidebar)

add_executable(test_collapsiblesection test_collapsiblesection.cpp
    ../src/ui/CollapsibleSection.cpp)
target_include_directories(test_collapsiblesection PRIVATE ../src)
target_link_libraries(test_collapsiblesection PRIVATE Qt6::Test Qt6::Widgets)
add_test(NAME test_collapsiblesection COMMAND test_collapsiblesection)

# 往返保真矩阵回归（全元素覆盖/嵌套矩阵/多轮收敛/失真链诊断，
# 见 tests/test_roundtrip_matrix.cpp 文件头说明）
add_executable(test_roundtrip_matrix test_roundtrip_matrix.cpp
    ../src/core/MarkdownHandler.cpp ../src/core/qt/QtMarkdownWriter.cpp)
target_include_directories(test_roundtrip_matrix PRIVATE ../src)
target_link_libraries(test_roundtrip_matrix PRIVATE Qt6::Test Qt6::Gui)
add_test(NAME test_roundtrip_matrix COMMAND test_roundtrip_matrix)

# Application 的 QFileOpenEvent 转发（macOS Finder 打开文件链路）。
# ThemeManager 对 qrc 缺失静默容错，无需链接资源；LanguageManager 用到
# QApplication，须链接 Qt6::Widgets（同 test_languagemanager）
add_executable(test_application test_application.cpp
    ../src/app/Application.cpp ../src/app/ThemeManager.cpp ../src/app/LanguageManager.cpp)
target_include_directories(test_application PRIVATE ../src)
target_link_libraries(test_application PRIVATE Qt6::Test Qt6::Widgets)
add_test(NAME test_application COMMAND test_application)

# FilePathMatch 谓词（TabWidget 同路径去重聚焦的核心）——零依赖小单元，
# 只链自身，不牵 EditorTab/QWebEngine 链
add_executable(test_editorpath test_editorpath.cpp
    ../src/ui/FilePathMatch.cpp)
target_include_directories(test_editorpath PRIVATE ../src)
target_link_libraries(test_editorpath PRIVATE Qt6::Test Qt6::Gui)
add_test(NAME test_editorpath COMMAND test_editorpath)

# DMG 背景图生成器（拖拽安装布局）：QImage 解码验证 PNG 合法性 + 渐变/箭头像素断言
add_executable(test_dmgbg test_dmgbg.cpp)
target_include_directories(test_dmgbg PRIVATE ../src)
target_link_libraries(test_dmgbg PRIVATE Qt6::Test Qt6::Gui)
add_test(NAME test_dmgbg COMMAND test_dmgbg)
