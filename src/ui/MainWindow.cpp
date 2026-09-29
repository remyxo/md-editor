#include "MainWindow.h"
#include "MenuBar.h"
#include "ToolBar.h"
#include "SideBar.h"
#include "FileTree.h"
#include "OutlineTree.h"
#include "TabWidget.h"
#include "EditorTab.h"
#include "StatusBar.h"
#include "SearchBar.h"
#include "SettingsDialog.h"
#include "TableGridPopup.h"
#include "AboutDialog.h"
#include "ExportProgressDialog.h"
#include "ExportHtmlAsync.h"
#include "../core/EditorBridge.h"
#include "../core/FileManager.h"
#include "../core/MarkdownHandler.h"
#include "../core/ImageManager.h"
#include "../core/JsEscape.h"
#include "../core/PdfExporter.h"
#include "../core/DocxExporter.h"
#include "../core/HtmlExporter.h"
#include "../core/ExportCommon.h"
#include "../core/TableColumnNormalizer.h"
#include "../app/Application.h"
#include "../app/ThemeManager.h"
#include "../app/SystemThemeWatcher.h"

#include <QVBoxLayout>
#include <QIcon>
#include <QElapsedTimer>   // 模式切换计时（状态栏反馈切换耗时分布）
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QAbstractButton>
#include <QJsonObject>
#include <QWebEnginePage>
#include <QTextStream>
#include <QSettings>
#include <QDebug>
#include <QShortcut>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QKeySequence>
#include <QPointer>
#include <memory>

// JS 单引号/模板字面量转义已收敛到 core/JsEscape.h 单一来源（jsSingleQuoted /
// forTemplateLiteral），本文件不再保留内联副本

// 读编辑器设置并应用到单个 tab（新 tab 页面加载完成时用）。曾直接调用四个
// apply*（各自遍历全部标签页）——N 个标签的会话恢复产生 O(N²) 次
// runJavaScript 与 4N 次 QSettings 构造解析；全局设置变更仍走完整 apply*
static void applyEditorConfigTo(EditorTab *tab)
{
    QSettings settings("md-editor", "md-editor");
    tab->setEditorFont(settings.value("editor/font", "Sans Serif").toString(),
                       settings.value("editor/fontSize", 18).toInt());
    tab->setEditorWidth(settings.value("editor/width", 80).toInt());
    QJsonObject s;
    s["lineNumbers"] = settings.value("editor/lineNumbers", false).toBool();
    s["wordWrap"] = settings.value("editor/wordWrap", true).toBool();
    s["tabSize"] = settings.value("editor/tabSize", 4).toInt();
    tab->setEditorSettings(s);
    tab->applyCustomCss(settings.value("editor/customCss").toString());
}

MainWindow::MainWindow(const QString &initialFile, QWidget *parent)
    : QMainWindow(parent)
{
    m_fileManager = new FileManager(this);

    setupUi();
    connectSignals();

    // 恢复侧边栏面板可见性（首次启动默认只显示大纲；两面板状态跨会话记忆）。
    // 此时信号 lambda 已连接：applyInitialPanes 发的信号会同步菜单勾选并幂等写回。
    {
        QSettings settings("md-editor", "md-editor");
        m_sideBar->applyInitialPanes(
            settings.value("sidebar/fileTreeVisible", false).toBool(),
            settings.value("sidebar/outlineVisible", true).toBool());
    }

    // 命令行传入文件路径（文件管理器双击打开）时优先打开该文件；
    // 否则恢复上次会话（无会话时自动新建空白标签页）
    if (!initialFile.isEmpty()) {
        onFileDoubleClicked(initialFile);
    } else {
        restoreSession();
    }
}

MainWindow::~MainWindow() {}

void MainWindow::setupUi()
{
    setWindowTitle("Markdown Editor");
    setWindowIcon(QIcon(":/icons/md-editor.svg"));
    resize(1280, 800);

    // 菜单栏
    m_menuBar = new MenuBar(this);
    setMenuBar(m_menuBar);

    // 工具栏
    m_toolBar = new ToolBar(this);
    addToolBar(m_toolBar);

    // 中央区域：搜索栏 + (侧边栏 + 编辑器)
    QWidget *central = new QWidget(this);
    QVBoxLayout *centralLayout = new QVBoxLayout(central);
    centralLayout->setContentsMargins(0, 0, 0, 0);
    centralLayout->setSpacing(0);

    // 搜索栏（默认隐藏，按 Ctrl+F 显示）
    m_searchBar = new SearchBar(central);
    centralLayout->addWidget(m_searchBar);

    // 侧边栏 + 编辑器（可拖拽分割）
    m_splitter = new QSplitter(Qt::Horizontal, central);

    m_sideBar = new SideBar(m_splitter);
    m_splitter->addWidget(m_sideBar);

    m_tabWidget = new TabWidget(m_splitter);
    m_splitter->addWidget(m_tabWidget);

    m_splitter->setStretchFactor(0, 0); // 侧边栏固定
    m_splitter->setStretchFactor(1, 1); // 编辑器伸展
    m_splitter->setSizes({320, 960});

    centralLayout->addWidget(m_splitter, 1);
    setCentralWidget(central);

    // 状态栏
    m_statusBar = new StatusBar(this);
    setStatusBar(m_statusBar);

    // Ctrl+F / Ctrl+H 的快捷键由编辑菜单 QAction 携带（见 MenuBar），
    // 此处不再建 QShortcut，避免 Ambiguous shortcut overload。

    // F3 / Shift+F3：下一个 / 上一个（仅搜索栏可见时生效）
    QShortcut *nextSc = new QShortcut(QKeySequence("F3"), this);
    connect(nextSc, &QShortcut::activated, this, [this]() {
        if (m_searchBar->isVisible()) runEditorCommand("window.editorApi.searchNext();");
    });
    QShortcut *prevSc = new QShortcut(QKeySequence("Shift+F3"), this);
    connect(prevSc, &QShortcut::activated, this, [this]() {
        if (m_searchBar->isVisible()) runEditorCommand("window.editorApi.searchPrev();");
    });

    // Esc：优先关闭模态编辑面板，否则隐藏搜索栏
    QShortcut *escShortcut = new QShortcut(QKeySequence("Esc"), this);
    connect(escShortcut, &QShortcut::activated, this, [this]() {
        if (m_modalDialogOpen) {
            // 关闭 JS 侧的模态面板（Mermaid/数学公式/超链接/语言选择器/脚注改名）
            runEditorCommand("window.editorApi._linkEditor.close();"
                             "window.editorApi._mermaidEditor.close();"
                             "window.editorApi._mathEditor.close();"
                             "window.editorApi._emojiPicker.close();"
                             "window.editorApi._codeLangPicker.close();"
                             "window.editorApi._footnoteEditor.close();");
        } else {
            m_searchBar->setVisible(false);
        }
    });

    // Ctrl+Shift+M 插入块级公式（Ctrl+M 行内公式由菜单「插入公式」QAction 携带，
    // 此处不再重复注册 QShortcut，否则与菜单快捷键冲突报 "Ambiguous shortcut"）
    QShortcut *mathBlockShortcut = new QShortcut(QKeySequence("Ctrl+Shift+M"), this);
    connect(mathBlockShortcut, &QShortcut::activated, this, [this]() {
        runEditorCommand("window.editorApi.insertMath('block');");
    });

    // 系统主题监听（无 portal 环境自动降级恒 light）
    m_systemThemeWatcher = SystemThemeWatcher::createForProduction(this);
    connect(m_systemThemeWatcher, &SystemThemeWatcher::systemThemeChanged,
            this, [this](const QString &actual) {
        // 仅 system 模式下跟随系统变化
        if (m_themeMode == "system") onThemeSwitch("system");
    });

    // 恢复主题偏好模式（system/light/dark，缺省 light）
    QSettings settings("md-editor", "md-editor");
    QString savedMode = settings.value("appearance/theme", "light").toString();
    onThemeSwitch(savedMode);
}

