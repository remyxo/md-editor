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

signals:
    void exportFinished(bool success, const QString &outputPath);
    void exportError(const QString &error);
    // paged.js 分页失败/超时，已回退原生分页导出（产物无页眉页脚）——提示用户
    void pagedFallbackUsed();

private slots:
    void onLoadFinished(bool ok);
    void onPdfPrintingFinished(const QString &filePath, bool success);

private:
    void pollPrintState();

    QWebEngineView *m_renderView;
    QString m_pendingOutputPath;
    QPageLayout m_pendingLayout;   // paged 版式（0 边距——@page 已承载页边距）
    QPageLayout m_legacyLayout;    // 原生版式（用户边距）——legacy/回退共用
    // runJavaScript 不等待 Promise（Qt 6.8 实测回调立即触发），完成态靠轮询
    // window.__printState（paged/legacy/fallback）获取
    QTimer m_printPoll;
    int m_printPollCount = 0;
    bool m_printStarted = false;   // 防轮询回调竞态导致重复 printToPdf
};

#endif // MD_EDITOR_PDFEXPORTER_H
