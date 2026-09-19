#ifndef MD_EDITOR_CDPPRINTER_H
#define MD_EDITOR_CDPPRINTER_H

#include <QObject>
#include <QMarginsF>
#include <QUrl>

class QWebEnginePage;
class QNetworkAccessManager;
class QWebSocket;
class QTimer;

// CDP（Chrome DevTools Protocol）打印通道：Chromium 原生 Page.printToPdf +
// displayHeaderFooter 页眉页脚模板，取代运行时 paged.js 分页引擎——2026-09-15
// 性能取证：paged.js 逐页 DOM 手术驱动 Blink 排版 ~0.8s/页（16 页 13s，纯 CPU，
// 外围 CSS/参数不可消），Chromium 原生分页同文档 <2.2s。CDP 走原生分页管线，
// 页眉页脚由打印模板注入（与 @page margin box 等价形态）。
//
// 前提：main() 在 QApplication 构造前调 enable()——inspector 端口是进程级
// Chromium flags，必须启动期定（不支持 0=随机，探空闲端口写入 flags）。
// 安全面：调试端口绑定 127.0.0.1 随机端口，可被本机进程连接（桌面单用户
// 环境下威胁模型 = 本机恶意进程，此时已失守）；连接错误实例/端口被抢时
// 由调用方回退链兜底（CDP → paged.js → Qt 原生打印）。
class CdpPrinter : public QObject {
    Q_OBJECT
public:
    struct Options {
        bool landscape = false;
        qreal paperWmm = 210.0;    // 纸宽（未换向）
        qreal paperHmm = 297.0;    // 纸高（未换向）
        QMarginsF marginsMm{18, 18, 18, 18};   // 左 上 右 下（与 PdfPageSpec 同语义）
        bool headerEnabled = false;
        bool footerEnabled = false;
        QString headerTitle;
    };

    // main() 里 QApplication 构造前调用：探测空闲端口写入
    // QTWEBENGINE_CHROMIUM_FLAGS（--remote-debugging-port=<port>）。重复调用幂等；
    // 探测失败静默跳过（debugPort()==0，调用方走非 CDP 路径）
    static void enable();
    // 已配置的调试端口（enable 成功且 Chromium 已起后有效），0 = 未启用
    static int debugPort();
    // printToPdf 方法可用性（2026-09-15 实测 Debian Qt 6.8.2 WebEngine 构建
    // 裁掉了 CDP Page.printToPdf——Page/Runtime domain 在、独缺此方法，与
    // DBus/CUPS 会话无关，是构建级差异；Windows/macOS 构建可能保留）。首次
    // 导出探测：'wasn't found' 错误即置 false 进程内缓存，后续导出不再尝试
    // （免每次白付页面加载+握手 ~0.7s）；乐观默认 true
    static bool pdfMethodAvailable() { return !s_methodBroken; }
    static void markPdfMethodBroken() { s_methodBroken = true; }

    // 页眉页脚模板（Chromium 打印模板语法：仅内联样式，pageNumber/totalPages
    // 由引擎注入文本）。拆静态供单测锁形
    static QString headerTemplate(const QString &title);
    static QString footerTemplate();

    explicit CdpPrinter(QObject *parent = nullptr);
    ~CdpPrinter() override;

    // 对 page 对应的渲染 target 发起原生打印。targetUrl 用于 /json/list 匹配
    // 该页的 webSocketDebuggerUrl（Qt 不暴露 targetId；导出视图 baseUrl 为
    // qrc:/web/，与编辑器页 qrc:/web/index.html 天然区分）。结果 finished 信号：
    // ok 时 outputPath 已写盘；失败携带原因，调用方回退
    void print(QWebEnginePage *page, const QUrl &targetUrl,
               const Options &opt, const QString &outputPath);

signals:
    void finished(bool ok, const QString &error);

private:
    void discoverTarget();
    void sendPrintCommand(const QString &wsUrl);

    QWebEnginePage *m_page = nullptr;
    QUrl m_targetUrl;
    Options m_opt;
    QString m_outputPath;
    QNetworkAccessManager *m_nam = nullptr;
    QWebSocket *m_ws = nullptr;
    QTimer *m_watchdog = nullptr;
    int m_discoverAttempts = 0;
    bool m_done = false;

    static bool s_methodBroken;   // printToPdf 构建级缺失缓存（见 pdfMethodAvailable）
};

#endif // MD_EDITOR_CDPPRINTER_H