void MainWindow::connectSignals()
{
    // 主题变更 → 刷新图标
    connect(Application::instance()->themeManager(), &ThemeManager::themeChanged,
            this, [this](const QString &theme) {
        m_toolBar->refreshThemeIcons(theme);
    });

    // === 菜单栏信号 ===
    connect(m_menuBar, &MenuBar::newFile, this, &MainWindow::onNewFile);
    connect(m_menuBar, &MenuBar::openFolder, this, &MainWindow::onOpenFolder);
    connect(m_menuBar, &MenuBar::openFile, this, [this]() {
        QString path = QFileDialog::getOpenFileName(this, tr("打开文件"), "",
                                                     tr("Markdown 文件 (*.md *.markdown);;所有文件 (*)"));
        if (!path.isEmpty()) onFileDoubleClicked(path);
    });
    connect(m_menuBar, &MenuBar::openRecentFile, this,
            &MainWindow::onFileDoubleClicked);
    connect(m_menuBar, &MenuBar::exportPdf, this, &MainWindow::onExportPdf);
    connect(m_menuBar, &MenuBar::exportDocx, this, &MainWindow::onExportDocx);
    connect(m_menuBar, &MenuBar::exportHtml, this, &MainWindow::onExportHtml);
    connect(m_menuBar, &MenuBar::switchTheme, this, &MainWindow::onThemeSwitch);
    connect(m_menuBar, &MenuBar::toggleOutline, this, [this](bool visible) {
        m_sideBar->setOutlineVisible(visible);
    });
    connect(m_menuBar, &MenuBar::toggleFileTree, this, [this](bool visible) {
        m_sideBar->setFileTreeVisible(visible);
    });
    // 标题栏点击收起面板 → 同步视图菜单勾选（setChecked 不发射 triggered，无环路）
    connect(m_sideBar, &SideBar::fileTreeVisibleChanged, this, [this](bool visible) {
        QAction *act = m_menuBar->findChild<QAction *>(QStringLiteral("fileTreeAction"));
        if (act) act->setChecked(visible);
        QSettings settings("md-editor", "md-editor");   // 三条路径（菜单/标题栏/Ctrl+B）全汇聚于此
        settings.setValue("sidebar/fileTreeVisible", visible);
    });
    connect(m_sideBar, &SideBar::outlineVisibleChanged, this, [this](bool visible) {
        QAction *act = m_menuBar->findChild<QAction *>(QStringLiteral("outlineAction"));
        if (act) act->setChecked(visible);
        QSettings settings("md-editor", "md-editor");
        settings.setValue("sidebar/outlineVisible", visible);
    });

    // Ctrl+B 整体收/展：可见→记住组合并全收；全收→恢复组合
    m_sidebarShortcut = new QShortcut(QKeySequence("Ctrl+B"), this);
    connect(m_sidebarShortcut, &QShortcut::activated, this, [this]() {
        QAction *fileAct = m_menuBar->findChild<QAction *>(QStringLiteral("fileTreeAction"));
        QAction *outlineAct = m_menuBar->findChild<QAction *>(QStringLiteral("outlineAction"));
        if (!fileAct || !outlineAct) return;
        if (fileAct->isChecked() || outlineAct->isChecked()) {
            m_lastPaneCombo = {fileAct->isChecked(), outlineAct->isChecked()};
            fileAct->setChecked(false);
            outlineAct->setChecked(false);
        } else {
            fileAct->setChecked(m_lastPaneCombo.first);
            outlineAct->setChecked(m_lastPaneCombo.second);
        }
        // setChecked 不发射 triggered，需手动同步面板
        m_sideBar->setFileTreeVisible(fileAct->isChecked());
        m_sideBar->setOutlineVisible(outlineAct->isChecked());
    });
    connect(m_menuBar, &MenuBar::toggleSourceMode, this, &MainWindow::onToggleSourceMode);
    connect(m_menuBar, &MenuBar::toggleFullscreen, this, [this]() {
        if (isFullScreen())
            showNormal();
        else
            showFullScreen();
    });
    connect(m_menuBar, &MenuBar::insertLink, this, &MainWindow::onInsertLink);
    connect(m_menuBar, &MenuBar::insertImage, this, &MainWindow::onInsertImage);
    connect(m_menuBar, &MenuBar::insertTable, this, &MainWindow::onInsertTable);
    connect(m_menuBar, &MenuBar::insertMath, this, [this]() {
        runEditorCommand("window.editorApi.insertMath('inline');");
    });
    connect(m_menuBar, &MenuBar::insertCode, this, [this]() {
        runEditorCommand("window.editorApi.toggleCode();");
    });
    connect(m_menuBar, &MenuBar::insertCodeBlock, this, [this]() {
        runEditorCommand("window.editorApi.insertCodeBlock();");
    });
    connect(m_menuBar, &MenuBar::quit, this, &MainWindow::close);
    connect(m_menuBar, &MenuBar::saveFile, this, &MainWindow::onSaveFile);
    connect(m_menuBar, &MenuBar::saveFileAs, this, &MainWindow::onSaveFileAs);
    connect(m_menuBar, &MenuBar::undo, this, [this]() { runEditorCommand("window.editorApi.undo();"); });
    connect(m_menuBar, &MenuBar::redo, this, [this]() { runEditorCommand("window.editorApi.redo();"); });
    connect(m_menuBar, &MenuBar::findRequested, this, [this]() {
        // 仅查找模式下 Ctrl+F 切换显隐；替换模式时切回仅查找
        if (m_searchBar->isVisible() && !m_searchBar->replaceModeActive())
            m_searchBar->setVisible(false);
        else
            showSearchBar(false);
    });
    connect(m_menuBar, &MenuBar::replaceRequested, this, [this]() { showSearchBar(true); });
    // 工具栏撤销/重做按钮
    QAction *toolUndo = m_toolBar->findChild<QAction *>("undoAction");
    if (toolUndo)
        connect(toolUndo, &QAction::triggered, this, [this]() { runEditorCommand("window.editorApi.undo();"); });
    QAction *toolRedo = m_toolBar->findChild<QAction *>("redoAction");
    if (toolRedo)
        connect(toolRedo, &QAction::triggered, this, [this]() { runEditorCommand("window.editorApi.redo();"); });
    connect(m_menuBar, &MenuBar::showMarkdownGuide, this, &MainWindow::onShowMarkdownGuide);
    connect(m_menuBar, &MenuBar::showSettings, this, &MainWindow::onShowSettings);
    connect(m_menuBar, &MenuBar::showAbout, this, [this]() {
        AboutDialog dlg(this);
        dlg.exec();
    });

    // === 工具栏信号 ===
    connect(m_toolBar, &ToolBar::boldClicked, this, &MainWindow::onBoldClicked);
    connect(m_toolBar, &ToolBar::italicClicked, this, &MainWindow::onItalicClicked);
    connect(m_toolBar, &ToolBar::strikethroughClicked, this, [this]() {
        runEditorCommand("window.editorApi.toggleStrike();");
    });
    connect(m_toolBar, &ToolBar::codeClicked, this, [this]() {
        runEditorCommand("window.editorApi.toggleCode();");
    });
    connect(m_toolBar, &ToolBar::underlineClicked, this, [this]() {
        runEditorCommand("window.editorApi.toggleUnderline();");
    });
    connect(m_toolBar, &ToolBar::highlightClicked, this, [this]() {
        runEditorCommand("window.editorApi.toggleHighlight();");
    });
    connect(m_toolBar, &ToolBar::subscriptClicked, this, [this]() {
        runEditorCommand("window.editorApi.toggleSubscript();");
    });
    connect(m_toolBar, &ToolBar::superscriptClicked, this, [this]() {
        runEditorCommand("window.editorApi.toggleSuperscript();");
    });
    connect(m_toolBar, &ToolBar::clearFormatClicked, this, [this]() {
        runEditorCommand("window.editorApi.clearFormatting();");
    });
    connect(m_toolBar, &ToolBar::alignSelected, this, [this](const QString &alignment) {
        runEditorCommand(QString("window.editorApi.setTextAlign('%1');").arg(alignment));
    });
    // 颜色按钮：#rrggbb 无单引号，安全；防御性转义以防扩展支持命名色
    // 注：lambda 按值接 color 以便调用非 const 的 QString::replace
    connect(m_toolBar, &ToolBar::textColorSelected, this, [this](QString color) {
        runEditorCommand(QString("window.editorApi.applyTextColor('%1');").arg(color.replace('\'', "\\'")));
    });
    connect(m_toolBar, &ToolBar::bgColorSelected, this, [this](QString color) {
        runEditorCommand(QString("window.editorApi.applyBgColor('%1');").arg(color.replace('\'', "\\'")));
    });
    connect(m_toolBar, &ToolBar::textColorCleared, this, [this]() {
        runEditorCommand("window.editorApi.clearTextColor();");
    });
    connect(m_toolBar, &ToolBar::bgColorCleared, this, [this]() {
        runEditorCommand("window.editorApi.clearBgColor();");
    });
    connect(m_toolBar, &ToolBar::headingSelected, this, &MainWindow::onHeadingSelected);
    connect(m_toolBar, &ToolBar::bulletListClicked, this, [this]() {
        runEditorCommand("window.editorApi.toggleBulletList();");
    });
    connect(m_toolBar, &ToolBar::orderedListClicked, this, [this]() {
        runEditorCommand("window.editorApi.toggleOrderedList();");
    });
    connect(m_toolBar, &ToolBar::taskListClicked, this, [this]() {
        runEditorCommand("window.editorApi.toggleTaskList();");
    });
    connect(m_toolBar, &ToolBar::blockquoteClicked, this, [this]() {
        runEditorCommand("window.editorApi.toggleBlockquote();");
    });
    connect(m_toolBar, &ToolBar::linkClicked, this, &MainWindow::onInsertLink);
    connect(m_toolBar, &ToolBar::imageClicked, this, &MainWindow::onInsertImage);
    connect(m_toolBar, &ToolBar::tableClicked, this, &MainWindow::onInsertTable);
    connect(m_toolBar, &ToolBar::codeBlockClicked, this, [this]() {
        runEditorCommand("window.editorApi.insertCodeBlock();");
    });
    connect(m_toolBar, &ToolBar::horizontalRuleClicked, this, [this]() {
        runEditorCommand("window.editorApi.insertHorizontalRule();");
    });
    connect(m_toolBar, &ToolBar::mathClicked, this, [this]() {
        runEditorCommand("window.editorApi.insertMath('inline');");
    });
    connect(m_toolBar, &ToolBar::mermaidClicked, this, [this]() {
        runEditorCommand("window.editorApi.insertMermaid();");
    });
    connect(m_toolBar, &ToolBar::emojiClicked, this, [this]() { onInsertEmoji(); });
    connect(m_toolBar, &ToolBar::footnoteClicked, this, [this]() {
        runEditorCommand("window.editorApi.insertFootnote();");
    });
    connect(m_menuBar, &MenuBar::insertEmoji, this, [this]() { onInsertEmoji(); });
    connect(m_menuBar, &MenuBar::insertFootnote, this, [this]() {
        runEditorCommand("window.editorApi.insertFootnote();");
    });
    connect(m_toolBar, &ToolBar::sourceToggleClicked, this, &MainWindow::onToggleSourceMode);
    connect(m_toolBar, &ToolBar::pdfExportClicked, this, &MainWindow::onExportPdf);
    connect(m_toolBar, &ToolBar::docxExportClicked, this, &MainWindow::onExportDocx);
    connect(m_toolBar, &ToolBar::htmlExportClicked, this, &MainWindow::onExportHtml);

    // === 标签页管理 ===
    connect(m_tabWidget, &TabWidget::editorBridgeCreated,
            this, &MainWindow::onEditorBridgeCreated);
    connect(m_tabWidget, &TabWidget::currentTabChanged,
            this, &MainWindow::onTabChanged);
    connect(m_tabWidget, &TabWidget::tabSaveRequested,
            this, [this](int) { onSaveFile(); });
    connect(m_tabWidget, &TabWidget::tabClosed, this,
            [this](int index) {
                EditorTab *tab = m_tabWidget->tabAt(index);
                if (tab) {
                    QString path = tab->webView()->property("filePath").toString();
                    // 其他标签页还开着同文件时不得停监控（曾按路径全局 unwatch，
                    // 关一个 tab 误停另一个的外部变更检测）
                    bool sharedElsewhere = false;
                    for (int i = 0; i < m_tabWidget->count(); ++i) {
                        EditorTab *other = m_tabWidget->tabAt(i);
                        if (other && other != tab
                            && other->webView()->property("filePath").toString() == path) {
                            sharedElsewhere = true;
                            break;
                        }
                    }
                    if (!path.isEmpty() && !sharedElsewhere)
                        m_fileManager->unwatchFile(path);
                }
                // tabClosed 在 removeTab 之前发出：count()<=1 说明这是最后一个标签页，
                // 关闭后无活动文档——清空大纲并复位状态栏，避免残留上一文档的
                // 标题结构/行列/字数（大纲只由各标签页桥的 headingsUpdated 推送，
                // 最后一个标签销毁后无人再推送，须在此主动清空）
                if (m_tabWidget->count() <= 1) {
                    m_sideBar->outlineTree()->updateHeadings(QJsonArray());
                    m_statusBar->updateCursorPosition(1, 1, 0);
                    m_statusBar->updateWordCount(0, 0, 0);
                }
            });

    // === 文件读入失败 ===
    // 统一弹窗（打开对话框/双击/拖放/命令行/外部变更重载）；会话恢复期间静默
    // （读失败条目直接跳过，见 restoreSession——此前该信号无人连接，读失败
    // 静默返回 null：会话恢复开空标签页、重载静默放弃）
    // 写失败详情捕获（writeFile 同步发射，false 返回前已送达此处）：
    // 供 writeTabToFile 的失败弹窗展示具体错误（权限/磁盘满等）
    connect(m_fileManager, &FileManager::fileWriteError, this,
            [this](const QString &, const QString &error) {
                m_lastWriteError = error;
            });
    connect(m_fileManager, &FileManager::fileReadError, this,
            [this](const QString &path, const QString &error) {
                if (m_suppressReadErrors) return;
                QMessageBox box(QMessageBox::Warning, tr("打开失败"),
                                tr("无法打开文件：\n%1\n\n%2").arg(path, error),
                                QMessageBox::Ok, this);
                box.button(QMessageBox::Ok)->setText(tr("确定"));
                box.exec();
            });

    // === 文件外部变更 ===
    connect(m_fileManager, &FileManager::fileExternallyModified, this,
            [this](const QString &path) {
                // 查找打开此文件的标签页
                for (int i = 0; i < m_tabWidget->count(); ++i) {
                    EditorTab *tab = m_tabWidget->tabAt(i);
                    if (tab && tab->webView()->property("filePath").toString() == path) {
                        // 本页有未保存修改时明确警告丢弃后果（曾无差别「是否重
                        // 新加载」，用户确认即静默覆盖本地编辑）
                        const bool dirty = m_tabWidget->tabText(i).startsWith("● ");
                        QMessageBox box(QMessageBox::Question, tr("文件已变更"),
                            dirty
                                ? tr("文件 %1 已被外部程序修改，且本页有未保存的修改。\n重新加载将丢弃本地修改，是否重新加载？").arg(path)
                                : tr("文件 %1 已被外部程序修改。\n是否重新加载？").arg(path),
                            QMessageBox::Yes | QMessageBox::No, this);
                        box.button(QMessageBox::Yes)->setText(tr("是"));
                        box.button(QMessageBox::No)->setText(tr("否"));
                        if (box.exec() == QMessageBox::Yes) {
                            QString encoding;
                            QString content = m_fileManager->readFile(path, &encoding);
                            if (!content.isNull()) {
                                tab->setEncoding(encoding);
                                tab->loadFile(path, content);
                                refreshEncodingLabel();
                            }
                        }
                        break;
                    }
                }
            });

    // === 文件树 ===
    connect(m_sideBar->fileTree(), &FileTree::fileDoubleClicked,
            this, &MainWindow::onFileDoubleClicked);

    // === macOS：Finder 双击/「打开方式」的文件 ===
    // QFileOpenEvent → Application::fileOpenRequested（该链路不经过 argv），
    // 复用文件树双击的打开逻辑（存在性检查/开标签/大文件警告全部沿用）
    connect(Application::instance(), &Application::fileOpenRequested,
            this, &MainWindow::onFileDoubleClicked);

    // === 侧边栏「打开文件夹」按钮 ===
    connect(m_sideBar, &SideBar::openFolderClicked,
            this, &MainWindow::onOpenFolder);

    // === 大纲点击 → 跳转到对应位置 ===
    connect(m_sideBar->outlineTree(), &OutlineTree::headingClicked,
            this, [this](int pos) {
        runEditorCommand(QString("window.editorApi.scrollToPos(%1);").arg(pos));
    });

    // === 搜索栏 ===
    // JS 字符串字面量转义统一走 core/JsEscape.h（query / rep 嵌入 runJavaScript）
    auto runSearchJs = [this](const QString &jsBody) {
        EditorTab *tab = m_tabWidget->currentEditorTab();
        if (tab) tab->runJsCommand("(function(){var ed=window.editorApi;if(!ed||!ed.editor)return;" + jsBody + "})();");
    };

    connect(m_searchBar, &SearchBar::searchParamsChanged, this,
            [this, runSearchJs](const QString &text, bool cs, bool rx, bool ww) {
        runSearchJs(QStringLiteral("ed.startSearch('%1',%2,%3,%4);")
                    .arg(JsEscape::jsSingleQuoted(text)).arg(cs).arg(rx).arg(ww));
    });
    connect(m_searchBar, &SearchBar::searchResumeRequested, this,
            [this, runSearchJs](const QString &text, bool cs, bool rx, bool ww) {
        runSearchJs(QStringLiteral("ed.resumeSearch('%1',%2,%3,%4);")
                    .arg(JsEscape::jsSingleQuoted(text)).arg(cs).arg(rx).arg(ww));
    });
    connect(m_searchBar, &SearchBar::findNextRequested, this, [this, runSearchJs]() { runSearchJs("ed.searchNext();"); });
    connect(m_searchBar, &SearchBar::findPrevRequested, this, [this, runSearchJs]() { runSearchJs("ed.searchPrev();"); });
    connect(m_searchBar, &SearchBar::replaceRequested, this, [this, runSearchJs](const QString &rep) {
        runSearchJs(QStringLiteral("ed.replaceOne('%1');").arg(JsEscape::jsSingleQuoted(rep)));
    });
    connect(m_searchBar, &SearchBar::replaceAllRequested, this, [this, runSearchJs](const QString &rep) {
        runSearchJs(QStringLiteral("ed.replaceAll('%1');").arg(JsEscape::jsSingleQuoted(rep)));
    });
    connect(m_searchBar, &SearchBar::closeRequested, this, [this, runSearchJs]() {
        m_searchBar->setVisible(false);
        runSearchJs("ed.stopSearch();");
    });
}

