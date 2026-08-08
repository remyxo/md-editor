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
    // 数学公式分隔符不是正文行（避免 BRMARKER 插入导致多余 <br>）
    if (trimmed.startsWith("$$")) return false;
    // 引用行也是正文（需保留内部换行）
    if (c == '>') return true;
    return c != '#' && c != '-' && c != '|'
           && !(c >= '0' && c <= '9' && trimmed.length() > 1
                && trimmed[1] == '.');
}

// 反转义代码块 / 行内代码内容中的 HTML 实体。
// 代码内容以占位符形式绕过了 QTextDocument::setHtml 的实体解码，需手动还原，
// 否则保存到 .md 时会残留 &gt; / &lt; / &amp;（影响 Mermaid 源码 A-->B 等）。
// 顺序：先解码其余命名实体，最后解码 &amp;，避免把 &amp;lt; 错误还原成 <。
static QString unescapeHtmlEntities(const QString &s)
{
    QString r = s;
    r.replace("&lt;", "<");
    r.replace("&gt;", ">");
    r.replace("&quot;", "\"");
    r.replace("&#39;", "'");
    r.replace("&apos;", "'");
    r.replace("&amp;", "&");
    return r;
}

// 提取下标 ~x~（Pandoc 语法）为占位符。
// Qt 的 markdown 解析器把单个 ~x~ 当作删除线（text-decoration:line-through），
// 必须在 setMarkdown 前替换为占位符，后处理时再还原为 <sub>。
// 负向断言 (?<!~)~(?!~) 排除删除线 ~~x~~；内容存入 out，后处理时 HTML 转义还原。
static QString extractSubscripts(QString line, QStringList &out)
{
    QRegularExpression re("(?<!~)~(?!~)([^~\\n]+?)(?<!~)~(?!~)");
    int offset = 0;
    while (true) {
        auto m = re.match(line, offset);
        if (!m.hasMatch()) break;
        out << m.captured(1);
        QString ph = "ZZSB" + QString::number(out.size() - 1) + "ZZ";
        line.replace(m.capturedStart(), m.capturedLength(), ph);
        offset = m.capturedStart() + ph.length();
    }
    return line;
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
    bool inMathBlock = false;
    QStringList codeLangs; // 跟踪每个代码块的语言
    QStringList subContents; // 下标内容（~x~），后处理还原为 <sub>

    QStringList mathBlocks;  // 收集 $$ 块的内容
    QStringList mathLines;    // 当前 $$ 块的行

    for (int i = 0; i < lines.size(); ++i) {
        const QString &line = lines[i];
        const QString &next = (i + 1 < lines.size()) ? lines[i + 1] : QString();

        if (line.startsWith("$$")) {
            if (!inMathBlock) {
                mathLines.clear();
                inMathBlock = true;
                // 占位符
                processed += "ZZMB" + QString::number(mathBlocks.size()) + "ZZ\n";
            } else {
                // 构建 LaTeX，加入备份，后处理时替换占位符
                QString latex;
                for (const QString &l : mathLines)
                    latex += l + '\n';
                latex = latex.trimmed();
                if (latex.contains('\n')) {
                    QStringList parts;
                    for (const QString &l : latex.split('\n'))
                        if (!l.trimmed().isEmpty())
                            parts << l.trimmed();
                    latex = "\\begin{aligned}" + parts.join(" \\\\ ") + "\\end{aligned}";
                }
                mathBlocks << ("<div data-type=\"block-math\" data-latex=\"" + latex + "\"></div>");
                inMathBlock = false;
            }
        } else if (inMathBlock) {
            mathLines << line;
        } else if (line.startsWith("```")) {
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
            processed += extractSubscripts(line, subContents) + " " + MARKER + "\n";
        } else {
            processed += extractSubscripts(line, subContents) + '\n';
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
                // 合并为一个 <pre>，内部文本用 \n 分隔
                QString merged = lines.join('\n');
                merged.replace(QRegularExpression("<[^>]*>"), "");
                QString replacement = "<pre>" + merged + "</pre>";
                html.replace(blockStart, blockEnd - blockStart, replacement);
                searchFrom = blockStart + replacement.length();
            }
        }
    }

    // 后处理 1.5: 统一代码块为 TipTap codeBlock 契约 <pre><code class="language-L">
    //   TipTap 从 <pre> 的 firstElementChild(<code>) 的 language- class 读取语言；
    //   合并后的 <pre> 为纯文本，单行 <pre> 可能带 style 属性与 <code> 子节点，统一重建。
    if (!codeLangs.isEmpty()) {
        QRegularExpression preBlockRe("<pre[^>]*>(.*?)</pre>",
            QRegularExpression::DotMatchesEverythingOption);
        int langIdx = 0;
        int offset = 0;
        while (langIdx < codeLangs.size()) {
            auto m = preBlockRe.match(html, offset);
            if (!m.hasMatch()) break;
            QString inner = m.captured(1);
            inner.remove(QRegularExpression("<[^>]*>")); // 去掉 <code> 等内部标签，保留实体
            const QString &lang = codeLangs[langIdx];
            QString codeOpen = lang.isEmpty()
                ? QStringLiteral("<code>")
                : QStringLiteral("<code class=\"language-") + lang + "\">";
            QString replacement = "<pre>" + codeOpen + inner + "</code></pre>";
            html.replace(m.capturedStart(), m.capturedLength(), replacement);
            offset = m.capturedStart() + replacement.length();
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
    // 行内公式 $...$ → <span data-type="inline-math">；上下标 Pandoc → <sub>/<sup>
    // 此块统一保护 <code>/<pre>：$、~、^ 在代码内均不应被转换
    {
        QStringList codeBlocks;
        // 保护 <pre> 和 <code>（含属性）
        QRegularExpression codeRe(
            R"(<(pre|code)[^>]*>.*?</\1>)",
            QRegularExpression::DotMatchesEverythingOption);
        int n = 0;
        int offset = 0;
        while (true) {
            auto m = codeRe.match(html, offset);
            if (!m.hasMatch()) break;
            codeBlocks << m.captured();
            html.replace(m.capturedStart(), m.capturedLength(),
                         QString("__CODE%1__").arg(n++));
            offset = m.capturedStart();
        }
        // 上标（Pandoc 语法）：^x^ → <sup>
        //   下标 ~x~ 已在 setMarkdown 前替换为占位符（Qt 会把 ~x~ 当作删除线）。
        //   此时行内公式 $...$ 仍为字面文本（单个 ^ 的公式需成对才匹配，安全），
        //   块级公式已是 ZZMB 占位符（无 ^），安全。
        html.replace(QRegularExpression("\\^([^\\^\\n]+?)\\^"),
                     "<sup>\\1</sup>");
        // 行内公式 $...$ → inline-math（(?<!\$)\$ 确保不匹配 $$）
        html.replace(QRegularExpression(
            R"((?<!\$)\$(?!\$)([^$\n]+?)(?<!\$)\$(?!\$))"),
            "<span data-type=\"inline-math\" data-latex=\"\\1\"></span>");
        // 恢复 <code>/<pre>
        for (int i = 0; i < codeBlocks.size(); ++i)
            html.replace(QString("__CODE%1__").arg(i), codeBlocks[i]);
    }
    // 恢复下标占位符 ZZSB → <sub>（内容来自原始 markdown，需 HTML 转义）
    for (int i = 0; i < subContents.size(); ++i) {
        QString c = subContents[i];
        c.replace('&', "&amp;").replace('<', "&lt;").replace('>', "&gt;");
        html.replace("ZZSB" + QString::number(i) + "ZZ", "<sub>" + c + "</sub>");
    }
    // 恢复数学公式占位符（前后可能有空 <p>，一并清除）
    for (int i = 0; i < mathBlocks.size(); ++i) {
        QString ph = QString("ZZMB%1ZZ").arg(i);
        // 占位符在 <p>ZZMB0ZZ</p> 中，替换时连 <p> 一起换掉
        html.replace(QRegularExpression("<p[^>]*>" + ph + "</p>"), mathBlocks[i]);
        html.replace(ph, mathBlocks[i]); // fallback
    }
    // 清除紧邻 math div 的空 <p>（占位符产生的伪影，不误删正文空行）
    html.replace(QRegularExpression(
        "<p[^>]*>(<br[^>]*>)?</p>\\s*(<div data-type=\"block-math\")"), "\\3");
    html.replace(QRegularExpression(
        "(</div>)\\s*<p[^>]*>(<br[^>]*>)?</p>"), "\\1");

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

    // 0) 数学公式 → $$/$$ 或 $$...$$（在 setHtml 前保护，避免 data-latex 被丢弃）
    //    注: 属性顺序不固定 — toHtml 输出 data-type 在前，TipTap 输出 data-latex 在前
    {
        int n = 0;
        int offset = 0;
        // 先匹配整个元素，再从中提取 data-latex（属性顺序无关）
        QRegularExpression blockMathTagRe(
            R"re(<div\b[^>]*\bdata-type\s*=\s*"block-math"[^>]*></div>)re");
        QRegularExpression latexRe(R"re(data-latex\s*=\s*"([^"]*)")re");
        while (true) {
            auto m = blockMathTagRe.match(processed, offset);
            if (!m.hasMatch()) break;
            QString tag = m.captured();
            auto lm = latexRe.match(tag);
            if (!lm.hasMatch()) { offset = m.capturedStart() + 1; continue; }
            QString latex = lm.captured(1);
            latex.replace("\\begin{aligned}", "");
            latex.replace("\\end{aligned}", "");
            QStringList parts;
            for (const QString &p : latex.split(" \\\\ "))
                parts << p.trimmed();
            QString md = "$$\n" + parts.join("\n") + "\n$$\n";
            QString ph = QString("ZZBM%1ZZ").arg(n++);
            placeholders << ph;
            backups << md;
            processed.replace(m.capturedStart(), m.capturedLength(), ph);
            offset = m.capturedStart() + ph.length();
        }
        // 行内公式（同理：属性顺序无关）
        QRegularExpression inlineMathTagRe(
            R"re(<span\b[^>]*\bdata-type\s*=\s*"inline-math"[^>]*></span>)re");
        offset = 0;
        while (true) {
            auto m = inlineMathTagRe.match(processed, offset);
            if (!m.hasMatch()) break;
            QString tag = m.captured();
            auto lm = latexRe.match(tag);
            if (!lm.hasMatch()) { offset = m.capturedStart() + 1; continue; }
            QString latex = lm.captured(1);
            QString md = "$" + latex + "$";
            QString ph = QString("ZZIM%1ZZ").arg(n++);
            placeholders << ph;
            backups << md;
            processed.replace(m.capturedStart(), m.capturedLength(), ph);
            offset = m.capturedStart() + ph.length();
        }
    }

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
            // 提取语言（TipTap codeBlock 输出 <code class="language-xxx">）
            QString lang;
            QRegularExpression langRe("class=\"language-([^\"]+)\"");
            auto lm = langRe.match(code);
            if (lm.hasMatch()) lang = lm.captured(1);
            code.remove(QRegularExpression("<[^>]*>"));
            code = unescapeHtmlEntities(code);
            code = code.trimmed();
            QString ph = QString("ZZCB%1ZZ").arg(n++);
            placeholders << ph;
            QString fence = lang.isEmpty() ? QStringLiteral("```") : ("```" + lang);
            backups << fence + "\n" + code + "\n```";
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

    // 上下标 <sub>/<sup> → Pandoc ~x~/^x^（toMarkdown 会丢弃 sub/sup 标签）
    //   Pandoc 上下标内容为纯文本，剥离内部富文本标签
    {
        int n = 0;
        int offset = 0;
        QRegularExpression subRe(R"(<sub[^>]*>(.*?)</sub>)",
            QRegularExpression::DotMatchesEverythingOption);
        while (true) {
            auto m = subRe.match(processed, offset);
            if (!m.hasMatch()) break;
            QString text = m.captured(1);
            text.remove(QRegularExpression("<[^>]*>"));
            QString ph = QString("ZZSB%1ZZ").arg(n++);
            placeholders << ph;
            backups << "~" + text + "~";
            processed.replace(m.capturedStart(), m.capturedLength(), ph);
            offset = m.capturedStart() + ph.length();
        }
        offset = 0;
        QRegularExpression supRe(R"(<sup[^>]*>(.*?)</sup>)",
            QRegularExpression::DotMatchesEverythingOption);
        while (true) {
            auto m = supRe.match(processed, offset);
            if (!m.hasMatch()) break;
            QString text = m.captured(1);
            text.remove(QRegularExpression("<[^>]*>"));
            QString ph = QString("ZZSP%1ZZ").arg(n++);
            placeholders << ph;
            backups << "^" + text + "^";
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
            backups << "`" + unescapeHtmlEntities(m.captured(1)) + "`";
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
            backups << "`" + unescapeHtmlEntities(m.captured(1)) + "`";
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
