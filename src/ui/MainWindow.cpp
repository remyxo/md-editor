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
#include "../core/EditorBridge.h"
#include "../core/FileManager.h"
#include "../core/MarkdownHandler.h"
#include "../core/ImageManager.h"
#include "../core/PdfExporter.h"
#include "../core/DocxExporter.h"
#include "../app/Application.h"
#include "../app/ThemeManager.h"

#include <QVBoxLayout>
#include <QIcon>
#include <QFile>
#include <QFileDialog>
#include <QMessageBox>
#include <QJsonObject>
#include <QWebEnginePage>
#include <QTextStream>
#include <QSettings>
#include <QShortcut>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QKeySequence>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    m_fileManager = new FileManager(this);

    setupUi();
    connectSignals();

    // 恢复上次会话（无会话时自动新建空白标签页）
    restoreSession();
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

    // Ctrl+F 显示/隐藏搜索栏（仅查找模式）
    QShortcut *findShortcut = new QShortcut(QKeySequence::Find, this);
    connect(findShortcut, &QShortcut::activated, this, [this]() {
        m_searchBar->setVisible(!m_searchBar->isVisible());
        if (m_searchBar->isVisible()) {
            m_searchBar->setReplaceMode(false);
            m_searchBar->focusFindField();
        }
    });

    // Ctrl+H：显示搜索栏 + 展开替换行
    QShortcut *replaceShortcut = new QShortcut(QKeySequence("Ctrl+H"), this);
    connect(replaceShortcut, &QShortcut::activated, this, [this]() {
        m_searchBar->setVisible(true);
        m_searchBar->setReplaceMode(true);
        m_searchBar->focusFindField();
    });

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
            // 关闭 JS 侧的模态面板（Mermaid/数学公式/超链接/语言选择器）
            runEditorCommand("window.editorApi._linkEditor.close();"
                             "window.editorApi._mermaidEditor.close();"
                             "window.editorApi._mathEditor.close();"
                             "window.editorApi._emojiPicker.close();"
                             "window.editorApi._codeLangPicker.close();");
        } else {
            m_searchBar->setVisible(false);
        }
    });

    // Ctrl+M 插入行内公式
    QShortcut *mathInlineShortcut = new QShortcut(QKeySequence("Ctrl+M"), this);
    connect(mathInlineShortcut, &QShortcut::activated, this, [this]() {
        runEditorCommand("window.editorApi.insertMath('inline');");
    });
    // Ctrl+Shift+M 插入块级公式
    QShortcut *mathBlockShortcut = new QShortcut(QKeySequence("Ctrl+Shift+M"), this);
    connect(mathBlockShortcut, &QShortcut::activated, this, [this]() {
        runEditorCommand("window.editorApi.insertMath('block');");
    });

    // 初始化亮色主题
    QSettings settings("md-editor", "md-editor");
    QString savedTheme = settings.value("appearance/theme", "light").toString();
    Application::instance()->themeManager()->switchTheme(savedTheme);
    m_toolBar->refreshThemeIcons(savedTheme);
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
        QString path = QFileDialog::getOpenFileName(this, "打开文件", "",
                                                     "Markdown 文件 (*.md *.markdown);;所有文件 (*)");
        if (!path.isEmpty()) onFileDoubleClicked(path);
    });
    connect(m_menuBar, &MenuBar::openRecentFile, this,
            &MainWindow::onFileDoubleClicked);
    connect(m_menuBar, &MenuBar::exportPdf, this, &MainWindow::onExportPdf);
    connect(m_menuBar, &MenuBar::exportDocx, this, &MainWindow::onExportDocx);
    connect(m_menuBar, &MenuBar::switchTheme, this, &MainWindow::onThemeSwitch);
    connect(m_menuBar, &MenuBar::toggleSidebar, this, [this]() {
        m_sideBar->setCollapsed(!m_sideBar->isCollapsed());
    });
    connect(m_menuBar, &MenuBar::toggleSourceMode, this, &MainWindow::onToggleSourceMode);
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
                    if (!path.isEmpty()) m_fileManager->unwatchFile(path);
                }
            });

    // === 文件外部变更 ===
    connect(m_fileManager, &FileManager::fileExternallyModified, this,
            [this](const QString &path) {
                // 查找打开此文件的标签页
                for (int i = 0; i < m_tabWidget->count(); ++i) {
                    EditorTab *tab = m_tabWidget->tabAt(i);
                    if (tab && tab->webView()->property("filePath").toString() == path) {
                        auto r = QMessageBox::question(this, "文件已变更",
                            QString("文件 %1 已被外部程序修改。\n是否重新加载？").arg(path),
                            QMessageBox::Yes | QMessageBox::No);
                        if (r == QMessageBox::Yes) {
                            QString content = m_fileManager->readFile(path);
                            if (!content.isNull())
                                tab->loadFile(path, content);
                        }
                        break;
                    }
                }
            });

    // === 文件树 ===
    connect(m_sideBar->fileTree(), &FileTree::fileDoubleClicked,
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
    // JS 字符串字面量转义（query / rep 嵌入 runJavaScript）
    auto jsStr = [](const QString &s) {
        QString t(s);
        t.replace("\\", "\\\\").replace("'", "\\'").replace("\n", "\\n");
        return t;
    };
    auto runSearchJs = [this](const QString &jsBody) {
        EditorTab *tab = m_tabWidget->currentEditorTab();
        if (tab) tab->runJsCommand("(function(){var ed=window.editorApi;if(!ed||!ed.editor)return;" + jsBody + "})();");
    };

    connect(m_searchBar, &SearchBar::searchParamsChanged, this,
            [this, &jsStr, &runSearchJs](const QString &text, bool cs, bool rx, bool ww) {
        runSearchJs(QStringLiteral("ed.startSearch('%1',%2,%3,%4);")
                    .arg(jsStr(text)).arg(cs).arg(rx).arg(ww));
    });
    connect(m_searchBar, &SearchBar::findNextRequested, this, [this, &runSearchJs]() { runSearchJs("ed.searchNext();"); });
    connect(m_searchBar, &SearchBar::findPrevRequested, this, [this, &runSearchJs]() { runSearchJs("ed.searchPrev();"); });
    connect(m_searchBar, &SearchBar::replaceRequested, this, [this, &jsStr, &runSearchJs](const QString &rep) {
        runSearchJs(QStringLiteral("ed.replaceOne('%1');").arg(jsStr(rep)));
    });
    connect(m_searchBar, &SearchBar::replaceAllRequested, this, [this, &jsStr, &runSearchJs](const QString &rep) {
        runSearchJs(QStringLiteral("ed.replaceAll('%1');").arg(jsStr(rep)));
    });
    connect(m_searchBar, &SearchBar::closeRequested, this, [this, &runSearchJs]() {
        m_searchBar->setVisible(false);
        runSearchJs("ed.stopSearch();");
    });
}

