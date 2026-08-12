#include "DocxExporter.h"
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QXmlStreamWriter>
#include <QRegularExpression>
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

// 提取 <body> 中的纯文本，按段落分割，生成简单 OOXML
static QString bodyToParagraphs(const QString &html)
{
    QString body = html;
    // 提取 <body> ... </body> 内容（若存在）
    int bs = body.indexOf("<body");
    int be = body.indexOf("</body>");
    if (bs >= 0 && be > bs) {
        bs = body.indexOf('>', bs) + 1;
        body = body.mid(bs, be - bs);
    }

    // 把常见块级标签替换为换行分隔
    body.replace(QRegularExpression("<br\\s*/?>", QRegularExpression::CaseInsensitiveOption), "\n");
    body.replace(QRegularExpression("</?(p|h[1-6]|pre|li|div|blockquote|table|tr|hr)[^>]*>",
                                    QRegularExpression::CaseInsensitiveOption), "\n");
    body.replace(QRegularExpression("<[/]?(ul|ol|thead|tbody)[^>]*>",
                                    QRegularExpression::CaseInsensitiveOption), "");
    body.replace(QRegularExpression("<td[^>]*>", QRegularExpression::CaseInsensitiveOption), "\t");
    body.replace(QRegularExpression("</t[dh]>", QRegularExpression::CaseInsensitiveOption), "");

    // 剥离剩余的行内标签，只留文本
    body.replace(QRegularExpression("<[^>]*>"), "");

    // 解码常见 HTML 实体（QTextDocument 的 toPlainText 也能处理，但这里使用字符串替换）
    body.replace("&lt;", "<");
    body.replace("&gt;", ">");
    body.replace("&amp;", "&");
    body.replace("&quot;", "\"");
    body.replace("&#39;", "'");

    // 按行分割，去除空行拼合
    QStringList lines = body.split('\n');
    QString result;
    for (const QString &line : lines) {
        QString trimmed = line.trimmed();
        if (trimmed.isEmpty()) continue;
        result += "<w:p><w:r><w:t xml:space=\"preserve\">"
                + trimmed.toHtmlEscaped()
                + "</w:t></w:r></w:p>\n";
    }

    if (result.isEmpty()) {
        result = "<w:p><w:r><w:t></w:t></w:r></w:p>\n";
    }
    return result;
}

bool DocxExporter::writeDocx(const QString &html, const QString &outputPath)
{
    // 1. 准备临时目录
    QTemporaryDir tmpDir;
    if (!tmpDir.isValid()) return false;
    QString base = tmpDir.path();

    // 2. 创建目录结构
    QDir().mkpath(base + "/word/_rels");
    QDir().mkpath(base + "/_rels");

    // 3. 写入 [Content_Types].xml
    {
        QFile f(base + "/[Content_Types].xml");
        if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
        f.write("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
                "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">\n"
                "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>\n"
                "<Default Extension=\"xml\" ContentType=\"application/xml\"/>\n"
                "</Types>\n");
        f.close();
    }

    // 4. 写入 _rels/.rels
    {
        QFile f(base + "/_rels/.rels");
        if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
        f.write("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
                "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">\n"
                "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"word/document.xml\"/>\n"
                "</Relationships>\n");
        f.close();
    }

    // 5. 写入 word/_rels/document.xml.rels
    {
        QFile f(base + "/word/_rels/document.xml.rels");
        if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
        f.write("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
                "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">\n"
                "</Relationships>\n");
        f.close();
    }

    // 6. 写入 word/document.xml（简单纯文本段落）
    {
        QFile f(base + "/word/document.xml");
        if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
        f.write("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
                "<w:document xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\">\n"
                "<w:body>\n");
        f.write(bodyToParagraphs(html).toUtf8());
        f.write("</w:body>\n</w:document>\n");
        f.close();
    }

    // 7. 使用 zip 打包为 .docx
    QProcess zipProc;
    zipProc.setWorkingDirectory(base);
    QStringList args;
    args << "-r" << outputPath << ".";
    zipProc.start("zip", args);
    if (!zipProc.waitForFinished(15000)) return false;
    return zipProc.exitCode() == 0;
}
