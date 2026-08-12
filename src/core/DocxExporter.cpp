#include "DocxExporter.h"
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QRegularExpression>
#include <QTemporaryDir>

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

// XML 安全字符转换
static QString xmlEscape(QString s)
{
    s.replace("&", "&amp;");
    s.replace("<", "&lt;");
    s.replace(">", "&gt;");
    s.replace("\"", "&quot;");
    s.replace("'", "&apos;");
    return s;
}

// 从 HTML 提取文本段落
static QString bodyToParagraphs(const QString &html)
{
    QString body = html;

    // 提取 body 内容
    int bs = body.indexOf("<body");
    int be = body.indexOf("</body>");
    if (bs >= 0 && be > bs) {
        bs = body.indexOf('>', bs) + 1;
        body = body.mid(bs, be - bs);
    }

    // 替换块级标签为换行（用于分段）
    body.replace(QRegularExpression("<br\\s*/?>", QRegularExpression::CaseInsensitiveOption), "\n");
    body.replace(QRegularExpression("</?(p|h[1-6]|pre|li|div|blockquote|tr|hr)[^>]*>",
                                    QRegularExpression::CaseInsensitiveOption), "\n");
    body.replace(QRegularExpression("</?(ul|ol|thead|tbody|table)[^>]*>",
                                    QRegularExpression::CaseInsensitiveOption), "");
    body.replace(QRegularExpression("<t[dh][^>]*>", QRegularExpression::CaseInsensitiveOption), "\t");

    // 剥离所有残留标签
    body.replace(QRegularExpression("<[^>]*>"), "");

    // 解码 HTML 实体
    body.replace("&lt;", "<");
    body.replace("&gt;", ">");
    body.replace("&amp;", "&");
    body.replace("&quot;", "\"");
    body.replace("&apos;", "'");
    body.replace("&#39;", "'");
    body.replace("&nbsp;", " ");
    // 处理数字实体 &#xNNNN; &#NNNN;（Qt 6.4 的 QString::replace 不支持 lambda）
    {
        QRegularExpression hexRe("&#x([0-9a-fA-F]+);");
        int pos = 0;
        while (pos < body.length()) {
            auto m = hexRe.match(body, pos);
            if (!m.hasMatch()) break;
            QChar ch(m.captured(1).toInt(nullptr, 16));
            body.replace(m.capturedStart(), m.capturedLength(), ch);
            pos = m.capturedStart() + 1;
        }
    }
    {
        QRegularExpression decRe("&#(\\d+);");
        int pos = 0;
        while (pos < body.length()) {
            auto m = decRe.match(body, pos);
            if (!m.hasMatch()) break;
            QChar ch(m.captured(1).toInt());
            body.replace(m.capturedStart(), m.capturedLength(), ch);
            pos = m.capturedStart() + 1;
        }
    }

    // 按行分段
    QStringList lines = body.split('\n');
    QString result;
    for (const QString &line : lines) {
        QString trimmed = line.trimmed();
        if (trimmed.isEmpty()) continue;
        result += "<w:p><w:r><w:t xml:space=\"preserve\">"
                + xmlEscape(trimmed)
                + "</w:t></w:r></w:p>\n";
    }

    if (result.isEmpty())
        result = "<w:p><w:r><w:t></w:t></w:r></w:p>\n";
    return result;
}

bool DocxExporter::writeDocx(const QString &html, const QString &outputPath)
{
    QTemporaryDir tmpDir;
    if (!tmpDir.isValid()) return false;
    QString base = tmpDir.path();

    QDir().mkpath(base + "/word/_rels");
    QDir().mkpath(base + "/_rels");

    // [Content_Types].xml
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

    // _rels/.rels
    {
        QFile f(base + "/_rels/.rels");
        if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
        f.write("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
                "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">\n"
                "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"word/document.xml\"/>\n"
                "</Relationships>\n");
        f.close();
    }

    // word/_rels/document.xml.rels
    {
        QFile f(base + "/word/_rels/document.xml.rels");
        if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
        f.write("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
                "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">\n"
                "</Relationships>\n");
        f.close();
    }

    // word/document.xml
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

    // zip 打包
    QFile::remove(outputPath);
    QProcess zipProc;
    zipProc.setWorkingDirectory(base);
    zipProc.start("zip", QStringList() << "-r" << outputPath << ".");
    if (!zipProc.waitForFinished(15000)) return false;
    return zipProc.exitCode() == 0;
}
