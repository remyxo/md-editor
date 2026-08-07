#include "SideBar.h"
#include "FileTree.h"
#include "OutlineTree.h"
#include <QVBoxLayout>
#include <QTabWidget>

SideBar::SideBar(QWidget *parent)
    : QWidget(parent)
    , m_fileTree(new FileTree(this))
    , m_outlineTree(new OutlineTree(this))
    , m_openFolderBtn(nullptr)
    , m_collapsed(false)
{
    setupUi();
}

void SideBar::setupUi()
{
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    QTabWidget *tabWidget = new QTabWidget(this);
    tabWidget->addTab(m_fileTree, "文件");
    tabWidget->addTab(m_outlineTree, "大纲");
    layout->addWidget(tabWidget);

    m_openFolderBtn = new QPushButton("打开文件夹...", this);
    connect(m_openFolderBtn, &QPushButton::clicked, this, &SideBar::openFolderClicked);
    layout->addWidget(m_openFolderBtn);

    setMinimumWidth(240);
}

FileTree *SideBar::fileTree() const { return m_fileTree; }
OutlineTree *SideBar::outlineTree() const { return m_outlineTree; }

void SideBar::setCollapsed(bool collapsed)
{
    m_collapsed = collapsed;
    setVisible(!collapsed);
}

bool SideBar::isCollapsed() const { return m_collapsed; }
