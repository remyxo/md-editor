#include "CollapsibleSection.h"
#include <QToolButton>
#include <QVBoxLayout>
#include <QPropertyAnimation>

// 展开态内容区最大高度：取大值让外层布局填充分配空间（不设固定高，
// 否则面板无法随窗口拉高）
static constexpr int kExpandedMax = 10000;

CollapsibleSection::CollapsibleSection(QWidget *parent)
    : QWidget(parent)
    , m_headerBtn(new QToolButton(this))
    , m_layout(new QVBoxLayout(this))
    , m_heightAnim(new QPropertyAnimation(this))
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

    m_heightAnim->setDuration(300);
    m_heightAnim->setEasingCurve(QEasingCurve::InOutCubic);
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
    content->setMinimumHeight(0);
    content->setMaximumHeight(m_collapsed ? 0 : kExpandedMax);
    m_layout->addWidget(content, 1);
    m_heightAnim->setTargetObject(content);
    m_heightAnim->setPropertyName("maximumHeight");
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
    if (m_content)
        startHeightAnimation(collapsed ? 0 : kExpandedMax);
    emit collapsedChanged(collapsed);
}

int CollapsibleSection::contentMaxHeight() const
{
    return m_content ? m_content->maximumHeight() : 0;
}

void CollapsibleSection::startHeightAnimation(int target)
{
    if (!m_content)
        return;
    m_heightAnim->stop();
    m_heightAnim->setStartValue(m_content->maximumHeight());
    m_heightAnim->setEndValue(target);
    m_heightAnim->start();
}

void CollapsibleSection::updateHeader()
{
    m_headerBtn->setText(QString(m_collapsed ? QLatin1String("▶ ")
                                             : QLatin1String("▼ "))
                         + m_title);
}
