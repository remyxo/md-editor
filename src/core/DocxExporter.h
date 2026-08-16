#ifndef MD_EDITOR_DOCXEXPORTER_H
#define MD_EDITOR_DOCXEXPORTER_H

#include <QObject>
#include "MermaidRenderer.h"

class DocxExporter : public QObject {
    Q_OBJECT
public:
    explicit DocxExporter(QObject *parent = nullptr);

    void exportToDocx(const QString &html, const QString &docDir, const QString &outputPath);

    // 提取 <pre><code class="language-mermaid"> 的源码（纯函数，供测试）
    static QStringList extractMermaidSources(const QString &html);

signals:
    void exportFinished(bool success, const QString &outputPath);
    void exportError(const QString &error);

private slots:
    void onMermaidRendered(const QVector<MermaidRenderer::Result> &results);

private:
    bool writeDocx(const QString &html, const QString &docDir, const QString &outputPath);

    QString m_docDir;
    QString m_outputPath;
    QString m_lastHtml;
    QStringList m_mermaidSources;
    QVector<MermaidRenderer::Result> m_mermaidResults;
    MermaidRenderer *m_renderer = nullptr;
};

#endif // MD_EDITOR_DOCXEXPORTER_H