void MainWindow::onEditorBridgeCreated(EditorBridge *bridge)
{
    // 连接光标位置更新（仅当前标签页）：状态栏行列 + 大纲高亮当前所属标题
    connect(bridge, &EditorBridge::cursorPositionChanged, this,
            [this, bridge](int line, int col, int pos, int /*tabId*/) {
                EditorTab *cur = m_tabWidget->currentEditorTab();
                if (cur && cur->bridge() == bridge) {
                    m_statusBar->updateCursorPosition(line, col, 0);
                    m_sideBar->outlineTree()->highlightAtPos(pos);
                }
            });

    // 连接大纲更新（仅当前标签页：后台标签页的异步事务——图片回填、mermaid
    // 确认、低亮批次等触发的 onUpdate——曾会把当前页大纲整体重建成后台页的
    // 标题结构。切页时由 onTabChanged 主动重推）
    connect(bridge, &EditorBridge::headingsUpdated, this,
            [this, bridge](const QJsonArray &headings, int /*tabId*/) {
                EditorTab *cur = m_tabWidget->currentEditorTab();
                if (cur && cur->bridge() == bridge)
                    m_sideBar->outlineTree()->updateHeadings(headings);
            });

    // 连接字数统计 → 状态栏（仅当前标签页）
    connect(bridge, &EditorBridge::wordCountUpdated, this,
            [this, bridge](int chars, int words, int /*tabId*/) {
                EditorTab *cur = m_tabWidget->currentEditorTab();
                if (cur && cur->bridge() == bridge)
                    m_statusBar->updateWordCount(chars, words, 0);
            });

    // 连接格式状态同步 → 工具栏（仅当前标签页，同上守卫）
    connect(bridge, &EditorBridge::formatStateChanged, this,
            [this, bridge](const QJsonObject &format, int tabId) {
                EditorTab *cur = m_tabWidget->currentEditorTab();
                if (cur && cur->bridge() == bridge)
                    onFormatChanged(format, tabId);
            });

    // 连接撤销/重做状态 → 工具栏（仅当前标签页，同上守卫）
    connect(bridge, &EditorBridge::historyStateChanged, this,
            [this, bridge](bool canUndo, bool canRedo, int /*tabId*/) {
                EditorTab *cur = m_tabWidget->currentEditorTab();
                if (cur && cur->bridge() == bridge)
                    m_toolBar->updateHistoryState(canUndo, canRedo);
            });

    // 搜索计数回传 → 仅更新当前 tab 的搜索栏
    connect(bridge, &EditorBridge::searchMatchUpdated, this,
            [this, bridge](int total, int current, int /*tabId*/) {
                EditorTab *cur = m_tabWidget->currentEditorTab();
                if (cur && cur->bridge() == bridge)
                    m_searchBar->setCount(total, current);
            });

    // 跟踪模态编辑面板（Mermaid/数学公式）打开/关闭状态 → Esc 快捷键优先级
    connect(bridge, &EditorBridge::modalDialogStateChanged, this,
            [this](bool open, int /*tabId*/) {
                m_modalDialogOpen = open;
            });

    // 图片落盘请求（粘贴/拖拽）：落盘后回调 JS 把占位图 src 替换为相对路径
    connect(bridge, &EditorBridge::imageSaveRequested, this,
            [this, bridge](const QString &requestId, const QString &base64,
                           const QString &ext, const QString &suggestedName, int /*tabId*/) {
        // bridge 的 parent 即拥有它的 EditorTab（见 EditorTab 构造 m_bridge(new EditorBridge(tabId, this))）
        EditorTab *tab = qobject_cast<EditorTab *>(bridge->parent());
        if (tab) onImageSaveRequested(tab, requestId, base64, ext, suggestedName);
    });

    // 页面加载完成后应用字体设置
    EditorTab *tab = m_tabWidget->currentEditorTab();
    if (tab) {
        connect(tab, &EditorTab::pageLoadFinished, this,
                [this, tab](bool ok) {
            if (!ok) return;
            // 只应用本 tab（曾调四个 apply* 全量广播所有标签页，会话恢复 O(N²)）
            applyEditorConfigTo(tab);
            // 补推当前实际主题，新 tab 与全局一致（缺口①）
            tab->setEditorTheme(
                Application::instance()->themeManager()->currentTheme());
            // 补推当前语言，新 tab 与全局一致（JS 弹窗文案跟随）
            tab->setEditorLanguage(
                Application::instance()->languageManager()->currentLanguage());
        });
    }
}

