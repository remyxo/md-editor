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
    void historyChanged(bool canUndo, bool canRedo);
    // 模态编辑面板（Mermaid/数学公式）打开/关闭通知，供 MainWindow Esc 快捷键判断优先级
    void modalDialogChanged(bool open);
    // JS → C++：请求把粘贴/拖拽的图片字节落盘（requestId 用于回调定位占位图）
    void requestImageSave(const QString &requestId, const QString &base64,
                          const QString &ext, const QString &suggestedName);

signals:
    // 通知 C++ UI 组件
    void cursorPositionChanged(int line, int col, int tabId);
    void dirtyStateChanged(bool dirty, int tabId);
    void headingsUpdated(const QJsonArray &headings, int tabId);
    void formatStateChanged(const QJsonObject &format, int tabId);
    void wordCountUpdated(int chars, int words, int tabId);
    void historyStateChanged(bool canUndo, bool canRedo, int tabId);
    void modalDialogStateChanged(bool open, int tabId);
    void imageSaveRequested(const QString &requestId, const QString &base64,
                            const QString &ext, const QString &suggestedName, int tabId);

private:
    int m_tabId;
};

#endif // MD_EDITOR_EDITORBRIDGE_H
