#include "MenuBar.h"
#include <QAction>
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
    m_fileMenu = addMenu("文件(&F)");

    QAction *newAct = m_fileMenu->addAction("新建(&N)");
    newAct->setShortcut(QKeySequence::New);
    connect(newAct, &QAction::triggered, this, &MenuBar::newFile);

    QAction *openAct = m_fileMenu->addAction("打开文件(&O)...");
    openAct->setShortcut(QKeySequence::Open);
    connect(openAct, &QAction::triggered, this, &MenuBar::openFile);

    QAction *openFolderAct = m_fileMenu->addAction("打开文件夹...");
    openFolderAct->setShortcut(QKeySequence("Ctrl+Shift+O"));
    connect(openFolderAct, &QAction::triggered, this, &MenuBar::openFolder);

    m_recentFilesMenu = m_fileMenu->addMenu("最近文件");
    m_fileMenu->addSeparator();

    QAction *saveAct = m_fileMenu->addAction("保存(&S)");
    saveAct->setShortcut(QKeySequence::Save);
    connect(saveAct, &QAction::triggered, this, &MenuBar::saveFile);

    QAction *saveAsAct = m_fileMenu->addAction("另存为...");
    saveAsAct->setShortcut(QKeySequence("Ctrl+Shift+S"));
    connect(saveAsAct, &QAction::triggered, this, &MenuBar::saveFileAs);

    m_fileMenu->addSeparator();

    QMenu *exportMenu = m_fileMenu->addMenu("导出");
    QAction *exportPdfAct = exportMenu->addAction("导出 PDF...");
    exportPdfAct->setShortcut(QKeySequence("Ctrl+P"));
    connect(exportPdfAct, &QAction::triggered, this, &MenuBar::exportPdf);
    QAction *exportDocxAct = exportMenu->addAction("导出 DOCX...");
    connect(exportDocxAct, &QAction::triggered, this, &MenuBar::exportDocx);

    m_fileMenu->addSeparator();

    QAction *quitAct = m_fileMenu->addAction("退出(&Q)");
    quitAct->setShortcut(QKeySequence::Quit);
    connect(quitAct, &QAction::triggered, this, &MenuBar::quit);

    // === 编辑菜单 ===
    m_editMenu = addMenu("编辑(&E)");

    QAction *undoAct = m_editMenu->addAction("撤销(&U)");
    undoAct->setShortcut(QKeySequence::Undo);
    connect(undoAct, &QAction::triggered, this, &MenuBar::undo);

    QAction *redoAct = m_editMenu->addAction("重做(&R)");
    redoAct->setShortcut(QKeySequence::Redo);
    connect(redoAct, &QAction::triggered, this, &MenuBar::redo);

    // === 视图菜单 ===
    m_viewMenu = addMenu("视图(&V)");

    QAction *sidebarAct = m_viewMenu->addAction("侧边栏");
    sidebarAct->setShortcut(QKeySequence("Ctrl+B"));
    sidebarAct->setCheckable(true);
    sidebarAct->setChecked(true);
    connect(sidebarAct, &QAction::triggered, this, &MenuBar::toggleSidebar);

    QAction *outlineAct = m_viewMenu->addAction("大纲");
    outlineAct->setCheckable(true);
    outlineAct->setChecked(true);
    connect(outlineAct, &QAction::triggered, this, &MenuBar::toggleOutline);

    m_viewMenu->addSeparator();

    QAction *sourceAct = m_viewMenu->addAction("源码模式");
    sourceAct->setShortcut(QKeySequence("Ctrl+\\"));
    sourceAct->setCheckable(true);
    sourceAct->setObjectName("sourceModeAction");
    connect(sourceAct, &QAction::triggered, this, &MenuBar::toggleSourceMode);

    QAction *fullscreenAct = m_viewMenu->addAction("全屏");
    fullscreenAct->setShortcut(QKeySequence("F11"));
    connect(fullscreenAct, &QAction::triggered, this, &MenuBar::toggleFullscreen);

    m_viewMenu->addSeparator();

    QMenu *themeMenu = m_viewMenu->addMenu("主题");
    QAction *lightAct = themeMenu->addAction("亮色");
    connect(lightAct, &QAction::triggered, this, [this]() { emit switchTheme("light"); });
    QAction *darkAct = themeMenu->addAction("暗色");
    connect(darkAct, &QAction::triggered, this, [this]() { emit switchTheme("dark"); });

    // === 插入菜单 ===
    m_insertMenu = addMenu("插入(&I)");

    QAction *linkAct = m_insertMenu->addAction("链接...");
    linkAct->setShortcut(QKeySequence("Ctrl+K"));
    connect(linkAct, &QAction::triggered, this, &MenuBar::insertLink);

    QAction *imageAct = m_insertMenu->addAction("图片...");
    imageAct->setShortcut(QKeySequence("Ctrl+Shift+I"));
    connect(imageAct, &QAction::triggered, this, &MenuBar::insertImage);

    QAction *tableAct = m_insertMenu->addAction("表格...");
    connect(tableAct, &QAction::triggered, this, &MenuBar::insertTable);

    QAction *mathAct = m_insertMenu->addAction("插入公式(&M)");
    mathAct->setShortcut(QKeySequence("Ctrl+M"));
    connect(mathAct, &QAction::triggered, this, &MenuBar::insertMath);

    QAction *codeAct = m_insertMenu->addAction("行内代码");
    connect(codeAct, &QAction::triggered, this, &MenuBar::insertCode);

    QAction *codeBlockAct = m_insertMenu->addAction("代码块");
    connect(codeBlockAct, &QAction::triggered, this, &MenuBar::insertCodeBlock);

    QAction *hrAct = m_insertMenu->addAction("分割线");
    connect(hrAct, &QAction::triggered, this, &MenuBar::insertHorizontalRule);

    QAction *emojiAct = m_insertMenu->addAction("表情符号...");
    connect(emojiAct, &QAction::triggered, this, &MenuBar::insertEmoji);

    QAction *footnoteAct = m_insertMenu->addAction("脚注...");
    connect(footnoteAct, &QAction::triggered, this, &MenuBar::insertFootnote);

    // === 帮助菜单 ===
    m_helpMenu = addMenu("帮助(&H)");

    QAction *guideAct = m_helpMenu->addAction("Markdown 语法参考(&G)");
    connect(guideAct, &QAction::triggered, this, &MenuBar::showMarkdownGuide);

    QAction *settingsAct = m_helpMenu->addAction("设置...");
    connect(settingsAct, &QAction::triggered, this, &MenuBar::showSettings);

    m_helpMenu->addSeparator();

    QAction *aboutAct = m_helpMenu->addAction("关于...");
    connect(aboutAct, &QAction::triggered, this, &MenuBar::showAbout);

    loadRecentFiles();
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
        QAction *empty = m_recentFilesMenu->addAction("(无)");
        empty->setEnabled(false);
    } else {
        for (const QString &f : valid) {
            QAction *act = m_recentFilesMenu->addAction(f);
            connect(act, &QAction::triggered, this, [this, f]() {
                emit openRecentFile(f);
            });
        }
        m_recentFilesMenu->addSeparator();
        QAction *clearAct = m_recentFilesMenu->addAction("清除最近文件列表");
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
