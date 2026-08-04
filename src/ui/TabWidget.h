#ifndef MD_EDITOR_TABWIDGET_H
#define MD_EDITOR_TABWIDGET_H

#include <QTabWidget>
#include <QMap>

class EditorTab;
class EditorBridge;

class TabWidget : public QTabWidget {
    Q_OBJECT
public:
    explicit TabWidget(QWidget *parent = nullptr);

    EditorTab *openNewTab();
    EditorTab *openFileTab(const QString &path, const QString &content);
    EditorTab *currentEditorTab() const;
    EditorTab *tabAt(int index) const;

    void setTabTitle(int index, const QString &title);
    void setTabDirty(int index, bool dirty);
    void markTabSaved(int index, const QString &filePath);

signals:
    void currentTabChanged(int tabId);
    void editorBridgeCreated(EditorBridge *bridge);
    void tabSaveRequested(int index);

private slots:
    void onTabCloseRequested(int index);
    void onCurrentChanged(int index);

private:
    int m_nextTabId;
    QMap<int, EditorTab *> m_tabs;
    QString generateUntitledName();
};

#endif