void MainWindow::onEditorBridgeCreated(EditorBridge *bridge)
{
    // 连接光标位置更新（仅当前标签页）
    connect(bridge, &EditorBridge::cursorPositionChanged, this,
            [this, bridge](int line, int col, int /*tabId*/) {
                EditorTab *cur = m_tabWidget->currentEditorTab();
                if (cur && cur->bridge() == bridge)
                    m_statusBar->updateCursorPosition(line, col, 0);
            });

    // 连接大纲更新
    connect(bridge, &EditorBridge::headingsUpdated, this,
            [this](const QJsonArray &headings, int /*tabId*/) {
                m_sideBar->outlineTree()->updateHeadings(headings);
            });

    // 连接字数统计 → 状态栏（仅当前标签页）
    connect(bridge, &EditorBridge::wordCountUpdated, this,
            [this, bridge](int chars, int words, int /*tabId*/) {
                EditorTab *cur = m_tabWidget->currentEditorTab();
                if (cur && cur->bridge() == bridge)
                    m_statusBar->updateWordCount(chars, words, 0);
            });

    // 连接格式状态同步 → 工具栏
    connect(bridge, &EditorBridge::formatStateChanged, this,
            &MainWindow::onFormatChanged);

    // 连接撤销/重做状态 → 工具栏
    connect(bridge, &EditorBridge::historyStateChanged, this,
            [this](bool canUndo, bool canRedo, int /*tabId*/) {
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
                [this](bool ok) { if (ok) { applyEditorFont(); applyEditorWidth(); applyEditorSettings(); } });
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
    if (QAction *act = m_menuBar->findChild<QAction *>(
            QStringLiteral("sourceModeAction")))
        act->setChecked(srcMode);
    if (tab) {
        tab->setInTable(false);
        // 同步新标签页的格式状态到工具栏
        tab->runJsCommand("if(window.editorApi && window.editorApi.syncFormatState) window.editorApi.syncFormatState();");
    }
    // 切 tab：搜索栏计数重置为新当前 tab 的状态（JS 侧异步回传实际计数）
    if (m_searchBar->isVisible()) {
        m_searchBar->setCount(0, 0);
        runEditorCommand("if(window.editorApi && window.editorApi._notifySearchCount) window.editorApi._notifySearchCount();");
    }
}

EditorBridge *MainWindow::currentBridge() const
{
    EditorTab *tab = m_tabWidget->currentEditorTab();
    return tab ? tab->bridge() : nullptr;
}

void MainWindow::runEditorCommand(const QString &js)
{
    EditorTab *tab = m_tabWidget->currentEditorTab();
    if (tab) {
        tab->runJsCommand(js);
    }
}

// 转义字符串以安全嵌入 JS 模板字面量 `...`（与 EditorTab::loadFile 一致）
static QString escapeForJsTemplate(const QString &s)
{
    QString r = s;
    r.replace("\\", "\\\\");
    r.replace("`", "\\`");
    r.replace("$", "\\$");
    return r;
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

// 转义为单引号 JS 字符串字面量内容（不含外层引号）
static QString jsEscape(const QString &s)
{
    QString out = s;
    out.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    out.replace(QLatin1Char('\''), QStringLiteral("\\'"));
    out.replace(QLatin1Char('\n'), QStringLiteral("\\n"));
    out.replace(QLatin1Char('\r'), QStringLiteral("\\r"));
    return out;
}

// 构造图片落盘结果回调 JS：成功 {ok:true,path}；失败 {ok:false,err}
static QString jsImageSaveResult(const QString &requestId, bool ok,
                                 const QString &path, const QString &err)
{
    if (ok)
        return QStringLiteral("window.editorApi._onImageSaveResult('%1',{ok:true,path:'%2'});")
            .arg(jsEscape(requestId), jsEscape(path));
    return QStringLiteral("window.editorApi._onImageSaveResult('%1',{ok:false,err:'%2'});")
        .arg(jsEscape(requestId), jsEscape(err));
}

void MainWindow::onImageSaveRequested(EditorTab *tab, const QString &requestId,
                                      const QString &base64, const QString &ext,
                                      const QString &suggestedName)
{
    QString filePath = tab->webView()->property("filePath").toString();
    if (filePath.isEmpty() || !QFile::exists(filePath)) {
        m_statusBar->showMessage("请先保存文档再插入图片", 3000);
        tab->runJsCommand(jsImageSaveResult(requestId, false, QString(), QStringLiteral("unsaved")));
        return;
    }
    QByteArray bytes = QByteArray::fromBase64(base64.toUtf8());
    if (bytes.isEmpty()) {
        m_statusBar->showMessage("图片数据为空", 3000);
        tab->runJsCommand(jsImageSaveResult(requestId, false, QString(), QStringLiteral("io")));
        return;
    }
    QString rel = ImageManager::saveImage(filePath, bytes, suggestedName, ext);
    if (rel.isEmpty()) {
        m_statusBar->showMessage("图片保存失败", 3000);
        tab->runJsCommand(jsImageSaveResult(requestId, false, QString(), QStringLiteral("io")));
        return;
    }
    m_statusBar->showMessage("已插入图片", 1500);
    tab->runJsCommand(jsImageSaveResult(requestId, true, rel, QString()));
}

void MainWindow::onInsertImage()
{
    QString path = QFileDialog::getOpenFileName(this, "插入图片", "",
                                                 "图片文件 (*.png *.jpg *.jpeg *.gif *.svg *.webp)");
    if (!path.isEmpty()) {
        QString js = QString("window.editorApi.insertImage('%1');").arg(path);
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
        tab->webView()->page()->runJavaScript(
            QStringLiteral("window.editorApi.getContent()"),
            [this, tab](const QVariant &result) {
                QString html = result.toString();
                QString md = MarkdownHandler::fromHtml(html);
                QString escaped = escapeForJsTemplate(md);
                tab->runJsCommand(
                    QString("window.editorApi.enterSourceMode(`%1`);").arg(escaped));
                tab->setSourceMode(true);
                m_toolBar->setSourceMode(true);
                m_statusBar->setSourceMode(true);
                // 源码模式下 editorApi 搜索方法直接 return，给用户提示
                if (m_searchBar->isVisible())
                    m_statusBar->showMessage("源码模式暂不支持搜索", 3000);
                if (QAction *act = m_menuBar->findChild<QAction *>(
                        QStringLiteral("sourceModeAction")))
                    act->setChecked(true);
                m_sourceModeToggling = false;
            });
    } else {
        // → 退出源码模式：获取 textarea MD → 转换为 HTML → 加载到编辑器
        tab->webView()->page()->runJavaScript(
            QStringLiteral("window.editorApi.getSourceContent()"),
            [this, tab](const QVariant &result) {
                QString md = result.toString();
                QString html = MarkdownHandler::toHtml(md);
                QString escaped = escapeForJsTemplate(html);
                tab->runJsCommand(
                    QString("window.editorApi.exitSourceMode(`%1`);").arg(escaped));
                tab->setSourceMode(false);
                m_toolBar->setSourceMode(false);
                m_statusBar->setSourceMode(false);
                if (QAction *act = m_menuBar->findChild<QAction *>(
                        QStringLiteral("sourceModeAction")))
                    act->setChecked(false);
                m_sourceModeToggling = false;
            });
    }
}

void MainWindow::onExportPdf()
{
    EditorTab *tab = m_tabWidget->currentEditorTab();
    if (!tab) return;

    // 先弹出保存对话框
    QString path = QFileDialog::getSaveFileName(this, "导出 PDF", "document.pdf",
                                                 "PDF 文件 (*.pdf)");
    if (path.isEmpty()) return;

    // 异步获取编辑器 HTML 内容
    tab->webView()->page()->runJavaScript(
        "window.editorApi.getContent();",
        [this, path](const QVariant &result) {
            QString html = result.toString();
            auto *exporter = new PdfExporter(this);
            connect(exporter, &PdfExporter::exportFinished, this,
                    [this, exporter](bool ok, const QString &out) {
                m_statusBar->showMessage(ok ? QString("已导出: %1").arg(out) : "导出失败", 3000);
                exporter->deleteLater();
            });
            exporter->exportToPdf(html, path);
        });
}

void MainWindow::onExportDocx()
{
    EditorTab *tab = m_tabWidget->currentEditorTab();
    if (!tab) return;

    QString path = QFileDialog::getSaveFileName(this, "导出 DOCX", "document.docx",
                                                 "Word 文档 (*.docx)");
    if (path.isEmpty()) return;

    // 异步获取编辑器 HTML 内容
    tab->webView()->page()->runJavaScript(
        "window.editorApi.getContent();",
        [this, path](const QVariant &result) {
            QString html = result.toString();
            auto *exporter = new DocxExporter(this);
            connect(exporter, &DocxExporter::exportFinished, this,
                    [this, exporter](bool ok, const QString &out) {
                m_statusBar->showMessage(ok ? QString("已导出: %1").arg(out) : "导出失败", 3000);
                exporter->deleteLater();
            });
            exporter->exportToDocx(html, path);
        });
}

void MainWindow::onThemeSwitch(const QString &theme)
{
    Application::instance()->themeManager()->switchTheme(theme);
    // 保存主题偏好
    QSettings settings("md-editor", "md-editor");
    settings.setValue("appearance/theme", theme);
    settings.sync();
    for (int i = 0; i < m_tabWidget->count(); ++i) {
        EditorTab *tab = m_tabWidget->tabAt(i);
        if (tab) tab->setEditorTheme(theme);
    }
}

// === 文件操作 ===

void MainWindow::onOpenFolder()
{
    QString path = QFileDialog::getExistingDirectory(this, "打开文件夹", "");
    if (!path.isEmpty()) {
        m_sideBar->fileTree()->setRootPath(path);
        m_statusBar->showMessage(QString("已打开文件夹: %1").arg(path), 3000);
    }
}

void MainWindow::onFileDoubleClicked(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::warning(this, "打开失败",
                             QString("无法打开文件: %1").arg(path));
        return;
    }

    QTextStream in(&file);
    // Qt6 QTextStream 默认使用 UTF-8
    QString content = in.readAll();
    file.close();

    EditorTab *tab = m_tabWidget->openFileTab(path, content);
    tab->setFilePath(path);
    m_fileManager->watchFile(path);
    m_menuBar->addRecentFile(path);
    m_statusBar->updateFilePath(path);
}

void MainWindow::onNewFile()
{
    m_tabWidget->openNewTab();
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
    // 异步获取 JS HTML 内容 → 转 Markdown → 写入
    tab->webView()->page()->runJavaScript(
        "window.editorApi.getContent();",
        [this, tab, filePath](const QVariant &result) {
            QString html = result.toString();
            QString md = MarkdownHandler::fromHtml(html);
            if (m_fileManager->writeFile(filePath, md)) {
                m_statusBar->showMessage("已保存", 2000);
                tab->runJsCommand("window.editorApi.markClean();");
                int idx = m_tabWidget->indexOf(tab);
                if (idx >= 0) {
                    m_tabWidget->markTabSaved(idx, filePath);
                }
                m_tabWidget->completePendingTabClose();
            } else {
                m_statusBar->showMessage("保存失败", 3000);
            }
        }
    );
}

void MainWindow::onSaveFileAs()
{
    EditorTab *tab = m_tabWidget->currentEditorTab();
    if (!tab) return;

    QString path = QFileDialog::getSaveFileName(this, "另存为", "untitled.md",
                                                 "Markdown 文件 (*.md *.markdown)");
    if (path.isEmpty()) return;

    tab->webView()->page()->runJavaScript(
        "window.editorApi.getContent();",
        [this, tab, path](const QVariant &result) {
            QString html = result.toString();
            QString md = MarkdownHandler::fromHtml(html);
            if (m_fileManager->writeFile(path, md)) {
                m_statusBar->showMessage("已保存", 2000);
                tab->runJsCommand("window.editorApi.markClean();");
                tab->setFilePath(path);
                int idx = m_tabWidget->indexOf(tab);
                if (idx >= 0) {
                    m_tabWidget->markTabSaved(idx, path);
                }
                m_tabWidget->completePendingTabClose();
            } else {
                m_statusBar->showMessage("保存失败", 3000);
            }
        }
    );
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
    dialog.exec();
    applyEditorFont();
    applyEditorWidth();
    applyEditorSettings();
}

void MainWindow::onShowMarkdownGuide()
{
    QFile f(":/web/markdown-guide.md");
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return;
    QString md = QString::fromUtf8(f.readAll());
    f.close();

    EditorTab *tab = m_tabWidget->openHelpTab("Markdown 语法", md);
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

void MainWindow::closeEvent(QCloseEvent *event)
{
    // 检查所有标签页是否有未保存的更改
    for (int i = 0; i < m_tabWidget->count(); ++i) {
        QString title = m_tabWidget->tabText(i);
        if (title.startsWith("● ")) {
            auto r = QMessageBox::question(this, "未保存的更改",
                QString("'%1' 有未保存的更改。\n\n是否保存后关闭？").arg(title),
                QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
            if (r == QMessageBox::Cancel) {
                event->ignore();
                return;
            }
            if (r == QMessageBox::Save) {
                m_tabWidget->setCurrentIndex(i);
                onSaveFile(); // 保存当前文件
            }
        }
    }
    saveSession();
    QMainWindow::closeEvent(event);
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
    if (openFiles.isEmpty()) {
        m_tabWidget->openNewTab();
        return;
    }

    // 恢复标签页（跳过未命名的空白标签页）
    for (const QString &path : openFiles) {
        if (path.isEmpty()) {
            continue; // 不恢复未命名的空白标签
        } else if (QFile::exists(path)) {
            QString content = m_fileManager->readFile(path);
            EditorTab *tab = m_tabWidget->openFileTab(path, content);
            tab->setFilePath(path);
            m_fileManager->watchFile(path);
            m_menuBar->addRecentFile(path);
        }
    }

    // 若所有文件都不存在或均为空，创建一个空白标签页
    if (m_tabWidget->count() == 0) {
        m_tabWidget->openNewTab();
    }

    // 恢复活动标签页
    int activeTab = settings.value("session/activeTab", 0).toInt();
    if (activeTab >= 0 && activeTab < m_tabWidget->count()) {
        m_tabWidget->setCurrentIndex(activeTab);
    }

    // 恢复窗口几何信息
    QByteArray geometry = settings.value("session/geometry").toByteArray();
    if (!geometry.isEmpty()) {
        restoreGeometry(geometry);
    }
}
