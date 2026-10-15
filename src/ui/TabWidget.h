#ifndef MD_EDITOR_TABWIDGET_H
#define MD_EDITOR_TABWIDGET_H

#include <QTabWidget>
#include <QMap>
#include <QPointer>
#include <QSet>
#include <QVector>

class EditorTab;
class EditorBridge;

class TabWidget : public QTabWidget {
    Q_OBJECT
public:
    explicit TabWidget(QWidget *parent = nullptr);

    EditorTab *openNewTab();
    EditorTab *openFileTab(const QString &path, const QString &content, bool lazy = false);
    EditorTab *openHelpTab(const QString &title, const QString &mdContent);
    // 同路径文件已打开时聚焦既有标签页并返回之；未打开返回 nullptr。
    // 供「重开即聚焦」的两级使用：MainWindow 打开前的早退（跳过大文件确认
    // 与重复读盘）与 openFileTab 入口去重（覆盖会话恢复等全部调用方）
    EditorTab *focusFileTab(const QString &path);
    EditorTab *currentEditorTab() const;
    EditorTab *tabAt(int index) const;

    void setTabDirty(int index, bool dirty);
    void markTabSaved(int index, const QString &filePath);
    void retranslateUi();
    // 程序化直接关闭标签页（无脏检查/保存弹窗——调用方须确保非脏）。收尾走
    // onTabCloseRequested 同款路径（tabClosed 信号→MainWindow 的 unwatch 等
    // 联动依赖它）。用于惰性恢复标签激活读盘失败（文件在恢复后被删/损坏）
    void closeTabDirect(EditorTab *tab);

    // 批量关闭（右键菜单「关闭其他/右侧/全部」）：逐个走 requestCloseTab
    // 的脏检查流程——非脏直关；脏标签弹三键框，保存走异步链（完成后自动
    // 继续队列），取消则中止剩余（已关闭的不恢复）。列表顺序即关闭顺序
    void closeTabs(const QVector<EditorTab *> &tabs);

signals:
    void currentTabChanged(int tabId);
    void editorBridgeCreated(EditorBridge *bridge);
    void tabSaveRequested(int index);
    void tabClosed(int index);

public slots:
    void completePendingTabClose();

private slots:
    void onTabCloseRequested(int index);
    void onCurrentChanged(int index);
    // 标签栏右键菜单：关闭 / 关闭其他 / 关闭右侧 / 关闭全部
    void onTabBarContextMenu(const QPoint &pos);

private:
    // 单个标签的关闭请求全流程（脏检查+三键弹窗）。返回：
    //   Closed      —— 已同步关闭（非脏或选「不保存」）
    //   SavePending —— 已发 tabSaveRequested，等 completePendingTabClose 收尾
    //   Cancelled   —— 用户取消，未关闭
    enum class CloseResult { Closed, SavePending, Cancelled };
    CloseResult requestCloseTab(int index);

    // 驱动批量关闭队列：循环处理队首（失效/已关的弹出续走），脏弹窗的
    // 保存/取消路径由此挂起（保存完成经 completePendingTabClose 回灌）
    void processBatchCloseQueue();

    // 标签栏事件过滤：中键点击关闭（复用关闭请求的脏检查流程）
    bool eventFilter(QObject *obj, QEvent *ev) override;

    // 关闭收尾公共段：发 tabClosed、清理登记、移除并销毁（下标须有效）
    void closeTabAt(int index);
    int m_nextTabId;
    QPointer<EditorTab> m_pendingCloseTab;    // 待关闭 tab（记指针防下标漂移；QPointer 防 tab 已销毁的悬垂解引用）
    QVector<QPointer<EditorTab>> m_batchCloseQueue;   // 批量关闭待处理队列（右键菜单）
    QMap<int, EditorTab *> m_tabs;
    QSet<EditorTab *> m_untitledTabs;   // 显式追踪未命名 tab（帮助页无 tooltip 但非未命名）
    QString generateUntitledName();
};

#endif
