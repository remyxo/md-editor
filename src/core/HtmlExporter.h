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
    static QString mimeForExt(const QString &ext);
    static QString inlineCssFonts(const QString &css, const QString &fontDir);
    static QString inlineImages(const QString &html, const QString &docDir);
    static QString bodyCss(const QString &theme);
    static QString renderScript(const QString &theme);

signals:
    void exportFinished(bool success, const QString &outputPath);
    void exportError(const QString &error);

private:
    QString readResource(const QString &path) const;
};

#endif // MD_EDITOR_HTMLEXPORTER_H
