#include "SideBar.h"
#include "CollapsibleSection.h"
#include "FileTree.h"
#include "OutlineTree.h"
#include <QVBoxLayout>
#include <QPropertyAnimation>

SideBar::SideBar(QWidget *parent)
    : QWidget(parent)
    , m_fileSection(new CollapsibleSection(this))
    , m_outlineSection(new CollapsibleSection(this))
    , m_fileTree(new FileTree(this))
    , m_outlineTree(new OutlineTree(this))
    , m_openFolderBtn(nullptr)
    , m_widthAnim(new QPropertyAnimation(this, "maximumWidth", this))
{
    setMinimumWidth(0);   // 允许宽度动画收到 0
    setupUi();
    m_widthAnim->setDuration(300);
    m_widthAnim->setEasingCurve(QEasingCurve::InOutCubic);
    connect(m_widthAnim, &QPropertyAnimation::finished,
            this, &SideBar::onWidthAnimFinished);
}

void SideBar::setupUi()
{
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_fileSection->setObjectName(QStringLiteral("fileTreeSection"));
    m_fileSection->setTitle(tr("目录树"));
    m_fileSection->setContent(m_fileTree);   // addWidget 会把 FileTree 重挂到 Section
    layout->addWidget(m_fileSection);

    m_outlineSection->setObjectName(QStringLiteral("outlineSection"));
    m_outlineSection->setTitle(tr("大纲"));
    m_outlineSection->setContent(m_outlineTree);
    layout->addWidget(m_outlineSection);

    m_openFolderBtn = new QPushButton(tr("打开文件夹..."), this);
    connect(m_openFolderBtn, &QPushButton::clicked, this, &SideBar::openFolderClicked);
    layout->addWidget(m_openFolderBtn);

    // 展开初始态：构造时 setMinimumWidth(0) 允许宽度收到 0，但启动即展开态
    // 需恢复 240 下限，否则用户在 QSplitter 中可直接拖到 0 且无法自恢复
    setMinimumWidth(240);

    retranslateUi();
}

FileTree *SideBar::fileTree() const { return m_fileTree; }
OutlineTree *SideBar::outlineTree() const { return m_outlineTree; }

void SideBar::setFileTreeVisible(bool visible)
{
    m_fileTreeVisible = visible;
    m_openFolderBtn->setVisible(visible);   // 「打开文件夹」跟随目录树面板展开状态
    m_fileSection->setCollapsed(!visible);
    updateWholeCollapse();
}

void SideBar::setOutlineVisible(bool visible)
{
    m_outlineVisible = visible;
    m_outlineSection->setCollapsed(!visible);
    updateWholeCollapse();
}

// 两面板全收 → 侧边栏宽度收起；任一面板展开（从全收态）→ 宽度展开
void SideBar::updateWholeCollapse()
{
    const bool bothOff = !m_fileTreeVisible && !m_outlineVisible;
    if (bothOff != m_wholeCollapsed) {
        m_wholeCollapsed = bothOff;
        animateWidth(bothOff ? 0 : m_savedWidth);
    }
}

void SideBar::animateWidth(int target)
{
    // 展开动画中途再全收时，width()/maximumWidth() 是瞬态值（如 120），
    // 若此时覆盖 m_savedWidth 会把好宽度（240）污染成瞬态值，下次展开恢复过窄。
    // 故先记录动画是否运行：运行中只复用已保存宽度，不重新捕获。
    const bool wasAnimRunning = (m_widthAnim->state() == QAbstractAnimation::Running);
    m_widthAnim->stop();                 // 快速连续切换：从当前实际值起算
    setMinimumWidth(0);
    if (target == 0) {
        if (!wasAnimRunning)
            m_savedWidth = width() > 0 ? width() : m_savedWidth;  // 收起前记录可见宽
        setMaximumWidth(m_savedWidth);
        m_widthAnim->setStartValue(m_savedWidth);
    } else {
        m_widthAnim->setStartValue(maximumWidth());           // 可能中途 0 或半值
    }
    m_widthAnim->setEndValue(target);
    m_widthAnim->start();
}

void SideBar::onWidthAnimFinished()
{
    if (m_wholeCollapsed) {
        setMaximumWidth(0);                 // 收起完成：保持 0 宽（不 setVisible）
        setMinimumWidth(0);
    } else {
        setMaximumWidth(QWIDGETSIZE_MAX);   // 展开完成：恢复可自由调整
        // 下限跟随 m_savedWidth（用户拖窄后收起再展开，不被 240 弹回）
        setMinimumWidth(qMin(m_savedWidth, 240));
    }
}

void SideBar::retranslateUi()
{
    m_fileSection->setTitle(tr("目录树"));
    m_outlineSection->setTitle(tr("大纲"));
    if (m_openFolderBtn)
        m_openFolderBtn->setText(tr("打开文件夹..."));
}
