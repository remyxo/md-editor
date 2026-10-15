#include <QtTest>
#include <QJsonArray>
#include <QJsonObject>
#include "ui/OutlineTree.h"
#include <QStandardItemModel>

// 大纲树缩进规则回归：显示档位 = 标题级别 − 文档内最高级别（统一基准），
// 平铺线性排列（保持文档顺序）。覆盖三轮用户场景：
// ① 全 h4 文档同级全部对齐最左（曾因按栈大小弹栈链式误嵌套）；
// ② 中间插入标题3 后标题3 到最左、标题4 整体缩进一档；
// ③ 跳级混合（h4,h3,h4,h2,h4,h5）：标题3 比标题4 更左、同 level 恒同档。
class TestOutlineTree : public QObject {
    Q_OBJECT
private slots:
    void sameLevelAllAlign();       // 全同级：全部 0 档、平铺无嵌套
    void higherLevelInserted();     // 全 h4 中插入 h3：h3 最左、h4 统一缩进
    void mixedLevelsUnifiedBase();  // 跳级混合：档位 = level − 文档最高级别
    void normalNestingIndent();     // 常规 h1-h3：0/1/2 档
    void indentLevelFunction();     // 纯函数边界
    void collapseHidesSubordinate();// 折叠隐藏下级段、展开恢复
    void collapseRespectsNested();  // 外层展开后段内已折叠的子段保持折叠
    void hasChildrenMarks();        // 箭头标记：紧邻下一行更深 ⇔ 有下级段

private:
    static QJsonArray headings(std::initializer_list<std::pair<int, const char *>> hs);
    static QList<int> indentsOf(const OutlineTree &tree);
    static QStringList textsOf(const OutlineTree &tree);
};

QJsonArray TestOutlineTree::headings(
    std::initializer_list<std::pair<int, const char *>> hs)
{
    QJsonArray arr;
    int pos = 1;
    for (const auto &h : hs) {
        QJsonObject obj;
        obj.insert(QStringLiteral("level"), h.first);
        obj.insert(QStringLiteral("text"), QString::fromUtf8(h.second));
        obj.insert(QStringLiteral("pos"), pos++);
        arr.append(obj);
    }
    return arr;
}

QList<int> TestOutlineTree::indentsOf(const OutlineTree &tree)
{
    auto *model = qobject_cast<QStandardItemModel *>(tree.model());
    QList<int> out;
    QStandardItem *root = model->invisibleRootItem();
    for (int i = 0; i < root->rowCount(); ++i)
        out << root->child(i)->data(OutlineTree::RoleIndentLevel).toInt();
    return out;
}

QStringList TestOutlineTree::textsOf(const OutlineTree &tree)
{
    auto *model = qobject_cast<QStandardItemModel *>(tree.model());
    QStringList out;
    QStandardItem *root = model->invisibleRootItem();
    for (int i = 0; i < root->rowCount(); ++i)
        out << root->child(i)->text();
    return out;
}

// ① 全 h4：所有项平铺在根下、缩进档位全部为 0（对齐最左）
void TestOutlineTree::sameLevelAllAlign()
{
    OutlineTree tree;
    tree.updateHeadings(headings({{4, "四一"}, {4, "四二"}, {4, "四三"}}));

    auto *model = qobject_cast<QStandardItemModel *>(tree.model());
    QStandardItem *root = model->invisibleRootItem();
    QCOMPARE(root->rowCount(), 3);   // 平铺：全部为根的直接子项
    for (int i = 0; i < root->rowCount(); ++i)
        QCOMPARE(root->child(i)->rowCount(), 0);
    QCOMPARE(indentsOf(tree), QList<int>({0, 0, 0}));
    QCOMPARE(textsOf(tree),
             QStringList({QString::fromUtf8("四一"), QString::fromUtf8("四二"),
                          QString::fromUtf8("四三")}));
}

// ② 全 h4 中间插入 h3：标题3 档位 0（最左），其余标题4 统一 1 档
void TestOutlineTree::higherLevelInserted()
{
    OutlineTree tree;
    tree.updateHeadings(headings({{4, "四一"}, {4, "四二"}, {3, "三"}, {4, "四三"}}));

    QCOMPARE(indentsOf(tree), QList<int>({1, 1, 0, 1}));
    // 文档顺序保持线性：标题3 仍排在四二之后
    QCOMPARE(textsOf(tree),
             QStringList({QString::fromUtf8("四一"), QString::fromUtf8("四二"),
                          QString::fromUtf8("三"), QString::fromUtf8("四三")}));
}

