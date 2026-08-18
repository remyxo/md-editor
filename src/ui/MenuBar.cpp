#include "MenuBar.h"
#include <QAction>
#include <QActionGroup>
#include <QKeySequence>
#include <QSettings>
#include <QFileInfo>

MenuBar::MenuBar(QWidget *parent)
    : QMenuBar(parent)
{
    setupUi();
}

void MenuBar::setupUi()
{
    // === 文件菜单 ===
    m_fileMenu = addMenu(tr("文件(&F)"));

    m_newAct = m_fileMenu->addAction(tr("新建(&N)"));
    m_newAct->setShortcut(QKeySequence::New);
    connect(m_newAct, &QAction::triggered, this, &MenuBar::newFile);

    m_openAct = m_fileMenu->addAction(tr("打开文件(&O)..."));
    m_openAct->setShortcut(QKeySequence::Open);
    connect(m_openAct, &QAction::triggered, this, &MenuBar::openFile);

    m_openFolderAct = m_fileMenu->addAction(tr("打开文件夹..."));
    m_openFolderAct->setShortcut(QKeySequence("Ctrl+Shift+O"));
    connect(m_openFolderAct, &QAction::triggered, this, &MenuBar::openFolder);

    m_recentFilesMenu = m_fileMenu->addMenu(tr("最近文件"));
    m_fileMenu->addSeparator();

    m_saveAct = m_fileMenu->addAction(tr("保存(&S)"));
    m_saveAct->setShortcut(QKeySequence::Save);
    connect(m_saveAct, &QAction::triggered, this, &MenuBar::saveFile);

    m_saveAsAct = m_fileMenu->addAction(tr("另存为..."));
    m_saveAsAct->setShortcut(QKeySequence("Ctrl+Shift+S"));
    connect(m_saveAsAct, &QAction::triggered, this, &MenuBar::saveFileAs);

    m_fileMenu->addSeparator();

    m_exportMenu = m_fileMenu->addMenu(tr("导出"));
    m_exportPdfAct = m_exportMenu->addAction(tr("导出 PDF..."));
    m_exportPdfAct->setShortcut(QKeySequence("Ctrl+P"));
    connect(m_exportPdfAct, &QAction::triggered, this, &MenuBar::exportPdf);
    m_exportDocxAct = m_exportMenu->addAction(tr("导出 DOCX..."));
    connect(m_exportDocxAct, &QAction::triggered, this, &MenuBar::exportDocx);
    m_exportHtmlAct = m_exportMenu->addAction(tr("导出 HTML..."));
    connect(m_exportHtmlAct, &QAction::triggered, this, &MenuBar::exportHtml);

    m_fileMenu->addSeparator();

    m_quitAct = m_fileMenu->addAction(tr("退出(&Q)"));
    m_quitAct->setShortcut(QKeySequence::Quit);
    connect(m_quitAct, &QAction::triggered, this, &MenuBar::quit);

    // === 编辑菜单 ===
    m_editMenu = addMenu(tr("编辑(&E)"));

    m_undoAct = m_editMenu->addAction(tr("撤销(&U)"));
    m_undoAct->setShortcut(QKeySequence::Undo);
    connect(m_undoAct, &QAction::triggered, this, &MenuBar::undo);

    m_redoAct = m_editMenu->addAction(tr("重做(&R)"));
    m_redoAct->setShortcut(QKeySequence::Redo);
    connect(m_redoAct, &QAction::triggered, this, &MenuBar::redo);

    m_findAct = m_editMenu->addAction(tr("搜索(&F)"));
    m_findAct->setShortcut(QKeySequence::Find);
    connect(m_findAct, &QAction::triggered, this, &MenuBar::findRequested);

    m_replaceAct = m_editMenu->addAction(tr("替换(&H)"));
    m_replaceAct->setShortcut(QKeySequence("Ctrl+H"));
    connect(m_replaceAct, &QAction::triggered, this, &MenuBar::replaceRequested);

    // === 视图菜单 ===
    m_viewMenu = addMenu(tr("视图(&V)"));

    m_sidebarAct = m_viewMenu->addAction(tr("侧边栏"));
    m_sidebarAct->setShortcut(QKeySequence("Ctrl+B"));
    m_sidebarAct->setCheckable(true);
    m_sidebarAct->setChecked(true);
    connect(m_sidebarAct, &QAction::triggered, this, &MenuBar::toggleSidebar);

    m_outlineAct = m_viewMenu->addAction(tr("大纲"));
    m_outlineAct->setCheckable(true);
    m_outlineAct->setChecked(true);
    connect(m_outlineAct, &QAction::triggered, this, &MenuBar::toggleOutline);

    m_viewMenu->addSeparator();

    m_sourceAct = m_viewMenu->addAction(tr("源码模式"));
    m_sourceAct->setShortcut(QKeySequence("Ctrl+\\"));
    m_sourceAct->setCheckable(true);
    m_sourceAct->setObjectName("sourceModeAction");
    connect(m_sourceAct, &QAction::triggered, this, &MenuBar::toggleSourceMode);

    m_fullscreenAct = m_viewMenu->addAction(tr("全屏"));
    m_fullscreenAct->setShortcut(QKeySequence("F11"));
    connect(m_fullscreenAct, &QAction::triggered, this, &MenuBar::toggleFullscreen);

    m_viewMenu->addSeparator();

    m_themeMenu = m_viewMenu->addMenu(tr("主题"));
    QActionGroup *themeGroup = new QActionGroup(this);
    themeGroup->setExclusive(true);
    auto addThemeAct = [&](const QString &label, const QString &mode, const QString &checked) {
        QAction *act = m_themeMenu->addAction(label);
        act->setCheckable(true);
        act->setChecked(mode == checked);
        act->setActionGroup(themeGroup);
        connect(act, &QAction::triggered, this, [this, mode]() { emit switchTheme(mode); });
        return act;
    };
    const QString savedMode = [] {
        QSettings settings("md-editor", "md-editor");
        return settings.value("appearance/theme", "light").toString();
    }();
    m_themeActs.clear();
    m_themeActs << addThemeAct(tr("跟随系统"), "system", savedMode)
                << addThemeAct(tr("亮色"), "light", savedMode)
                << addThemeAct(tr("暗色"), "dark", savedMode);

    // === 插入菜单 ===
    m_insertMenu = addMenu(tr("插入(&I)"));

    m_linkAct = m_insertMenu->addAction(tr("链接..."));
    m_linkAct->setShortcut(QKeySequence("Ctrl+K"));
    connect(m_linkAct, &QAction::triggered, this, &MenuBar::insertLink);

    m_imageAct = m_insertMenu->addAction(tr("图片..."));
    m_imageAct->setShortcut(QKeySequence("Ctrl+Shift+I"));
    connect(m_imageAct, &QAction::triggered, this, &MenuBar::insertImage);

    m_tableAct = m_insertMenu->addAction(tr("表格..."));
    connect(m_tableAct, &QAction::triggered, this, &MenuBar::insertTable);

    m_mathAct = m_insertMenu->addAction(tr("插入公式(&M)"));
    m_mathAct->setShortcut(QKeySequence("Ctrl+M"));
    connect(m_mathAct, &QAction::triggered, this, &MenuBar::insertMath);

    m_codeAct = m_insertMenu->addAction(tr("行内代码"));
    connect(m_codeAct, &QAction::triggered, this, &MenuBar::insertCode);

    m_codeBlockAct = m_insertMenu->addAction(tr("代码块"));
    connect(m_codeBlockAct, &QAction::triggered, this, &MenuBar::insertCodeBlock);

    m_hrAct = m_insertMenu->addAction(tr("分割线"));
    connect(m_hrAct, &QAction::triggered, this, &MenuBar::insertHorizontalRule);

    m_emojiAct = m_insertMenu->addAction(tr("表情符号..."));
    connect(m_emojiAct, &QAction::triggered, this, &MenuBar::insertEmoji);

    m_footnoteAct = m_insertMenu->addAction(tr("脚注..."));
    connect(m_footnoteAct, &QAction::triggered, this, &MenuBar::insertFootnote);

    // === 帮助菜单 ===
    m_helpMenu = addMenu(tr("帮助(&H)"));

    m_guideAct = m_helpMenu->addAction(tr("Markdown 语法参考(&G)"));
    connect(m_guideAct, &QAction::triggered, this, &MenuBar::showMarkdownGuide);

    m_settingsAct = m_helpMenu->addAction(tr("设置..."));
    connect(m_settingsAct, &QAction::triggered, this, &MenuBar::showSettings);

    m_helpMenu->addSeparator();

    m_aboutAct = m_helpMenu->addAction(tr("关于..."));
    connect(m_aboutAct, &QAction::triggered, this, &MenuBar::showAbout);

    retranslateUi();  // 统一重设文案（含最近文件菜单的动态文本）
}