void MainWindow::onTabChanged(int tabId)
{
    Q_UNUSED(tabId)
    EditorTab *tab = m_tabWidget->currentEditorTab();
    // 源码模式状态逐标签页独立：切换标签时同步工具栏/状态栏/菜单
    // 勾选状态到新标签页的当前模式。
    bool srcMode = tab ? tab->isSourceMode() : false;
    m_toolBar->setSourceMode(srcMode);
    m_statusBar->setSourceMode(srcMode);
    refreshEncodingLabel();   // 编码逐标签页独立
    if (QAction *act = m_menuBar->findChild<QAction *>(
            QStringLiteral("sourceModeAction")))
        act->setChecked(srcMode);
    // 重推新标签页的大纲/格式/撤销状态：这些信号带「当前标签页」守卫后，后台
    // 标签页的异步通知不再污染当前页 UI，切页时须主动请求重推
    if (tab) {
        // 切换到仍在加载的标签页时 editorApi 可能尚未定义（页面脚本未执行完，
        // macOS 冷启动窗口期尤甚）——无守卫会抛未捕获 TypeError 且因同段执行
        // 连带跳过 format/history 推送（守卫与下方 syncFormatState 同款）
        tab->runJsCommand(
            QStringLiteral("if(window.editorApi){"
                           "window.editorApi.notifyHeadings();"
                           "window.editorApi.notifyFormat();"
                           "window.editorApi.notifyHistory();}"));
    }
    if (tab) {
        tab->setInTable(false);
        // 同步新标签页的格式状态到工具栏
        tab->runJsCommand("if(window.editorApi && window.editorApi.syncFormatState) window.editorApi.syncFormatState();");
    }
    // 切 tab：恢复搜索（查询词会话级、匹配与位置文档级——插件 state 逐 tab 独立，
    // 参数未变时保留当前匹配位置，参数变了才重新搜索）
    if (m_searchBar->isVisible() && tab) {
        m_searchBar->setCount(0, 0);   // 先清零，等新 tab 异步回传实际计数
        m_searchBar->emitResumeNow();
    }
}

EditorBridge *MainWindow::currentBridge() const
{
    EditorTab *tab = m_tabWidget->currentEditorTab();
    return tab ? tab->bridge() : nullptr;
}

void MainWindow::refreshEncodingLabel()
{
    EditorTab *tab = m_tabWidget->currentEditorTab();
    m_statusBar->updateEncoding(tab ? tab->encoding() : QString());
}

void MainWindow::showSearchBar(bool withReplace)
{
    m_searchBar->setVisible(true);
    m_searchBar->setReplaceMode(withReplace);
    m_searchBar->focusFindField();
}

void MainWindow::runEditorCommand(const QString &js)
{
    EditorTab *tab = m_tabWidget->currentEditorTab();
    if (tab) {
        tab->runJsCommand(js);
    }
}

// === 工具栏响应实现 ===

void MainWindow::onBoldClicked()
{
    runEditorCommand("window.editorApi.toggleBold();");
}

void MainWindow::onItalicClicked()
{
    runEditorCommand("window.editorApi.toggleItalic();");
}

void MainWindow::onHeadingSelected(int level)
{
    QString js = QString("window.editorApi.setHeading(%1);").arg(level);
    runEditorCommand(js);
}

void MainWindow::onInsertTable()
{
    TableGridPopup *popup = new TableGridPopup(m_toolBar);
    connect(popup, &TableGridPopup::gridSelected, this,
            [this](int rows, int cols) {
                QString js = QString("window.editorApi.insertTable(%1, %2);")
                    .arg(rows).arg(cols);
                runEditorCommand(js);
            });
    popup->move(QCursor::pos());
    popup->show();
}

void MainWindow::onInsertEmoji()
{
    EditorTab *tab = m_tabWidget->currentEditorTab();
    if (!tab) return;

    // 锚点：工具栏 emoji 按钮左下角；菜单触发时无按钮，用光标位置兜底
    QAction *a = m_toolBar->findChild<QAction *>("emojiAction");
    QWidget *btn = a ? m_toolBar->widgetForAction(a) : nullptr;
    QPoint gbl = btn ? btn->mapToGlobal(QPoint(0, btn->height())) : QCursor::pos();
    QPoint local = tab->webView()->mapFromGlobal(gbl);  // 默认 zoom=1.0：CSS px = 部件 px
    QString js = QString("window.editorApi.openEmojiPicker(%1, %2);")
                     .arg(local.x()).arg(local.y());
    tab->runJsCommand(js);
}

void MainWindow::onInsertLink()
{
    runEditorCommand("window.editorApi.insertLink();");
}

