#ifndef MD_EDITOR_FILETREE_H
#define MD_EDITOR_FILETREE_H

#include <QTreeView>
#include <QFileSystemModel>

class FileTree : public QTreeView {
    Q_OBJECT
public:
    explicit FileTree(QWidget *parent = nullptr);
    void setRootPath(const QString &path);
    QString rootPath() const;

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
