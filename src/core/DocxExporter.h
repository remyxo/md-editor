#ifndef MD_EDITOR_DOCXEXPORTER_H
#define MD_EDITOR_DOCXEXPORTER_H

#include <QObject>
#include "MermaidRenderer.h"
#include "MathMlRenderer.h"

class DocxExporter : public QObject {
    Q_OBJECT
public:
    explicit DocxExporter(QObject *parent = nullptr);

    void exportToDocx(const QString &html, const QString &docDir, const QString &outputPath);

    // 提取 <pre><code class="language-mermaid"> 的源码（纯函数，供测试）
    static QStringList extractMermaidSources(const QString &html);
    // 提取行内/块级公式的 LaTeX 源码（按文档顺序，纯函数，供测试）
    static QStringList extractMathSources(const QString &html);

signals:
    void exportFinished(bool success, const QString &outputPath);
    void exportError(const QString &error);

private slots:
    void onMermaidRendered(const QVector<MermaidRenderer::Result> &results);
    void onMathRendered(const QVector<QString> &mathmlList);

private:
    bool writeDocx(const QString &html, const QString &docDir, const QString &outputPath);
    void maybeFinish();   // mermaid 与公式两个异步渲染均完成后写 DOCX

    QString m_docDir;
    QString m_outputPath;
    QString m_lastHtml;
    QStringList m_mermaidSources;
    QVector<MermaidRenderer::Result> m_mermaidResults;
    MermaidRenderer *m_renderer = nullptr;

    // 公式渲染：m_mathSources（LaTeX）与 m_mathOMML（转换后的 OMML 片段）按下标对齐，
    // 渲染/转换失败的项 m_mathOMML 为空串，解析时回退为 $...$ 源码文本。
    QStringList m_mathSources;
    QStringList m_mathOMML;
    MathMlRenderer *m_mathRenderer = nullptr;
    bool m_mermaidPending = false;
    bool m_mathPending = false;
};

#endif // MD_EDITOR_DOCXEXPORTER_H