// 构造图片落盘结果回调 JS：成功 {ok:true,path}；失败 {ok:false,err}
// （字符串转义统一走 JsEscape::jsSingleQuoted，见 core/JsEscape.h）
static QString jsImageSaveResult(const QString &requestId, bool ok,
                                 const QString &path, const QString &err)
{
    if (ok)
        return QStringLiteral("window.editorApi._onImageSaveResult('%1',{ok:true,path:'%2'});")
            .arg(JsEscape::jsSingleQuoted(requestId), JsEscape::jsSingleQuoted(path));
    return QStringLiteral("window.editorApi._onImageSaveResult('%1',{ok:false,err:'%2'});")
        .arg(JsEscape::jsSingleQuoted(requestId), JsEscape::jsSingleQuoted(err));
}

void MainWindow::onImageSaveRequested(EditorTab *tab, const QString &requestId,
                                      const QString &base64, const QString &ext,
                                      const QString &suggestedName)
{
    QString filePath = tab->webView()->property("filePath").toString();
    if (filePath.isEmpty() || !QFile::exists(filePath)) {
        m_statusBar->showMessage(tr("请先保存文档再插入图片"), 3000);
        tab->runJsCommand(jsImageSaveResult(requestId, false, QString(), QStringLiteral("unsaved")));
        return;
    }
    QByteArray bytes = QByteArray::fromBase64(base64.toUtf8());
    if (bytes.isEmpty()) {
        m_statusBar->showMessage(tr("图片数据为空"), 3000);
        tab->runJsCommand(jsImageSaveResult(requestId, false, QString(), QStringLiteral("io")));
        return;
    }
    QString rel = ImageManager::saveImage(filePath, bytes, suggestedName, ext);
    if (rel.isEmpty()) {
        m_statusBar->showMessage(tr("图片保存失败"), 3000);
        tab->runJsCommand(jsImageSaveResult(requestId, false, QString(), QStringLiteral("io")));
        return;
    }
    m_statusBar->showMessage(tr("已插入图片"), 1500);
    tab->runJsCommand(jsImageSaveResult(requestId, true, rel, QString()));
}

void MainWindow::onInsertImage()
{
    QString path = QFileDialog::getOpenFileName(this, tr("插入图片"), "",
                                                 tr("图片文件 (*.png *.jpg *.jpeg *.gif *.svg *.webp)"));
    if (!path.isEmpty()) {
        // 路径含 ' 或 \（Windows）时须转义，否则 JS 语法错误、插入静默失败
        QString js = QString("window.editorApi.insertImage('%1');")
                         .arg(JsEscape::jsSingleQuoted(path));
        runEditorCommand(js);
    }
}

void MainWindow::onToggleSourceMode()
{
    EditorTab *tab = m_tabWidget->currentEditorTab();
    if (!tab || m_sourceModeToggling) return;
    m_sourceModeToggling = true;

    if (!tab->isSourceMode()) {
        // → 进入源码模式：获取编辑器 HTML → 转换为 MD → 显示到 textarea
        // 模式切换计时（JS 读取 → fromHtml 转换 → JS 填充），结果显示在状态栏：
        // 用户靠此数据反馈切换耗时分布，属正式功能而非调试插桩
        QElapsedTimer perfTotal;
        perfTotal.start();
        tab->webView()->page()->runJavaScript(
            QStringLiteral("window.editorApi.getContent()"),
            [this, tab, perfTotal](const QVariant &result) {
                QString html = result.toString();
                const qint64 jsReadMs = perfTotal.elapsed();
                QElapsedTimer conv;
                conv.start();
                QString md = MarkdownHandler::fromHtml(html);
                const qint64 convMs = conv.elapsed();
                QString escaped = JsEscape::forTemplateLiteral(md);
                QElapsedTimer jsFill;
                jsFill.start();
                tab->webView()->page()->runJavaScript(
                    QString("window.editorApi.enterSourceMode(`%1`);").arg(escaped),
                    [this, tab, jsReadMs, convMs, jsFill, perfTotal](const QVariant &) {
                        tab->setSourceMode(true);
                        m_toolBar->setSourceMode(true);
                        m_statusBar->setSourceMode(true);
                        if (QAction *act = m_menuBar->findChild<QAction *>(
                                QStringLiteral("sourceModeAction")))
                            act->setChecked(true);
                        const qint64 fillMs = jsFill.elapsed() - 15; // 减去经验 IPC 开销
                        const QString msg = QStringLiteral(
                            "【计时】进入源码: JS读取 %1ms · fromHtml %2ms · JS填充 %3ms · 总计 %4ms")
                            .arg(jsReadMs).arg(convMs).arg(qMax<qint64>(fillMs, 0))
                            .arg(perfTotal.elapsed());
                        qWarning() << msg;
                        m_statusBar->showMessage(msg, 6000);
                        m_sourceModeToggling = false;
                    });
            });
    } else {
        // → 退出源码模式：JS 单调用内判断源码是否编辑过——
        //   未编辑：JS 直接复用内存文档完成切换（免 C++ toHtml 与 HTML 传输）；
        //   已编辑：返回 MD → C++ toHtml → exitSourceMode(html) 全量重建
        // 模式切换计时（快速路径总计 / 慢路径 JS读取→toHtml→JS重建），状态栏反馈用
        QElapsedTimer perfTotal;
        perfTotal.start();
        tab->webView()->page()->runJavaScript(
            QStringLiteral("window.editorApi.exitSourceModeOrGetMd()"),
            [this, tab, perfTotal](const QVariant &result) {
                // QVariant 类型区分：JS true（快速路径完成）vs string（待转换 MD）
                const bool fastPath = result.userType() == QMetaType::Bool && result.toBool();
                auto finishExit = [this, tab](const QString &msg) {
                    tab->setSourceMode(false);
                    m_toolBar->setSourceMode(false);
                    m_statusBar->setSourceMode(false);
                    if (QAction *act = m_menuBar->findChild<QAction *>(
                            QStringLiteral("sourceModeAction")))
                        act->setChecked(false);
                    qWarning() << msg;
                    m_statusBar->showMessage(msg, 6000);
                    m_sourceModeToggling = false;
                };
                if (fastPath) {
                    finishExit(QStringLiteral("【计时】退出源码: 快速路径（未编辑，免转换）· 总计 %1ms")
                                   .arg(perfTotal.elapsed()));
                    return;
                }
                QString md = result.toString();
                const qint64 jsReadMs = perfTotal.elapsed();
                QElapsedTimer conv;
                conv.start();
                QString html = MarkdownHandler::toHtml(md);
                const qint64 convMs = conv.elapsed();
                QString escaped = JsEscape::forTemplateLiteral(html);
                QElapsedTimer jsBuild;
                jsBuild.start();
                tab->webView()->page()->runJavaScript(
                    QString("window.editorApi.exitSourceMode(`%1`);").arg(escaped),
                    [this, tab, jsReadMs, convMs, jsBuild, perfTotal, finishExit](const QVariant &) {
                        const qint64 buildMs = jsBuild.elapsed() - 15; // 减去经验 IPC 开销
                        finishExit(QStringLiteral(
                                       "【计时】退出源码: JS读取 %1ms · toHtml %2ms · JS重建 %3ms · 总计 %4ms")
                                       .arg(jsReadMs).arg(convMs).arg(qMax<qint64>(buildMs, 0))
                                       .arg(perfTotal.elapsed()));
                    });
            });
    }
}

// 导出默认文件名：已命名文件取文件基名（去路径与后缀），未命名（未保存）用 untitled
static QString exportDefaultName(const QString &filePath, const QString &ext)
{
    QString base = filePath.isEmpty() ? QStringLiteral("untitled")
                                      : QFileInfo(filePath).completeBaseName();
    if (base.isEmpty()) base = QStringLiteral("untitled");   // 如 ".md" 这类隐藏名
    return base + "." + ext;
}

// 导出用时后缀：<1s 整数毫秒；≥1s 秒（四舍五入 1 位小数）。
// 用 translate 而非 tr：文件内静态自由函数无类 tr 上下文，显式挂 MainWindow
static QString exportTimeSuffix(qint64 elapsedMs)
{
    if (elapsedMs < 1000)
        return QCoreApplication::translate("MainWindow", "%1 毫秒").arg(elapsedMs);
    return QCoreApplication::translate("MainWindow", "%1 秒")
        .arg(QString::number(elapsedMs / 1000.0, 'f', 1));
}

