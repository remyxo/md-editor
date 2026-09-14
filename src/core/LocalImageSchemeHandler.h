#ifndef MD_EDITOR_LOCALIMAGESCHEMEHANDLER_H
#define MD_EDITOR_LOCALIMAGESCHEMEHANDLER_H

#include <QWebEngineUrlSchemeHandler>
#include <QSet>

// 自定义 localimg:// 协议处理器。
//
// 背景：编辑器页面从 qrc:/web/editor.html 加载（页面源为 qrc）。Chromium 不允许 qrc 源
// 页面加载 file:// 子资源（LocalContentCanAccessFileUrls 只对 file 源页面生效），
// 故工具栏插入/拖拽落盘的本地图片以 file:// 渲染时被拦截
// （"Not allowed to load local resource"）。
//
// 方案：editor.js 把本地图片 src 解析为 localimg://<绝对路径>，由本 Handler 读取磁盘
// 图片字节并以正确 MIME 返回，绕过 file:// 跨源限制。attrs.src 仍存原始相对/绝对路径，
// file:// 永不到达 .md，往返保真不受影响。
//
// 安全：仅服务 MIME 为 image/* 的常规文件，且路径（canonical 化后）必须落在
// 已注册的文档目录白名单内（addAllowedDir）——避免成为任意文件读取通道。
class LocalImageSchemeHandler : public QWebEngineUrlSchemeHandler {
    Q_OBJECT
public:
    explicit LocalImageSchemeHandler(QObject *parent = nullptr);

    // 注册允许服务的文档目录（EditorTab::setFilePath 时调用；进程级集合）。
    // 按引用计数管理：同目录多标签页打开各持一引，全部关闭后撤销（历史上
    // 只增不减，长期会话的授权面随打开过的目录单调增长）
    static void addAllowedDir(const QString &dir);
    // 撤销一次注册（EditorTab 换文件路径 / 标签页析构时调用；计数归零移除）
    static void removeAllowedDir(const QString &dir);

protected:
    void requestStarted(QWebEngineUrlRequestJob *job) override;

private:
    static QSet<QString> s_allowedDirs;
    static QHash<QString, int> s_dirRefs;
};

// 自定义协议名（小写 ASCII）
// 必须在 QWebEngineProfile 首次创建前调用 QWebEngineUrlScheme::registerScheme 注册
constexpr const char *kLocalImageScheme = "localimg";

#endif // MD_EDITOR_LOCALIMAGESCHEMEHANDLER_H