// ③ 跳级混合（test.md 场景 h4,h3,h4,h2,h4,h5，最高级别=h2）：
// 档位 2/1/2/0/2/3——标题3 比标题4 更左、三个标题4 同档、标题2 最左
void TestOutlineTree::mixedLevelsUnifiedBase()
{
    OutlineTree tree;
    tree.updateHeadings(headings({{4, "标题4"}, {3, "标题3"}, {4, "4-1"},
                                  {2, "标题2"}, {4, "4-2"}, {5, "5-1"}}));

    QCOMPARE(indentsOf(tree), QList<int>({2, 1, 2, 0, 2, 3}));
    const QList<int> ind = indentsOf(tree);
    QVERIFY2(ind[1] < ind[0], "标题3 必须比标题4 更左");
    QVERIFY2(ind[0] == ind[2] && ind[2] == ind[4], "三个标题4 必须同档对齐");
}

// 常规 h1/h2/h3 文档：0/1/2 档
void TestOutlineTree::normalNestingIndent()
{
    OutlineTree tree;
    tree.updateHeadings(headings({{1, "一"}, {2, "二"}, {3, "三"}}));

    QCOMPARE(indentsOf(tree), QList<int>({0, 1, 2}));
}

// 纯函数：clamp 下界 0；档位 = level − minLevel
void TestOutlineTree::indentLevelFunction()
{
    QCOMPARE(OutlineTree::indentLevelForHeading(1, 1), 0);
    QCOMPARE(OutlineTree::indentLevelForHeading(6, 4), 2);
    QCOMPARE(OutlineTree::indentLevelForHeading(1, 3), 0);   // 越界防御
}

// 箭头标记：紧邻下一行档位更深 ⇔ 有下级段；段尾与同级行无箭头
void TestOutlineTree::hasChildrenMarks()
{
    OutlineTree tree;
    // 档位 2/1/2/0/2/3：行0(2) 后是 1（更浅）→ 无；行1(1) 后是 2 → 有；
    // 行2(2) 后是 0 → 无；行3(0) 后是 2 → 有；行4(2) 后是 3 → 有；行5 无
    tree.updateHeadings(headings({{4, "a"}, {3, "b"}, {4, "c"},
                                  {2, "d"}, {4, "e"}, {5, "f"}}));
    auto *model = qobject_cast<QStandardItemModel *>(tree.model());
    QStandardItem *root = model->invisibleRootItem();
    const bool expected[] = {false, true, false, true, true, false};
    for (int i = 0; i < root->rowCount(); ++i)
        QCOMPARE(root->child(i)->data(OutlineTree::RoleHasChildren).toBool(),
                 expected[i]);
}

// 折叠隐藏下级段、展开恢复；折叠不越段（后续更浅/同级行保持可见）
void TestOutlineTree::collapseHidesSubordinate()
{
    OutlineTree tree;
    // 档位 0/1/2/0：d 的下级段 = 行1..2（不含行3）
    tree.updateHeadings(headings({{2, "d"}, {4, "e"}, {5, "f"}, {2, "g"}}));
    // 平铺列表：所有顶层行的 parent 均为无效 index（不可见根）
    const QModelIndex root;
    QVERIFY(!tree.isRowHidden(1, root) && !tree.isRowHidden(2, root));

    tree.toggleCollapse(tree.model()->index(0, 0));   // 折叠 d
    QVERIFY(tree.isRowHidden(1, root));
    QVERIFY(tree.isRowHidden(2, root));
    QVERIFY(!tree.isRowHidden(3, root));              // 同级 g 不受影响
    QVERIFY(tree.model()->index(0, 0)
                .data(OutlineTree::RoleCollapsed).toBool());

    tree.toggleCollapse(tree.model()->index(0, 0));   // 展开 d
    QVERIFY(!tree.isRowHidden(1, root) && !tree.isRowHidden(2, root));
}

// 级联折叠：子段自身已折叠时，外层折叠再展开后子段保持折叠（孙代仍隐藏）
void TestOutlineTree::collapseRespectsNested()
{
    OutlineTree tree;
    // 档位 0/1/2/0：b(行1) 下有 c(行2)
    tree.updateHeadings(headings({{2, "a"}, {3, "b"}, {4, "c"}, {2, "d"}}));
    const QModelIndex root;

    tree.toggleCollapse(tree.model()->index(1, 0));   // 先折叠 b（隐藏 c）
    QVERIFY(tree.isRowHidden(2, root));

    tree.toggleCollapse(tree.model()->index(0, 0));   // 再折叠 a（隐藏 b、c）
    QVERIFY(tree.isRowHidden(1, root) && tree.isRowHidden(2, root));

    tree.toggleCollapse(tree.model()->index(0, 0));   // 展开 a：b 恢复，c 仍折叠
    QVERIFY(!tree.isRowHidden(1, root));
    QVERIFY(tree.isRowHidden(2, root));
}

QTEST_MAIN(TestOutlineTree)
#include "test_outlinetree.moc"
