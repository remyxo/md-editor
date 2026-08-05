#include "MarkdownHandler.h"
#include <QTextDocument>
#include <QRegularExpression>

static bool isParagraphLine(const QString &line)
{
    if (line.isEmpty()) return false;
    QString trimmed = line.trimmed();
    QChar c = trimmed[0];
    if (c == '`') {
        if (!trimmed.startsWith("```"))
            return true;
        return false;
    }
    // 引用行也是正文（需保留内部换行）
    if (c == '>') return true;
    return c != '#' && c != '-' && c != '|'
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
    QStringList codeLangs; // 跟踪每个代码块的语言

    for (int i = 0; i < lines.size(); ++i) {
        const QString &line = lines[i];
        const QString &next = (i + 1 < lines.size()) ? lines[i + 1] : QString();

        if (line.startsWith("```")) {
            if (!inCodeBlock) {
                // 进入代码块，提取语言
                QString lang = line.mid(3).trimmed();
                codeLangs << (lang.isEmpty() ? QString() : lang);
                processed += "```\n"; // 去掉语言标记避免干扰 setMarkdown
            } else {
                processed += line + '\n';
            }
            inCodeBlock = !inCodeBlock;
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

    // 后处理 1: 合并连续 <pre> 块（QTextDocument 每行一个 <pre>）
    {
        QRegularExpression preRe(
            R"(<pre[^>]*>(.*?)</pre>)",
            QRegularExpression::DotMatchesEverythingOption);
        int searchFrom = 0;
        while (true) {
            auto m = preRe.match(html, searchFrom);
            if (!m.hasMatch()) break;
            int blockStart = m.capturedStart();
            int blockEnd = blockStart + m.capturedLength();
            QStringList lines;
            lines << m.captured(1);
            searchFrom = blockEnd;
            // 收集连续 <pre>
            while (true) {
                auto next = preRe.match(html, searchFrom);
                if (!next.hasMatch()) break;
                QString between = html.mid(blockEnd,
                    next.capturedStart() - blockEnd);
                if (between.trimmed().isEmpty()) {
                    lines << next.captured(1);
                    blockEnd = next.capturedStart() + next.capturedLength();
                    searchFrom = blockEnd;
                } else break;
            }
            if (lines.size() > 1) {
                QString merged = lines.join('\n');
                // 保留内部代码格式但合并为一个 <pre>
                QString replacement = "<pre>" + merged + "</pre>";
                html.replace(blockStart, blockEnd - blockStart, replacement);
                searchFrom = blockStart + replacement.length();
            }
        }
    }

    // 后处理 1.5: 给合并后的 <pre> 添加语言类型
    if (!codeLangs.isEmpty()) {
        QRegularExpression preTagRe("<pre>");
        int langIdx = 0;
        int offset = 0;
        while (langIdx < codeLangs.size()) {
            auto m = preTagRe.match(html, offset);
            if (!m.hasMatch()) break;
            if (!codeLangs[langIdx].isEmpty()) {
                QString replacement = "<pre data-lang=\"" + codeLangs[langIdx] + "\">";
                html.replace(m.capturedStart(), m.capturedLength(), replacement);
                offset = m.capturedStart() + replacement.length();
            } else {
                offset = m.capturedStart() + m.capturedLength();
            }
            langIdx++;
        }
    }

    // 后处理 2: BRMARKER → <br>
    html.replace(MARKER + " ", "<br>");
    html.replace(MARKER, "<br>");

    // 后处理 2: QTextDocument 内联样式 → 语义标签
    //    引用: <p margin-left:40px>... → <blockquote><p>...</p></blockquote>
    {
        QRegularExpression re(
            R"(<p([^>]*margin-left:\s*40px[^>]*margin-right:\s*40px[^>]*)>(.*?)</p>)",
            QRegularExpression::DotMatchesEverythingOption);
        html.replace(re, "<blockquote><p\\1>\\2</p></blockquote>");
    }
    //    monospace span → <code>
    {
        QRegularExpression re(
            R"(<span[^>]*font-family:\s*'monospace'[^>]*>(.*?)</span>)");
        html.replace(re, "<code>\\1</code>");
    }
    //    ==highlight== → <mark>（QTextDocument 不识别此语法，== 留存为纯文本）
    {
        QRegularExpression re("==([^=\n]+?)==");
        html.replace(re, "<mark>\\1</mark>");
    }

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
            QString ph = QString("ZZCB%1ZZ").arg(n++);
            placeholders << ph;
            backups << "```\n" + code + "\n```";
            processed.replace(blockStart, blockEnd - blockStart, ph);
            searchFrom = blockStart + ph.length();
        }
    }

    // 2) 引用 → > 前缀
    //    （跳过已被 <blockquote> 包裹的，由后续 blockquote 标签步处理）
    {
        int n = 0;
        // <blockquote> 标签先处理
        QRegularExpression bqTagRe(
            R"(<blockquote[^>]*>(.*?)</blockquote>)",
            QRegularExpression::DotMatchesEverythingOption);
        int offset = 0;
        while (true) {
            auto m = bqTagRe.match(processed, offset);
            if (!m.hasMatch()) break;
            QString inner = m.captured(1);
            // 块级标签 → 换行，保留段落边界
            inner.replace(QRegularExpression("</?p[^>]*>"), "\n");
            inner.replace(QRegularExpression("</?div[^>]*>"), "\n");
            inner.replace("<br>", "\n");
            inner.replace("<br/>", "\n");
            inner.replace("<br />", "\n");
            inner.remove(QRegularExpression("<[^>]*>"));
            inner = inner.trimmed();
            QStringList lines = inner.split('\n');
            QString quoted;
            for (const QString &l : lines) {
                QString t = l.trimmed();
                if (!t.isEmpty())
                    quoted += "> " + t + "\n";
            }
            quoted = quoted.trimmed();
            QString ph = QString("ZZBQ%1ZZ").arg(n++);
            placeholders << ph;
            backups << quoted;
            processed.replace(m.capturedStart(), m.capturedLength(), ph);
            offset = m.capturedStart() + ph.length();
        }
        // 裸 margin-left <p>（未被 <blockquote> 包裹的）
        QRegularExpression bqRe(
            R"(<p[^>]*margin-left:\s*40px[^>]*margin-right:\s*40px[^>]*>(.*?)</p>)",
            QRegularExpression::DotMatchesEverythingOption);
        offset = 0;
        while (true) {
            auto m = bqRe.match(processed, offset);
            if (!m.hasMatch()) break;
            QString text = m.captured(1);
            text.replace("<br>", "\n");
            text.replace("<br/>", "\n");
            text.replace("<br />", "\n");
            text.remove(QRegularExpression("<[^>]*>"));
            text = text.trimmed();
            QStringList lines = text.split('\n');
            QString quoted;
            for (const QString &l : lines) {
                QString t = l.trimmed();
                if (!t.isEmpty())
                    quoted += "> " + t + "\n";
            }
            quoted = quoted.trimmed();
            QString ph = QString("ZZBQ%1ZZ").arg(n++);
            placeholders << ph;
            backups << quoted;
            processed.replace(m.capturedStart(), m.capturedLength(), ph);
            offset = m.capturedStart() + ph.length();
        }
    }

    // 3) 高亮 <mark>text</mark> → ==text==
    {
        QRegularExpression re(
            R"(<mark[^>]*>(.*?)</mark>)",
            QRegularExpression::DotMatchesEverythingOption);
        int n = 0;
        int offset = 0;
        while (true) {
            auto m = re.match(processed, offset);
            if (!m.hasMatch()) break;
            QString ph = QString("ZZMK%1ZZ").arg(n++);
            placeholders << ph;
            backups << "==" + m.captured(1) + "==";
            processed.replace(m.capturedStart(), m.capturedLength(), ph);
            offset = m.capturedStart() + ph.length();
        }
    }

    // 4) 行内代码 <code>text</code> 及 <span monospace> → 占位符
    {
        // <code> 标签（TipTap 输出）
        QRegularExpression codeRe(
            R"(<code[^>]*>(.*?)</code>)",
            QRegularExpression::DotMatchesEverythingOption);
        int n = 0;
        int offset = 0;
        while (true) {
            auto m = codeRe.match(processed, offset);
            if (!m.hasMatch()) break;
            QString ph = QString("ZZCD%1ZZ").arg(n++);
            placeholders << ph;
            backups << "`" + m.captured(1) + "`";
            processed.replace(m.capturedStart(), m.capturedLength(), ph);
            offset = m.capturedStart() + ph.length();
        }
        // <span monospace>（QTextDocument 原始输出）
        QRegularExpression spanRe(
            R"(<span[^>]*font-family:\s*'monospace'[^>]*>(.*?)</span>)",
            QRegularExpression::DotMatchesEverythingOption);
        n = 0;
        offset = 0;
        while (true) {
            auto m = spanRe.match(processed, offset);
            if (!m.hasMatch()) break;
            QString ph = QString("ZZIC%1ZZ").arg(n++);
            placeholders << ph;
            backups << "`" + m.captured(1) + "`";
            processed.replace(m.capturedStart(), m.capturedLength(), ph);
            offset = m.capturedStart() + ph.length();
        }
    }

    QTextDocument doc;
    doc.setHtml(processed);
    QString md = doc.toMarkdown();

    // 3) 还原占位符（逆序：后入先还原，防止嵌套占位符丢失）
    for (int i = placeholders.size() - 1; i >= 0; --i)
        md.replace(placeholders[i], backups[i]);

    while (md.endsWith('\n'))
        md.chop(1);
    return md;
}
