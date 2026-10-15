#include "OutlineTree.h"
#include <QJsonObject>
#include <QStyledItemDelegate>
#include <QStyleOptionViewItem>
#include <QPainter>
#include <QMouseEvent>
#include <QApplication>

namespace {

// 每级缩进步长（像素）。对齐 MS Office 大纲观感：每级一至两个字符宽的
// 紧凑档位，六级全深也不过多挤压标题文本
constexpr int kIndentPerLevel = 16;
// 箭头槽宽（像素）：每行最左侧统一预留（有无下级都留，保证同档文本对齐），
// 折叠箭头画在槽内
constexpr int kArrowSlot = 16;

// 大纲条目绘制：缩进档位存于 RoleIndentLevel（级别 − 文档最高级别），
// 文本区右移「档位×步长 + 箭头槽」绘制；有下级段（RoleHasChildren）的行
// 在箭头槽内画树形箭头（PE_IndicatorBranch，与 QTreeView 原生同款），
// 指向由 RoleCollapsed 决定。选中/悬停高亮与省略号由
// QStyledItemDelegate 按平移后的矩形常规处理（吃到主题 QSS 配色）。
class OutlineItemDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override
    {
        const int level = qMax(0, index.data(OutlineTree::RoleIndentLevel).toInt());
        const int offset = level * kIndentPerLevel;

        // 下级折叠箭头（画在内容区左侧的缩进带内，不与背景块重叠）
        if (index.data(OutlineTree::RoleHasChildren).toBool()) {
            QStyleOptionViewItem branch = option;
            branch.rect = QRect(option.rect.x() + offset + 2,
                                option.rect.y() + (option.rect.height() - 12) / 2,
                                12, 12);
            branch.state |= QStyle::State_Children;
            if (!index.data(OutlineTree::RoleCollapsed).toBool())
                branch.state |= QStyle::State_Open;
            QStyle *s = option.widget ? option.widget->style()
                                      : QApplication::style();
            s->drawPrimitive(QStyle::PE_IndicatorBranch, &branch, painter,
                             option.widget);
        }

        QStyleOptionViewItem opt = option;
        const int shift = offset + kArrowSlot;
        opt.rect.translate(shift, 0);
        opt.rect.setWidth(opt.rect.width() - shift);
        QStyledItemDelegate::paint(painter, opt, index);
    }
};

} // namespace

OutlineTree::OutlineTree(QWidget *parent)
    : QTreeView(parent)
    , m_model(new QStandardItemModel(this))
{
    m_model->setHorizontalHeaderLabels({tr("大纲")});
    setModel(m_model);
    setHeaderHidden(true);
    setEditTriggers(QAbstractItemView::NoEditTriggers);
    // 平铺列表：无树形分支区，缩进与箭头均由 delegate 绘制
    setRootIsDecorated(false);
    setItemsExpandable(false);
    setItemDelegate(new OutlineItemDelegate(this));

    connect(this, &QTreeView::clicked, this, &OutlineTree::onItemClicked);
}

void OutlineTree::updateHeadings(const QJsonArray &headings)
{
    m_model->clear();
    // clear() 会清掉 header 标签，需重设
    m_model->setHorizontalHeaderLabels({tr("大纲")});

    if (headings.isEmpty()) {
        applyHighlight();
        return;
    }

    // 先求文档内最高标题级别（数字最小），作为缩进基准
    int minLevel = 6;
    for (const QJsonValue &val : headings)
        minLevel = qMin(minLevel, val.toObject()[QStringLiteral("level")].toInt());

    // 平铺构建：所有标题按文档顺序直接挂在根下，不按级别嵌套。
    // 嵌套树在跳级文档（如只有 h4、无 h1-h3）下要么把同级标题链式误嵌套、
    // 要么无法表达「标题3 比前面的标题4 更浅」（父级只能来自前面的标题）；
    // 统一基准缩进与文档序线性排列都与嵌套结构无关，由 delegate 绘制。
    // RoleHasChildren 依赖下一行档位，两遍构建（先算档位序列再建项）
    struct Entry { int level; QString text; int pos; int indent; };
    QVector<Entry> entries;
    entries.reserve(headings.size());
    int pos = 0;
    for (const QJsonValue &val : headings) {
        QJsonObject obj = val.toObject();
        Entry e;
        e.level = obj["level"].toInt();
        e.text = obj["text"].toString();
        e.pos = obj["pos"].toInt();
        e.indent = indentLevelForHeading(e.level, minLevel);
        entries.append(e);
    }

    QStandardItem *root = m_model->invisibleRootItem();
    for (int i = 0; i < entries.size(); ++i) {
        const Entry &e = entries[i];
        auto *item = new QStandardItem(e.text);
        item->setData(e.pos, Qt::UserRole);
        item->setData(e.level, RoleHeadingLevel);
        item->setData(e.indent, RoleIndentLevel);
        // 紧邻下一行档位更深 ⇔ 下级段非空（段内档位序列保证中间不会回落）
        item->setData(i + 1 < entries.size() && entries[i + 1].indent > e.indent,
                      RoleHasChildren);
        root->appendRow(item);
    }

    applyHighlight();
}

