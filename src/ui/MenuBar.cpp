#include "MenuBar.h"
#include <QAction>
#include <QKeySequence>

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

    m_fileMenu->addSeparator();

    QAction *saveAct = m_fileMenu->addAction("保存(&S)");
    saveAct->setShortcut(QKeySequence::Save);
    connect(saveAct, &QAction::triggered, this, &MenuBar::saveFile);

    QAction *saveAsAct = m_fileMenu->addAction("另存为...");
    saveAsAct->setShortcut(QKeySequence("Ctrl+Shift+S"));
    connect(saveAsAct, &QAction::triggered, this, &MenuBar::saveFileAs);

    m_fileMenu->addSeparator();

    QAction *exportAct = m_fileMenu->addAction("导出 PDF...");
    exportAct->setShortcut(QKeySequence("Ctrl+P"));
    connect(exportAct, &QAction::triggered, this, &MenuBar::exportPdf);

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

    QAction *hrAct = m_insertMenu->addAction("分割线");
    connect(hrAct, &QAction::triggered, this, &MenuBar::insertHorizontalRule);

    // === 帮助菜单 ===
    m_helpMenu = addMenu("帮助(&H)");

    QAction *settingsAct = m_helpMenu->addAction("设置...");
    connect(settingsAct, &QAction::triggered, this, &MenuBar::showSettings);

    m_helpMenu->addSeparator();

    QAction *aboutAct = m_helpMenu->addAction("关于...");
    connect(aboutAct, &QAction::triggered, this, &MenuBar::showAbout);
}
