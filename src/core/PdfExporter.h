#ifndef MD_EDITOR_PDFEXPORTER_H
#define MD_EDITOR_PDFEXPORTER_H

#include <QObject>
#include <QJsonArray>
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
    // 原生分页+覆盖层页眉页脚（2026-09-19 提速方案，机制经 print-probe 实证）：
    // 不注入 paged.js——页眉用 thead 重复带（Chromium 打印每页重复表头），
    // 页码用 abs 覆盖层画进下边距（top=k*contentH+8，每页精确落位且不增页）。
    // 仅内部使用：exportToPdf 判定文档无脚注引用时置位生成该形态 HTML
    bool nativeHf = false;
    // 页眉/页脚任一启用即需要页眉页脚（nativeHf 走原生覆盖层，否则 paged.js）
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
    // 进入最终写盘阶段（printToPdf 前）：total 为总页数（paged 终态页数 /
    // nativeHf 第二遍的数出页数），0 表示页数不可知（legacy/回退/nativeHf
    // 计数遍）。Chromium printToPdf 是一次性黑盒——写盘本身无逐页进度可发，
    // 调用方只能展示「正在写出 PDF」
    void writingPdf(int total);

private slots:
    void onLoadFinished(bool ok);
    void onPdfPrintingFinished(const QString &filePath, bool success);

private:
    void pollPrintState();
    // paged 版渲染 HTML 装载（paged 主路径与 nativeHf 回退共用：重连信号+复位）
    void startPagedExport(const QString &pagedFullHtml);
    // 原生页眉页脚路径任一环节失败 → 重建 paged 版 HTML 走完整 paged.js 链路
    void fallbackFromNativeHf(const QString &reason);

    QWebEngineView *m_renderView;
    QString m_pendingOutputPath;
    // 本次导出的输入与规格（nativeHf 回退时重建 paged 版 HTML 用）
    PdfPageSpec m_spec;
    QString m_srcHtml, m_srcDocDir, m_srcKatexCss, m_srcCustomCss;
    QString m_nativeHfTemp;   // nativeHf 第一遍 PDF（数页数用，完事即删）
    int m_nativeHfPass = 0;   // 0=非该路径；1=数页数遍；2=成品遍
    QJsonArray m_hfTops;      // 覆盖层页码标记纵向（px，T/H_c 流模型累计）
    int m_hfTotal = 0;        // 总页数
    qreal m_hfH1Px = 0;       // 页码首片高（内容区高 − thead，页 1 占位）
    qreal m_hfHcPx = 0;       // 页码后续片高（内容区高）
    qreal m_hfTotalPx = 0;    // numhost 总高（n 片和 = n 页）
    void stampAndPrintHf();   // 注入标记并打成品（首轮与校正轮共用）
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
