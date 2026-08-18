#ifndef MD_EDITOR_EDITORTAB_H
#define MD_EDITOR_EDITORTAB_H

#include <QWidget>
#include <QWebEngineView>
#include <QWebChannel>
#include <QMenu>
#include "../core/EditorBridge.h"

// 自定义 WebEngineView，重写上下文菜单为中文
class EditorWebView : public QWebEngineView {
    Q_OBJECT
public:
    explicit EditorWebView(QWidget *parent = nullptr);
    void setInTable(bool inTable);
    void setHasSelection(bool has);
    void setLinkUrl(const QString &url);

protected:
    void contextMenuEvent(QContextMenuEvent *event) override;

private:
    QMenu *m_contextMenu;
    bool m_inTable;
    bool m_hasSelection;
    QString m_linkUrl;
    void setupContextMenu();
};

class EditorTab : public QWidget {
    Q_OBJECT
public:
    explicit EditorTab(int tabId, QWidget *parent = nullptr);

    EditorBridge *bridge() const;
    QWebEngineView *webView() const;

    void loadFile(const QString &path, const QString &content);
    void setFilePath(const QString &path);
    void setEditorTheme(const QString &theme);
    void setEditorLanguage(const QString &lang);
    void setEditorFont(const QString &family, int size);
    void setEditorWidth(int width);
    void applyCustomCss(const QString &css);
    void setEditorSettings(const QJsonObject &settings);
    void setInTable(bool inTable);
    void setHasSelection(bool has);
    void setLinkUrl(const QString &url);
    void setPermanentReadOnly(bool permanent);
    bool isPermanentReadOnly() const;
    void setSourceMode(bool on);
    bool isSourceMode() const;
    void runJsCommand(const QString &js);

signals:
    void pageLoadFinished(bool ok);

private slots:
    void onPageLoadFinished(bool ok);

private:
    void setupWebChannel();
    void pushDocDir();
    int m_tabId;
    EditorWebView *m_webView;
    QWebChannel *m_webChannel;
    EditorBridge *m_bridge;
    bool m_pageLoaded;
    bool m_permanentReadOnly = false;
    bool m_sourceMode = false;
};

#endif // MD_EDITOR_EDITORTAB_H
