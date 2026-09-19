#ifndef MD_EDITOR_PDFEXPORTER_H
#define MD_EDITOR_PDFEXPORTER_H

#include <QObject>
#include <QWebEngineView>
#include <QPageLayout>
#include <QPageSize>
#include <QTimer>

// PDF 分页规格（buildRenderHtml 纯字符串路径的参数载体，exportToPdf 从 QSettings 组装）
struct PdfPageSpec {
    QPageSize::PageSizeId paper = QPageSize::A4;
    QPageLayout::Orientation orientation = QPageLayout::Portrait;
    QMarginsF marginsMm{18, 18, 18, 18};   // 左 上 右 下（mm）
    bool headerEnabled = false;            // @top-center 文档标题
    bool footerEnabled = false;            // @bottom-right 第 X 页 / 共 Y 页
    QString headerTitle;
    // CDP 快路径（Chromium 原生分页 + 原生页眉页脚模板）：不注入 paged.js，
    // 由 CdpPrinter 走 Page.printToPdf。页眉/页脚规格（标题/边距/纸型）仍由
    // 本 spec 承载，页脚页码由打印模板注入（与 @bottom-right 等价形态）。
    // 仅内部使用：exportToPdf 判定 CDP 可走时置位生成轻量 HTML
    bool cdpMode = false;
    // 页眉/页脚任一启用即激活 paged.js 分页（全关走原 Chromium 分页管线）
    bool pagedActive() const { return headerEnabled || footerEnabled; }
};

class PdfExporter : public QObject {
    Q_OBJECT
public:
    explicit PdfExporter(QObject *parent = nullptr);
    ~PdfExporter() override;

    void exportToPdf(const QString &html, const QString &docDir, const QString &outputPath,
                     QPageLayout::Orientation orientation = QPageLayout::Portrait,
                     QPageSize::PageSizeId paperSize = QPageSize::A4);

    // 构建 PDF 渲染 HTML（纯字符串逻辑，供测试）
    static QString buildRenderHtml(const QString &html, const QString &docDir,
                                   const QString &katexCss, const QString &customCss);

    // 分页版：@page 规则 + paged.js 注入（spec.pagedActive() 时）
    static QString buildRenderHtml(const QString &html, const QString &docDir,
                                   const QString &katexCss, const QString &customCss,
                                   const PdfPageSpec &spec);

    // paged 路径的 Qt 打印边距（mm，0.1mm 微量值，实现处注释详述根因）——
    // 提为静态函数供单测锁形（每边 > 0 且 <= 1.0mm）
    static QMarginsF pagedPrintMargins();

    // 本次导出是否走了回退（paged.js 失败/超时/内容校验不过 → 原生分页，产物
    // 无页眉页脚）。pagedFallbackUsed 信号先于 printToPdf 触发，其状态栏提示
    // 会被随后的导出成功消息覆盖——调用方须在 exportFinished 时据此把回退
    // 说明并入最终消息
    bool fallbackUsed() const { return m_fallbackUsed; }

signals:
    void exportFinished(bool success, const QString &outputPath);
    void exportError(const QString &error);
    // paged.js 分页失败/超时，已回退原生分页导出（产物无页眉页脚）——提示用户
    void pagedFallbackUsed();
    // 分页进行中的页数进度（paged 路径，页数增长时发；2026-09-15 性能取证
    // 大文档分页以十秒计，静默等待体感差）。调用方展示「正在分页：已生成 N 页」
    void pagedProgress(int pages);

private slots:
    void onLoadFinished(bool ok);
    void onPdfPrintingFinished(const QString &filePath, bool success);

private:
    void pollPrintState();
    // CDP 快路径（Chromium 原生分页+页眉页脚模板）：流程 = 轻量 HTML（cdpMode，
    // 不注 paged.js）→ 轮询 __cdpReady（KaTeX/mermaid 渲染完）→ CdpPrinter 打印；
    // 任一环节失败回退 paged.js 完整链路（m_pagedFullHtml 已备好）
    void startCdpExport(const QString &cdpFullHtml);
    void pollCdpReady();
    void launchCdpPrint();
    void fallbackToPaged(const QString &reason);
    void startPagedExport();

    QWebEngineView *m_renderView;
    class CdpPrinter *m_cdp = nullptr;
    QString m_pagedFullHtml;     // paged.js 版渲染 HTML（CDP 回退时直接用）
    int m_cdpReadyPolls = 0;     // __cdpReady 轮询计数（50ms × 200 = 10s 超时）
    PdfPageSpec m_cdpSpec;       // CDP 打印参数源（纸型/边距/横纵向/页眉页脚）
    QString m_pendingOutputPath;
    QPageLayout m_pendingLayout;   // paged 版式（0.1mm 微量边距——@page 已承载页边距，见 pagedPrintMargins）
    QPageLayout m_legacyLayout;    // 原生版式（用户边距）——legacy/回退共用
    // runJavaScript 不等待 Promise（Qt 6.8 实测回调立即触发），完成态靠轮询
    // window.__printState（paged/legacy/fallback）获取
    QTimer m_printPoll;
    int m_printPollCount = 0;
    bool m_printStarted = false;   // 防轮询回调竞态导致重复 printToPdf
    bool m_fallbackUsed = false;   // 进入回退路径（fallback/超时）时置位
    int m_lastPagedPages = 0;      // 进度信号去重（页数增长才发 pagedProgress）
};

#endif // MD_EDITOR_PDFEXPORTER_H