void MenuBar::setThemeChecked(const QString &mode)
{
    static const QMap<QString, int> kIndex = {
        {"system", 0}, {"light", 1}, {"dark", 2}};
    const int idx = kIndex.value(mode, 1);
    if (idx < m_themeActs.size())
        m_themeActs[idx]->setChecked(true);
}

void MenuBar::retranslateUi()
{
    m_fileMenu->setTitle(tr("文件(&F)"));
    m_newAct->setText(tr("新建(&N)"));
    m_openAct->setText(tr("打开文件(&O)..."));
    m_openFolderAct->setText(tr("打开文件夹..."));
    m_recentFilesMenu->setTitle(tr("最近文件"));
    m_saveAct->setText(tr("保存(&S)"));
    m_saveAsAct->setText(tr("另存为..."));
    m_exportMenu->setTitle(tr("导出"));
    m_exportPdfAct->setText(tr("导出 PDF..."));
    m_exportDocxAct->setText(tr("导出 DOCX..."));
    m_exportHtmlAct->setText(tr("导出 HTML..."));
    m_quitAct->setText(tr("退出(&Q)"));

    m_editMenu->setTitle(tr("编辑(&E)"));
    m_undoAct->setText(tr("撤销(&U)"));
    m_redoAct->setText(tr("重做(&R)"));
    m_findAct->setText(tr("搜索(&F)"));
    m_replaceAct->setText(tr("替换(&H)"));

    m_viewMenu->setTitle(tr("视图(&V)"));
    m_sidebarAct->setText(tr("侧边栏"));
    m_outlineAct->setText(tr("大纲"));
    m_sourceAct->setText(tr("源码模式"));
    m_fullscreenAct->setText(tr("全屏"));
    m_themeMenu->setTitle(tr("主题"));
    // m_themeActs 顺序：0=system 1=light 2=dark
    m_themeActs[0]->setText(tr("跟随系统"));
    m_themeActs[1]->setText(tr("亮色"));
    m_themeActs[2]->setText(tr("暗色"));

    m_insertMenu->setTitle(tr("插入(&I)"));
    m_linkAct->setText(tr("链接..."));
    m_imageAct->setText(tr("图片..."));
    m_tableAct->setText(tr("表格..."));
    m_mathAct->setText(tr("插入公式(&M)"));
    m_codeAct->setText(tr("行内代码"));
    m_codeBlockAct->setText(tr("代码块"));
    m_hrAct->setText(tr("分割线"));
    m_emojiAct->setText(tr("表情符号..."));
    m_footnoteAct->setText(tr("脚注..."));

    m_helpMenu->setTitle(tr("帮助(&H)"));
    m_guideAct->setText(tr("Markdown 语法参考(&G)"));
    m_settingsAct->setText(tr("设置..."));
    m_aboutAct->setText(tr("关于..."));

    updateRecentFilesMenu();  // 「(无)」「清除最近文件列表」随语言刷新
}

