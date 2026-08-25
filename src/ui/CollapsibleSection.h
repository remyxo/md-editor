#ifndef MD_EDITOR_COLLAPSIBLESECTION_H
#define MD_EDITOR_COLLAPSIBLESECTION_H

#include <QWidget>

class QToolButton;
class QVBoxLayout;
class QPropertyAnimation;

// 通用可折叠面板：标题栏（点击切换折叠）+ 内容区（高度动画展开/收起）。
// 收起时仅标题栏可见；展开时内容区高度动画恢复，让外层布局填充分配空间。
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

    int contentMaxHeight() const;   // 内容区当前 maximumHeight（测试断言用）

signals:
    void collapsedChanged(bool collapsed);

private:
    void startHeightAnimation(int target);
    void updateHeader();

    QToolButton *m_headerBtn;
    QWidget *m_content = nullptr;
    QVBoxLayout *m_layout;
    QPropertyAnimation *m_heightAnim;
    QString m_title;
    bool m_collapsed = false;
};

#endif // MD_EDITOR_COLLAPSIBLESECTION_H
