find_package(Qt6 REQUIRED COMPONENTS Test Gui Concurrent)

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

# HtmlExporter 列宽归一脚本运行时读 qrc（:/web/table-column-layout.js），
# 须挂 app.qrc 否则 readResource 返回空串（同 test_exportprogress）
add_executable(test_htmlexporter test_htmlexporter.cpp ../src/core/HtmlExporter.cpp ../src/core/ExportCommon.cpp ../src/resources/app.qrc)
target_include_directories(test_htmlexporter PRIVATE ../src)
# Qt6::Qml 供 QJSEngine 做内联脚本语法检查（列宽归一脚本一处语法错即静默失效）
# Qt6::Gui：ExportCommon::pageContentWidthTwips 用 QPageSize/QPageLayout（QtGui 类），
# 本目标编译 ExportCommon.cpp，缺 Gui 头文件路径会编译失败
target_link_libraries(test_htmlexporter PRIVATE Qt6::Test Qt6::Gui Qt6::Qml)
add_test(NAME test_htmlexporter COMMAND test_htmlexporter)

add_executable(test_docxexporter test_docxexporter.cpp ../src/core/DocxExporter.cpp ../src/core/miniz/miniz.c ../src/core/MermaidRenderer.cpp ../src/core/MathMlRenderer.cpp ../src/core/MathMlConverter.cpp ../src/core/ExportCommon.cpp ../src/core/TableColumnNormalizer.cpp ../src/core/HtmlExporter.cpp ../src/resources/app.qrc)
target_include_directories(test_docxexporter PRIVATE ../src)
# DocxExporter.cpp 用到 QImageReader（QtGui），必须链接 Qt6::Gui；
# MermaidRenderer.cpp/MathMlRenderer.cpp 用 QWebEngine，须链接 WebEngineWidgets。
# TableColumnNormalizer/HtmlExporter：前者链 WebEngine（仅静态函数入测），后者 readResource 需 app.qrc
# 测试只用非 mermaid/非公式 HTML（extractMermaidSources/extractMathSources 返回空 → 不构造
# 渲染器），链接 WebEngine 不触发无头渲染；MathMlConverter 为纯函数，可直接单测。
target_link_libraries(test_docxexporter PRIVATE Qt6::Test Qt6::Gui Qt6::WebEngineWidgets)
add_test(NAME test_docxexporter COMMAND test_docxexporter)

add_executable(test_pdfexporter test_pdfexporter.cpp ../src/core/PdfExporter.cpp ../src/core/PdfPageGeometry.cpp ../src/core/miniz/miniz.c ../src/core/HtmlExporter.cpp ../src/core/ExportCommon.cpp ../src/resources/app.qrc)
target_include_directories(test_pdfexporter PRIVATE ../src)
# PdfExporter.cpp 用到 QWebEngineView/Page/Settings，缺 WebEngineWidgets 会链接失败；
# 测试只调用静态 buildRenderHtml，从不构造 PdfExporter，不会触发无头渲染
target_link_libraries(test_pdfexporter PRIVATE Qt6::Test Qt6::Gui Qt6::WebEngineWidgets Qt6::Qml Qt6::Pdf)
add_test(NAME test_pdfexporter COMMAND test_pdfexporter)

# PdfPageGeometry：PDF 内容流几何测量（nativeHf 页脚定位的 SVG 底沿缺口
# 修复件）——纯字节解析，无 Qt Gui/WebEngine 依赖
add_executable(test_pdfpagegeometry test_pdfpagegeometry.cpp
    ../src/core/PdfPageGeometry.cpp ../src/core/miniz/miniz.c)
target_include_directories(test_pdfpagegeometry PRIVATE ../src)
target_link_libraries(test_pdfpagegeometry PRIVATE Qt6::Test Qt6::Gui)
add_test(NAME test_pdfpagegeometry COMMAND test_pdfpagegeometry)

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

# W4-1 转换移线程：ConversionQueue 时序契约（串行次序/代次护栏/析构丢弃/
# 线程身份）+ worker 线程与 GUI 线程转换逐字节一致 + UI 冻结量化
add_executable(test_conversionqueue test_conversionqueue.cpp
    ../src/core/ConversionQueue.cpp ../src/core/MarkdownHandler.cpp
    ../src/core/qt/QtMarkdownWriter.cpp)
