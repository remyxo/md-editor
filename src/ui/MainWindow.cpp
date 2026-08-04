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
#include "../core/EditorBridge.h"
#include "../core/FileManager.h"
#include "../core/MarkdownHandler.h"
#include "../app/Application.h"
#include "../app/ThemeManager.h"

#include <QVBoxLayout>
#include <QFileDialog>
#include <QMessageBox>
#include <QFile>
#include <QJsonObject>
#include <QWebEnginePage>
#include <QTextStream>
#include <QShortcut>
#include <QKeySequence>
#include <QSettings>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    m_fileManager = new FileManager(this);

    setupUi();
    connectSignals();

    // 启动时新建一个空白标签页
    m_tabWidget->openNewTab();

    // 恢复上次会话（在最后调用，确保所有组件已初始化）
    restoreSession();
}

MainWindow::~MainWindow() {}

void MainWindow::setupUi()
{
    setWindowTitle("Markdown Editor");
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
    m_splitter->setSizes({260, 1020});

    centralLayout->addWidget(m_splitter, 1);
    setCentralWidget(central);

    // 状态栏
    m_statusBar = new StatusBar(this);
    setStatusBar(m_statusBar);

    // Ctrl+F 显示/隐藏搜索栏
    QShortcut *findShortcut = new QShortcut(QKeySequence::Find, this);
    connect(findShortcut, &QShortcut::activated, this, [this]() {
        m_searchBar->setVisible(!m_searchBar->isVisible());
        if (m_searchBar->isVisible()) {
            m_searchBar->focusFindField();
        }
    });

    // Esc 隐藏搜索栏
    QShortcut *escShortcut = new QShortcut(QKeySequence("Esc"), this);
    connect(escShortcut, &QShortcut::activated, this, [this]() {
        m_searchBar->setVisible(false);
    });

    // 初始化亮色主题
    Application::instance()->themeManager()->switchTheme("light");
}

void MainWindow::connectSignals()
{
    // === 菜单栏信号 ===
    connect(m_menuBar, &MenuBar::newFile, this, &MainWindow::onNewFile);
    connect(m_menuBar, &MenuBar::openFolder, this, &MainWindow::onOpenFolder);
    connect(m_menuBar, &MenuBar::openFile, this, [this]() {
        QString path = QFileDialog::getOpenFileName(this, "打开文件", "",
                                                     "Markdown 文件 (*.md *.markdown);;所有文件 (*)");
        if (!path.isEmpty()) onFileDoubleClicked(path);
    });
    connect(m_menuBar, &MenuBar::exportPdf, this, &MainWindow::onExportPdf);
    connect(m_menuBar, &MenuBar::switchTheme, this, &MainWindow::onThemeSwitch);
    connect(m_menuBar, &MenuBar::toggleSidebar, this, [this]() {
        m_sideBar->setCollapsed(!m_sideBar->isCollapsed());
    });
    connect(m_menuBar, &MenuBar::toggleSourceMode, this, &MainWindow::onToggleSourceMode);
    connect(m_menuBar, &MenuBar::insertLink, this, &MainWindow::onInsertLink);
    connect(m_menuBar, &MenuBar::insertImage, this, &MainWindow::onInsertImage);
    connect(m_menuBar, &MenuBar::insertTable, this, &MainWindow::onInsertTable);
    connect(m_menuBar, &MenuBar::quit, this, &MainWindow::close);
    connect(m_menuBar, &MenuBar::saveFile, this, &MainWindow::onSaveFile);
    connect(m_menuBar, &MenuBar::saveFileAs, this, &MainWindow::onSaveFileAs);
    connect(m_menuBar, &MenuBar::undo, this, [this]() { runEditorCommand("window.editorApi.undo();"); });
    connect(m_menuBar, &MenuBar::redo, this, [this]() { runEditorCommand("window.editorApi.redo();"); });
    connect(m_menuBar, &MenuBar::showSettings, this, &MainWindow::onShowSettings);

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
    connect(m_toolBar, &ToolBar::horizontalRuleClicked, this, [this]() {
        runEditorCommand("window.editorApi.insertHorizontalRule();");
    });
    connect(m_toolBar, &ToolBar::sourceToggleClicked, this, &MainWindow::onToggleSourceMode);
    connect(m_toolBar, &ToolBar::pdfExportClicked, this, &MainWindow::onExportPdf);

    // === 标签页管理 ===
    connect(m_tabWidget, &TabWidget::editorBridgeCreated,
            this, &MainWindow::onEditorBridgeCreated);
    connect(m_tabWidget, &TabWidget::currentTabChanged,
            this, &MainWindow::onTabChanged);

    // === 文件树 ===
    connect(m_sideBar->fileTree(), &FileTree::fileDoubleClicked,
            this, &MainWindow::onFileDoubleClicked);

    // === 侧边栏「打开文件夹」按钮 ===
    connect(m_sideBar, &SideBar::openFolderClicked,
            this, &MainWindow::onOpenFolder);

    // === 搜索栏 ===
    connect(m_searchBar, &SearchBar::closeRequested, this, [this]() {
        m_searchBar->setVisible(false);
    });
    connect(m_searchBar, &SearchBar::findRequested, this, [this](
            const QString &, bool) {
        m_statusBar->showMessage("查找功能将在后续版本中完善", 3000);
    });
}

