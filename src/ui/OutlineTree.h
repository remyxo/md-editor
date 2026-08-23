#ifndef MD_EDITOR_OUTLINETREE_H
#define MD_EDITOR_OUTLINETREE_H

#include <QTreeView>
#include <QStandardItemModel>
#include <QJsonArray>

class OutlineTree : public QTreeView {
    Q_OBJECT
public:
    explicit OutlineTree(QWidget *parent = nullptr);

    // 语言切换后重设全部文案（Task 10 调用）
    void retranslateUi();

public slots:
    void updateHeadings(const QJsonArray &headings);
    // 光标位置（ProseMirror 文档 pos）变化时高亮当前所属标题项
    void highlightAtPos(int pos);

signals:
    void headingClicked(int pos);

private slots:
    void onItemClicked(const QModelIndex &index);

private:
    void applyHighlight();   // 按最近光标位置高亮当前所属标题
    QStandardItemModel *m_model;
    int m_currentPos = -1;   // 最近一次光标位置（ProseMirror 文档 pos，-1 表示尚无）
};

#endif // MD_EDITOR_OUTLINETREE_H
