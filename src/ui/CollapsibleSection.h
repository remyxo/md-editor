#ifndef MD_EDITOR_COLLAPSIBLESECTION_H
#define MD_EDITOR_COLLAPSIBLESECTION_H

#include <QWidget>

class QToolButton;
class QVBoxLayout;
class QPropertyAnimation;

// 通用可折叠面板：标题栏（点击切换折叠）+ 内容区。
// 高度动画作用于「面板自身 maximumHeight」：收起 → 标题栏高度；展开 → 大值
// 上限，让外层布局按 stretch 填充分配。内容区在面板内 stretch 1 跟随面板伸缩。
class CollapsibleSection : public QWidget {
    Q_OBJECT
public:
    explicit CollapsibleSection(QWidget *parent = nullptr);

    void setTitle(const QString &title);
    QString title() const;

    void setContent(QWidget *content);
    QWidget *content() const;

    bool isCollapsed() const;
    void setCollapsed(bool collapsed);
    void setCollapsed(bool collapsed, bool animate);

    int contentMaxHeight() const;   // 被动画驱动的面板 maximumHeight（展开 kExpandedMax / 收起标题栏高，测试断言用）
    int collapsedHeight() const;    // 收起态面板高度 = 标题栏高度

signals:
    void collapsedChanged(bool collapsed);

private:
    void startHeightAnimation(int target, int contentTarget);
    void updateHeader();

    QToolButton *m_headerBtn;
    QWidget *m_content = nullptr;
    QVBoxLayout *m_layout;
    QPropertyAnimation *m_heightAnim;    // 面板自身 maximumHeight（整体伸缩）
    QPropertyAnimation *m_contentAnim;   // 内容 maximumHeight（收起归 0，标题栏不被挤压）
    QString m_title;
    bool m_collapsed = false;
};

#endif // MD_EDITOR_COLLAPSIBLESECTION_H