void MainWindow::onExportPdf()
{
    EditorTab *tab = m_tabWidget->currentEditorTab();
    if (!tab) return;

    // 先弹出保存对话框
    QString filePath = tab->webView()->property("filePath").toString();
    QString path = QFileDialog::getSaveFileName(this, tr("导出 PDF"),
                                                exportDefaultName(filePath, "pdf"),
                                                tr("PDF 文件 (*.pdf)"));
    if (path.isEmpty()) return;

    // 文档目录（解析相对图片路径）；未保存文档无相对图片（插入图片须先保存），空 filePath 回退 CWD 亦无影响
    QString docDir = QFileInfo(filePath).absolutePath();

    // 异步获取编辑器 HTML 内容（源码模式下取 source-view 当前内容经 toHtml，
    // 否则导出的是进入源码前的旧快照）
    ExportHtmlAsync::get(tab, [this, path, docDir](const QString &html) {
            // 用时从内容就绪（进度对话框出现）起算，到 exportFinished
            auto start = std::make_shared<QElapsedTimer>();
            start->start();
            // 进度对话框（2026-09-21）：paged.js 大文档导出以十秒计，静默等待
            // 会让用户误以为卡死而随手关程序——模态展示「正在导出」，导出完成
            // 前不可关闭；成功自动关闭，失败转错误态等用户确认。QPointer 防
            // 对话框自销毁后迟到信号触空（WA_DeleteOnClose）
            QPointer<ExportProgressDialog> dlg(new ExportProgressDialog(this));
            dlg->show();
            auto *exporter = new PdfExporter(this);
            // 失败路径（空内容/引擎故障）须有反馈且回收导出器（其持有隐藏
            // 渲染视图，滞留即整个渲染进程泄漏到窗口关闭）
            connect(exporter, &PdfExporter::exportError, this,
                    [this, exporter, dlg](const QString &err) {
                m_statusBar->showMessage(tr("导出失败: %1").arg(err), 5000);
                if (dlg) dlg->showError(err);
                exporter->deleteLater();
            });
            connect(exporter, &PdfExporter::exportFinished, this,
                    [this, exporter, dlg, start](bool ok, const QString &out) {
                // 本条是状态栏最后一条消息：走回退时并入回退说明并延长展示——
                // pagedFallbackUsed 的独立提示早于 printToPdf 触发，1-3s 后即被
                // 本条覆盖，用户须从这里得知产物无页眉页脚
                if (ok && exporter->fallbackUsed())
                    m_statusBar->showMessage(
                        tr("已导出: %1（分页引擎回退，本次无页眉页脚）, 用时 %2")
                            .arg(out, exportTimeSuffix(start->elapsed())), 6000);
                else
                    m_statusBar->showMessage(
                        ok ? tr("已导出: %1, 用时 %2").arg(out, exportTimeSuffix(start->elapsed()))
                           : tr("导出失败"), 3000);
                if (dlg) {
                    if (ok)
                        dlg->showFinished();
                    else
                        dlg->showError(tr("PDF 生成失败"));
                }
                exporter->deleteLater();
            });
            // paged.js 失败/超时回退原生分页——打印进行中的过渡提示（最终说明
            // 见上方 exportFinished 的组合消息）
            connect(exporter, &PdfExporter::pagedFallbackUsed, this, [this]() {
                m_statusBar->showMessage(
                    tr("页眉页脚引擎失败，正在按普通版式导出（无页眉页脚）"), 6000);
            });
            // 分页进度：大文档分页以十秒计（2026-09-15 取证 ~0.8s/页），静默
            // 等待体感差——页数增长即刷新提示，导出完成后被结果消息覆盖
            connect(exporter, &PdfExporter::pagedProgress, this,
                    [this, dlg](int pages) {
                m_statusBar->showMessage(
                    tr("正在分页：已生成 %1 页…").arg(pages), 3000);
                if (dlg) dlg->showPagedProgress(pages);
            });
            // 写盘阶段（printToPdf 前）：total>0 展示总页数，0 表示页数未知
            connect(exporter, &PdfExporter::writingPdf, this,
                    [dlg](int total) {
                if (dlg) dlg->showWriting(total);
            });
            exporter->exportToPdf(html, docDir, path);
        }, ExportHtmlAsync::Mode::Pdf);
}

void MainWindow::onExportDocx()
{
    EditorTab *tab = m_tabWidget->currentEditorTab();
    if (!tab) return;

    QString filePath = tab->webView()->property("filePath").toString();
    QString path = QFileDialog::getSaveFileName(this, tr("导出 DOCX"),
                                                exportDefaultName(filePath, "docx"),
                                                tr("Word 文档 (*.docx)"));
    if (path.isEmpty()) return;

    // 文档目录（解析相对图片路径）；未保存文档无相对图片（插入图片须先保存），空 filePath 回退 CWD 亦无影响
    QString docDir = QFileInfo(filePath).absolutePath();

    // 异步获取编辑器 HTML 内容（源码模式下取 source-view 当前内容经 toHtml，
    // 否则导出的是进入源码前的旧快照）
    ExportHtmlAsync::get(tab, [this, path, docDir](const QString &html) {
            // 用时从内容就绪（进度起点）起算：含列宽归一 + mermaid/公式 + 写盘
            auto start = std::make_shared<QElapsedTimer>();
            start->start();
            auto *exporter = new DocxExporter(this);
            // 失败路径（空内容/生成失败/mermaid 挂死）须有反馈且回收导出器
            connect(exporter, &DocxExporter::exportError, this,
                    [this, exporter](const QString &err) {
                        m_statusBar->showMessage(tr("导出失败: %1").arg(err), 5000);
                        exporter->deleteLater();
                    });
            connect(exporter, &DocxExporter::exportFinished, this,
                    [this, exporter, start](bool ok, const QString &out) {
                        m_statusBar->showMessage(
                            ok ? tr("已导出: %1, 用时 %2").arg(out, exportTimeSuffix(start->elapsed()))
                               : tr("导出失败"), 3000);
                        exporter->deleteLater();
                    });
            // 表格列宽归一（与 PDF 同构）：编辑器 live colgroup 是窗口宽基准，
            // 直接缩放到 Word 页宽会短列一字一行/长列独占——经隐藏页按 DOCX
            // 基准（页宽 avail/11pt/tblCellMar 边距）重测重分配。无表格直通
            // （免页面加载）；归一失败（空串）降级原 html（live colgroup 缩放
            // 路径仍在 DocxExporter 内，行为同改造前）
            if (!html.contains(QLatin1String("<table"))) {
                exporter->exportToDocx(html, docDir, path);
                return;
            }
            auto *norm = TableColumnNormalizer::instance();
            // busy（上一轮未结算）返回 0 且不发信号：就地降级直出原 html。
            // 接受路径无同步发射（setHtml→loadFinished 恒异步），先取 id 再连接；
            // 并发双导出共享单例时，各单按 id 匹配消费自己的结果，不互抢
            const quint64 normId = norm->normalize(html, docDir,
                                                   ExportCommon::pageContentWidthTwips());
            if (normId == 0) {
                exporter->exportToDocx(html, docDir, path);
                return;
            }
            auto conn = std::make_shared<QMetaObject::Connection>();
            *conn = connect(norm, &TableColumnNormalizer::finished, this,
                    [conn, exporter, html, docDir, path, normId](
                            quint64 requestId, const QString &normalized) {
                        // 他轮请求的结果：不是本单，不消费不断连
                        if (requestId != normId) return;
                        QObject::disconnect(*conn);   // 单发：只消费一次结果
                        exporter->exportToDocx(normalized.isEmpty() ? html : normalized,
                                               docDir, path);
                    });
        },
        ExportHtmlAsync::Mode::Docx);
}

void MainWindow::onExportHtml()
{
    EditorTab *tab = m_tabWidget->currentEditorTab();
    if (!tab) return;

    QString filePath = tab->webView()->property("filePath").toString();
    QString path = QFileDialog::getSaveFileName(this, tr("导出 HTML"),
                                                exportDefaultName(filePath, "html"),
                                                tr("HTML 文件 (*.html)"));
    if (path.isEmpty()) return;

    // 文档目录（解析相对图片路径）；未保存文档无相对图片（插入图片须先保存），空 filePath 回退 CWD 亦无影响
    QString docDir = QFileInfo(filePath).absolutePath();
    QString theme = Application::instance()->themeManager()->currentTheme();

    // 源码模式下取 source-view 当前内容经 toHtml，否则导出的是进入源码前的旧快照
    ExportHtmlAsync::get(tab, [this, path, docDir, theme](const QString &html) {
            // 用时从内容就绪起算，到 exportFinished
            auto start = std::make_shared<QElapsedTimer>();
            start->start();
            auto *exporter = new HtmlExporter(this);
            connect(exporter, &HtmlExporter::exportError, this,
                    [this, exporter](const QString &err) {
                m_statusBar->showMessage(tr("导出失败: %1").arg(err), 5000);
                exporter->deleteLater();
            });
            connect(exporter, &HtmlExporter::exportFinished, this,
                    [this, exporter, start](bool ok, const QString &out) {
                        m_statusBar->showMessage(
                            ok ? tr("已导出: %1, 用时 %2").arg(out, exportTimeSuffix(start->elapsed()))
                               : tr("导出失败"), 3000);
                        exporter->deleteLater();
                    });
            exporter->exportToHtml(html, docDir, path, theme);
        });
}

void MainWindow::onThemeSwitch(const QString &mode)
{
    m_themeMode = mode;
    // system 模式解析为当前系统实际主题；light/dark 原样使用
    QString actual = (mode == "system")
                         ? m_systemThemeWatcher->currentSystemTheme()
                         : mode;
    Application::instance()->themeManager()->switchTheme(actual);
    m_toolBar->refreshThemeIcons(actual);
    // 保存偏好模式（存的不是实际主题）
    QSettings settings("md-editor", "md-editor");
    settings.setValue("appearance/theme", mode);
    settings.sync();
    for (int i = 0; i < m_tabWidget->count(); ++i) {
        EditorTab *tab = m_tabWidget->tabAt(i);
        if (tab) tab->setEditorTheme(actual);
    }
    m_menuBar->setThemeChecked(mode);
}

