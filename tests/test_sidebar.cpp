#include <QtTest>
#include "ui/SideBar.h"
#include "ui/CollapsibleSection.h"
#include <QPushButton>

// 侧边栏双面板（目录树/大纲）独立折叠 + 整体宽度收起行为测试
class TestSideBar : public QObject {
    Q_OBJECT
private slots:
    void initial_state();            // 初始两面板展开，侧边栏可见
    void file_only();                // 只目录树：大纲收起，按钮保留
    void outline_only();             // 只大纲：目录树收起，按钮隐藏
    void all_off_collapses_whole();  // 全收：宽度动画到 0
    void recover_from_all_off();     // 从全收重开目录树：宽度恢复
private:
    CollapsibleSection *fileSection(const SideBar &sb) const;
    CollapsibleSection *outlineSection(const SideBar &sb) const;
    QPushButton *openBtn(const SideBar &sb) const;
};

CollapsibleSection *TestSideBar::fileSection(const SideBar &sb) const
{
    return sb.findChild<CollapsibleSection *>(QStringLiteral("fileTreeSection"));
}

CollapsibleSection *TestSideBar::outlineSection(const SideBar &sb) const
{
    return sb.findChild<CollapsibleSection *>(QStringLiteral("outlineSection"));
}

QPushButton *TestSideBar::openBtn(const SideBar &sb) const
{
    return sb.findChild<QPushButton *>();
}

void TestSideBar::initial_state()
{
    SideBar sb;
    sb.show();
    QVERIFY(sb.isVisible());
    QVERIFY(!fileSection(sb)->isCollapsed());
    QVERIFY(!outlineSection(sb)->isCollapsed());
    QVERIFY(openBtn(sb)->isVisibleTo(&sb));
}

void TestSideBar::file_only()
{
    SideBar sb;
    sb.show();
    sb.setOutlineVisible(false);
    QVERIFY(outlineSection(sb)->isCollapsed());
    QVERIFY(!fileSection(sb)->isCollapsed());
    QVERIFY(sb.isVisible());
    QVERIFY(openBtn(sb)->isVisibleTo(&sb));  // 目录树在，按钮保留
    QTest::qWait(400);
    QVERIFY(sb.width() > 0);                 // 单面板收起不触发整体收起
}

void TestSideBar::outline_only()
{
    SideBar sb;
    sb.show();
    sb.setFileTreeVisible(false);
    QVERIFY(fileSection(sb)->isCollapsed());
    QVERIFY(!outlineSection(sb)->isCollapsed());
    QVERIFY(sb.isVisible());
    QVERIFY(!openBtn(sb)->isVisibleTo(&sb)); // 无目录树则按钮隐藏
}

void TestSideBar::all_off_collapses_whole()
{
    SideBar sb;
    sb.show();
    sb.setFileTreeVisible(false);
    sb.setOutlineVisible(false);
    QVERIFY(fileSection(sb)->isCollapsed());
    QVERIFY(outlineSection(sb)->isCollapsed());
    QTest::qWait(400);
    QCOMPARE(sb.maximumWidth(), 0);          // 两面板全收 → 宽度收到 0
}

void TestSideBar::recover_from_all_off()
{
    SideBar sb;
    sb.show();
    sb.setFileTreeVisible(false);
    sb.setOutlineVisible(false);
    QTest::qWait(400);
    sb.setFileTreeVisible(true);
    QVERIFY(!fileSection(sb)->isCollapsed());
    QVERIFY(outlineSection(sb)->isCollapsed());
    QTest::qWait(400);
    QVERIFY(sb.width() > 0);                 // 重开 → 宽度恢复
    QVERIFY(sb.maximumWidth() != 0);
}

QTEST_MAIN(TestSideBar)
#include "test_sidebar.moc"
