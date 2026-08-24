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
{
    setupUi();
}

void SideBar::setupUi()
{
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_tabWidget = new QTabWidget(this);
    m_tabWidget->addTab(m_fileTree, tr("目录树"));
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
    m_tabWidget->setTabText(0, tr("目录树"));
    m_tabWidget->setTabText(1, tr("大纲"));
    m_openFolderBtn->setText(tr("打开文件夹..."));
}

FileTree *SideBar::fileTree() const { return m_fileTree; }
OutlineTree *SideBar::outlineTree() const { return m_outlineTree; }

void SideBar::setFileTreeVisible(bool visible)
{
    m_fileTreeVisible = visible;
    m_tabWidget->tabBar()->setTabVisible(0, visible);
    // 「打开文件夹...」按钮服务目录树，跟随其显隐
    m_openFolderBtn->setVisible(visible);
    // 停驻页被隐藏时切到另一个可见页
    if (!visible && m_tabWidget->currentIndex() == 0 && m_outlineVisible)
        m_tabWidget->setCurrentIndex(1);
    updateVisibility();
}

void SideBar::setOutlineVisible(bool visible)
{
    m_outlineVisible = visible;
    m_tabWidget->tabBar()->setTabVisible(1, visible);
    if (!visible && m_tabWidget->currentIndex() == 1 && m_fileTreeVisible)
        m_tabWidget->setCurrentIndex(0);
    updateVisibility();
}

void SideBar::updateVisibility()
{
    setVisible(m_fileTreeVisible || m_outlineVisible);
    // 当前页指向隐藏 tab 时，落到第一个可见页（全关时无需处理）
    if (m_fileTreeVisible || m_outlineVisible) {
        if (!m_tabWidget->isTabVisible(m_tabWidget->currentIndex()))
            m_tabWidget->setCurrentIndex(m_fileTreeVisible ? 0 : 1);
    }
}
