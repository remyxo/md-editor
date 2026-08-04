#include "MarkdownHandler.h"
#include <QTextDocument>
#include <QRegularExpression>

static bool isParagraphLine(const QString &line)
{
    if (line.isEmpty()) return false;
    QString trimmed = line.trimmed();
    QChar c = trimmed[0];
    // 跳过行内代码反引号（非代码块标记）
    if (c == '`') {
        // 代码块以 ``` 开头，行内代码以单个 ` 开头 → 仍是正文
        if (!trimmed.startsWith("```"))
            return true;
        return false;
    }
    return c != '#' && c != '-' && c != '>' && c != '|'
           && !(c >= '0' && c <= '9' && trimmed.length() > 1
                && trimmed[1] == '.');
}

QString MarkdownHandler::toHtml(const QString &markdown)
{
    // 预处理：两行正文间单 \n → 行末嵌入 BRMARKER
    // QTextDocument::setMarkdown() 不支持硬换行（<br>、  \n、\ 均被忽略）
    // 用纯文本标记 BRMARKER 嵌入行末，后处理时替换为 <br>
    static const QString MARKER = QStringLiteral("BRMARKER");

    QStringList lines = markdown.split('\n');
    QString processed;
    bool inCodeBlock = false;

    for (int i = 0; i < lines.size(); ++i) {
        const QString &line = lines[i];
        const QString &next = (i + 1 < lines.size()) ? lines[i + 1] : QString();

        if (line.startsWith("```")) {
            inCodeBlock = !inCodeBlock;
            processed += line + '\n';
        } else if (inCodeBlock) {
            processed += line + '\n';
        } else if (line.isEmpty()) {
            processed += '\n';
        } else if (isParagraphLine(line) && !next.isEmpty()
                   && isParagraphLine(next)) {
            // 两行正文 → 第一行末尾嵌入标记，setMarkdown 保留该标记
            processed += line + " " + MARKER + "\n";
        } else {
            processed += line + '\n';
        }
    }

    QTextDocument doc;
    doc.setMarkdown(processed);
    QString html = doc.toHtml();

    // 后处理：BRMARKER → <br>（setMarkdown 在标记后插入了空格，一并去除）
    html.replace(MARKER + " ", "<br>");
    html.replace(MARKER, "<br>");
    return html;
}

QString MarkdownHandler::toPlainText(const QString &markdown)
{
    QTextDocument doc;
    doc.setMarkdown(markdown);
    return doc.toPlainText();
}

QString MarkdownHandler::extractTitle(const QString &markdown)
{
    // 提取第一个 H1 标题
    QRegularExpression re("^#\\s+(.+)$", QRegularExpression::MultilineOption);
    QRegularExpressionMatch m = re.match(markdown);
    return m.hasMatch() ? m.captured(1).trimmed() : QString();
}

QString MarkdownHandler::fromHtml(const QString &html)
{
    QTextDocument doc;
    doc.setHtml(html);
    QString md = doc.toMarkdown();
    // QTextDocument 在末尾添加多余空行，去掉
    while (md.endsWith('\n'))
        md.chop(1);
    return md;
}
