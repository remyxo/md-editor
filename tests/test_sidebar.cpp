#include <QtTest>
#include "ui/SideBar.h"
#include "ui/CollapsibleSection.h"
#include <QPushButton>
#include <QToolButton>
#include <QSplitter>

// 侧边栏双面板（大纲在上、目录树在下，6:4 可拖动）+ 独立折叠沉底 + 整体宽度收起
class TestSideBar : public QObject {
    Q_OBJECT
private slots:
    void initial_state();            // 初始两面板展开，侧边栏可见
    void outline_on_top_six_four();  // 大纲在上、目录树在下，比例约 6:4
    void file_only();                // 只目录树：大纲收起，按钮保留
    void outline_only();             // 只大纲：目录树收起，按钮隐藏
    void collapsed_sinks_to_bottom();// 任一面板收起，其标题栏沉到侧边栏底部
    void expand_restores_saved_ratio(); // 只显示目录树后再显示大纲，恢复展开比例
    void all_off_collapses_whole();  // 全收：宽度动画到 0
    void recover_from_all_off();     // 从全收重开目录树：宽度恢复
    void rapid_toggle_preserves_saved_width(); // 展开动画中途再全收，不破坏 m_savedWidth
    void header_collapse_triggers_whole_collapse();   // 标题栏点击收起两面板 → 宽度收起
    void header_collapse_emits_visibility_signals();  // 标题栏点击 → visibleChanged 信号
    void whole_collapse_hides_widget();               // 全收完成 setVisible(false)，编辑区可回填；重开恢复可见
    void restore_collapses_file_tree_only();  // 启动恢复：只显示大纲，按钮隐藏
    void restore_all_off_hides_sidebar();     // 启动恢复：全收 → 侧边栏隐藏
    void restore_then_reopen_keeps_ratio();   // 恢复收起后重开目录树，比例未被污染
    void restore_emits_signals();             // 恢复发 visibleChanged 信号（菜单同步依赖）
private:
    CollapsibleSection *fileSection(const SideBar &sb) const;
    CollapsibleSection *outlineSection(const SideBar &sb) const;
    QPushButton *openBtn(const SideBar &sb) const;
    QSplitter *splitter(const SideBar &sb) const;
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

QSplitter *TestSideBar::splitter(const SideBar &sb) const
{
    return sb.findChild<QSplitter *>();
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

// 大纲在上、目录树在下，splitter 默认比例约 6:4（大纲更高），中间分隔可拖动
void TestSideBar::outline_on_top_six_four()
{
    SideBar sb;
    sb.show();
    QTest::qWait(100);                      // 等布局 + showEvent 里 setSizes 生效
    QVERIFY(splitter(sb)->widget(0) == outlineSection(sb));
    QVERIFY(splitter(sb)->widget(1) == fileSection(sb));
    const int oh = outlineSection(sb)->height();
    const int fh = fileSection(sb)->height();
    QVERIFY(oh > fh);                       // 大纲更高（6:4 → 大纲占多）
    QVERIFY(oh < fh * 2);                   // 比例不至于极端
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

// 任一面板收起 → 其标题栏沉到侧边栏底部（另一面板填满上方）
void TestSideBar::collapsed_sinks_to_bottom()
{
    SideBar sb;
    sb.show();
    // 大纲收起 → 目录树在上、大纲标题栏在底部
    sb.setOutlineVisible(false);
    QTest::qWait(400);
    QVERIFY(splitter(sb)->widget(0) == fileSection(sb));
    QVERIFY(splitter(sb)->widget(1) == outlineSection(sb));
    QVERIFY(outlineSection(sb)->isCollapsed());
    // 恢复大纲，再收目录树 → 大纲在上、目录树标题栏在底部
    sb.setOutlineVisible(true);
    QTest::qWait(400);
    sb.setFileTreeVisible(false);
    QTest::qWait(400);
    QVERIFY(splitter(sb)->widget(0) == outlineSection(sb));
    QVERIFY(splitter(sb)->widget(1) == fileSection(sb));
    QVERIFY(fileSection(sb)->isCollapsed());
}

// 回归：先只显示目录树（大纲收起），再显示大纲，比例应恢复上次展开比例（大纲 > 目录树），
// 而非停留在收起时的「目录树占绝大部分」。
void TestSideBar::expand_restores_saved_ratio()
{
    SideBar sb;
    sb.show();
    QTest::qWait(100);                      // 等布局 + showEvent 6:4 生效
    QVERIFY(outlineSection(sb)->height() > fileSection(sb)->height());

    sb.setOutlineVisible(false);            // 先只显示目录树
    QTest::qWait(400);
    QVERIFY(outlineSection(sb)->isCollapsed());
    QVERIFY(fileSection(sb)->height() > outlineSection(sb)->height());   // 目录树占绝大部分

    sb.setOutlineVisible(true);             // 再显示大纲
    QTest::qWait(400);
    QVERIFY(!outlineSection(sb)->isCollapsed());
    QVERIFY(outlineSection(sb)->height() > fileSection(sb)->height());   // 恢复 6:4（大纲更高）
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

// 回归：展开动画进行中再全收，不得用瞬态宽度覆盖 m_savedWidth。
// 裸 QWidget（无 QSplitter）中宽度动画只改 maximumWidth、width() 不跟随，
// 故用 resize() 模拟 QSplitter 把宽度推到位，制造「展开中途瞬态宽度」，
// 再全收 → 重开，验证宽度下限恢复为首次全收前宽度而非被污染后的窄值。
void TestSideBar::rapid_toggle_preserves_saved_width()
{
    SideBar sb;
    sb.show();
    QTest::qWait(100);                          // 等布局稳定，取首次全收前可见宽
    const int expected = sb.width();
    QVERIFY(expected > 0);

    sb.setFileTreeVisible(false);               // 全收并等收起动画完成
    sb.setOutlineVisible(false);
    QTest::qWait(400);
    QCOMPARE(sb.maximumWidth(), 0);

    sb.setFileTreeVisible(true);                // 开始展开动画
    QTest::qWait(150);                          // 中途打断：动画尚未完成
    sb.resize(sb.maximumWidth(), sb.height());  // 模拟 QSplitter：width 跟随瞬态 maximumWidth
    const int transientWidth = sb.width();
    QVERIFY(transientWidth < expected);         // 确认确实处于展开中途（瞬态宽 < 原宽）

    sb.setFileTreeVisible(false);               // 未等展开完成立刻再全收
    QTest::qWait(400);                          // 等收起动画完成
    QCOMPARE(sb.maximumWidth(), 0);

    sb.setFileTreeVisible(true);                // 再次重开目录树
    QTest::qWait(400);                          // 等展开动画完成
    QVERIFY(sb.maximumWidth() != 0);
    // 未被中途瞬态宽度污染：宽度下限恢复为首次全收前宽度（取 min 240 截断）
    QCOMPARE(sb.width(), qMin(expected, 240));
}

void TestSideBar::header_collapse_triggers_whole_collapse()
{
    SideBar sb;
    sb.show();
    CollapsibleSection *fs = fileSection(sb);
    CollapsibleSection *os = outlineSection(sb);
    QToolButton *fileHeader = fs->findChild<QToolButton *>(QStringLiteral("collapsibleHeader"));
    QToolButton *outlineHeader = os->findChild<QToolButton *>(QStringLiteral("collapsibleHeader"));
    QVERIFY(fileHeader && outlineHeader);
    fileHeader->click();    // 标题栏点击 → 目录树收起
    outlineHeader->click(); // 标题栏点击 → 大纲收起
    QVERIFY(fs->isCollapsed());
    QVERIFY(os->isCollapsed());
    QTRY_COMPARE(sb.maximumWidth(), 0);   // 两面板全收 → 宽度动画收起
    QVERIFY(!openBtn(sb)->isVisibleTo(&sb));  // 目录树收起 → 「打开文件夹」隐藏
}

// 问题2 回归：两面板全收后宽度动画到 0，必须 setVisible(false) 让 QSplitter
// 把腾出的空间分给编辑区（否则只缩 0 宽但 splitter 不重分配，编辑区旁残留空白）。
// 重开任一面板 → setVisible(true) + 宽度恢复。
void TestSideBar::whole_collapse_hides_widget()
{
    SideBar sb;
    sb.show();
    QTest::qWait(100);
    const int before = sb.width();
    QVERIFY(before > 0);

    sb.setFileTreeVisible(false);
    sb.setOutlineVisible(false);
    QTest::qWait(400);
    QCOMPARE(sb.maximumWidth(), 0);
    QVERIFY(!sb.isVisible());                 // 收起完成 → 隐藏，让编辑区回填

    sb.setFileTreeVisible(true);              // 重开目录树 → 恢复可见 + 宽度
    QTest::qWait(400);
    QVERIFY(sb.isVisible());
    QVERIFY(sb.width() > 0);
    QCOMPARE(sb.width(), qMin(before, 240));
}

void TestSideBar::header_collapse_emits_visibility_signals()
{
    SideBar sb;
    sb.show();
    QSignalSpy fileSpy(&sb, &SideBar::fileTreeVisibleChanged);
    QSignalSpy outlineSpy(&sb, &SideBar::outlineVisibleChanged);
    QToolButton *fileHeader = fileSection(sb)->findChild<QToolButton *>(QStringLiteral("collapsibleHeader"));
    QVERIFY(fileHeader);
    fileHeader->click();
    QCOMPARE(fileSpy.count(), 1);
    QCOMPARE(fileSpy.takeFirst().at(0).toBool(), false);
    QToolButton *outlineHeader = outlineSection(sb)->findChild<QToolButton *>(QStringLiteral("collapsibleHeader"));
    QVERIFY(outlineHeader);
    outlineHeader->click();
    QCOMPARE(outlineSpy.count(), 1);
    QCOMPARE(outlineSpy.takeFirst().at(0).toBool(), false);
}

// === 启动恢复（applyInitialPanes）：MainWindow 读 QSettings 后调用 ===

// 恢复「只显示大纲」：目录树收起、按钮隐藏、不触发整体收起
void TestSideBar::restore_collapses_file_tree_only()
{
    SideBar sb;
    sb.applyInitialPanes(false, true);
    sb.show();
    QVERIFY(fileSection(sb)->isCollapsed());
    QVERIFY(!outlineSection(sb)->isCollapsed());
    QVERIFY(!openBtn(sb)->isVisibleTo(&sb));  // 「打开文件夹」跟随目录树隐藏
    QTest::qWait(400);
    QVERIFY(sb.isVisible());
    QVERIFY(sb.maximumWidth() != 0);          // 单面板收起不触发整体收起
    QVERIFY(sb.width() > 0);
}

// 恢复「全收」：侧边栏直接隐藏（无动画）。SideBar 须挂父 widget——顶层直接
// sb.show() 会强行覆盖预置的 setVisible(false)，与真实应用「子 widget 显隐
// 标记」语义不符。
void TestSideBar::restore_all_off_hides_sidebar()
{
    QWidget parent;
    auto *sb = new SideBar(&parent);
    sb->applyInitialPanes(false, false);
    QCOMPARE(sb->maximumWidth(), 0);
    parent.show();
    QVERIFY(!sb->isVisibleTo(&parent));       // 父显示不拉起侧边栏（显隐标记生效）
}

// 关键回归：构造期（未布局）恢复收起 → show → 再展开目录树，大纲仍占大头。
// 证明 m_savedSplitSizes 未被构造期垃圾尺寸污染（若污染，恢复出坏比例）。
void TestSideBar::restore_then_reopen_keeps_ratio()
{
    SideBar sb;
    sb.applyInitialPanes(false, true);
    sb.show();
    QTest::qWait(100);                        // 等 showEvent setSizes({360,240}) 生效
    QVERIFY(fileSection(sb)->isCollapsed());
    sb.setFileTreeVisible(true);              // 用户点开目录树
    QTest::qWait(400);
    QVERIFY(!fileSection(sb)->isCollapsed());
    QVERIFY(outlineSection(sb)->height() > fileSection(sb)->height());  // 6:4 量级
}

// 恢复须发两个 visibleChanged 信号各一次（MainWindow 菜单勾选同步依赖）
void TestSideBar::restore_emits_signals()
{
    SideBar sb;
    QSignalSpy fileSpy(&sb, &SideBar::fileTreeVisibleChanged);
    QSignalSpy outlineSpy(&sb, &SideBar::outlineVisibleChanged);
    sb.applyInitialPanes(false, true);
    QCOMPARE(fileSpy.count(), 1);
    QCOMPARE(fileSpy.takeFirst().at(0).toBool(), false);
    QCOMPARE(outlineSpy.count(), 1);
    QCOMPARE(outlineSpy.takeFirst().at(0).toBool(), true);
}

QTEST_MAIN(TestSideBar)
#include "test_sidebar.moc"
