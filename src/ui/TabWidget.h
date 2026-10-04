#ifndef MD_EDITOR_TABWIDGET_H
#define MD_EDITOR_TABWIDGET_H

#include <QTabWidget>
#include <QMap>
#include <QPointer>
#include <QSet>

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

private:
    int m_nextTabId;
    QPointer<EditorTab> m_pendingCloseTab;    // 待关闭 tab（记指针防下标漂移；QPointer 防 tab 已销毁的悬垂解引用）
    QMap<int, EditorTab *> m_tabs;
    QSet<EditorTab *> m_untitledTabs;   // 显式追踪未命名 tab（帮助页无 tooltip 但非未命名）
    QString generateUntitledName();
};

#endif
