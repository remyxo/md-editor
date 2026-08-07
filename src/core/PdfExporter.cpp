#include "PdfExporter.h"
#include <QWebEnginePage>
#include <QWebEngineSettings>
#include <QUrl>
#include <QSettings>

PdfExporter::PdfExporter(QObject *parent)
    : QObject(parent)
    , m_renderView(new QWebEngineView())
{
    m_renderView->setVisible(false);
    m_renderView->settings()->setAttribute(
        QWebEngineSettings::LocalContentCanAccessFileUrls, true);
}

PdfExporter::~PdfExporter()
{
    delete m_renderView;
}

void PdfExporter::exportToPdf(const QString &html, const QString &outputPath,
                              QPageLayout::Orientation orientation,
                              QPageSize::PageSizeId paperSize)
{
    QSettings settings("md-editor", "md-editor");
    double mt = settings.value("export/marginTop", 1.8).toDouble();
    double mb = settings.value("export/marginBottom", 1.8).toDouble();
    double ml = settings.value("export/marginLeft", 1.8).toDouble();
    double mr = settings.value("export/marginRight", 1.8).toDouble();
    int orient = settings.value("export/orientation",
        static_cast<int>(orientation)).toInt();
    int paper = settings.value("export/paperSize",
        static_cast<int>(paperSize)).toInt();

    QString fullHtml = QString(
        "<!DOCTYPE html><html><head><meta charset=\"UTF-8\"><style>"
        "@media print{"
        "@page{margin:0;}"
        "body{font-family:\"Noto Sans SC\",\"DejaVu Sans\",sans-serif;"
        "font-size:12pt;line-height:1.6;color:#333;}"
        "h1{font-size:1.8em;}h2{font-size:1.4em;}h3{font-size:1.2em;}"
        "pre{background:#f5f5f5;padding:12px;border-radius:4px;"
        "font-family:monospace;font-size:10pt;overflow-x:auto;}"
        "code{background:#f5f5f5;padding:1px 4px;border-radius:2px;}"
        "table{border-collapse:collapse;width:100%%;}"
        "th,td{border:1px solid #ddd;padding:6px 10px;}"
        "img{max-width:100%%;}"
        "blockquote{border-left:3px solid #0078d4;padding-left:12px;"
        "color:#666;margin:1em 0;}"
        "}"
        "</style></head><body>%1</body></html>"
    ).arg(html);

    m_pendingOutputPath = outputPath;
    // 设置值为 cm，QPageLayout::Millimeter 需要 mm
    m_pendingLayout = QPageLayout(QPageSize(static_cast<QPageSize::PageSizeId>(paper)),
        static_cast<QPageLayout::Orientation>(orient),
        QMarginsF(ml * 10, mt * 10, mr * 10, mb * 10), QPageLayout::Millimeter);

    disconnect(m_renderView, &QWebEngineView::loadFinished, this, nullptr);
    disconnect(m_renderView->page(), &QWebEnginePage::pdfPrintingFinished,
               this, nullptr);

    connect(m_renderView, &QWebEngineView::loadFinished,
            this, &PdfExporter::onLoadFinished);
    connect(m_renderView->page(), &QWebEnginePage::pdfPrintingFinished,
            this, &PdfExporter::onPdfPrintingFinished);

    m_renderView->setHtml(fullHtml, QUrl("about:blank"));
}

void PdfExporter::onLoadFinished(bool ok)
{
    if (!ok) {
        emit exportError("HTML 渲染失败");
        return;
    }
    m_renderView->page()->printToPdf(m_pendingOutputPath, m_pendingLayout);
}

void PdfExporter::onPdfPrintingFinished(const QString &filePath, bool success)
{
    if (success)
        emit exportFinished(true, filePath);
    else
        emit exportError("PDF 生成失败");
}
