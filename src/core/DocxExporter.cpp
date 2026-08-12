#include "DocxExporter.h"
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTextDocument>

DocxExporter::DocxExporter(QObject *parent)
    : QObject(parent)
{
}

void DocxExporter::exportToDocx(const QString &html, const QString &outputPath)
{
    if (writeDocx(html, outputPath)) {
        emit exportFinished(true, outputPath);
    } else {
        emit exportError("DOCX 生成失败");
    }
}

// XML 特殊字符转义
static QString xmlEscape(const QString &s)
{
    QString r = s;
    r.replace("&", "&amp;");
    r.replace("<", "&lt;");
    r.replace(">", "&gt;");
    r.replace("\"", "&quot;");
    return r;
}

bool DocxExporter::writeDocx(const QString &html, const QString &outputPath)
{
    // 1. 用 QTextDocument 从 HTML 提取纯文本
    //    TipTap getHTML() 返回 body 片段，包装为完整 HTML 以提高解析可靠性
    QTextDocument doc;
    doc.setHtml("<html><body>" + html + "</body></html>");
    QString plain = doc.toPlainText();

    // 2. 按段落分割，生成 OOXML
    QString bodyXml;
    QStringList lines = plain.split('\n');
    for (const QString &line : lines) {
        QString t = line.trimmed();
        if (t.isEmpty()) continue;
        // 过滤控制字符（保留 tab 和正常文本）
        t.remove(QRegularExpression("[\\x00-\\x08\\x0b\\x0c\\x0e-\\x1f]"));
        bodyXml += "<w:p><w:r><w:t xml:space=\"preserve\">"
                 + xmlEscape(t)
                 + "</w:t></w:r></w:p>\n";
    }
    if (bodyXml.isEmpty())
        bodyXml = "<w:p><w:r><w:t></w:t></w:r></w:p>\n";

    // 3. 写入 OOXML 结构到临时目录
    QTemporaryDir tmpDir;
    if (!tmpDir.isValid()) return false;
    QString base = tmpDir.path();
    QDir().mkpath(base + "/word/_rels");
    QDir().mkpath(base + "/_rels");

    auto writeFile = [](const QString &path, const QByteArray &data) -> bool {
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly)) return false;
        f.write(data);
        f.close();
        return true;
    };

    if (!writeFile(base + "/[Content_Types].xml",
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">\n"
        "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>\n"
        "<Default Extension=\"xml\" ContentType=\"application/xml\"/>\n"
        "</Types>\n")) return false;

    if (!writeFile(base + "/_rels/.rels",
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">\n"
        "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"word/document.xml\"/>\n"
        "</Relationships>\n")) return false;

    if (!writeFile(base + "/word/_rels/document.xml.rels",
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">\n"
        "</Relationships>\n")) return false;

    QByteArray docBytes = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<w:document xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\">\n"
        "<w:body>\n"
        + bodyXml.toUtf8()
        + "</w:body>\n</w:document>\n";
    if (!writeFile(base + "/word/document.xml", docBytes)) return false;

    // 4. zip 打包
    QFile::remove(outputPath);
    QProcess zipProc;
    zipProc.setWorkingDirectory(base);
    zipProc.start("zip", QStringList() << "-r" << outputPath << ".");
    if (!zipProc.waitForFinished(15000)) return false;
    return zipProc.exitCode() == 0;
}
