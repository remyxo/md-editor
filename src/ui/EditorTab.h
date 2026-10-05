#ifndef MD_EDITOR_EDITORTAB_H
#define MD_EDITOR_EDITORTAB_H

#include <QWidget>
#include <QWebEngineView>
#include <QWebChannel>
#include <QMenu>
#include "../core/EditorBridge.h"
#include "../core/ConversionQueue.h"

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
    ~EditorTab() override;

    EditorBridge *bridge() const;
    QWebEngineView *webView() const;

    void loadFile(const QString &path, const QString &content);
    void setFilePath(const QString &path);
    void setEncoding(const QString &encoding);
    // 编辑器页面已加载完成（onPageLoadFinished 置位）。页面未就绪时 loadFile
    // 等内容接口是 no-op——调用方（含 selftest 工装）须先等本标志再推内容
    bool pageLoaded() const { return m_pageLoaded; }
    // 惰性读盘（W4-2 会话恢复）：非活动标签页只登记路径不读盘，激活（成为
    // 当前标签页且页面就绪）时经 pendingPathReadRequested 交 MainWindow 读盘
    void setPendingPath(const QString &path);
    bool flushPendingLoad();
    // 仍有未读盘的惰性路径（保存/外部变更提示等需规避的窗口期）
    bool hasPendingRead() const;
    // 内容在途（I1）：loadFile 已入队转换、setContent 尚未在页面落地——此窗口
    // 内编辑器仍是空文档，保存快照必为空（writeTabToFile 据此跳过防空串写盘）
    bool isContentInFlight() const;
    QString encoding() const;
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
    void setSourceMode(bool on);
    bool isSourceMode() const;
    void runJsCommand(const QString &js);
    // 异步转换入队（同 tab 串行、完成回调 GUI 线程执行；本 tab 析构则在途
    // 结果直接丢弃）。loadFile 内部用于 toHtml；MainWindow 保存/切源码模式
    // 经此走 fromHtml——重负载转换不再阻塞 GUI 线程
    void enqueueConversion(ConversionQueue::ConvFn conv, ConversionQueue::Done done,
                           bool tiedToLoadEpoch = false);

signals:
    void pageLoadFinished(bool ok);
    // 惰性读盘触发（本 tab 已成为当前标签页且页面加载完成）：FileManager 与
    // 失败关标签的编排都在 MainWindow，读盘经此信号委托（同线程直连）
    void pendingPathReadRequested(const QString &path);

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
    QString m_pendingPath;      // 惰性读盘的待读路径（非活动标签页，激活时 flush）
    bool m_contentInFlight = false;   // 内容在途标志（I1，见 isContentInFlight）
    bool m_permanentReadOnly = false;
    bool m_sourceMode = false;
    // 本 tab 的 MD↔HTML 异步转换队列（loadFile/保存/切源码模式共用；随本
    // tab 析构而在途结果自然丢弃）。QObject 成员不设 parent：由成员析构销毁
    ConversionQueue m_convQueue;
    // 已注册进 localimg:// 目录围栏的文档目录（换文件/析构时撤销，见 setFilePath）
    QString m_allowedDir;
};

#endif // MD_EDITOR_EDITORTAB_H
