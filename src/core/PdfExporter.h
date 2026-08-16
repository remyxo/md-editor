#ifndef MD_EDITOR_PDFEXPORTER_H
#define MD_EDITOR_PDFEXPORTER_H

#include <QObject>
#include <QWebEngineView>
#include <QPageLayout>
#include <QPageSize>

class PdfExporter : public QObject {
    Q_OBJECT
public:
    explicit PdfExporter(QObject *parent = nullptr);
    ~PdfExporter() override;

    void exportToPdf(const QString &html, const QString &docDir, const QString &outputPath,
                     QPageLayout::Orientation orientation = QPageLayout::Portrait,
                     QPageSize::PageSizeId paperSize = QPageSize::A4);

signals:
    void exportFinished(bool success, const QString &outputPath);
    void exportError(const QString &error);

private slots:
    void onLoadFinished(bool ok);
    void onPdfPrintingFinished(const QString &filePath, bool success);

private:
    QWebEngineView *m_renderView;
    QString m_pendingOutputPath;
    QPageLayout m_pendingLayout;
};

#endif // MD_EDITOR_PDFEXPORTER_H
