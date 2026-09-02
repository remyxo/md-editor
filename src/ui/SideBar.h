#ifndef MD_EDITOR_SIDEBAR_H
#define MD_EDITOR_SIDEBAR_H

#include <QWidget>
#include <QPushButton>

class CollapsibleSection;
class FileTree;
class OutlineTree;
class QSplitter;
class QPropertyAnimation;
class QShowEvent;

// 侧边栏：大纲在上、目录树在下，垂直 splitter 分隔（中间可拖动调高，初始 6:4）。
// 单面板收起 → 收成一条标题栏并沉到侧边栏底部；两面板全收 → 侧边栏整体宽度动画收起。
class SideBar : public QWidget {
    Q_OBJECT
public:
    explicit SideBar(QWidget *parent = nullptr);

    FileTree *fileTree() const;
    OutlineTree *outlineTree() const;

    // 双开关：大纲/目录树面板独立折叠；两面板全收时侧边栏整体宽度收起
    void setFileTreeVisible(bool visible);
    void setOutlineVisible(bool visible);

    // 启动恢复面板初始态：MainWindow 读 QSettings 后调用（无动画、阻断信号链直置）
    void applyInitialPanes(bool fileTree, bool outline);

    // 语言切换后重设全部文案
    void retranslateUi();

signals:
    void openFolderClicked();
    // 面板可见性变化（含标题栏点击路径），供 MainWindow 同步视图菜单勾选
    void fileTreeVisibleChanged(bool visible);
    void outlineVisibleChanged(bool visible);

protected:
    void showEvent(QShowEvent *event) override;   // 首次显示布局就绪后设初始 6:4 分隔

private:
    void setupUi();
    void updateWholeCollapse();
    void updateSectionOrder();
    void rememberExpandedSizes();
    void restoreExpandedSizes();
    void animateWidth(int target);
    void onWidthAnimFinished();

    CollapsibleSection *m_fileSection;
    CollapsibleSection *m_outlineSection;
    FileTree *m_fileTree;
    OutlineTree *m_outlineTree;
    QPushButton *m_openFolderBtn;
    QSplitter *m_splitter;
    QPropertyAnimation *m_widthAnim;
    bool m_fileTreeVisible = true;
    bool m_outlineVisible = true;
    bool m_wholeCollapsed = false;
    bool m_initialSplitSet = false;
    int m_savedWidth = 240;
    QList<int> m_savedSplitSizes;   // 两面板都展开时的分隔比例，收起时保存/重开时恢复
};

#endif // MD_EDITOR_SIDEBAR_H
