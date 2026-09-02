#include "SideBar.h"
#include "CollapsibleSection.h"
#include "FileTree.h"
#include "OutlineTree.h"
#include <QVBoxLayout>
#include <QSplitter>
#include <QPropertyAnimation>
#include <QShowEvent>
#include <QSignalBlocker>

SideBar::SideBar(QWidget *parent)
    : QWidget(parent)
    , m_fileSection(new CollapsibleSection(this))
    , m_outlineSection(new CollapsibleSection(this))
    , m_fileTree(new FileTree(this))
    , m_outlineTree(new OutlineTree(this))
    , m_openFolderBtn(nullptr)
    , m_splitter(new QSplitter(Qt::Vertical, this))
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

    // 大纲在上、目录树在下，垂直 splitter 分隔（中间可拖动调高）；stretch 3:2 维持 6:4
    m_outlineSection->setObjectName(QStringLiteral("outlineSection"));
    m_outlineSection->setTitle(tr("大纲"));
    m_outlineSection->setContent(m_outlineTree);
    m_fileSection->setObjectName(QStringLiteral("fileTreeSection"));
    m_fileSection->setTitle(tr("目录树"));
    m_fileSection->setContent(m_fileTree);
    m_splitter->addWidget(m_outlineSection);
    m_splitter->addWidget(m_fileSection);
    m_splitter->setStretchFactor(0, 3);
    m_splitter->setStretchFactor(1, 2);
    layout->addWidget(m_splitter, 1);

    m_openFolderBtn = new QPushButton(tr("打开文件夹..."), this);
    connect(m_openFolderBtn, &QPushButton::clicked, this, &SideBar::openFolderClicked);
    layout->addWidget(m_openFolderBtn);

    // 标题栏点击路径：折叠状态变化 → 同步可见标志 + 收起面板沉底 + 比例保存/恢复
    // + 整体收起 + 通知菜单
    connect(m_outlineSection, &CollapsibleSection::collapsedChanged, this,
            [this](bool collapsed) {
                m_outlineVisible = !collapsed;
                if (collapsed)
                    rememberExpandedSizes();   // 收起前保存两面板展开时的比例
                updateSectionOrder();          // 收起面板沉底 / 展开面板复位
                if (!collapsed)
                    restoreExpandedSizes();    // 重开后恢复上次展开比例（或 6:4）
                updateWholeCollapse();
                emit outlineVisibleChanged(!collapsed);
            });
    connect(m_fileSection, &CollapsibleSection::collapsedChanged, this,
            [this](bool collapsed) {
                m_fileTreeVisible = !collapsed;
                m_openFolderBtn->setVisible(!collapsed);   // 「打开文件夹」跟随目录树
                if (collapsed)
                    rememberExpandedSizes();
                updateSectionOrder();
                if (!collapsed)
                    restoreExpandedSizes();
                updateWholeCollapse();
                emit fileTreeVisibleChanged(!collapsed);
            });

    // 展开初始态：恢复 240 下限，避免用户拖到 0 无法自恢复
    setMinimumWidth(240);

    retranslateUi();
}

FileTree *SideBar::fileTree() const { return m_fileTree; }
OutlineTree *SideBar::outlineTree() const { return m_outlineTree; }

void SideBar::setFileTreeVisible(bool visible)
{
    m_fileSection->setCollapsed(!visible);
}

void SideBar::setOutlineVisible(bool visible)
{
    m_outlineSection->setCollapsed(!visible);
}

