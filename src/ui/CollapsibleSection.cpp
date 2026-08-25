#include "CollapsibleSection.h"
#include <QToolButton>
#include <QVBoxLayout>
#include <QPropertyAnimation>

// 展开态面板最大高度上限：取大值让外层布局按 stretch 填充分配空间
// （面板实际高度由父布局决定，此值仅是「不设上限」的哨兵）
static constexpr int kExpandedMax = 10000;

CollapsibleSection::CollapsibleSection(QWidget *parent)
    : QWidget(parent)
    , m_headerBtn(new QToolButton(this))
    , m_layout(new QVBoxLayout(this))
    , m_heightAnim(new QPropertyAnimation(this))
    , m_contentAnim(new QPropertyAnimation(this))
{
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(0);

    m_headerBtn->setObjectName(QStringLiteral("collapsibleHeader"));
    m_headerBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_headerBtn->setCheckable(true);
    m_headerBtn->setAutoRaise(true);
    m_headerBtn->setStyleSheet(
        "QToolButton#collapsibleHeader { border: none; padding: 6px 8px;"
        " text-align: left; font-weight: bold; background: transparent; }");
    // 点击切换折叠：checked 即收起
    connect(m_headerBtn, &QToolButton::clicked, this,
            [this](bool checked) { setCollapsed(checked); });
    m_layout->addWidget(m_headerBtn);

    // 面板自身 maximumHeight 动画：收起 → 标题栏高；展开 → 大值上限。
    // 父布局按 stretch 分配空间并随 maxH 变化逐帧重排，实现面板整体平滑伸缩。
    m_heightAnim->setTargetObject(this);
    m_heightAnim->setPropertyName("maximumHeight");
    m_heightAnim->setDuration(300);
    m_heightAnim->setEasingCurve(QEasingCurve::InOutCubic);

    // 内容 maximumHeight 同步动画：收起归 0（标题栏不被内容挤压占高），展开恢复大值
    m_contentAnim->setPropertyName("maximumHeight");
    m_contentAnim->setDuration(300);
    m_contentAnim->setEasingCurve(QEasingCurve::InOutCubic);
}

void CollapsibleSection::setTitle(const QString &title)
{
    m_title = title;
    updateHeader();
}

QString CollapsibleSection::title() const
{
    return m_title;
}

void CollapsibleSection::setContent(QWidget *content)
{
    m_content = content;
    content->setMinimumHeight(0);   // 收起时允许内容压缩到 0（面板缩成标题栏）
    m_layout->addWidget(content, 1);
    m_contentAnim->setTargetObject(content);
}

QWidget *CollapsibleSection::content() const
{
    return m_content;
}

bool CollapsibleSection::isCollapsed() const
{
    return m_collapsed;
}

void CollapsibleSection::setCollapsed(bool collapsed)
{
    setCollapsed(collapsed, true);
}

void CollapsibleSection::setCollapsed(bool collapsed, bool animate)
{
    if (m_collapsed == collapsed)
        return;
    m_collapsed = collapsed;
    m_headerBtn->setChecked(collapsed);
    updateHeader();
    if (!collapsed && m_content)
        m_content->setVisible(true);
    const int target = collapsed ? collapsedHeight() : kExpandedMax;
    const int contentTarget = collapsed ? 0 : kExpandedMax;
    if (animate)
        startHeightAnimation(target, contentTarget);
    else {
        m_heightAnim->stop();     // 立即设置前停掉运行中的动画，防僵尸动画覆盖
        m_contentAnim->stop();
        setMaximumHeight(target);
        m_content->setMaximumHeight(contentTarget);
    }
    emit collapsedChanged(collapsed);
}

int CollapsibleSection::contentMaxHeight() const
{
    // 返回被动画驱动的面板 maximumHeight（展开 kExpandedMax / 收起标题栏高）
    return maximumHeight();
}

int CollapsibleSection::collapsedHeight() const
{
    // 收起态面板高度 = 标题栏高度（内容 maximumHeight 归 0 → 布局分配 0）
    return m_headerBtn->sizeHint().height();
}

void CollapsibleSection::startHeightAnimation(int target, int contentTarget)
{
    // 起点用「当前实际高度」而非 maximumHeight：maxH 是约束，展开态为 16M，
    // 从约束值起动画会让面板全程不动、末尾才跳变。从实际高度起，面板随 maxH
    // 逐帧收缩/展开，动画平滑。
    m_heightAnim->stop();
    m_contentAnim->stop();
    m_heightAnim->setStartValue(height());
    m_heightAnim->setEndValue(target);
    m_contentAnim->setStartValue(m_content->height());
    m_contentAnim->setEndValue(contentTarget);
    m_heightAnim->start();
    m_contentAnim->start();
}

void CollapsibleSection::updateHeader()
{
    // 箭头用 QToolButton 内置 arrowType（样式绘制、随主题，不依赖字体字形，避免乱码）
    m_headerBtn->setArrowType(m_collapsed ? Qt::RightArrow : Qt::DownArrow);
    m_headerBtn->setText(m_title);
}
