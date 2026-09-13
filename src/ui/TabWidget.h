#ifndef MD_EDITOR_TABWIDGET_H
#define MD_EDITOR_TABWIDGET_H

#include <QTabWidget>
#include <QMap>
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
    EditorTab *m_pendingCloseTab = nullptr;   // 待关闭 tab（记指针防下标漂移）
    QMap<int, EditorTab *> m_tabs;
    QSet<EditorTab *> m_untitledTabs;   // 显式追踪未命名 tab（帮助页无 tooltip 但非未命名）
    QString generateUntitledName();
};

#endif
