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
    // QTextDocument::toMarkdown() 不能正确处理 <code> / <pre>
    // 策略：先替换代码为占位符 → toMarkdown() → 还原占位符

    QStringList placeholders, backups;
    QString processed = html;

    // 1) 代码块：多重 <pre>（每行一个，QTextDocument 特有）→ 合并
    {
        QRegularExpression preRe(
            R"(<pre[^>]*>(.*?)</pre>)",
            QRegularExpression::DotMatchesEverythingOption);
        int n = 0;
        int searchFrom = 0;
        while (true) {
            auto m = preRe.match(processed, searchFrom);
            if (!m.hasMatch()) break;

            QStringList lines;
            int blockStart = m.capturedStart();
            int blockEnd = blockStart + m.capturedLength();
            lines << m.captured(1);
            searchFrom = blockEnd;

            while (true) {
                auto next = preRe.match(processed, searchFrom);
                if (!next.hasMatch()) break;
                QString between = processed.mid(blockEnd,
                    next.capturedStart() - blockEnd);
                if (between.trimmed().isEmpty()) {
                    lines << next.captured(1);
                    blockEnd = next.capturedStart() + next.capturedLength();
                    searchFrom = blockEnd;
                } else {
                    break;
                }
            }

            QString code = lines.join('\n');
            code.remove(QRegularExpression("<[^>]*>"));
            code = code.trimmed();
            QString ph = QString("\x01CB%1\x01").arg(n++);
            placeholders << ph;
            backups << "```\n" + code + "\n```";
            processed.replace(blockStart, blockEnd - blockStart, ph);
            searchFrom = blockStart + ph.length();
        }
    }

    // 2) 行内代码 <span ...monospace...>text</span> → 占位符
    {
        QRegularExpression re(
            R"(<span[^>]*font-family:\s*'monospace'[^>]*>(.*?)</span>)",
            QRegularExpression::DotMatchesEverythingOption);
        int n = 0;
        int offset = 0;
        while (true) {
            auto m = re.match(processed, offset);
            if (!m.hasMatch()) break;
            QString ph = QString("\x02IC%1\x02").arg(n++);
            placeholders << ph;
            backups << "`" + m.captured(1) + "`";
            processed.replace(m.capturedStart(), m.capturedLength(), ph);
            offset = m.capturedStart() + ph.length();
        }
    }

    QTextDocument doc;
    doc.setHtml(processed);
    QString md = doc.toMarkdown();

    // 3) 还原占位符
    for (int i = 0; i < placeholders.size(); ++i)
        md.replace(placeholders[i], backups[i]);

    while (md.endsWith('\n'))
        md.chop(1);
    return md;
}