void MenuBar::addRecentFile(const QString &path)
{
    QSettings settings("md-editor", "md-editor");
    QStringList files = settings.value("recentFiles").toStringList();

    // 去重：移除已存在的相同路径
    files.removeAll(path);
    // 插入到最前面
    files.prepend(path);
    // 最多保留 10 条
    while (files.size() > 10) files.removeLast();

    settings.setValue("recentFiles", files);
    settings.sync();

    updateRecentFilesMenu();
}

void MenuBar::loadRecentFiles()
{
    updateRecentFilesMenu();
}

void MenuBar::updateRecentFilesMenu()
{
    m_recentFilesMenu->clear();

    QSettings settings("md-editor", "md-editor");
    QStringList files = settings.value("recentFiles").toStringList();

    // 剔除已不存在的文件
    QStringList valid;
    for (const QString &f : files) {
        if (QFileInfo::exists(f))
            valid.append(f);
    }

    if (valid.isEmpty()) {
        QAction *empty = m_recentFilesMenu->addAction(tr("(无)"));
        empty->setEnabled(false);
    } else {
        for (const QString &f : valid) {
            QAction *act = m_recentFilesMenu->addAction(f);
            connect(act, &QAction::triggered, this, [this, f]() {
                emit openRecentFile(f);
            });
        }
        m_recentFilesMenu->addSeparator();
        QAction *clearAct = m_recentFilesMenu->addAction(tr("清除最近文件列表"));
        connect(clearAct, &QAction::triggered, this, [this]() {
            QSettings s("md-editor", "md-editor");
            s.remove("recentFiles");
            s.sync();
            updateRecentFilesMenu();
        });
    }

    // 更新已保存的有效路径（剔除失效的）
    if (valid.size() != files.size()) {
        settings.setValue("recentFiles", valid);
        settings.sync();
    }
}
