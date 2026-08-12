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

// 从 HTML 提取纯文本，按块级元素分割为段落
static QString bodyToParagraphs(const QString &html)
{
    QString body = html;
    // 提取 <body> 内容（若存在）
    int bs = body.indexOf("<body");
    int be = body.indexOf("</body>");
    if (bs >= 0 && be > bs) {
        bs = body.indexOf('>', bs) + 1;
        body = body.mid(bs, be - bs);
    }

    // 解码常见 HTML 实体
    body.replace("&lt;", "<");
    body.replace("&gt;", ">");
    body.replace("&amp;", "&");
    body.replace("&quot;", "\"");
    body.replace("&#39;", "'");

    // 用 QTextDocument 来提取纯文本（可靠地剥离所有 HTML 标签）
    // 但会丢失段落分隔。因此先用换行替代块级标签，再用 QTextDocument 提取文本
    body.replace(QRegularExpression("<br\\s*/?>", QRegularExpression::CaseInsensitiveOption), "\n");
    body.replace(QRegularExpression("</?(p|h[1-6]|pre|li|div|blockquote|tr|hr)[^>]*>",
                                    QRegularExpression::CaseInsensitiveOption), "\n");
    body.replace(QRegularExpression("</?(ul|ol|thead|tbody|table)[^>]*>",
                                    QRegularExpression::CaseInsensitiveOption), "");
    body.replace(QRegularExpression("<t[dh][^>]*>", QRegularExpression::CaseInsensitiveOption), "\t");

    // 用 QTextDocument 剥离所有残留 HTML 标签
    QTextDocument doc;
    doc.setHtml(body);
    QString text = doc.toPlainText();

    // 按行分割，过滤空行
    QStringList lines = text.split('\n');
    QString result;
    for (const QString &line : lines) {
        QString trimmed = line.trimmed();
        if (trimmed.isEmpty()) continue;
        // XML 转义 & 保留空格
        QString escaped = trimmed.toHtmlEscaped();
        result += "<w:p><w:r><w:t xml:space=\"preserve\">"
                + escaped
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
        f.write("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n");
        f.write("<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">\n");
        f.write("<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>\n");
        f.write("<Default Extension=\"xml\" ContentType=\"application/xml\"/>\n");
        f.write("</Types>\n");
        f.close();
    }

    // 4. 写入 _rels/.rels
    {
        QFile f(base + "/_rels/.rels");
        if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
        f.write("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n");
        f.write("<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">\n");
        f.write("<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"word/document.xml\"/>\n");
        f.write("</Relationships>\n");
        f.close();
    }

    // 5. 写入 word/_rels/document.xml.rels
    {
        QFile f(base + "/word/_rels/document.xml.rels");
        if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
        f.write("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n");
        f.write("<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">\n");
        f.write("</Relationships>\n");
        f.close();
    }

    // 6. 写入 word/document.xml（纯文本段落）
    {
        QFile f(base + "/word/document.xml");
        if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
        f.write("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n");
        f.write("<w:document xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\">\n");
        f.write("<w:body>\n");
        f.write(bodyToParagraphs(html).toUtf8());
        f.write("</w:body>\n");
        f.write("</w:document>\n");
        f.close();
    }

    // 7. 删除可能已存在的输出文件（zip 会追加而非覆盖）
    QFile::remove(outputPath);

    // 8. 使用 zip 打包（-j 不保留目录结构？不对，需要保留相对路径）
    QProcess zipProc;
    zipProc.setWorkingDirectory(base);
    QStringList args;
    args << "-r" << outputPath << "." << "-x" << "*.docx";
    zipProc.start("zip", args);
    if (!zipProc.waitForFinished(15000)) return false;
    return zipProc.exitCode() == 0;
}
