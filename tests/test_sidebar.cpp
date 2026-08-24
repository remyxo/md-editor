#include <QtTest>
#include "ui/SideBar.h"
#include <QTabWidget>
#include <QPushButton>

// 侧边栏双开关（目录树/大纲）行为测试
class TestSideBar : public QObject {
    Q_OBJECT
private slots:
    void initial_state();       // 初始：两面板可见，侧边栏可见
    void file_only();           // 只目录树：大纲 tab 隐藏，按钮保留
    void outline_only();        // 只大纲：文件 tab 隐藏，按钮隐藏，停驻页切到 1
    void all_off();             // 全关：侧边栏整体隐藏
    void recover_from_all_off();// 从全关恢复目录树：侧边栏重新显示
    void hiding_current_page(); // 停在大纲页关大纲：切回文件页
    void recover_lands_on_visible_page(); // 从全关恢复：落到可见页而非隐藏 tab
private:
    QTabWidget *tabs(const SideBar &sb) const;
    QPushButton *openBtn(const SideBar &sb) const;
};

QTabWidget *TestSideBar::tabs(const SideBar &sb) const
{
    return sb.findChild<QTabWidget *>();
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
    QVERIFY(tabs(sb)->isTabVisible(0));
    QVERIFY(tabs(sb)->isTabVisible(1));
    QVERIFY(openBtn(sb)->isVisible());
}

void TestSideBar::file_only()
{
    SideBar sb;
    sb.show();
    sb.setOutlineVisible(false);
    QVERIFY(!tabs(sb)->isTabVisible(1));
    QVERIFY(tabs(sb)->isTabVisible(0));
    QVERIFY(sb.isVisible());
    QVERIFY(openBtn(sb)->isVisibleTo(&sb));   // 按钮跟随目录树
}

void TestSideBar::outline_only()
{
    SideBar sb;
    sb.show();
    sb.setFileTreeVisible(false);
    QVERIFY(!tabs(sb)->isTabVisible(0));
    QVERIFY(tabs(sb)->isTabVisible(1));
    QVERIFY(sb.isVisible());
    QVERIFY(!openBtn(sb)->isVisibleTo(&sb));  // 无目录树则按钮隐藏
    QCOMPARE(tabs(sb)->currentIndex(), 1);    // 停驻页自动切到可见页
}

void TestSideBar::all_off()
{
    SideBar sb;
    sb.show();
    sb.setFileTreeVisible(false);
    sb.setOutlineVisible(false);
    QVERIFY(!sb.isVisible());
}

void TestSideBar::recover_from_all_off()
{
    SideBar sb;
    sb.show();
    sb.setFileTreeVisible(false);
    sb.setOutlineVisible(false);
    QVERIFY(!sb.isVisible());
    sb.setFileTreeVisible(true);
    QVERIFY(sb.isVisible());
    QVERIFY(tabs(sb)->isTabVisible(0));
}

void TestSideBar::hiding_current_page()
{
    SideBar sb;
    sb.show();
    tabs(sb)->setCurrentIndex(1);             // 停在大纲页
    sb.setOutlineVisible(false);
    QCOMPARE(tabs(sb)->currentIndex(), 0);    // 切回文件页
}

void TestSideBar::recover_lands_on_visible_page()
{
    SideBar sb;
    sb.show();
    tabs(sb)->setCurrentIndex(1);             // 停在大纲页
    sb.setFileTreeVisible(false);
    sb.setOutlineVisible(false);              // 全关，current 停在隐藏的 1
    sb.setFileTreeVisible(true);              // 只重开目录树
    QVERIFY(sb.isVisible());
    QCOMPARE(tabs(sb)->currentIndex(), 0);    // 落到可见页，不留在隐藏 tab
}

QTEST_MAIN(TestSideBar)
#include "test_sidebar.moc"
