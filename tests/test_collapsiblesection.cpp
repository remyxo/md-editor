#include <QtTest>
#include "ui/CollapsibleSection.h"
#include <QLabel>

// CollapsibleSection 可折叠面板行为测试
class TestCollapsibleSection : public QObject {
    Q_OBJECT
private slots:
    void initial_expanded();          // 初始未收起，内容可展开
    void collapse_animates_height();  // 收起：高度动画到 0，isCollapsed 立即翻转
    void expand_restores_height();    // 展开：高度动画恢复
    void collapse_immediate_no_animation(); // animate=false 立即生效，不带动画
    void collapsed_signal();          // collapsedChanged 信号
private:
    QLabel *makeContent(CollapsibleSection &s) const;
};

QLabel *TestCollapsibleSection::makeContent(CollapsibleSection &s) const
{
    QLabel *lbl = new QLabel(QStringLiteral("内容"), &s);
    s.setContent(lbl);
    return lbl;
}

void TestCollapsibleSection::initial_expanded()
{
    CollapsibleSection s;
    QLabel *lbl = makeContent(s);
    s.show();
    QVERIFY(!s.isCollapsed());
    QVERIFY(lbl->maximumHeight() > 0);
}

void TestCollapsibleSection::collapse_animates_height()
{
    CollapsibleSection s;
    QLabel *lbl = makeContent(s);
    s.show();
    s.setCollapsed(true);
    QVERIFY(s.isCollapsed());            // 状态标志立即翻转
    QTRY_COMPARE(lbl->maximumHeight(), 0);   // 轮询等 300ms 动画完成
}

void TestCollapsibleSection::expand_restores_height()
{
    CollapsibleSection s;
    QLabel *lbl = makeContent(s);
    s.show();
    s.setCollapsed(true);
    QTRY_COMPARE(lbl->maximumHeight(), 0);   // 收起动画完成
    s.setCollapsed(false);
    QVERIFY(!s.isCollapsed());
    QTRY_VERIFY(lbl->maximumHeight() > 0);   // 轮询等展开动画恢复
}

void TestCollapsibleSection::collapse_immediate_no_animation()
{
    CollapsibleSection s;
    makeContent(s);
    s.show();
    s.setCollapsed(true, false);
    QVERIFY(s.isCollapsed());
    QCOMPARE(s.contentMaxHeight(), 0);        // animate=false：立即收起，无需等动画
    s.setCollapsed(false, false);
    QVERIFY(!s.isCollapsed());
    QVERIFY(s.contentMaxHeight() > 0);        // animate=false：立即恢复大值
}

void TestCollapsibleSection::collapsed_signal()
{
    CollapsibleSection s;
    makeContent(s);
    QSignalSpy spy(&s, &CollapsibleSection::collapsedChanged);
    s.setCollapsed(true);
    QCOMPARE(spy.count(), 1);
    QVERIFY(spy.takeFirst().at(0).toBool());
}

QTEST_MAIN(TestCollapsibleSection)
#include "test_collapsiblesection.moc"