void MainWindow::retranslateUi()
{
    m_menuBar->retranslateUi();
    m_toolBar->retranslateUi();
    m_statusBar->retranslateUi();
    m_sideBar->retranslateUi();
    m_sideBar->fileTree()->retranslateUi();
    m_sideBar->outlineTree()->retranslateUi();
    m_tabWidget->retranslateUi();
    m_searchBar->retranslateUi();
}

void MainWindow::onLanguageSwitch(const QString &lang)
{
    Application::instance()->languageManager()->switchLanguage(lang);  // 装/卸 translator + 持久化 + 发信号
    retranslateUi();
    for (int i = 0; i < m_tabWidget->count(); ++i) {
        EditorTab *tab = m_tabWidget->tabAt(i);
        if (tab) tab->setEditorLanguage(lang);
    }
}

// === 文件操作 ===

void MainWindow::onOpenFolder()
{
    QString path = QFileDialog::getExistingDirectory(this, tr("打开文件夹"), "");
    if (!path.isEmpty()) {
        m_sideBar->fileTree()->setRootPath(path);
        m_statusBar->showMessage(tr("已打开文件夹: %1").arg(path), 3000);
    }
}

// ≥2MB 大文件软警告（手动打开与会话恢复共用）。
// 仅 2MB~10MB 且非二进制时提示：超上限 readFile 必拒、二进制有专门报错，
// 先问「是否仍要打开」只会造成双重弹窗
bool MainWindow::confirmLargeFile(const QString &path)
{
    const qint64 size = QFileInfo(path).size();
    if (size < FileManager::kLargeFileWarnBytes || size > FileManager::kMaxReadBytes
        || FileManager::isBinaryFile(path))
        return true;
    QMessageBox box(QMessageBox::Question, tr("文件较大"),
                    tr("文件 %1 大小为 %2 MB，打开后可能明显卡顿。\n是否仍要打开？")
                        .arg(path, QString::number(size / 1024.0 / 1024.0, 'f', 1)),
                    QMessageBox::Yes | QMessageBox::No, this);
    box.button(QMessageBox::Yes)->setText(tr("是"));
    box.button(QMessageBox::No)->setText(tr("否"));
    return box.exec() == QMessageBox::Yes;
}

void MainWindow::onFileDoubleClicked(const QString &path)
{
    // 已打开的文件：聚焦既有标签页即可（同一路径一标签，与主流编辑器一致）
    // ——跳过大文件确认与重复读盘，未保存修改保留在原 tab。
    // currentChanged 未触发（本就是当前 tab）时状态栏可能停留在他页，显式同步
    if (m_tabWidget->focusFileTab(path)) {
        m_menuBar->addRecentFile(path);
        m_statusBar->updateFilePath(path);
        refreshEncodingLabel();
        return;
    }

    // 软警告层：2MB~10MB 的大文件经确认后才打开（与会话恢复共用 confirmLargeFile）
    if (!confirmLargeFile(path))
        return;

    // 统一经 FileManager 读入：编码自动检测（BOM → UTF-8 → 系统编码回退）+
    // 大小守卫。读失败（null）已由 fileReadError 信号统一弹窗，不开空标签页
    // ——空内容标签页误保存会清空原文件
    QString encoding;
    QString content = m_fileManager->readFile(path, &encoding);
    if (content.isNull())
        return;

    EditorTab *tab = m_tabWidget->openFileTab(path, content);
    tab->setFilePath(path);
    tab->setEncoding(encoding);
    m_fileManager->watchFile(path);
    m_menuBar->addRecentFile(path);
    m_statusBar->updateFilePath(path);
    refreshEncodingLabel();
}

void MainWindow::onNewFile()
{
    m_tabWidget->openNewTab();
}

// ── selftest 专用入口（--selftest，无头自测试；见 app/SelfTestRunner.cpp）──
EditorTab *MainWindow::selfTestCurrentTab() { return m_tabWidget->currentEditorTab(); }
void MainWindow::selfTestToggleSourceMode() { onToggleSourceMode(); }

void MainWindow::writeTabToFile(EditorTab *tab, const QString &path, bool setPathAfter,
                                 std::function<void(bool)> cb)
{
    if (!tab)
        return;
    // 异步获取内容 → 写入。快照携带文档版本：落盘后 markCleanIfUnchanged 比对，
    // 保存窗口（getContent→writeFile 异步间隙）内再编辑则不清脏标记——旧
    // markClean 无条件清，窗口内的击键静默失去未保存指示（关闭不再提示）。
    // 源码模式下编辑器文档是进入源码前的旧快照：取 source-view 内容——本身即
    // MD，无需 fromHtml；否则源码编辑被静默丢弃
    const bool sourceMode = tab->isSourceMode();
    tab->webView()->page()->runJavaScript(
        sourceMode ? QStringLiteral("window.editorApi.getSourceSnapshot();")
                   : QStringLiteral("window.editorApi.getSaveSnapshot();"),
        [this, tab, path, setPathAfter, sourceMode, cb](const QVariant &result) {
            const QVariantMap snap = result.toMap();
            QString md = sourceMode ? snap.value(QStringLiteral("source")).toString()
                                    : MarkdownHandler::fromHtml(snap.value(QStringLiteral("html")).toString());
            const int docVersion = snap.value(QStringLiteral("version"), -1).toInt();
            const bool ok = m_fileManager->writeFile(path, md);
            if (ok) {
                m_statusBar->showMessage(tr("已保存"), 2000);
                tab->runJsCommand(
                    QStringLiteral("window.editorApi.markCleanIfUnchanged(%1);").arg(docVersion));
                if (setPathAfter)
                    tab->setFilePath(path);
                int idx = m_tabWidget->indexOf(tab);
                if (idx >= 0) {
                    m_tabWidget->markTabSaved(idx, path);
                }
                m_tabWidget->completePendingTabClose();
            } else {
                m_statusBar->showMessage(tr("保存失败"), 3000);
                // 模态警告框：状态栏 3 秒小字在真实故障（磁盘满/只读目录/权限）
                // 下等于没有提示（用户实测向 /usr 另存「没有任何提示」）
                QMessageBox::warning(this, tr("保存失败"),
                    tr("无法写入 %1：\n%2").arg(path,
                        m_lastWriteError.isEmpty() ? tr("未知错误") : m_lastWriteError));
            }
            if (cb)
                cb(ok);
        });
}

void MainWindow::onSaveFile()
{
    EditorTab *tab = m_tabWidget->currentEditorTab();
    if (!tab) return;

    QString filePath = tab->webView()->property("filePath").toString();
    if (filePath.isEmpty() || !QFile::exists(filePath)) {
        onSaveFileAs();
        return;
    }
    writeTabToFile(tab, filePath, false, {});
}

void MainWindow::onSaveFileAs()
{
    EditorTab *tab = m_tabWidget->currentEditorTab();
    if (!tab) return;

    QString path = QFileDialog::getSaveFileName(this, tr("另存为"), "untitled.md",
                                                 tr("Markdown 文件 (*.md *.markdown)"));
    if (path.isEmpty()) return;

    writeTabToFile(tab, path, true, {});
}

void MainWindow::onFormatChanged(const QJsonObject &format, int /*tabId*/)
{
    m_toolBar->updateFormatState(format);
    // 同步表格内状态到当前标签的右键菜单
    EditorTab *tab = m_tabWidget->currentEditorTab();
    if (tab && format.contains("table"))
        tab->setInTable(format["table"].toBool());
    if (tab && format.contains("hasSelection"))
        tab->setHasSelection(format["hasSelection"].toBool());
    if (tab && format.contains("linkUrl"))
        tab->setLinkUrl(format["linkUrl"].toString());
}

void MainWindow::onShowSettings()
{
    SettingsDialog dialog(m_fileManager, this);
    connect(&dialog, &SettingsDialog::themeModeChanged,
            this, &MainWindow::onThemeSwitch);
    connect(&dialog, &SettingsDialog::languageChanged,
            this, [this, &dialog](const QString &lang) {
        onLanguageSwitch(lang);
        dialog.retranslateUi();  // 对话框自身随语言切换即时刷新
    });
    dialog.exec();
    applyEditorFont();
    applyEditorWidth();
    applyEditorSettings();
    applyCustomCss();
}

void MainWindow::onShowMarkdownGuide()
{
    QFile f(":/web/markdown-guide.md");
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return;
    QString md = QString::fromUtf8(f.readAll());
    f.close();

    EditorTab *tab = m_tabWidget->openHelpTab(tr("Markdown 语法"), md);
    tab->setPermanentReadOnly(true);
    // 页面加载后锁为只读
    connect(tab, &EditorTab::pageLoadFinished, this, [tab](bool ok) {
        if (!ok) return;
        tab->runJsCommand("window.editorApi.setEditable(false);");
    });
}

