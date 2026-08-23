#include "OutlineTree.h"
#include <QJsonObject>
#include <functional>

OutlineTree::OutlineTree(QWidget *parent)
    : QTreeView(parent)
    , m_model(new QStandardItemModel(this))
{
    m_model->setHorizontalHeaderLabels({tr("大纲")});
    setModel(m_model);
    setHeaderHidden(true);
    setEditTriggers(QAbstractItemView::NoEditTriggers);

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

    QStandardItem *root = m_model->invisibleRootItem();
    // 用栈来构建层级关系
    QList<QStandardItem *> levelStack; // levelStack[i] = 最近的第 (i+1) 级标题的 item

    for (const QJsonValue &val : headings) {
        QJsonObject obj = val.toObject();
        int level = obj["level"].toInt();
        QString text = obj["text"].toString();
        int pos = obj["pos"].toInt();

        auto *item = new QStandardItem(text);
        item->setData(pos, Qt::UserRole);

        // 找到父节点：向上查找最近的比当前级别小的标题
        while (levelStack.size() >= level) {
            levelStack.removeLast();
        }

        QStandardItem *parent = levelStack.isEmpty() ? root : levelStack.last();
        parent->appendRow(item);

        levelStack.append(item);
    }

    expandAll();
    // 大纲重建会清空当前项高亮，按最近光标位置恢复（onSelectionUpdate 里
    // cursorChanged 先于 headingsChanged 到达，重建会擦掉刚设的高亮）
    applyHighlight();
}

void OutlineTree::onItemClicked(const QModelIndex &index)
{
    int pos = index.data(Qt::UserRole).toInt();
    emit headingClicked(pos);
}

void OutlineTree::highlightAtPos(int pos)
{
    m_currentPos = pos;
    applyHighlight();
}

void OutlineTree::applyHighlight()
{
    // 高亮「最后一个 pos <= 光标位置」的标题项（即光标当前所属的最近标题）。
    // 标题 pos 在 build 时按文档序递增，故记录最大匹配 pos 即当前小节。
    if (m_currentPos < 0) {
        setCurrentIndex(QModelIndex());
        return;
    }
    QStandardItem *match = nullptr;
    int bestPos = -1;
    std::function<void(QStandardItem *)> visit = [&](QStandardItem *item) {
        for (int i = 0; i < item->rowCount(); ++i) {
            QStandardItem *child = item->child(i);
            int p = child->data(Qt::UserRole).toInt();
            if (p <= m_currentPos && p > bestPos) {
                bestPos = p;
                match = child;
            }
            visit(child);
        }
    };
    visit(m_model->invisibleRootItem());

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