// 启动恢复面板可见性（MainWindow 读 QSettings 后调用）。必须用 QSignalBlocker
// 阻断 collapsedChanged 信号链直置状态：构造期 splitter 尺寸未定形，
// rememberExpandedSizes 会把垃圾比例存进 m_savedSplitSizes（用户重开目录树时
// 恢复出坏比例）；且启动不需要收起动画。末尾统一发 visibleChanged 信号，
// 供 MainWindow 同步菜单勾选与持久化。
void SideBar::applyInitialPanes(bool fileTree, bool outline)
{
    const QSignalBlocker fileBlocker(m_fileSection);
    const QSignalBlocker outlineBlocker(m_outlineSection);
    m_fileSection->setCollapsed(!fileTree, false);   // 无动画重载
    m_outlineSection->setCollapsed(!outline, false);
    m_fileTreeVisible = fileTree;
    m_outlineVisible = outline;
    m_openFolderBtn->setVisible(fileTree);
    updateSectionOrder();   // 恢复「大纲收起+目录树展开」时交换面板顺序
    m_wholeCollapsed = !fileTree && !outline;
    if (m_wholeCollapsed) { // 复现全收态：无动画直接置隐藏
        setMaximumWidth(0);
        setMinimumWidth(0);
        setVisible(false);
    }
    emit fileTreeVisibleChanged(fileTree);
    emit outlineVisibleChanged(outline);
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

// 收起的面板沉底：仅大纲收起且目录树展开时交换两面板（目录树在上、大纲标题栏在下）。
// 交换后重设 stretch（stretch 按 splitter 索引而非 widget，须跟随新顺序）。
void SideBar::updateSectionOrder()
{
    const bool outlineCollapsed = m_outlineSection->isCollapsed();
    const bool fileCollapsed = m_fileSection->isCollapsed();
    const bool wantFileTop = outlineCollapsed && !fileCollapsed;   // 大纲收起 → 目录树移上
    const bool fileTopNow = (m_splitter->widget(0) == m_fileSection);
    if (wantFileTop == fileTopNow)
        return;
    const QList<int> sizes = m_splitter->sizes();
    QWidget *b = m_splitter->widget(1);
    m_splitter->insertWidget(0, b);               // 把底部 widget 移到顶部 → 交换两面板
    m_splitter->setSizes({sizes[1], sizes[0]});   // 交换时同步交换各自高度
    m_splitter->setStretchFactor(0, wantFileTop ? 2 : 3);
    m_splitter->setStretchFactor(1, wantFileTop ? 3 : 2);
}

// 从「两面板都展开」收起一面时，保存当前展开比例（6:4 或用户拖出的比例）。
// 被收起面板的 visible 此刻已置 false，故用「另一面板仍展开」判断是否处于
// 双展开→单展开的过渡；尺寸以 [大纲, 目录树] 自然序保存，动画尚未收缩，仍是展开值。
void SideBar::rememberExpandedSizes()
{
    if (!m_savedSplitSizes.isEmpty())
        return;
    if (m_outlineVisible || m_fileTreeVisible)   // 另一面板仍展开（从双展开收起）
        m_savedSplitSizes = m_splitter->sizes();
}

// 重开面板至「两面板都展开」时，恢复上次保存的展开比例；否则用默认 6:4。
// 调用时机须在 updateSectionOrder 之后（此时顺序已复位为 [大纲, 目录树]），
// 尺寸映射才正确。
void SideBar::restoreExpandedSizes()
{
    if (m_savedSplitSizes.isEmpty())
        return;
    if (m_outlineVisible && m_fileTreeVisible) {
        m_splitter->setSizes(m_savedSplitSizes);
        m_savedSplitSizes.clear();
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
        // 展开：收起态已 setVisible(false)，须先恢复可见再动画，
        // 否则动画跑在隐藏 widget 上、QSplitter 也不重挂面板。
        setVisible(true);
        m_widthAnim->setStartValue(maximumWidth());           // 可能中途 0 或半值
    }
    m_widthAnim->setEndValue(target);
    m_widthAnim->start();
}

void SideBar::onWidthAnimFinished()
{
    if (m_wholeCollapsed) {
        setMaximumWidth(0);                 // 收起完成：保持 0 宽
        setMinimumWidth(0);
        // 隐藏侧边栏让 QSplitter 把腾出的空间分给编辑区（否则只缩到 0 宽，
        // splitter 不重分配，编辑区旁残留一段空白）。
        setVisible(false);
    } else {
        setVisible(true);
        setMaximumWidth(QWIDGETSIZE_MAX);   // 展开完成：恢复可自由调整
        // 下限跟随 m_savedWidth（用户拖窄后收起再展开，不被 240 弹回）
        setMinimumWidth(qMin(m_savedWidth, 240));
    }
}

void SideBar::retranslateUi()
{
    m_outlineSection->setTitle(tr("大纲"));
    m_fileSection->setTitle(tr("目录树"));
    if (m_openFolderBtn)
        m_openFolderBtn->setText(tr("打开文件夹..."));
}

// 首次显示布局就绪后设初始分隔 6:4（大纲 60% / 目录树 40%），之后用户可拖动调整
void SideBar::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    if (!m_initialSplitSet) {
        m_initialSplitSet = true;
        m_splitter->setSizes({360, 240});
    }
}
