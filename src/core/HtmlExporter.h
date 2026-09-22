#ifndef MD_EDITOR_HTMLEXPORTER_H
#define MD_EDITOR_HTMLEXPORTER_H

#include <QObject>
#include <QString>

class HtmlExporter : public QObject {
    Q_OBJECT
public:
    explicit HtmlExporter(QObject *parent = nullptr);

    // 导出为自包含单文件 HTML（资源全 base64 内联）
    void exportToHtml(const QString &html, const QString &docDir,
                      const QString &outputPath, const QString &theme);

    // 可测试的纯逻辑助手
    static QString inlineCssFonts(const QString &css, const QString &fontDir);
    static QString inlineImages(const QString &html, const QString &docDir);
    static QString bodyCss(const QString &theme);
    static QString renderScript(const QString &theme);
    // 纯 KaTeX 渲染 JS 片段（不含 <script> 标签）：DOMContentLoaded 后渲染
    // 全部 [data-latex] 元素，带 window.katex 守卫。HTML 与 PDF 导出共用。
    static QString katexRenderScript();

    // 纯 mermaid 渲染 JS 片段（不含 <script> 标签），定义 window.__renderAllMermaid()
    //（返回 Promise，全部渲染完才 resolve）。HTML 与 PDF 导出共用。
    static QString mermaidRenderScript(const QString &theme);

    // 表格列宽归一 JS 片段（不含 <script> 标签），定义
    // window.__normalizeTableColumns(cfg)。HTML 与 PDF 导出共用同一实现：
    // 短列保真、长列吸收（详见实现处注释）。
    static QString tableColumnNormalizeScript();

signals:
    void exportFinished(bool success, const QString &outputPath);
    void exportError(const QString &error);

};

#endif // MD_EDITOR_HTMLEXPORTER_H
