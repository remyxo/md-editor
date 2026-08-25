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
    QTest::qWait(400);                   // 等 300ms 动画完成
    QCOMPARE(lbl->maximumHeight(), 0);   // 收起后内容高度为 0
}

void TestCollapsibleSection::expand_restores_height()
{
    CollapsibleSection s;
    QLabel *lbl = makeContent(s);
    s.show();
    s.setCollapsed(true);
    QTest::qWait(400);
    s.setCollapsed(false);
    QVERIFY(!s.isCollapsed());
    QTest::qWait(400);
    QVERIFY(lbl->maximumHeight() > 0);   // 恢复可展开
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