void OutlineTree::onItemClicked(const QModelIndex &index)
{
    int pos = index.data(Qt::UserRole).toInt();
    emit headingClicked(pos);
}

// 点击箭头槽切换折叠、不触发跳转；点击其余区域走默认（发出 clicked 跳转）
void OutlineTree::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        const QModelIndex idx = indexAt(event->pos());
        if (idx.isValid() && idx.data(RoleHasChildren).toBool()) {
            const int level = qMax(0, idx.data(RoleIndentLevel).toInt());
            const int arrowStart = level * kIndentPerLevel;
            if (event->pos().x() >= arrowStart
                && event->pos().x() < arrowStart + kArrowSlot) {
                toggleCollapse(idx);
                return;
            }
        }
    }
    QTreeView::mousePressEvent(event);
}

void OutlineTree::toggleCollapse(const QModelIndex &index)
{
    if (!index.isValid())
        return;
    QStandardItem *item = m_model->itemFromIndex(index);
    if (!item || !item->data(RoleHasChildren).toBool())
        return;

    const bool collapsed = !item->data(RoleCollapsed).toBool();
    item->setData(collapsed, RoleCollapsed);   // dataChanged 触发箭头重绘
    applySubordinateVisibility(index.row(), collapsed);
}

// 下级段结束行（不含）：自 row+1 起首个档位不深于 row 的行；无更深后续则
// 为末行+1
int OutlineTree::subordinateEnd(int row) const
{
    QStandardItem *root = m_model->invisibleRootItem();
    const int own = root->child(row)->data(RoleIndentLevel).toInt();
    int j = row + 1;
    for (; j < root->rowCount(); ++j)
        if (root->child(j)->data(RoleIndentLevel).toInt() <= own)
            break;
    return j;
}

// 折叠：整段隐藏。展开：段内逐行维护「生效折叠头」栈——行自身不被任何
// 折叠头罩住才显示；行自身已折叠时入栈，其更深后代继续隐藏
void OutlineTree::applySubordinateVisibility(int row, bool collapsed)
{
    QStandardItem *root = m_model->invisibleRootItem();
    const int end = subordinateEnd(row);
    const QModelIndex rootIdx = root->index();
    if (collapsed) {
        for (int j = row + 1; j < end; ++j)
            setRowHidden(j, rootIdx, true);
        return;
    }
    QList<int> stack;   // 生效中的折叠头的档位
    for (int j = row + 1; j < end; ++j) {
        QStandardItem *child = root->child(j);
        const int d = child->data(RoleIndentLevel).toInt();
        while (!stack.isEmpty() && stack.last() >= d)
            stack.removeLast();
        setRowHidden(j, rootIdx, !stack.isEmpty());
        if (child->data(RoleCollapsed).toBool())
            stack.append(d);
    }
}

void OutlineTree::highlightAtPos(int pos)
{
    m_currentPos = pos;
    applyHighlight();
}

void OutlineTree::applyHighlight()
{
    // 高亮「最后一个 pos <= 光标位置」的标题项（即光标当前所属的最近标题）。
    // 标题 pos 在 build 时按文档序递增，平铺列表自上而下首个满足者即当前小节。
    if (m_currentPos < 0) {
        setCurrentIndex(QModelIndex());
        return;
    }
    QStandardItem *match = nullptr;
    int bestPos = -1;
    QStandardItem *root = m_model->invisibleRootItem();
    for (int i = 0; i < root->rowCount(); ++i) {
        QStandardItem *child = root->child(i);
        const int p = child->data(Qt::UserRole).toInt();
        if (p <= m_currentPos && p > bestPos) {
            bestPos = p;
            match = child;
        }
    }

    if (match) {
        QModelIndex idx = match->index();
        setCurrentIndex(idx);
        scrollTo(idx);   // 高亮项滚入可见区
    } else {
        setCurrentIndex(QModelIndex());   // 光标位于首个标题之前：清除高亮
    }
}

// 语言切换后重设 header 文案
void OutlineTree::retranslateUi()
{
    m_model->setHorizontalHeaderLabels({tr("大纲")});
}
