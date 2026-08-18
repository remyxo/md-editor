#include "FileTree.h"
#include <QMenu>
#include <QProcess>
#include <QInputDialog>
#include <QMessageBox>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QLineEdit>

FileTree::FileTree(QWidget *parent)
    : QTreeView(parent)
    , m_model(new QFileSystemModel(this))
{
    m_model->setNameFilters({"*.md", "*.markdown"});
    m_model->setNameFilterDisables(false); // 隐藏不匹配的文件
    m_model->setFilter(QDir::AllDirs | QDir::Files | QDir::NoDotAndDotDot);
    setModel(m_model);

    // 隐藏大小、类型等列，只显示名称
    setHeaderHidden(true);
    for (int i = 1; i < m_model->columnCount(); ++i) {
        hideColumn(i);
    }

    setContextMenuPolicy(Qt::CustomContextMenu);
    connect(this, &QTreeView::doubleClicked, this, &FileTree::onDoubleClicked);
    connect(this, &QTreeView::customContextMenuRequested, this, &FileTree::showContextMenu);
}

void FileTree::setRootPath(const QString &path)
{
    m_rootPath = path;
    setRootIndex(m_model->setRootPath(path));
}

QString FileTree::rootPath() const
{
    return m_rootPath;
}

void FileTree::onDoubleClicked(const QModelIndex &index)
{
    QFileInfo fileInfo = m_model->fileInfo(index);
    if (fileInfo.isFile()) {
        emit fileDoubleClicked(fileInfo.absoluteFilePath());
    }
}

void FileTree::showContextMenu(const QPoint &pos)
{
    QModelIndex index = indexAt(pos);
    QMenu menu;

    QAction *newFileAct = menu.addAction(tr("新建 Markdown 文件"));
    menu.addSeparator();

    QAction *renameAct = nullptr;
    QAction *deleteAct = nullptr;
    if (index.isValid()) {
        renameAct = menu.addAction(tr("重命名"));
        deleteAct = menu.addAction(tr("删除"));
        menu.addSeparator();
    }

    QAction *openFolderAct = menu.addAction(tr("在文件管理器中打开"));

    QAction *selected = menu.exec(viewport()->mapToGlobal(pos));

    if (selected == newFileAct) {
        QString parentPath = m_rootPath;
        if (index.isValid()) {
            QFileInfo fi = m_model->fileInfo(index);
            parentPath = fi.isDir() ? fi.absoluteFilePath() : fi.absolutePath();
        }
        QString name = QInputDialog::getText(this, tr("新建文件"), tr("文件名:"), QLineEdit::Normal, "untitled.md");
        if (!name.isEmpty()) {
            QString newPath = parentPath + "/" + name;
            QFile file(newPath);
            if (file.open(QIODevice::WriteOnly)) {
                file.close();
            }
        }
    } else if (selected == renameAct && renameAct) {
        QFileInfo fi = m_model->fileInfo(index);
        QString newName = QInputDialog::getText(this, tr("重命名"), tr("新文件名:"), QLineEdit::Normal, fi.fileName());
        if (!newName.isEmpty() && newName != fi.fileName()) {
            QString newPath = fi.absolutePath() + "/" + newName;
            QFile::rename(fi.absoluteFilePath(), newPath);
        }
    } else if (selected == deleteAct && deleteAct) {
        QFileInfo fi = m_model->fileInfo(index);
        QMessageBox::StandardButton r = QMessageBox::question(
            this, tr("确认删除"),
            tr("确定要删除 '%1' 吗？").arg(fi.fileName())
        );
        if (r == QMessageBox::Yes) {
            if (fi.isDir()) {
                QDir(fi.absoluteFilePath()).removeRecursively();
            } else {
                QFile::remove(fi.absoluteFilePath());
            }
        }
    } else if (selected == openFolderAct) {
        QString path = m_rootPath;
        if (index.isValid()) {
            QFileInfo fi = m_model->fileInfo(index);
            path = fi.isDir() ? fi.absoluteFilePath() : fi.absolutePath();
        }
        QProcess::startDetached("xdg-open", {path});
    }
}

// 右键菜单与对话框文案均动态创建，每次显示时经 tr() 取当前语言，无需缓存重设
void FileTree::retranslateUi()
{
}
