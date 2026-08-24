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

    // 双开关：目录树/大纲面板独立显隐，全关时侧边栏整体隐藏
    void setFileTreeVisible(bool visible);
    void setOutlineVisible(bool visible);

    // 语言切换后重设全部文案（Task 10 调用）
    void retranslateUi();

signals:
    void openFolderClicked();

private:
    void setupUi();
    void updateVisibility();
    FileTree *m_fileTree;
    OutlineTree *m_outlineTree;
    QTabWidget *m_tabWidget;
    QPushButton *m_openFolderBtn;
    bool m_fileTreeVisible = true;
    bool m_outlineVisible = true;
};

#endif // MD_EDITOR_SIDEBAR_H
