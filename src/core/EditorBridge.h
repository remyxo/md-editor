#ifndef MD_EDITOR_EDITORBRIDGE_H
#define MD_EDITOR_EDITORBRIDGE_H

#include <QObject>
#include <QJsonArray>
#include <QJsonObject>

class EditorBridge : public QObject {
    Q_OBJECT
public:
    explicit EditorBridge(int tabId, QObject *parent = nullptr);
    int tabId() const;

public slots:
    // JS → C++：状态同步
    void cursorChanged(int line, int col);
    void contentChanged(bool dirty);
    void wordCountChanged(int chars, int words);
    void openUrl(const QString &url);
    void headingsChanged(const QString &json);
    void formatChanged(const QString &json);

signals:
    // 通知 C++ UI 组件
    void cursorPositionChanged(int line, int col, int tabId);
    void dirtyStateChanged(bool dirty, int tabId);
    void headingsUpdated(const QJsonArray &headings, int tabId);
    void formatStateChanged(const QJsonObject &format, int tabId);
    void wordCountUpdated(int chars, int words, int tabId);

private:
    int m_tabId;
};

#endif // MD_EDITOR_EDITORBRIDGE_H
