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

    m_tabWidget = new QTabWidget(this);
    m_tabWidget->addTab(m_fileTree, tr("文件"));
    m_tabWidget->addTab(m_outlineTree, tr("大纲"));
    layout->addWidget(m_tabWidget);

    m_openFolderBtn = new QPushButton(tr("打开文件夹..."), this);
    connect(m_openFolderBtn, &QPushButton::clicked, this, &SideBar::openFolderClicked);
    layout->addWidget(m_openFolderBtn);

    setMinimumWidth(240);

    retranslateUi();  // 统一重设文案（首次设置，语言切换后 MainWindow 再次调用）
}

// 语言切换后重设全部文案
void SideBar::retranslateUi()
{
    m_tabWidget->setTabText(0, tr("文件"));
    m_tabWidget->setTabText(1, tr("大纲"));
    m_openFolderBtn->setText(tr("打开文件夹..."));
}

FileTree *SideBar::fileTree() const { return m_fileTree; }
OutlineTree *SideBar::outlineTree() const { return m_outlineTree; }

void SideBar::setCollapsed(bool collapsed)
{
    m_collapsed = collapsed;
    setVisible(!collapsed);
}

bool SideBar::isCollapsed() const { return m_collapsed; }