void MainWindow::applyEditorFont()
{
    QSettings settings("md-editor", "md-editor");
    QString family = settings.value("editor/font", "Sans Serif").toString();
    int size = settings.value("editor/fontSize", 18).toInt();
    for (int i = 0; i < m_tabWidget->count(); ++i) {
        EditorTab *tab = m_tabWidget->tabAt(i);
        if (tab) tab->setEditorFont(family, size);
    }
}

void MainWindow::applyEditorWidth()
{
    QSettings settings("md-editor", "md-editor");
    int percent = settings.value("editor/width", 80).toInt();
    for (int i = 0; i < m_tabWidget->count(); ++i) {
        EditorTab *tab = m_tabWidget->tabAt(i);
        if (tab) tab->setEditorWidth(percent);
    }
}

// === 会话保存与恢复 ===

void MainWindow::applyEditorSettings()
{
    QSettings settings("md-editor", "md-editor");
    QJsonObject s;
    s["lineNumbers"] = settings.value("editor/lineNumbers", false).toBool();
    s["wordWrap"] = settings.value("editor/wordWrap", true).toBool();
    s["tabSize"] = settings.value("editor/tabSize", 4).toInt();
    for (int i = 0; i < m_tabWidget->count(); ++i) {
        EditorTab *tab = m_tabWidget->tabAt(i);
        if (tab) tab->setEditorSettings(s);
    }
}

void MainWindow::applyCustomCss()
{
    QSettings settings("md-editor", "md-editor");
    QString css = settings.value("editor/customCss").toString();
    for (int i = 0; i < m_tabWidget->count(); ++i) {
        EditorTab *tab = m_tabWidget->tabAt(i);
        if (tab) tab->applyCustomCss(css);
    }
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    // 异步保存队列进行中：忽略再次关闭请求（等队列完成后主动 close()）
    if (m_closeSavePending) {
        event->ignore();
        return;
    }

    // 检查所有标签页是否有未保存的更改
    QList<EditorTab *> toSave;
    for (int i = 0; i < m_tabWidget->count(); ++i) {
        QString title = m_tabWidget->tabText(i);
        if (title.startsWith("● ")) {
            QMessageBox box(QMessageBox::Question, tr("未保存的更改"),
                tr("'%1' 有未保存的更改。\n\n是否保存后关闭？").arg(title),
                QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, this);
            box.button(QMessageBox::Save)->setText(tr("保存"));
            box.button(QMessageBox::Discard)->setText(tr("不保存并关闭"));
            box.button(QMessageBox::Cancel)->setText(tr("取消"));
            auto r = box.exec();
            if (r == QMessageBox::Cancel) {
                event->ignore();
                return;
            }
            if (r == QMessageBox::Save) {
                EditorTab *tab = m_tabWidget->tabAt(i);
                if (tab)
                    toSave << tab;
            }
        }
    }

    if (toSave.isEmpty()) {
        saveSession();
        QMainWindow::closeEvent(event);
        return;
    }

    // 逐 tab 异步保存，全部落盘后再真正关闭。旧实现调 onSaveFile()（异步）
    // 后立即 accept：窗口随事件循环销毁，runJavaScript 回调不再执行——
    // 用户点了「保存」但文件未落盘
    event->ignore();
    m_closeSavePending = true;
    m_closeSaveQueue = toSave;
    processCloseSaveQueue();
}

void MainWindow::processCloseSaveQueue()
{
    while (!m_closeSaveQueue.isEmpty()) {
        EditorTab *tab = m_closeSaveQueue.takeFirst();
        if (m_tabWidget->indexOf(tab) < 0)
            continue;   // 队列处理期间该 tab 已被关闭
        QString filePath = tab->webView()->property("filePath").toString();
        if (filePath.isEmpty() || !QFile::exists(filePath)) {
            // 无路径的脏 tab：走另存为对话框；取消视为放弃关闭
            QString path = QFileDialog::getSaveFileName(this, tr("另存为"),
                                                        "untitled.md",
                                                        tr("Markdown 文件 (*.md *.markdown)"));
            if (path.isEmpty()) {
                abortCloseSaves();
                return;
            }
            writeTabToFile(tab, path, true, [this](bool) { processCloseSaveQueue(); });
        } else {
            writeTabToFile(tab, filePath, false, [this](bool ok) {
                if (!ok) {
                    abortCloseSaves();   // 保存失败：留在应用（状态栏已报错）
                    return;
                }
                processCloseSaveQueue();
            });
        }
        return;   // 异步等待回调续跑队列
    }
    // 队列清空：全部落盘，真正关闭
    m_closeSavePending = false;
    saveSession();
    close();
}

void MainWindow::abortCloseSaves()
{
    m_closeSavePending = false;
    m_closeSaveQueue.clear();
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls()) {
        for (const QUrl &url : event->mimeData()->urls()) {
            if (url.isLocalFile() && url.toLocalFile().endsWith(".md", Qt::CaseInsensitive)) {
                event->acceptProposedAction();
                return;
            }
        }
    }
    // 非 .md 文件交给基类/QWebEngineView 处理
    QMainWindow::dragEnterEvent(event);
}

void MainWindow::dropEvent(QDropEvent *event)
{
    if (event->mimeData()->hasUrls()) {
        for (const QUrl &url : event->mimeData()->urls()) {
            QString path = url.toLocalFile();
            if (url.isLocalFile() && path.endsWith(".md", Qt::CaseInsensitive)) {
                onFileDoubleClicked(path);
                event->acceptProposedAction();
                return;
            }
        }
    }
    QMainWindow::dropEvent(event);
}

void MainWindow::saveSession()
{
    QSettings settings("md-editor", "md-editor");

    // 保存打开的文件列表（跳过未命名的空白标签页）
    QStringList openFiles;
    for (int i = 0; i < m_tabWidget->count(); ++i) {
        QString path = m_tabWidget->tabToolTip(i);
        if (!path.isEmpty())
            openFiles << path;
    }
    settings.setValue("session/openFiles", openFiles);

    // 保存当前活动标签页
    settings.setValue("session/activeTab", m_tabWidget->currentIndex());

    // 保存窗口几何信息
    settings.setValue("session/geometry", saveGeometry());

    settings.sync();
}

void MainWindow::restoreSession()
{
    QSettings settings("md-editor", "md-editor");

    // 仅在设置中启用「启动时恢复上次会话」时恢复
    if (!settings.value("session/restore", true).toBool())
        return;

    const QStringList openFiles = settings.value("session/openFiles").toStringList();
    // 两段式恢复：先筛出存在的文件，再按活动标签位置决定立即/惰性加载
    QStringList restoreList;
    for (const QString &path : openFiles) {
        if (!path.isEmpty() && QFile::exists(path))
            restoreList << path;   // 跳过未命名的空白标签
    }
    if (restoreList.isEmpty()) {
        m_tabWidget->openNewTab();
        return;
    }

    // 非活动标签页惰性加载：启动只加载当前要看的标签页内容，其余标签页登记
    // 待载内容、切到时才加载（重文档会话免多页并行 setContent 的启动停顿）
    // 活动标签按路径定位（而非下标）：读失败条目跳过后标签下标会整体前移，
    // 按下标会激活错位的标签页
    const QString activePath = openFiles.value(
        qBound(0, settings.value("session/activeTab", 0).toInt(),
               qMax(openFiles.size() - 1, 0)));
    // 读失败（文件损坏/超 10MB 上限）的条目跳过，不开空标签页——空内容
    // 标签页误保存会清空原文件；启动期错误弹窗静默（避免弹窗轰炸）
    m_suppressReadErrors = true;
    QList<QPair<EditorTab *, QString>> lazyTabs;
    for (const QString &path : restoreList) {
        // 大文件软警告与会话恢复一致：拒绝则跳过该文件（不开标签/不 watch/不进最近）
        if (!confirmLargeFile(path))
            continue;
        QString encoding;
        QString content = m_fileManager->readFile(path, &encoding);
        if (content.isNull())
            continue;
        const bool isActive = (path == activePath);
        EditorTab *tab = m_tabWidget->openFileTab(path, isActive ? content : QString(),
                                                  !isActive);
        tab->setFilePath(path);
        tab->setEncoding(encoding);
        m_fileManager->watchFile(path);
        m_menuBar->addRecentFile(path);
        if (!isActive) lazyTabs.append(qMakePair(tab, content));
    }
    m_suppressReadErrors = false;

    // 全部条目读失败 → 按无会话处理，开空白标签页
    if (m_tabWidget->count() == 0) {
        m_tabWidget->openNewTab();
        return;
    }

    // 恢复活动标签页，随后登记惰性内容（避免 openFileTab 过程中的临时激活误触发加载）
    for (int i = 0; i < m_tabWidget->count(); ++i) {
        if (m_tabWidget->tabAt(i)->webView()->property("filePath").toString() == activePath) {
            m_tabWidget->setCurrentIndex(i);
            break;
        }
    }
    for (const auto &p : lazyTabs)
        p.first->setPendingContent(p.second);

    // 恢复窗口几何信息
    QByteArray geometry = settings.value("session/geometry").toByteArray();
    if (!geometry.isEmpty()) {
        restoreGeometry(geometry);
    }
}
