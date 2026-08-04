#ifndef MD_EDITOR_OUTLINETREE_H
#define MD_EDITOR_OUTLINETREE_H

#include <QTreeView>
#include <QStandardItemModel>
#include <QJsonArray>

class OutlineTree : public QTreeView {
    Q_OBJECT
public:
    explicit OutlineTree(QWidget *parent = nullptr);

public slots:
    void updateHeadings(const QJsonArray &headings);

signals:
    void headingClicked(int pos);

private slots:
    void onItemClicked(const QModelIndex &index);

private:
    QStandardItemModel *m_model;
};

#endif // MD_EDITOR_OUTLINETREE_H
