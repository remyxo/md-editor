#ifndef MD_EDITOR_SIDEBAR_H
#define MD_EDITOR_SIDEBAR_H

#include <QWidget>
#include <QPushButton>

class CollapsibleSection;
class FileTree;
class OutlineTree;
class QPropertyAnimation;

// 侧边栏：目录树/大纲两个可独立折叠面板（CollapsibleSection）上下堆叠。
// 单面板收起 → 收成一条标题栏；两面板全收 → 侧边栏整体宽度动画收起。
class SideBar : public QWidget {
    Q_OBJECT
public:
    explicit SideBar(QWidget *parent = nullptr);

    FileTree *fileTree() const;
    OutlineTree *outlineTree() const;

    // 双开关：目录树/大纲面板独立折叠；两面板全收时侧边栏整体宽度收起
    void setFileTreeVisible(bool visible);
    void setOutlineVisible(bool visible);

    // 语言切换后重设全部文案
    void retranslateUi();

signals:
    void openFolderClicked();

private:
    void setupUi();
    void updateWholeCollapse();
    void animateWidth(int target);
    void onWidthAnimFinished();

    CollapsibleSection *m_fileSection;
    CollapsibleSection *m_outlineSection;
    FileTree *m_fileTree;
    OutlineTree *m_outlineTree;
    QPushButton *m_openFolderBtn;
    QPropertyAnimation *m_widthAnim;
    bool m_fileTreeVisible = true;
    bool m_outlineVisible = true;
    bool m_wholeCollapsed = false;
    int m_savedWidth = 240;
};

#endif // MD_EDITOR_SIDEBAR_H
