#ifndef MD_EDITOR_EDITORTAB_H
#define MD_EDITOR_EDITORTAB_H

#include <QWidget>
#include <QWebEngineView>
#include <QWebChannel>
#include "../core/EditorBridge.h"

class EditorTab : public QWidget {
    Q_OBJECT
public:
    explicit EditorTab(int tabId, QWidget *parent = nullptr);

    EditorBridge *bridge() const;
    QWebEngineView *webView() const;

    void loadFile(const QString &path, const QString &content);
    void setEditorTheme(const QString &theme);
    void runJsCommand(const QString &js);

signals:
    void pageLoadFinished(bool ok);

private slots:
    void onPageLoadFinished(bool ok);

private:
    void setupWebChannel();
    int m_tabId;
    QWebEngineView *m_webView;
    QWebChannel *m_webChannel;
    EditorBridge *m_bridge;
    bool m_pageLoaded;
};

#endif // MD_EDITOR_EDITORTAB_H