void MainWindow::onEditorBridgeCreated(EditorBridge *bridge)
{
    // 连接光标位置更新
    connect(bridge, &EditorBridge::cursorPositionChanged,
            m_statusBar, &StatusBar::updateCursorPosition);

    // 连接大纲更新
    connect(bridge, &EditorBridge::headingsUpdated, this,
            [this](const QJsonArray &headings, int /*tabId*/) {
                m_sideBar->outlineTree()->updateHeadings(headings);
            });

    // 连接格式状态同步 → 工具栏
    connect(bridge, &EditorBridge::formatStateChanged, this,
            &MainWindow::onFormatChanged);
}

void MainWindow::onTabChanged(int tabId)
{
    Q_UNUSED(tabId)
    // 更新当前标签页关联的状态栏和大纲
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
    runEditorCommand("window.editorApi.insertTable(3, 3);");
}

void MainWindow::onInsertLink()
{
    runEditorCommand("window.editorApi.insertLink();");
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
    runEditorCommand("window.editorApi.toggleSourceMode();");
}

void MainWindow::onExportPdf()
{
    EditorTab *tab = m_tabWidget->currentEditorTab();
    if (!tab) return;

    QString path = QFileDialog::getSaveFileName(this, "导出 PDF", "document.pdf",
                                                 "PDF 文件 (*.pdf)");
    if (path.isEmpty()) return;

    // 获取编辑器 HTML 内容，通过隐藏 WebView 导出（后续任务完善）
    tab->runJsCommand("window.editorApi.getContent();");
    m_statusBar->showMessage("PDF 导出功能将在后续版本中完善", 3000);
}

void MainWindow::onThemeSwitch(const QString &theme)
{
    Application::instance()->themeManager()->switchTheme(theme);
    // 同步到所有已打开的标签页
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
    tab->webView()->setProperty("filePath", path);
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
                int idx = m_tabWidget->indexOf(tab);
                if (idx >= 0) {
                    m_tabWidget->markTabSaved(idx, filePath);
                }
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
                tab->webView()->setProperty("filePath", path);
                int idx = m_tabWidget->indexOf(tab);
                if (idx >= 0) {
                    m_tabWidget->markTabSaved(idx, path);
                    m_tabWidget->setTabDirty(idx, false);
                }
            } else {
                m_statusBar->showMessage("保存失败", 3000);
            }
        }
    );
}

void MainWindow::onFormatChanged(const QJsonObject &format, int /*tabId*/)
{
    m_toolBar->updateFormatState(format);
}

void MainWindow::onShowSettings()
{
    SettingsDialog dialog(m_fileManager, this);
    dialog.exec();
}

// === 会话保存与恢复 ===

void MainWindow::closeEvent(QCloseEvent *event)
{
    saveSession();
    QMainWindow::closeEvent(event);
}

void MainWindow::saveSession()
{
    QSettings settings("md-editor", "md-editor");

    // 保存打开的文件列表（未命名标签页保存为空字符串）
    QStringList openFiles;
    for (int i = 0; i < m_tabWidget->count(); ++i) {
        openFiles << m_tabWidget->tabToolTip(i);
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
    if (openFiles.isEmpty())
        return; // 无会话记录，保留启动时的空白标签页

    // 关闭启动时的空白标签页
    if (m_tabWidget->count() > 0) {
        EditorTab *tab = m_tabWidget->tabAt(0);
        m_tabWidget->removeTab(0);
        if (tab) tab->deleteLater();
    }

    // 恢复所有标签页
    for (const QString &path : openFiles) {
        if (path.isEmpty()) {
            // 未命名标签页
            m_tabWidget->openNewTab();
        } else if (QFile::exists(path)) {
            // 文件已存在才恢复，避免弹出错误提示
            QString content = m_fileManager->readFile(path);
            m_tabWidget->openFileTab(path, content);
        }
    }

    // 若所有文件都不存在，保留一个空白标签页
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
