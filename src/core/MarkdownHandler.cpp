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

// 提取 markdown 行中的原始 HTML 内联标签（<span style>/<u>），
// 替换为占位符 ZZHI{n}ZZ。这些标签无法用 Markdown 语法表达，
// 需保护后直通 Qt setMarkdown，后处理时还原为原始 HTML。
// 不用 DotMatchesEverythingOption——markdown 源码逐行处理，<span>/<u>
// 作为内联元素不会跨行（与 fromHtml() 侧不同，那里处理的是完整 HTML 文档）。
static QString extractHtmlInlineElements(const QString &line,
                                         QStringList &backups)
{
    QString r = line;
    int n = backups.size();
    // <span style="...">text</span>
    {
        QRegularExpression spanRe(
            R"(<span\b[^>]*\bstyle\s*=\s*"[^"]*"[^>]*>(.*?)</span>)");
        int offset = 0;
        while (true) {
            auto m = spanRe.match(r, offset);
            if (!m.hasMatch()) break;
            backups << m.captured();
            QString ph = QString("ZZHI%1ZZ").arg(n++);
            r.replace(m.capturedStart(), m.capturedLength(), ph);
            offset = m.capturedStart() + ph.length();
        }
    }
    // <u>text</u>
    {
        QRegularExpression uRe(R"(<u>(.*?)</u>)");
        int offset = 0;
        while (true) {
            auto m = uRe.match(r, offset);
            if (!m.hasMatch()) break;
            backups << m.captured();
            QString ph = QString("ZZHI%1ZZ").arg(n++);
            r.replace(m.capturedStart(), m.capturedLength(), ph);
            offset = m.capturedStart() + ph.length();
        }
    }
    // <kbd>text</kbd>（内联键盘元素，与 <u> 一样复用 ZZHI 占位符体系）
    {
        QRegularExpression kbdRe(R"(<kbd>(.*?)</kbd>)");
        int offset = 0;
        while (true) {
            auto m = kbdRe.match(r, offset);
            if (!m.hasMatch()) break;
            backups << m.captured();
            QString ph = QString("ZZHI%1ZZ").arg(n++);
            r.replace(m.capturedStart(), m.capturedLength(), ph);
            offset = m.capturedStart() + ph.length();
        }
    }
    // <small>text</small>（内联小字元素）
    {
        QRegularExpression smallRe(R"(<small>(.*?)</small>)");
        int offset = 0;
        while (true) {
            auto m = smallRe.match(r, offset);
            if (!m.hasMatch()) break;
            backups << m.captured();
            QString ph = QString("ZZHI%1ZZ").arg(n++);
            r.replace(m.capturedStart(), m.capturedLength(), ph);
            offset = m.capturedStart() + ph.length();
        }
    }
    return r;
}

// 提取脚注引用 [^id] → ZZFR 占位符（后处理还原为 <sup data-type="footnoteRef">）。
// 先保护行内代码 `...`（其内的 [^x] 原样保留），再转换引用，最后还原代码。
static QString extractFootnoteRefs(QString line, QStringList &refs)
{
    // 1) 暂存行内代码片段（含 [^x] 的反引号内容不转换）
    QStringList codes;
    QRegularExpression codeSpanRe("`([^`]+)`");
    int off = 0;
    while (true) {
        auto m = codeSpanRe.match(line, off);
        if (!m.hasMatch()) break;
        codes << m.captured(0);
        QString cph = "\x01CODE" + QString::number(codes.size() - 1) + "\x01";
        line.replace(m.capturedStart(), m.capturedLength(), cph);
        off = m.capturedStart() + cph.length();
    }
    // 2) 转换脚注引用 [^id]
    QRegularExpression refRe("\\[\\^([^\\]]+?)\\]");
    off = 0;
    while (true) {
        auto m = refRe.match(line, off);
        if (!m.hasMatch()) break;
        QString id = m.captured(1).trimmed();
        refs << ("<sup data-type=\"footnoteRef\" data-id=\"" + id + "\">"
                 + id + "</sup>");
        QString ph = "ZZFR" + QString::number(refs.size() - 1) + "ZZ";
        line.replace(m.capturedStart(), m.capturedLength(), ph);
        off = m.capturedStart() + ph.length();
    }
    // 3) 还原行内代码
    for (int i = 0; i < codes.size(); ++i)
        line.replace("\x01CODE" + QString::number(i) + "\x01", codes[i]);
    return line;
}

// 脚注定义内容（MD 行内）→ footnoteDef 契约 HTML（marker + footnote-content 包裹）。
// 内容借嵌套 QTextDocument 转 inline HTML（保留粗体/斜体/链接等），仅取首段 inline。
static QString buildFootnoteDefHtml(const QString &id, const QString &contentMd,
                                    QStringList &subContents)
{
    QTextDocument d;
    d.setMarkdown(extractSubscripts(contentMd, subContents));
    QString h = d.toHtml();
    auto bm = QRegularExpression("<body[^>]*>(.*)</body>",
        QRegularExpression::DotMatchesEverythingOption).match(h);
    QString inner = bm.hasMatch() ? bm.captured(1).trimmed() : QString();
    // 去掉外层 <p></p>，仅保留 inline
    inner.remove(QRegularExpression("^\\s*<p[^>]*>"));
    inner.remove(QRegularExpression("</p>\\s*$"));
    return "<div data-type=\"footnoteDef\" data-id=\"" + id + "\">"
         + "<span class=\"footnote-marker\">" + id + "</span>"
         + "<span class=\"footnote-content\">" + inner + "</span></div>";
}

// ===== 任务列表（GFM task list ↔ TipTap taskList/taskItem 契约）=====
// TipTap 只认 ul[data-type="taskList"] / li[data-type="taskItem"]（data-checked），
// 而 QTextDocument::setMarkdown 输出 Qt 自家格式（li class="unchecked/checked"），
// 双向都不兼容，需在 toHtml 预处理与 fromHtml 占位保护中显式转换。

// 判断是否为任务列表项行：- [ ] / - [x]（允许缩进与 -/*/+ 标记）
static bool isTaskItemLine(const QString &line)
{
    static const QRegularExpression re(
        "^\\s*[-*+]\\s+\\[[ xX]\\](?:\\s+\\S.*)?\\s*$");
    return re.match(line).hasMatch();
}

// 判断是否为列表项行（无序/有序，用于隔离规则的状态跟踪；
// 注意 **粗体** 行与 --- 分隔线不误判：标记后必须紧跟空白）
static bool isListItemLine(const QString &line)
{
    static const QRegularExpression re(
        "^\\s*(?:[-*+]\\s+\\S|\\d{1,9}[.)]\\s+\\S)");
    return re.match(line).hasMatch();
}

struct TaskParseItem {
    int depth;
    bool checked;
    QString text;
};

// 递归构建一层 taskList HTML：depth 相同的条目互为兄弟，
// 更深的条目嵌入前一条目内容（div）末尾，符合 TipTap taskItem 的
// content 'paragraph block*'（nested:true）契约。
static QString buildTaskListLevel(const QList<TaskParseItem> &items, int &idx,
                                  int depth, QStringList &subContents)
{
    QString html = QStringLiteral("<ul data-type=\"taskList\">");
    while (idx < items.size() && items[idx].depth == depth) {
        const TaskParseItem &it = items[idx++];
        // 条目行内内容借嵌套 QTextDocument 转换（保留粗体/斜体/链接等）；
        // 下标 ~x~ 先占位（Qt 会误判为删除线），由 toHtml 后处理统一还原
        QString inner;
        if (it.text.isEmpty()) {
            inner = QStringLiteral("<p></p>");
        } else {
            QTextDocument d;
            d.setMarkdown(extractSubscripts(it.text, subContents));
            QString h = d.toHtml();
            auto bm = QRegularExpression(
                "<body[^>]*>(.*)</body>",
                QRegularExpression::DotMatchesEverythingOption).match(h);
            inner = bm.hasMatch() ? bm.captured(1).trimmed() : QString();
            if (inner.isEmpty())
                inner = QStringLiteral("<p></p>");
        }
        html += QStringLiteral("<li data-type=\"taskItem\" data-checked=\"")
                + (it.checked ? QStringLiteral("true") : QStringLiteral("false"))
                + QStringLiteral("\"><label><input type=\"checkbox\"")
                + (it.checked ? QStringLiteral(" checked=\"checked\"") : QString())
                + QStringLiteral("><span></span></label><div>") + inner;
        if (idx < items.size() && items[idx].depth > depth)
            html += buildTaskListLevel(items, idx, depth + 1, subContents);
        html += QStringLiteral("</div></li>");
    }
    html += QStringLiteral("</ul>");
    return html;
}

// 一组连续任务列表行 → TipTap taskList 契约 HTML（2 空格缩进表示嵌套）
static QString buildTaskListHtml(const QStringList &taskLines,
                                 QStringList &subContents)
{
    static const QRegularExpression lineRe(
        "^(\\s*)[-*+]\\s+\\[([ xX])\\](?:\\s+(.*))?$");
    QList<TaskParseItem> items;
    int prevDepth = -1; // 首行缩进时钳制到 0，防止归零失败
    for (const QString &l : taskLines) {
        auto m = lineRe.match(l);
        if (!m.hasMatch()) continue;
        int depth = m.captured(1).length() / 2;
        if (depth > prevDepth + 1) depth = prevDepth + 1; // 防止非法深度跳变
        items.append({depth, m.captured(2).toLower() == "x",
                      m.captured(3).trimmed()});
        prevDepth = depth;
    }
    int idx = 0;
    return buildTaskListLevel(items, idx, 0, subContents);
}

// fromHtml 方向：从 <ul 开标签位置平衡扫描，返回配对 </ul> 的起始位置；失败返回 -1
static int findMatchingUlClose(const QString &s, int openTagStart)
{
    static const QRegularExpression ulTagRe("<(/?)ul\\b[^>]*>");
    int depth = 0;
    int pos = openTagStart;
    while (true) {
        auto m = ulTagRe.match(s, pos);
        if (!m.hasMatch()) return -1;
        if (m.captured(1).isEmpty()) ++depth;   // <ul ...>
        else if (--depth == 0) return m.capturedStart();
        pos = m.capturedEnd();
    }
}

// fromHtml 方向：从 <div 开标签位置平衡扫描，返回配对 </div> 的起始位置；失败返回 -1
static int findMatchingDivClose(const QString &s, int openTagStart)
{
    static const QRegularExpression divTagRe("<(/?)div\\b[^>]*>");
    int depth = 0;
    int pos = openTagStart;
    while (true) {
        auto m = divTagRe.match(s, pos);
        if (!m.hasMatch()) return -1;
        if (m.captured(1).isEmpty()) ++depth;    // <div ...>
        else if (--depth == 0) return m.capturedStart();
        pos = m.capturedEnd();
    }
}

// fromHtml 方向：从 <span 开标签位置平衡扫描，返回配对 </span> 的起始位置；失败返回 -1
//   footnote-content 内含 Qt 生成的嵌套 <span style="...">（粗体/链接着色等），
//   非贪婪 .*?</span> 会停在首个嵌套 </span>，必须平衡扫描才能取到完整内容。
static int findMatchingSpanClose(const QString &s, int openTagStart)
{
    static const QRegularExpression spanTagRe("<(/?)span\\b[^>]*>");
    int depth = 0;
    int pos = openTagStart;
    while (true) {
        auto m = spanTagRe.match(s, pos);
        if (!m.hasMatch()) return -1;
        if (m.captured(1).isEmpty()) ++depth;   // <span ...>
        else if (--depth == 0) return m.capturedStart();
        pos = m.capturedEnd();
    }
}

// 任务条目内容 → 纯文本：行内富文本转为 markdown 语法，其余标签剥离。
// 调用前提：嵌套任务列表已提取，行内代码/上下标/高亮/公式已是 ZZ* 占位符（原样保留）。
static QString taskItemInlineText(QString content)
{
    content.replace(QRegularExpression("<(strong|b)\\b[^>]*>(.*?)</\\1>",
        QRegularExpression::DotMatchesEverythingOption), "**\\2**");
    content.replace(QRegularExpression("<(em|i)\\b[^>]*>(.*?)</\\1>",
        QRegularExpression::DotMatchesEverythingOption), "*\\2*");
    content.replace(QRegularExpression("<(s|del)\\b[^>]*>(.*?)</\\1>",
        QRegularExpression::DotMatchesEverythingOption), "~~\\2~~");
    content.replace(QRegularExpression(
        "<a\\b[^>]*href\\s*=\\s*\"([^\"]*)\"[^>]*>(.*?)</a>",
        QRegularExpression::DotMatchesEverythingOption), "[\\2](\\1)");
    // 块边界 → 换行（条目内多段落用缩进续行表达）
    content.replace(QRegularExpression("<br\\s*/?>"), "\n");
    content.replace(QRegularExpression("</p>"), "\n");
    content.remove(QRegularExpression("<[^>]*>"));
    content = unescapeHtmlEntities(content);
    QStringList kept;
    for (const QString &l : content.split('\n')) {
        QString t = l.trimmed();
        if (!t.isEmpty()) kept << t;
    }
    return kept.join("\n");
}

// 任务列表 HTML → GFM markdown（indent 为嵌套层级，每层 2 空格）。
// 依赖调用顺序：行内代码/上下标/高亮/公式已先行占位保护，条目文本中的
// ZZ* 占位符原样保留，最终在 fromHtml 逆序还原阶段统一还原。
static QString taskListInnerToMd(const QString &inner, int indent)
{
    static const QRegularExpression liTagRe("<(/?)li\\b[^>]*>");
    static const QRegularExpression taskLiRe(
        "<li\\b[^>]*\\bdata-type\\s*=\\s*\"taskItem\"[^>]*>");
    static const QRegularExpression checkedRe("data-checked\\s*=\\s*\"(true|)\"");
    static const QRegularExpression ulOpenRe(
        "<ul\\b[^>]*\\bdata-type\\s*=\\s*\"taskList\"[^>]*>");

    QString pad(indent * 2, ' ');
    QString md;

    // 平衡扫描提取顶层 taskItem 条目（嵌套 li 在条目内容中递归处理）
    int depth = 0;
    int pos = 0;
    int itemOpenEnd = -1;    // 当前顶层条目开标签的末尾位置
    bool itemChecked = false;
    while (true) {
        auto m = liTagRe.match(inner, pos);
        if (!m.hasMatch()) break;
        if (m.captured(1).isEmpty()) {           // <li ...>
            if (depth == 0 && taskLiRe.match(m.captured()).hasMatch()) {
                itemOpenEnd = m.capturedEnd();
                itemChecked = checkedRe.match(m.captured()).hasMatch();
            }
            ++depth;
        } else {                                  // </li>
            --depth;
            if (depth == 0 && itemOpenEnd >= 0) {
                QString content = inner.mid(itemOpenEnd,
                                            m.capturedStart() - itemOpenEnd);
                // 1) 移除复选框 label（input/span 结构）
                content.remove(QRegularExpression(
                    "<label\\b[^>]*>.*?</label>",
                    QRegularExpression::DotMatchesEverythingOption));
                // 2) 嵌套任务列表递归提取（须在剥离标签前进行）
                QString nestedMd;
                while (true) {
                    auto um = ulOpenRe.match(content);
                    if (!um.hasMatch()) break;
                    int closeStart = findMatchingUlClose(content, um.capturedStart());
                    if (closeStart < 0) break;
                    nestedMd += taskListInnerToMd(
                        content.mid(um.capturedEnd(),
                                    closeStart - um.capturedEnd()),
                        indent + 1);
                    content.remove(um.capturedStart(),
                                   closeStart + 5 - um.capturedStart()); // 含 </ul>
                }
                // 3) 行内内容 → 文本；首行随复选框，续行缩进 2 空格
                QString text = taskItemInlineText(content);
                md += pad + (itemChecked ? "- [x] " : "- [ ] ");
                if (!text.isEmpty()) {
                    QStringList textLines = text.split('\n');
                    md += textLines.first();
                    for (int k = 1; k < textLines.size(); ++k)
                        md += "\n" + pad + "  " + textLines[k];
                }
                md += "\n";
                // 4) 嵌套任务列表行（已带自身缩进）
                md += nestedMd;
                itemOpenEnd = -1;
            }
        }
        pos = m.capturedEnd();
    }
    return md;
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
    QStringList taskLists; // 任务列表 HTML（TipTap taskList 契约），后处理还原
    QStringList footnoteRefs;   // 脚注引用 <sup> HTML（后处理还原）
    QStringList footnoteDefs;   // 脚注定义 def HTML（按 id 去重，首次定义顺序）
    QStringList footnoteDefIds; // 已收集的定义 id（去重用）
    QStringList htmlInlineBackups; // 原始 HTML 内联标签（<span style> / <u>）

    QStringList mathBlocks;  // 收集 $$ 块的内容
    QStringList mathLines;    // 当前 $$ 块的行

    // 上一处理行的块类型。隔离规则：列表后紧跟正文（无空行）时补空行，
    // 否则 md4c 的 lazy-continuation 会把正文吞入列表项、后续列表被合并。
    // （有意偏离严格 CommonMark：编辑区语义优先，同 blockquote 先例）
    enum BlockType { BT_NONE, BT_LIST, BT_PARA };
    BlockType lastBlock = BT_NONE;

    for (int i = 0; i < lines.size(); ++i) {
        const QString &line = lines[i];
        const QString &next = (i + 1 < lines.size()) ? lines[i + 1] : QString();

        // 脚注定义行 [^id]: 内容（非代码块）—— 抽取归集到文末容器，从正文流移除
        static const QRegularExpression footnoteDefRe("^\\[\\^([^\\]]+)\\]\\s*:\\s*(.*)$");
        auto footnoteDefMatch = footnoteDefRe.match(line);
        bool isFootnoteDef = !inCodeBlock && footnoteDefMatch.hasMatch();

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
            lastBlock = BT_NONE;
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
            lastBlock = BT_NONE;
        } else if (inCodeBlock) {
            processed += line + '\n';
        } else if (isFootnoteDef) {
            // 脚注定义行 [^id]: 内容（非代码块）—— 抽取归集到文末容器，从正文流移除
            QString fid = footnoteDefMatch.captured(1).trimmed();
            if (!footnoteDefIds.contains(fid)) {   // 同 id 取首次定义
                footnoteDefIds << fid;
                footnoteDefs << buildFootnoteDefHtml(fid, footnoteDefMatch.captured(2).trimmed(), subContents);
            }
            processed += '\n';   // 行被移除，留空行维持段落分隔
            lastBlock = BT_NONE;
        } else if (line.trimmed().isEmpty()) {
            processed += '\n';
            lastBlock = BT_NONE;
        } else if (isTaskItemLine(line)) {
            // 任务列表：收集连续行（含缩进嵌套），绕过 setMarkdown 整体转为
            // TipTap 契约 HTML（Qt 自家 class=unchecked/checked 格式 TipTap 不认）
            QStringList taskLines;
            while (i < lines.size() && isTaskItemLine(lines[i]))
                taskLines << lines[i++];
            --i; // for 循环会 ++i
            // 前一行非空且未以空行结尾时补空行，避免占位符被并入前一区块
            // （同隔离规则：也覆盖 $$ 占位符后紧跟任务列表的情况）
            if (!processed.isEmpty() && !processed.endsWith("\n\n"))
                processed += '\n';
            taskLists << buildTaskListHtml(taskLines, subContents);
            processed += "ZZTL" + QString::number(taskLists.size() - 1) + "ZZ\n";
            lastBlock = BT_LIST;
        } else if (isParagraphLine(line) && !next.isEmpty()
                   && isParagraphLine(next)) {
            // 隔离：列表后紧跟正文时补空行，避免被吞入列表项
            if (lastBlock == BT_LIST)
                processed += '\n';
            // 两行正文 → 第一行末尾嵌入标记，setMarkdown 保留该标记
            // 提取顺序：先下标 ~x~，再脚注 [^id]，最后 HTML 内联标签（<span style>/<u>）。
            // 若先提取 HTML 内联标签，标签内部的 MD 语法（~x~、[^id] 等）会被隐藏，
            // 后续 sub/footnote 转换器无法识别，往返数据丢失。先替换为占位符后，
            // 内层占位符随外层 <span>/<u> 一起被 ZZHI 包裹，还原时先开外层再开内层。
            QString cl = line;
            cl = extractSubscripts(cl, subContents);
            cl = extractFootnoteRefs(cl, footnoteRefs);
            cl = extractHtmlInlineElements(cl, htmlInlineBackups);
            processed += cl + " " + MARKER + "\n";
            lastBlock = BT_PARA;
        } else {
            // 隔离：列表后紧跟正文时补空行（同上）
            if (isParagraphLine(line) && lastBlock == BT_LIST)
                processed += '\n';
            // 隔离：正文后紧跟列表时同样补空行 —— CommonMark 规定非 1 起始的
            // 有序列表不能打断段落，md4c 会把列表项当作段落延续行吞并
            if (isListItemLine(line) && lastBlock == BT_PARA)
                processed += '\n';
            QString cl = line;
            cl = extractSubscripts(cl, subContents);
            cl = extractFootnoteRefs(cl, footnoteRefs);
            cl = extractHtmlInlineElements(cl, htmlInlineBackups);
            processed += cl + '\n';
            lastBlock = isListItemLine(line) ? BT_LIST
                        : (isParagraphLine(line) ? BT_PARA : BT_NONE);
        }
    }

    // 保护手写 <div> 块级容器（排除 data-type 内部结构）
    // 必须置于 setMarkdown 之前：QTextDocument 的 markdown 解析会丢弃 <div> 标签
    // （仅保留内部文本），若在 setMarkdown 之后再匹配已无 <div> 可保。
    // 仅保护单行 <div>：若开启 DotMatchesEverythingOption 允许多行匹配，backup
    // 捕获的 div 文本会包含 BRMARKER 占位符，而 BRMARKER→<br> 替换发生在 backup
    // 之后，还原时 ZZDV 会把字面 BRMARKER 文本放回 —— 数据损坏。div 规范仅包裹
    // 行内内容（天然单行），故限定单行作用域即可闭合该路径。
    // 注释见后处理 ZZDV 还原步骤：还原必须先于 ZZHI 等内层占位符。
    QStringList divBackups;
    {
        QRegularExpression divRe(
            R"(<div\b(?![^>]*\bdata-type\s*=)[^>]*>(.*?)</div>)");
        int n = 0;
        int offset = 0;
        while (true) {
            auto m = divRe.match(processed, offset);
            if (!m.hasMatch()) break;
            divBackups << m.captured();
            QString ph = QString("ZZDV%1ZZ").arg(n++);
            processed.replace(m.capturedStart(), m.capturedLength(), ph);
            offset = m.capturedStart() + ph.length();
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

    // 还原任务列表占位符 ZZTL → TipTap taskList 契约 HTML
    //   先于 mark/上下标/行内公式转换还原，条目文本中的相应语法也能被转换；
    //   ZZSB（下标）占位符由后续统一步骤还原
    for (int i = 0; i < taskLists.size(); ++i) {
        QString ph = QString("ZZTL%1ZZ").arg(i);
        html.replace(QRegularExpression("<p[^>]*>" + ph + "</p>"), taskLists[i]);
        html.replace(ph, taskLists[i]); // fallback：占位符未独立成段时
    }

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
        // 行内公式 $...$ → inline-math 必须先于上标 ^x^ 处理：
        //   先移除 $...$ 内的 ^ 字符（移入 data-latex 属性），否则上标正则
        //   \^[^\^\n]+?\^ 会跨区域误匹配——从 ^{n} 的 ^ 一直吃到 ^{2} 的 ^，
        //   把 \sum_{i=1}^{n} i^{2} 破坏为 \sum_{i=1}<sup>{n} i</sup>{2}。
        html.replace(QRegularExpression(
            R"((?<!\$)\$(?!\$)([^$\n]+?)(?<!\$)\$(?!\$))"),
            "<span data-type=\"inline-math\" data-latex=\"\\1\"></span>");
        // 保护 math span（上标正则会误匹配 data-latex 属性值内的 ^）
        QStringList mathSpans;
        QRegularExpression mathSpanRe(
            R"(<span\b[^>]*\bdata-type\s*=\s*"inline-math"[^>]*></span>)");
        offset = 0;
        while (true) {
            auto m = mathSpanRe.match(html, offset);
            if (!m.hasMatch()) break;
            mathSpans << m.captured();
            html.replace(m.capturedStart(), m.capturedLength(),
                         QString("__IMATH%1__").arg(mathSpans.size() - 1));
            offset = m.capturedStart();
        }
        // 上标（Pandoc 语法）：^x^ → <sup>
        //   下标 ~x~ 已在 setMarkdown 前替换为占位符（Qt 会把 ~x~ 当作删除线）。
        //   此时行内公式已被 __IMATH 占位符保护、块级公式已是 ZZMB 占位符，安全。
        html.replace(QRegularExpression("\\^([^\\^\\n]+?)\\^"),
                     "<sup>\\1</sup>");
        // 恢复 math span
        for (int i = 0; i < mathSpans.size(); ++i)
            html.replace(QString("__IMATH%1__").arg(i), mathSpans[i]);
        // 恢复 <code>/<pre>
        for (int i = 0; i < codeBlocks.size(); ++i)
            html.replace(QString("__CODE%1__").arg(i), codeBlocks[i]);
    }
    // 还原 <div> 占位符 ZZDV → 原始 <div>
    // 必须先于 ZZHI/ZZFR/ZZSB 等内层占位符还原：div 内部可能含这些占位符
    // （如 <div><span style>…</span></div>），先还原外层 div，内层占位符才能
    // 被后续各自的还原步骤继续处理（与 ZZHI 先于 ZZFR 同理）。
    for (int i = 0; i < divBackups.size(); ++i)
        html.replace("ZZDV" + QString::number(i) + "ZZ", divBackups[i]);
    // 还原 HTML 内联标签占位符 ZZHI → 原始 <span style> / <u>
    // 必须先于 ZZFR/ZZSB 还原：ZZHI 占位符内容可能含 <span>/<u> 内部的
    // ZZSB/ZZFR 占位符（如 <span>~2~</span>、<u>[^1]</u>），先开外层标签，
    // 内层占位符才能被后续 ZZFR/ZZSB 还原步骤继续处理。
    for (int i = 0; i < htmlInlineBackups.size(); ++i)
        html.replace("ZZHI" + QString::number(i) + "ZZ", htmlInlineBackups[i]);
    // 还原脚注引用占位符 ZZFR → <sup data-type="footnoteRef">
    for (int i = 0; i < footnoteRefs.size(); ++i)
        html.replace("ZZFR" + QString::number(i) + "ZZ", footnoteRefs[i]);

    // 追加脚注定义容器（仅当存在定义）
    if (!footnoteDefs.isEmpty()) {
        QString container = "<div data-type=\"footnoteContainer\">";
        for (const QString &d : footnoteDefs) container += d;
        container += "</div>";
        int bodyClose = html.indexOf("</body>");
        if (bodyClose >= 0) html.insert(bodyClose, container);
        else html += container;
    }
    // 恢复下标占位符 ZZSB → <sub>（内容来自原始 markdown，需 HTML 转义）
    // 注意：必须在脚注容器追加之后执行——脚注定义内容可能含 ZZSB 占位符。
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
            QString md = "$$\n" + parts.join("\n") + "\n$$";
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

    // 6) 脚注引用 <sup data-type="footnoteRef"> → [^id]
    //    必须在「上下标 <sup> → ^x^」步骤之前：否则 footnoteRef 的 <sup> 会被
    //    通用 sup 正则吞掉变成 ^1^，引用无法还原。此处优先识别 data-type 契约。
    {
        int n = 0;
        int offset = 0;
        QRegularExpression supTagRe(
            R"(<sup\b[^>]*\bdata-type\s*=\s*"footnoteRef"[^>]*>.*?</sup>)",
            QRegularExpression::DotMatchesEverythingOption);
        QRegularExpression idRe("data-id\\s*=\\s*\"([^\"]*)\"");
        while (true) {
            auto m = supTagRe.match(processed, offset);
            if (!m.hasMatch()) break;
            auto im = idRe.match(m.captured());
            QString id = im.hasMatch() ? im.captured(1) : QString();
            QString ph = QString("ZZFR%1ZZ").arg(n++);
            placeholders << ph;
            backups << "[^" + id + "]";
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

    // 5) 任务列表 <ul data-type="taskList"> → GFM - [ ] / - [x]
    //    QTextDocument 不认识 data-type/data-checked（保存后复选框丢失），
    //    在 setHtml 前整体提取为占位符，toMarkdown 后还原。
    //    置于行内代码/上下标/高亮/公式占位保护之后：条目文本中的 ZZ* 占位符
    //    被原样保留，最终在逆序还原阶段统一还原。
    {
        QRegularExpression ulOpenRe(
            "<ul\\b[^>]*\\bdata-type\\s*=\\s*\"taskList\"[^>]*>");
        int n = 0;
        int offset = 0;
        while (true) {
            auto m = ulOpenRe.match(processed, offset);
            if (!m.hasMatch()) break;
            int closeStart = findMatchingUlClose(processed, m.capturedStart());
            if (closeStart < 0) { offset = m.capturedStart() + 1; continue; }
            QString md2 = taskListInnerToMd(
                processed.mid(m.capturedEnd(), closeStart - m.capturedEnd()), 0);
            while (md2.endsWith('\n')) md2.chop(1);
            QString ph = QString("ZZTL%1ZZ").arg(n++);
            placeholders << ph;
            backups << md2;
            int totalLen = closeStart + 5 - m.capturedStart(); // 含 </ul>
            processed.replace(m.capturedStart(), totalLen, ph);
            offset = m.capturedStart() + ph.length();
        }
    }

    // 7) 脚注容器 <div data-type="footnoteContainer"> → [^id]: 内容（多条）
    //    位于所有行内格式（上下标/行内代码/任务列表）占位化之后：footnote-content
    //    内的 <sub>/<sup>/<code> 等已先被通用步骤替换为 ZZSB/ZZSP/ZZCD 占位符，
    //    它们作为字面文本穿过嵌套 QTextDocument，最终由逆序还原统一还原
    //    （ZZFD 最后追加 → 最先还原 → [^id]: aZZSB0ZZc → 再被 ZZSB 还原为 a~b~c）。
    //    依赖前置的 Step 6：footnoteRef 的 <sup> 已先隔离为 ZZFR，不会被本步误读。
    {
        QRegularExpression contOpenRe(
            R"(<div\b[^>]*\bdata-type\s*=\s*"footnoteContainer"[^>]*>)");
        int n = 0;
        int offset = 0;
        while (true) {
            auto om = contOpenRe.match(processed, offset);
            if (!om.hasMatch()) break;
            int closeStart = findMatchingDivClose(processed, om.capturedStart());
            if (closeStart < 0) { offset = om.capturedStart() + 1; continue; }
            QString inner = processed.mid(om.capturedEnd(),
                                          closeStart - om.capturedEnd());
            // 提取每条 def div
            QRegularExpression defDivRe(
                R"(<div\b[^>]*\bdata-type\s*=\s*"footnoteDef"[^>]*>(.*?)</div>)",
                QRegularExpression::DotMatchesEverythingOption);
            QRegularExpression defIdRe("data-id\\s*=\\s*\"([^\"]*)\"");
            QRegularExpression contentOpenRe(
                R"(<span\b[^>]*class\s*=\s*"footnote-content"[^>]*>)");
            QString mdBlock;
            int doff = 0;
            while (true) {
                auto dm = defDivRe.match(inner, doff);
                if (!dm.hasMatch()) break;
                auto iim = defIdRe.match(dm.captured());
                QString id = iim.hasMatch() ? iim.captured(1) : QString();
                // 取 footnote-content span 内的 inline HTML（跳过 marker）。
                // content 含 Qt 生成的嵌套 <span style="...">（粗体/链接色等），
                // 必须平衡扫描定位配对 </span>，非贪婪正则会停在首个嵌套闭合标签。
                QString defInner = dm.captured(1);
                auto com = contentOpenRe.match(defInner);
                QString contentHtml;
                if (com.hasMatch()) {
                    int spanClose = findMatchingSpanClose(defInner, com.capturedStart());
                    contentHtml = (spanClose >= 0)
                        ? defInner.mid(com.capturedEnd(), spanClose - com.capturedEnd())
                        : defInner.mid(com.capturedEnd());
                } else {
                    contentHtml = defInner;
                }
                // inline HTML → MD（嵌套 QTextDocument）
                QTextDocument d;
                d.setHtml(contentHtml);
                QString cm2 = d.toMarkdown();
                while (cm2.endsWith('\n')) cm2.chop(1);
                mdBlock += "[^" + id + "]: " + cm2 + "\n";
                doff = dm.capturedEnd();
            }
            QString ph = QString("ZZFD%1ZZ").arg(n++);
            placeholders << ph;
            backups << mdBlock;
            int totalLen = closeStart + 6 - om.capturedStart();  // 含 </div>
            processed.replace(om.capturedStart(), totalLen, ph);
            offset = om.capturedStart() + ph.length();
        }
    }

    // N) 保护 Markdown 无法表达的 HTML 内联元素 → 占位符
    //    QTextDocument::toMarkdown() 会丢弃内联 CSS 样式和下划线标签，
    //    占位符保护后还原为原始 HTML，与 toHtml() 形成对称往返。
    //    置于最后：本步匹配的最外层 <span>/<u> 包裹的文本可能含已被早期
    //    步骤保护的占位符（ZZ*），它们作为字面文本穿过 toMarkdown，最终由
    //    逆序还原统一还原（本步最后追加 → 最先还原最外层）。
    {
        int n = 0;
        int offset = 0;
        // <span style="...">text</span>
        QRegularExpression spanStyleRe(
            R"(<span\b[^>]*\bstyle\s*=\s*"[^"]*"[^>]*>(.*?)</span>)",
            QRegularExpression::DotMatchesEverythingOption);
        while (true) {
            auto m = spanStyleRe.match(processed, offset);
            if (!m.hasMatch()) break;
            QString ph = QString("ZZHS%1ZZ").arg(n++);
            placeholders << ph;
            backups << m.captured();  // 保留完整原始 HTML
            processed.replace(m.capturedStart(), m.capturedLength(), ph);
            offset = m.capturedStart() + ph.length();
        }
        // <u>text</u>（重置 offset：<u> 可能位于最后一个 <span> 之前）
        offset = 0;
        QRegularExpression uRe(
            R"(<u>(.*?)</u>)",
            QRegularExpression::DotMatchesEverythingOption);
        while (true) {
            auto m = uRe.match(processed, offset);
            if (!m.hasMatch()) break;
            QString ph = QString("ZZUL%1ZZ").arg(n++);
            placeholders << ph;
            backups << m.captured();  // 保留完整原始 HTML
            processed.replace(m.capturedStart(), m.capturedLength(), ph);
            offset = m.capturedStart() + ph.length();
        }
        // <kbd>text</kbd>（内联键盘元素，QTextDocument::toMarkdown 会丢弃标签）
        offset = 0;
        QRegularExpression kbdRe(
            R"(<kbd>(.*?)</kbd>)",
            QRegularExpression::DotMatchesEverythingOption);
        while (true) {
            auto m = kbdRe.match(processed, offset);
            if (!m.hasMatch()) break;
            QString ph = QString("ZZKD%1ZZ").arg(n++);
            placeholders << ph;
            backups << m.captured();
            processed.replace(m.capturedStart(), m.capturedLength(), ph);
            offset = m.capturedStart() + ph.length();
        }
        // <small>text</small>（内联小字元素）
        offset = 0;
        QRegularExpression smallRe(
            R"(<small>(.*?)</small>)",
            QRegularExpression::DotMatchesEverythingOption);
        while (true) {
            auto m = smallRe.match(processed, offset);
            if (!m.hasMatch()) break;
            QString ph = QString("ZZSM%1ZZ").arg(n++);
            placeholders << ph;
            backups << m.captured();
            processed.replace(m.capturedStart(), m.capturedLength(), ph);
            offset = m.capturedStart() + ph.length();
        }
        // <div>（块级容器，排除 data-type 内部结构）
        offset = 0;
        QRegularExpression divRe(
            R"(<div\b(?![^>]*\bdata-type\s*=)[^>]*>(.*?)</div>)",
            QRegularExpression::DotMatchesEverythingOption);
        while (true) {
            auto m = divRe.match(processed, offset);
            if (!m.hasMatch()) break;
            QString ph = QString("ZZDV%1ZZ").arg(n++);
            placeholders << ph;
            backups << m.captured();
            processed.replace(m.capturedStart(), m.capturedLength(), ph);
            offset = m.capturedStart() + ph.length();
        }
    }

    QTextDocument doc;
    doc.setHtml(processed);
    QString md = doc.toMarkdown();

    // 去除 ATX 标题行首"块级列表标记"的多余反斜杠转义。
    // QTextDocument::toMarkdown()（md4c）为防止有序/无序列表误解析，
    // 会给标题内联内容开头的列表标记加反斜杠（如 "## 1\. xxx"、"## \- xxx"）。
    // 但 ATX 标题的内联内容按内联规则解析，不触发块级列表语法，这些转义多余
    // 且影响源码可读性。仅处理绝对安全的纯列表标记：
    //   数字点/数字括号（1\. / 1\)）与列表符 - / +（\- / \+）；
    // * 与 # 涉及强调/ATX 闭合序列，保留原转义以免改变语义。
    // 正文行首的转义必须保留（否则 "1. xxx" 会被解析为有序列表）——
    // 正则锚定 #{1,6}，只作用于标题行。
    md.replace(
        QRegularExpression("^(#{1,6}\\s+)(\\d{1,9})\\\\([.)])",
                           QRegularExpression::MultilineOption),
        "\\1\\2\\3");
    md.replace(
        QRegularExpression("^(#{1,6}\\s+)\\\\([-+])",
                           QRegularExpression::MultilineOption),
        "\\1\\2");

    // 规范化块间多余空行：toMarkdown（md4c）在块边界（如占位符块→表格）
    // 会输出 3+ 连续换行；压缩为标准段落分隔（\n\n = 1 空行）。
    // 必须在占位符还原【之前】执行：此时刻占位符均为单行文本（ZZxxxZZ），
    // 自身不含换行；若放到还原之后，会破坏代码块/公式内用户有意保留的空行。
    md.replace(QRegularExpression("\\n{3,}"), "\n\n");

    // 3) 还原占位符（逆序：后入先还原，防止嵌套占位符丢失）
    for (int i = placeholders.size() - 1; i >= 0; --i)
        md.replace(placeholders[i], backups[i]);

    // 4) 去除 QTextDocument 在标题行注入的纯字体样式 <span>
    //    QTextDocument::setHtml 对标题默认渲染为 <hN><span style="font-weight:700;
    //    font-size:...">text</span></hN>，ZZHS 保护后在 MD 中显现为
    //    "# <span style="...">text</span>"。这里只剔除仅含 font-weight/font-size
    //    的 span（不含 color/background-color 等用户样式），避免 QTextDocument 的
    //    标题默认渲染泄漏到 .md 源码，同时保留用户手写的颜色 span。
    {
        static const QRegularExpression headingFontSpan(
            R"(^(#{1,6}\s+)<span\s+style\s*=\s*"(?![^"]*\bcolor\s*:|[^"]*\bbackground-color\s*:)[^"]*"[^>]*>(.*?)</span>)",
            QRegularExpression::MultilineOption);
        md.replace(headingFontSpan, "\\1\\2");
    }

    while (md.endsWith('\n'))
        md.chop(1);
    return md;
}
