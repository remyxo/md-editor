#ifndef MD_EDITOR_SIDEBAR_H
#define MD_EDITOR_SIDEBAR_H

#include <QWidget>
#include <QPushButton>

class FileTree;
class OutlineTree;

class SideBar : public QWidget {
    Q_OBJECT
public:
    explicit SideBar(QWidget *parent = nullptr);

    FileTree *fileTree() const;
    OutlineTree *outlineTree() const;

    void setCollapsed(bool collapsed);
    bool isCollapsed() const;

signals:
    void openFolderClicked();

private:

private:
    void setupUi();
    FileTree *m_fileTree;
    OutlineTree *m_outlineTree;
    QPushButton *m_openFolderBtn;
    bool m_collapsed;
};

#endif // MD_EDITOR_SIDEBAR_H
