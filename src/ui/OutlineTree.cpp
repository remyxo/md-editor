#include "OutlineTree.h"
#include <QJsonObject>

OutlineTree::OutlineTree(QWidget *parent)
    : QTreeView(parent)
    , m_model(new QStandardItemModel(this))
{
    m_model->setHorizontalHeaderLabels({"大纲"});
    setModel(m_model);
    setHeaderHidden(true);
    setEditTriggers(QAbstractItemView::NoEditTriggers);

    connect(this, &QTreeView::clicked, this, &OutlineTree::onItemClicked);
}

void OutlineTree::updateHeadings(const QJsonArray &headings)
{
    m_model->clear();

    if (headings.isEmpty()) return;

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
}

void OutlineTree::onItemClicked(const QModelIndex &index)
{
    int pos = index.data(Qt::UserRole).toInt();
    emit headingClicked(pos);
}