target_include_directories(test_conversionqueue PRIVATE ../src)
target_link_libraries(test_conversionqueue PRIVATE Qt6::Test Qt6::Gui Qt6::Concurrent)
add_test(NAME test_conversionqueue COMMAND test_conversionqueue)

# DMG 背景图生成器（拖拽安装布局）：QImage 解码验证 PNG 合法性 + 渐变/箭头像素断言
add_executable(test_dmgbg test_dmgbg.cpp)
target_include_directories(test_dmgbg PRIVATE ../src)
target_link_libraries(test_dmgbg PRIVATE Qt6::Test Qt6::Gui)
add_test(NAME test_dmgbg COMMAND test_dmgbg)

# MacInstallCheck 谓词（DMG 挂载卷/Gatekeeper 转位目录运行检测，启动提示用）
# ——纯路径单元，只链自身
add_executable(test_macinstallcheck test_macinstallcheck.cpp
    ../src/app/MacInstallCheck.cpp)
target_include_directories(test_macinstallcheck PRIVATE ../src)
target_link_libraries(test_macinstallcheck PRIVATE Qt6::Test)
add_test(NAME test_macinstallcheck COMMAND test_macinstallcheck)

# 导出进度对话框：GIF 资源形状锁（qrc 资源须挂 app.qrc）+ 状态机
# （Esc 不可关/错误态留住用户/成功自动关）
add_executable(test_exportprogress test_exportprogress.cpp
    ../src/ui/ExportProgressDialog.cpp ../src/resources/app.qrc)
target_include_directories(test_exportprogress PRIVATE ../src)
target_link_libraries(test_exportprogress PRIVATE Qt6::Test Qt6::Widgets)
add_test(NAME test_exportprogress COMMAND test_exportprogress)

# mdconv：MarkdownHandler 转换 CLI 桥（vitest 真实链路矩阵经子进程调用真实
# C++ 转换；亦是往返排查工具。测试基础设施，不进产品目标）
add_executable(mdconv mdconv.cpp
    ../src/core/MarkdownHandler.cpp ../src/core/qt/QtMarkdownWriter.cpp)
target_include_directories(mdconv PRIVATE ../src)
target_link_libraries(mdconv PRIVATE Qt6::Gui)

# mdconv 冒烟：CLI 桥与直连转换逐字节一致
# （测试直连 MarkdownHandler::fromHtml 作对照，须与 mdconv 同编其源）
add_executable(test_mdconv test_mdconv.cpp
    ../src/core/MarkdownHandler.cpp ../src/core/qt/QtMarkdownWriter.cpp)
target_compile_definitions(test_mdconv PRIVATE MDCONV_PATH="$<TARGET_FILE:mdconv>")
target_include_directories(test_mdconv PRIVATE ../src)
target_link_libraries(test_mdconv PRIVATE Qt6::Test Qt6::Gui)
add_test(NAME test_mdconv COMMAND test_mdconv)

# commonmark 语料收敛棘轮（A2/A5：deb CI on ubuntu-26.04 = Qt 6.10.2 基准把关）
add_executable(test_commonmark_corpus test_commonmark_corpus.cpp
    ../src/core/MarkdownHandler.cpp ../src/core/qt/QtMarkdownWriter.cpp)
target_compile_definitions(test_commonmark_corpus PRIVATE
    COMMONMARK_SPEC_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/commonmark/spec-0.31.2.json"
    COMMONMARK_RATCHET_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/commonmark/expected-diverge.txt")
target_include_directories(test_commonmark_corpus PRIVATE ../src)
target_link_libraries(test_commonmark_corpus PRIVATE Qt6::Test Qt6::Gui)
add_test(NAME test_commonmark_corpus COMMAND test_commonmark_corpus)

# gfm/自有语料全链路收敛（小棘轮：diverge 登记制，只减不增，同 commonmark）
add_executable(test_gfm_corpus test_gfm_corpus.cpp
    ../src/core/MarkdownHandler.cpp ../src/core/qt/QtMarkdownWriter.cpp)
target_compile_definitions(test_gfm_corpus PRIVATE
    GFM_CORPUS_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/gfm-corpus.md"
    GFM_RATCHET_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/gfm-expected-diverge.txt")
target_include_directories(test_gfm_corpus PRIVATE ../src)
target_link_libraries(test_gfm_corpus PRIVATE Qt6::Test Qt6::Gui)
add_test(NAME test_gfm_corpus COMMAND test_gfm_corpus)
