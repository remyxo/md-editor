#ifndef MD_EDITOR_SIDEBAR_H
#define MD_EDITOR_SIDEBAR_H

#include <QWidget>
#include <QPushButton>

class FileTree;
class OutlineTree;
class QTabWidget;

class SideBar : public QWidget {
    Q_OBJECT
public:
    explicit SideBar(QWidget *parent = nullptr);

    FileTree *fileTree() const;
    OutlineTree *outlineTree() const;

    void setCollapsed(bool collapsed);
    bool isCollapsed() const;

    // 语言切换后重设全部文案（Task 10 调用）
    void retranslateUi();

signals:
    void openFolderClicked();

private:
    void setupUi();
    FileTree *m_fileTree;
    OutlineTree *m_outlineTree;
    QTabWidget *m_tabWidget;
    QPushButton *m_openFolderBtn;
    bool m_collapsed;
};

#endif // MD_EDITOR_SIDEBAR_H
