#ifndef MD_EDITOR_FILETREE_H
#define MD_EDITOR_FILETREE_H

#include <QTreeView>
#include <QFileSystemModel>

class FileTree : public QTreeView {
    Q_OBJECT
public:
    explicit FileTree(QWidget *parent = nullptr);
    void setRootPath(const QString &path);

    // 语言切换后重设全部文案（Task 10 调用；右键菜单动态创建，故此处为空实现）
    void retranslateUi();

signals:
    void fileDoubleClicked(const QString &filePath);

private slots:
    void onDoubleClicked(const QModelIndex &index);
    void showContextMenu(const QPoint &pos);

private:
    QFileSystemModel *m_model;
    QString m_rootPath;
};

#endif // MD_EDITOR_FILETREE_H
