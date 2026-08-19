#include "MarkdownHandler.h"
#include <QTextDocument>
#include <QRegularExpression>
#include <QSet>

// 行是否仅为 <br> 等硬换行标签（含属性及自闭合写法）
// 用于 BRMARKER 逻辑：<br> 本身即是换行，其前后 \n 不应再触发 BRMARKER
static bool isBrOnlyLine(const QString &line)
{
    static const QRegularExpression brRe(
        R"(^\s*<br\b[^>]*>\s*$)");
    return brRe.match(line).hasMatch();
}

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
    // <br> 独占行仍算正文行——其后的 \n 仍需 BRMARKER 表达换行；
    // 但其前的 \n 由调用侧通过 isBrOnlyLine 抑制 BRMARKER
    if (c == '>') return true;
    return c != '#' && c != '-' && c != '|'
           && !(c >= '0' && c <= '9' && trimmed.length() > 1
                && trimmed[1] == '.');
}

// 图片独占行（![alt](src)，可带 title 与首尾空白）：视为块级图片行。
// 用于隔离：避免 md4c 的 lazy-continuation 把图片并入相邻段落成内联图片。
static bool isImageOnlyLine(const QString &line)
{
    static const QRegularExpression re(
        R"(^\s*!\[[^\]]*\]\([^)\n]*\)\s*$)");
    return re.match(line).hasMatch();
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
// 行内代码 `...` 内的 ~x~ 是字面文本（如 `H~2~O` 展示写法），先暂存代码段
// 再转换（与 extractFootnoteRefs 的 \x01CODE 保护一致），避免被误转为下标。
static QString extractSubscripts(QString line, QStringList &out)
{
    // 1) 暂存行内代码片段（反引号内的 ~x~ 原样保留）
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
    // 2) 转换 ~x~
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
    // 3) 还原行内代码
    for (int i = 0; i < codes.size(); ++i)
        line.replace("\x01CODE" + QString::number(i) + "\x01", codes[i]);
    return line;
}

// 提取转义等号 \= → ZZES 占位符（后处理还原为纯文本 =）。
// QTextDocument 将 \= 解释为转义序列 → 输出字面 =，导致 == 成对后
// 被 ==highlight== 正则误捕获；提前保护使 ==text== 转换在占位符间不受干扰。
static QString extractEscapedEquals(QString line, QStringList &out)
{
    QRegularExpression re("\\\\=");
    int offset = 0;
    while (true) {
        auto m = re.match(line, offset);
        if (!m.hasMatch()) break;
        out << "=";
        QString ph = "ZZES" + QString::number(out.size() - 1) + "ZZ";
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
// 行内代码 `...` 内的标签是字面文本（如 `H<sub>2</sub>O` 展示写法），
// 先暂存代码段再提取（与 extractFootnoteRefs 的 \x01CODE 保护一致）。
static QString extractHtmlInlineElements(const QString &line,
                                         QStringList &backups)
{
    QString r = line;
    // 1) 暂存行内代码片段（反引号内的 HTML 标签原样保留）
    QStringList codes;
    QRegularExpression codeSpanRe("`([^`]+)`");
    int codeOff = 0;
    while (true) {
        auto m = codeSpanRe.match(r, codeOff);
        if (!m.hasMatch()) break;
        codes << m.captured(0);
        QString cph = "\x01CODE" + QString::number(codes.size() - 1) + "\x01";
        r.replace(m.capturedStart(), m.capturedLength(), cph);
        codeOff = m.capturedStart() + cph.length();
    }
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
    // <mark>text</mark>（高亮，保护原始 HTML 不被 QTextDocument 丢弃）
    {
        QRegularExpression markRe(R"(<mark[^>]*>(.*?)</mark>)");
        int offset = 0;
        while (true) {
            auto m = markRe.match(r, offset);
            if (!m.hasMatch()) break;
            backups << m.captured();
            QString ph = QString("ZZHI%1ZZ").arg(n++);
            r.replace(m.capturedStart(), m.capturedLength(), ph);
            offset = m.capturedStart() + ph.length();
        }
    }
    // <sub>text</sub>（下标，保护原始 HTML 不被 QTextDocument 丢弃）
    {
        QRegularExpression subRe(R"(<sub[^>]*>(.*?)</sub>)");
        int offset = 0;
        while (true) {
            auto m = subRe.match(r, offset);
            if (!m.hasMatch()) break;
            backups << m.captured();
            QString ph = QString("ZZHI%1ZZ").arg(n++);
            r.replace(m.capturedStart(), m.capturedLength(), ph);
            offset = m.capturedStart() + ph.length();
        }
    }
    // <sup>text</sup>（上标，不含 data-type 属性——脚注 sup 另有专门处理）
    {
        QRegularExpression supRe(R"(<sup(?![^>]*\bdata-type\s*=)[^>]*>(.*?)</sup>)");
        int offset = 0;
        while (true) {
            auto m = supRe.match(r, offset);
            if (!m.hasMatch()) break;
            backups << m.captured();
            QString ph = QString("ZZHI%1ZZ").arg(n++);
            r.replace(m.capturedStart(), m.capturedLength(), ph);
            offset = m.capturedStart() + ph.length();
        }
    }
    // <br>（硬换行——自闭合 void 元素，保护不被 QTextDocument 丢弃）
    {
        QRegularExpression brRe(R"(<br\b[^>]*>)");
        int offset = 0;
        while (true) {
            auto m = brRe.match(r, offset);
            if (!m.hasMatch()) break;
            backups << m.captured();
            QString ph = QString("ZZHI%1ZZ").arg(n++);
            r.replace(m.capturedStart(), m.capturedLength(), ph);
            offset = m.capturedStart() + ph.length();
        }
    }
    // 还原行内代码
    for (int i = 0; i < codes.size(); ++i)
        r.replace("\x01CODE" + QString::number(i) + "\x01", codes[i]);
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

// 判断某行是否为「80 列折行」产生的续行（应拼回上一行），而非新的块起始。
// 反换行（fromHtml）用它区分：合法软换行已全部被 ZZ* 占位符保护，
// 故非空行之间的单 \n 只能是块边界或折行。块起始（返回 false）：
//   空行、列表项（数字./-/* 后跟内容）、块引用空行（仅 ">"）、
//   标题/表格/围栏/水平线（toMarkdown 输出中恒被空行隔开，正常不作为
//   非空行的后继出现，保守起见一并排除）。
static bool isWrapContinuation(const QString &line)
{
    if (line.isEmpty()) return false;
    const QString t = line.trimmed();
    if (t.isEmpty()) return false;
    if (t == QLatin1String(">")) return false;   // 块引用空行（段落分隔）
    if (isListItemLine(line)) return false;       // 列表项边界
    if (t.startsWith(QLatin1Char('#')) || t.startsWith(QLatin1Char('|')))
        return false;                             // 标题 / 表格
    if (t.startsWith("```") || t.startsWith("~~~")) return false; // 代码围栏
    static const QRegularExpression hrRe("^(-{3,}|\\*{3,}|_{3,})\\s*$");
    if (hrRe.match(t).hasMatch()) return false;   // 水平线
    return true;
}

// 去掉折行续行的上下文缩进前缀：块引用 "> " 与列表项前导空格。
// 折行发生在词边界，续行首字符必为词首（非空格），故去掉缩进是安全的。
static QString stripWrapIndent(QString line)
{
    if (line.startsWith("> "))
        line.remove(0, 2);
    else if (line.startsWith(QLatin1Char('>')))
        line.remove(0, 1);
    while (line.startsWith(QLatin1Char(' ')))
        line.remove(0, 1);
    return line;
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
    content.replace(QRegularExpression("<br\\b[^>]*>"), "\n");
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
    int codeFenceLen = 0; // 当前代码块开启围栏的反引号数（0 = 不在代码块）
    bool inMathBlock = false;
    QStringList codeBlocks;  // 每个代码块的最终 HTML（<pre><code class="language-X">…</code></pre>）
    QString codeFenceLang;   // 当前代码块的语言（围栏信息串）
    QStringList codeContent; // 当前代码块的内容行
    QStringList subContents; // 下标内容（~x~），后处理还原为 <sub>
    QStringList taskLists; // 任务列表 HTML（TipTap taskList 契约），后处理还原
    QStringList footnoteRefs;   // 脚注引用 <sup> HTML（后处理还原）
    QStringList footnoteDefs;   // 脚注定义 def HTML（按 id 去重，首次定义顺序）
    QStringList footnoteDefIds; // 已收集的定义 id（去重用）
    QStringList escapedEqBackups;  // 转义等号 \= → ZZES 保护
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
        bool isFootnoteDef = codeFenceLen == 0 && footnoteDefMatch.hasMatch();

        if (codeFenceLen == 0 && line.startsWith("$$")) {
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
            // 统计本行前导反引号数量（围栏长度）
            int fenceLen = 0;
            while (fenceLen < line.size() && line[fenceLen] == QLatin1Char('`'))
                ++fenceLen;
            if (codeFenceLen == 0) {
                // 开启新代码块：提取语言（信息串为围栏后的文本），整块用占位符保护，
                // 内容不经 setMarkdown（其围栏解析同样朴素，嵌套围栏会二次错乱）
                codeFenceLang = line.mid(fenceLen).trimmed();
                codeFenceLen = fenceLen;
                codeContent.clear();
                processed += "ZZCB" + QString::number(codeBlocks.size()) + "ZZ\n";
            } else if (fenceLen >= codeFenceLen && line.mid(fenceLen).trimmed().isEmpty()) {
                // 闭合代码块：本行为纯反引号且长度 >= 开启围栏
                QString content = codeContent.join('\n');
                QString codeOpen = codeFenceLang.isEmpty()
                    ? QStringLiteral("<code>")
                    : QStringLiteral("<code class=\"language-") + codeFenceLang + "\">";
                codeBlocks << "<pre>" + codeOpen + content.toHtmlEscaped() + "</code></pre>";
                codeFenceLen = 0;
            } else {
                // 代码块内容（含以 ``` 开头但不足以闭合的嵌套围栏行）
                codeContent << line;
            }
            lastBlock = BT_NONE;
        } else if (codeFenceLen > 0) {
            codeContent << line;
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
        } else if (isImageOnlyLine(line)) {
            // 图片独占行 → 强制独立块级图片（前后空行隔离），使 setMarkdown 产出
            // <p><img/></p>（后处理解包为裸 <img>）。否则 md4c 的 lazy-continuation
            // 会把图片并入相邻段落成内联图片，TipTap（Image 块级节点）解析时提升
            // 图片却留空段落，渲染成图片与正文之间的空行。
            if (!processed.isEmpty() && !processed.endsWith("\n\n"))
                processed += '\n';
            QString cl = line;
            cl = extractEscapedEquals(cl, escapedEqBackups);
            cl = extractSubscripts(cl, subContents);
            cl = extractFootnoteRefs(cl, footnoteRefs);
            cl = extractHtmlInlineElements(cl, htmlInlineBackups);
            processed += cl + '\n';
            // 图片后紧跟正文（无空行）同样补空行，避免正文被 lazy-continuation 并入图片段落
            if (!next.isEmpty() && isParagraphLine(next) && !isImageOnlyLine(next))
                processed += '\n';
            lastBlock = BT_PARA;
        } else if (isParagraphLine(line) && !next.isEmpty()
                   && isParagraphLine(next)
                   && !isBrOnlyLine(next)
                   && !isImageOnlyLine(next)) {
            // 隔离：列表后紧跟正文时补空行，避免被吞入列表项
            if (lastBlock == BT_LIST)
                processed += '\n';
            // 两行正文 → 第一行末尾嵌入标记，setMarkdown 保留该标记
            // 提取顺序：先下标 ~x~，再脚注 [^id]，最后 HTML 内联标签（<span style>/<u>）。
            // 若先提取 HTML 内联标签，标签内部的 MD 语法（~x~、[^id] 等）会被隐藏，
            // 后续 sub/footnote 转换器无法识别，往返数据丢失。先替换为占位符后，
            // 内层占位符随外层 <span>/<u> 一起被 ZZHI 包裹，还原时先开外层再开内层。
            QString cl = line;
            cl = extractEscapedEquals(cl, escapedEqBackups);
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
            cl = extractEscapedEquals(cl, escapedEqBackups);
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
    // 行内代码 `...` 内的 <div> 是字面文本，须先暂存（\x01CODE 保护，与
    // extractFootnoteRefs 一致）——否则被 ZZDV 捕获后还原为原始 HTML 插入
    // <code> 内，浏览器把 <div> 当结构元素解析，字面标签即"被过滤"。
    // 注释见后处理 ZZDV 还原步骤：还原必须先于 ZZHI 等内层占位符。
    QStringList divBackups;
    {
        // 1) 暂存行内代码片段。注意：processed 是多行 buffer，正则必须排除
        // \n（[^`\n]）——若允许跨行配对，双反引号写法（`` `x` ``）会与后续
        // 行的反引号误配对，把多行文本吞成一个"代码段"，替换占位符后多行
        // 被压成一行，后续还原与解析全部错乱。
        QStringList codes;
        QRegularExpression codeSpanRe("`([^`\n]+)`");
        int codeOff = 0;
        while (true) {
            auto m = codeSpanRe.match(processed, codeOff);
            if (!m.hasMatch()) break;
            codes << m.captured(0);
            QString cph = "\x01CODE" + QString::number(codes.size() - 1) + "\x01";
            processed.replace(m.capturedStart(), m.capturedLength(), cph);
            codeOff = m.capturedStart() + cph.length();
        }
        // 2) 保护 <div>。捕获文本含 \x01（行内代码占位符）时跳过——说明该
        // <div> 与 </div> 之间的配对不可靠（占位符还原后才是真实文本），
        // 此时不能备份，否则占位符被吞进 divBackups、步骤 3 还原不到，
        // 最终 \x01 泄漏进 HTML。
        QRegularExpression divRe(
            R"(<div\b(?![^>]*\bdata-type\s*=)[^>]*>(.*?)</div>)");
        int n = 0;
        int offset = 0;
        while (true) {
            auto m = divRe.match(processed, offset);
            if (!m.hasMatch()) break;
            if (m.captured().contains(QChar(1))) {
                offset = m.capturedStart() + 1;
                continue;
            }
            divBackups << m.captured();
            QString ph = QString("ZZDV%1ZZ").arg(n++);
            processed.replace(m.capturedStart(), m.capturedLength(), ph);
            offset = m.capturedStart() + ph.length();
        }
        // 3) 还原行内代码
        for (int i = 0; i < codes.size(); ++i)
            processed.replace("\x01CODE" + QString::number(i) + "\x01", codes[i]);
    }

    QTextDocument doc;
    doc.setMarkdown(processed);
    QString html = doc.toHtml();

    // 后处理 1: 还原代码块占位符 ZZCB → <pre><code class="language-X">…</code></pre>
    //   代码块内容在预处理阶段已整体收集并 HTML 转义，未经过 setMarkdown，
    //   因此可正确保留嵌套围栏（如 ```markdown 内嵌 ```mermaid）。
    for (int i = 0; i < codeBlocks.size(); ++i) {
        QString ph = QString("ZZCB%1ZZ").arg(i);
        html.replace(QRegularExpression("<p[^>]*>" + ph + "</p>"), codeBlocks[i]);
        html.replace(ph, codeBlocks[i]); // fallback：占位符未独立成段时
    }

    // 后处理 2: BRMARKER → <br data-brk>（标记以区分用户手写 <br>）
    html.replace(MARKER + " ", "<br data-brk>");
    html.replace(MARKER, "<br data-brk>");

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
        // ==highlight== → <mark>（code/math 已受占位符保护，== 在
        // 非代码文本中高亮，代码内的 == 不受影响）
        {
            QRegularExpression re("==([^=\n]+?)==");
            html.replace(re, "<mark>\\1</mark>");
        }
        // 还原转义等号 ZZES → =（在 ==→<mark> 之后，避免还原的 = 参与匹配）
        for (int i = 0; i < escapedEqBackups.size(); ++i)
            html.replace("ZZES" + QString::number(i) + "ZZ", escapedEqBackups[i]);
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

    // 修复 <hN><span font-size:xx-large><div align="X">text</div></span></hN>
    // → <hN style="text-align:X">text</hN>
    // QTextDocument 把 <div> 放在标题内，但 TipTap divBlock 是 block 节点不
    // 能嵌套在标题中。此处提取 <div align> → <hN style="text-align:">，
    // 利用已有 TextAlign 扩展渲染居中标题，fromHtml 的 ZZA 机制负责往返还原。
    {
        QRegularExpression re(
            R"re(<(h[1-6])\b[^>]*>\s*<span\b[^>]*\bfont-size\s*:\s*xx-large[^>]*>\s*<div\b[^>]*\balign\s*=\s*"([^"]*)"[^>]*>(.*?)</div>\s*</span>\s*</\1>)re",
            QRegularExpression::DotMatchesEverythingOption);
        int off = 0;
        while (true) {
            auto m = re.match(html, off);
            if (!m.hasMatch()) break;
            QString hTag = m.captured(1);
            QString align = m.captured(2);
            QString content = m.captured(3);
            QString replacement =
                QString("<%1 style=\"text-align:%2\">%3</%1>").arg(hTag, align, content);
            html.replace(m.capturedStart(), m.capturedLength(), replacement);
            off = m.capturedStart() + replacement.length();
        }
    }

    // 兜底：剥离标题内 QTextDocument 注入的 font span（font-size + font-weight）
    // 上面重组 regex 只处理含 <div align> 的标题且限于 h1（xx-large），工具栏
    // 反复操作后 TipTap getHTML() 可能产生非常规格式导致重组不匹配，未剥离的
    // font span（font-weight:700）被 TipTap Bold parseHTML 解析为 <strong> mark，
    // 后续往返持续污染。此处兜底剥离所有 h1-h3 标题内的纯字体 span，阻断污染链。
    {
        // 匹配 <hN...><span style="font-size:(xx-large|x-large|large);...font-weight:700...">content</span></hN>
        static const QRegularExpression fallbackRe(
            R"re(<(h[1-6])(\b[^>]*)>\s*<span\b[^>]*\bfont-size\s*:\s*(?:xx-large|x-large|large)\b[^>]*\bfont-weight\s*:\s*700[^>]*>(.*?)</span>\s*</\1>)re",
            QRegularExpression::DotMatchesEverythingOption);
        int off = 0;
        while (true) {
            auto m = fallbackRe.match(html, off);
            if (!m.hasMatch()) break;
            QString hTag = m.captured(1);
            QString hAttrs = m.captured(2);
            QString content = m.captured(3);
            QString replacement = QString("<%1%2>%3</%1>").arg(hTag, hAttrs, content);
            html.replace(m.capturedStart(), m.capturedLength(), replacement);
            off = m.capturedStart() + replacement.length();
        }
    }

    // 剥离表头内 QTextDocument 注入的 font-weight span。
    // QTextDocument 对表格首行（表头行）不使用 <th>，而是输出
    // <td><p><span style="font-weight:700;">text</span></p></td>。
    // 标题表头已由上方 fallback 处理；此处处理表格首行。
    // 与标题同理：未被剥离的 font-weight:700 会被 TipTap Bold parseHTML
    // 解析为 <strong> mark，后续往返持续污染。
    {
        // 匹配每个 <table> 后首个 <tr>（表头行），剥离其内 font-weight span
        static const QRegularExpression tableFirstRowRe(
            QStringLiteral("(<table\\b[^>]*>\\s*<tr\\b[^>]*>)(.*?)(</tr>)"),
            QRegularExpression::DotMatchesEverythingOption);
        int off = 0;
        while (true) {
            auto m = tableFirstRowRe.match(html, off);
            if (!m.hasMatch()) break;
            QString rowContent = m.captured(2);
            static const QRegularExpression fwSpanRe(
                QStringLiteral("<span\\b[^>]*\\bfont-weight\\s*:\\s*700[^>]*>(.*?)</span>"),
                QRegularExpression::DotMatchesEverythingOption);
            rowContent.replace(fwSpanRe, "\\1");
            QString replacement = m.captured(1) + rowContent + m.captured(3);
            html.replace(m.capturedStart(), m.capturedLength(), replacement);
            off = m.capturedStart() + replacement.length();
        }
    }

    // 将 QTextDocument 注入的纯字体 span 统一转为语义标签。
    // QTextDocument::setMarkdown 对所有内联格式（**粗体** / *斜体*）输出
    // <span style="font-weight:700;"> / <span style="font-style:italic;">
    // 而非 <strong> / <em>。这些 span 除被 Bold/Italic/colorSpan parseHTML
    // 三重捕获外，colorSpan 更会将其 style 原样重输出到 getHTML()，导致
    // 往返污染：**<span style="font-weight:700;">text</span>**。
    // 此处将仅含字体样式（无 color/background-color 等用户样式）的 span
    // 转为语义标签，从源头阻断污染链。
    {
        QRegularExpression spanStyleRe(
            QStringLiteral("<span\\b[^>]*\\bstyle\\s*=\\s*\"([^\"]*)\"[^>]*>(.*?)</span>"),
            QRegularExpression::DotMatchesEverythingOption);
        int off = 0;
        while (true) {
            auto m = spanStyleRe.match(html, off);
            if (!m.hasMatch()) break;
            QString style = m.captured(1);
            QString content = m.captured(2);
            int len = m.capturedLength();

            bool hasBold = style.contains(QRegularExpression("\\bfont-weight\\s*:\\s*700\\b"));
            bool hasItalic = style.contains(QRegularExpression("\\bfont-style\\s*:\\s*italic\\b"));
            bool hasColor = style.contains(QRegularExpression("\\bcolor\\s*:"));
            bool hasBg = style.contains(QRegularExpression("\\bbackground-color\\s*:"));

            if ((hasBold || hasItalic) && !hasColor && !hasBg) {
                QString replacement = content;
                if (hasBold && hasItalic)
                    replacement = QStringLiteral("<strong><em>%1</em></strong>").arg(content);
                else if (hasBold)
                    replacement = QStringLiteral("<strong>%1</strong>").arg(content);
                else
                    replacement = QStringLiteral("<em>%1</em>").arg(content);
                html.replace(m.capturedStart(), len, replacement);
                off = m.capturedStart() + replacement.length();
            } else {
                off = m.capturedStart() + len;
            }
        }
    }

    // 剥离 QTextDocument 在 <a> 标签内注入的链接样式 span。
    // QTextDocument::setMarkdown 将链接文字渲染为蓝色+下划线，产出
    // <a href="..."><span style="color:#0000ff;text-decoration:underline;">text</span></a>。
    // 这些 span 会被 TipTap colorSpan parseHTML 捕获为额外颜色标记，
    // 导致往返污染——链接中未着色的文字也被迫带上蓝色 <span style>。
    // 此时用户手写的 <span> 已由 ZZHI 占位符保护为纯文本，残留的均为
    // Qt 渲染伪影，直接剥离标签保留内容即可。
    {
        static const QRegularExpression linkTagRe(
            "<a\\b([^>]*>)(.*?)</a>",
            QRegularExpression::DotMatchesEverythingOption);
        int off = 0;
        while (true) {
            auto m = linkTagRe.match(html, off);
            if (!m.hasMatch()) break;
            QString inner = m.captured(2);
            inner.remove(QRegularExpression("<span\\b[^>]*>"));
            inner.remove("</span>");
            QString replacement = "<a" + m.captured(1) + inner + "</a>";
            html.replace(m.capturedStart(), m.capturedLength(), replacement);
            off = m.capturedStart() + replacement.length();
        }
    }

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

    // 块级图片解包：<p><img/></p> → 裸 <img>。
    // TipTap 的 Image 节点为块级原子节点（inline:false）。setMarkdown 把独立行图片
    // 渲染为 <p><img/></p>；若原样喂给 TipTap，解析器会把块级图片提升出 <p>、却把
    // 空 <p> 壳留在原位 —— 渲染成图片与相邻块（标题/段落）之间的空行。此处把"仅含
    // 一张图片（可有空白）"的 <p> 解开，使 TipTap 直接得到裸块级 <img>。
    // （与 fromHtml 步骤 E 互为对偶：保存方向 Qt 需 <p> 才认块；加载方向 TipTap 需裸 <img>）
    html.replace(QRegularExpression(
        QStringLiteral("<p[^>]*>\\s*(<img\\b[^>]*>)\\s*</p>")),
        QStringLiteral("\\1"));

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
    // <br data-brk>（编辑器软换行）占位符：与 backups 分开记录，因其还原需按
    // 所在行上下文决定缩进（段落 \n / 有序列表 \n···· / 无序列表 \n··），
    // 无法在保护阶段预先确定。反换行后统一按行上下文还原。
    QStringList brSoftPlaceholders;
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
            // 围栏长度取「内容中最长连续反引号序列 + 1」（至少 3），
            // 保证嵌套围栏（如外层 markdown 内含 ```cpp）往返后仍能正确闭合。
            // 若固定用 3 反引号，内层也是 3 反引号时外层会提前闭合，切回 WYSIWYG 即错乱。
            int maxRun = 0, run = 0;
            for (const QChar &c : code) {
                if (c == QLatin1Char('`')) {
                    ++run;
                    if (run > maxRun) maxRun = run;
                } else {
                    run = 0;
                }
            }
            int fenceLen = maxRun + 1;
            if (fenceLen < 3) fenceLen = 3;
            QString fence(fenceLen, QLatin1Char('`'));
            backups << fence + lang + "\n" + code + "\n" + fence;
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
            inner.replace(QRegularExpression("<br\\b[^>]*>"), "\n");
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
            text.replace(QRegularExpression("<br\\b[^>]*>"), "\n");
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

    // X) 剥离表头 <th> 内的 font-span 与 <strong> 冗余标记。
    //    toHtml 端已剥离 QTextDocument 注入的 <span style="font-weight:700;">，
    //    此处作为 fromHtml 侧的防御层处理两类残余：
    //    a) toHtml 兜底漏过的 font-weight span（含 QTextDocument 注入及
    //       TipTap colorSpan mark 重输出的字面 span）；
    //    b) TipTap Bold mark 解析后生成的 <strong> 标签（二者视觉均冗余——
    //       <th> 自身即为粗体）。
    //    剥离后 th 内仅保留文本/内联占位符，避免字面 span 或多余 **...**
    //    语法泄漏到 .md 表头行。置于 N) 之前：其他占位符（行内代码/上下标/
    //    高亮等）已先行保护；先剥离 th 级冗余标记，N) 再保护用户手写的
    //    <span style>（如颜色/字体等非 font-weight 样式），互不覆盖。
    {
        int off = 0;
        // 模式 a: <th...><span ...font-weight:700...>text</span></th>
        static const QRegularExpression thFontSpanRe(
            QStringLiteral("<(th)(\\b[^>]*)>\\s*<span\\b[^>]*\\bfont-weight\\s*:\\s*700[^>]*>(.*?)</span>\\s*</\\1>"),
            QRegularExpression::DotMatchesEverythingOption);
        while (true) {
            auto m = thFontSpanRe.match(processed, off);
            if (!m.hasMatch()) break;
            QString thAttrs = m.captured(2);
            QString content = m.captured(3);
            QString replacement = QString("<th%1>%2</th>").arg(thAttrs, content);
            processed.replace(m.capturedStart(), m.capturedLength(), replacement);
            off = m.capturedStart() + replacement.length();
        }
        // 模式 b: <th...><strong>text</strong></th>（TipTap Bold mark 产物）
        off = 0;
        static const QRegularExpression thStrongRe(
            QStringLiteral("<(th)(\\b[^>]*)>\\s*<strong\\b[^>]*>(.*?)</strong>\\s*</\\1>"),
            QRegularExpression::DotMatchesEverythingOption);
        while (true) {
            auto m = thStrongRe.match(processed, off);
            if (!m.hasMatch()) break;
            QString thAttrs = m.captured(2);
            QString content = m.captured(3);
            QString replacement = QString("<th%1>%2</th>").arg(thAttrs, content);
            processed.replace(m.capturedStart(), m.capturedLength(), replacement);
            off = m.capturedStart() + replacement.length();
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
        // 对齐：<p style="text-align:..."> / 标题 text-align → <div align="...">
        // TipTap TextAlign 为段落/标题注入 text-align CSS，toMarkdown 丢弃。
        // 提取对齐值并还原为 <div align="...">（已支持往返），内容 ZZ* 占位符
        // 在逆序还原阶段后续解析。
        offset = 0;
        QRegularExpression alignRe(
            R"re(<(p|h[1-6])\b[^>]*\bstyle\s*=\s*"([^"]*\btext-align\s*:\s*(left|center|right)[^"]*)"[^>]*>(.*?)</\1>)re",
            QRegularExpression::DotMatchesEverythingOption);
        while (true) {
            auto m = alignRe.match(processed, offset);
            if (!m.hasMatch()) break;
            QString tag = m.captured(1);
            QString fullStyle = m.captured(2);
            QString align = m.captured(3);
            QString content = m.captured(4);
            // 从 style 中剥离 text-align:XXX（含前后分号/空格）
            QString stripped = fullStyle;
            stripped.remove(QRegularExpression(
                R"(\s*text-align\s*:\s*(left|center|right)\s*;?)"));
            stripped = stripped.trimmed();
            if (stripped.endsWith(';'))
                stripped.chop(1);
            stripped = stripped.trimmed();
            QString ph = QString("ZZA%1ZZ").arg(n++);
            placeholders << ph;
            backups << QString("<div align=\"%1\">%2</div>").arg(align, content);
            QString replacement;
            if (stripped.isEmpty())
                replacement = QString("<%1>%2</%1>").arg(tag, ph);
            else
                replacement = QString("<%1 style=\"%2\">%3</%1>").arg(tag, stripped, ph);
            processed.replace(m.capturedStart(), m.capturedLength(), replacement);
            offset = m.capturedStart() + replacement.length();
        }
        // <br> 硬换行（不含 data-brk 标记——BRMARKER 生成的由 toMarkdown 处理）
        offset = 0;
        QRegularExpression brRe(
            R"re(<br\b(?![^>]*\bdata-brk)[^>]*>)re");
        while (true) {
            auto m = brRe.match(processed, offset);
            if (!m.hasMatch()) break;
            QString ph = QString("ZZBR%1ZZ").arg(n++);
            placeholders << ph;
            backups << m.captured();  // 保留原样 <br> 标签
            processed.replace(m.capturedStart(), m.capturedLength(), ph);
            offset = m.capturedStart() + ph.length();
        }
        // <br data-brk>（编辑器软换行）：同样在 setHtml 前保护为占位符，
        // 否则 toMarkdown 把它转成 \n / \n····，与 80 列折行字节级不可区分，
        // 反换行会误拼回、丢失软换行。占位符存活到反换行之后，再按上下文还原。
        offset = 0;
        QRegularExpression brSoftRe(
            R"re(<br\b[^>]*\bdata-brk[^>]*>)re");
        while (true) {
            auto m = brSoftRe.match(processed, offset);
            if (!m.hasMatch()) break;
            QString ph = QString("ZZBK%1ZZ").arg(n++);
            brSoftPlaceholders << ph;
            processed.replace(m.capturedStart(), m.capturedLength(), ph);
            offset = m.capturedStart() + ph.length();
        }
    }

    // D) 保护字面 **、*、==、&lt;、&gt; 不被 toMarkdown() 重新解释为 Markdown 语法。
    //    用户通过 \*\*、\=\=、\<、\> 转义写入的字面符号，经 toHtml 已转为
    //    纯文本/HTML 实体；QTextDocument::toMarkdown() 不会反转为转义形式，
    //    直接输出裸 **/==，且 &lt;→\< 但 &gt;→>（不加反斜杠），不对称。
    //    此处用简易状态机识别 HTML 标签外的字面符号/实体并替换为占位符，
    //    还原时输出为转义形式，保证 \<、\> 往返保真。
    //    孤立字面 *（非 **）同样需保护：Qt 6.4.2 的 toMarkdown 不会为标题/正文
    //    内联文本中的 * 添加转义，重载时 *A* 会被误解析为强调，破坏往返保真。
    //    注意：强调语法 * 来自 <em> 标签、在 setHtml 之后由 Qt 生成，本步在
    //    setHtml 之前运行，故只命中真正的字面 *，不影响 <em>→* 的正常转换。
    {
        QString result;
        result.reserve(processed.size());
        bool inTag = false;
        int nDs = 0; // ** 计数
        int nSt = 0; // 孤立 * 计数
        int nEq = 0; // == 计数
        int nLt = 0; // &lt; 计数
        int nGt = 0; // &gt; 计数
        for (int i = 0; i < processed.size(); ++i) {
            QChar c = processed[i];
            if (c == '<')
                inTag = true;
            else if (c == '>')
                inTag = false;

            if (!inTag && c == '*' && i + 1 < processed.size()
                && processed[i + 1] == '*') {
                QString ph = QString("ZZDS%1ZZ").arg(nDs++);
                placeholders << ph;
                backups << QStringLiteral("\\*\\*");
                result += ph;
                ++i;
            } else if (!inTag && c == '*') {
                // 孤立字面 *（已被上面的 ** 分支排除）：还原为 \*，
                // 使转义与 Qt 版本无关（6.4.2 不转义、6.8.2 转义，统一为转义）。
                QString ph = QString("ZZST%1ZZ").arg(nSt++);
                placeholders << ph;
                backups << QStringLiteral("\\*");
                result += ph;
            } else if (!inTag && c == '=' && i + 1 < processed.size()
                       && processed[i + 1] == '=') {
                QString ph = QString("ZZEQ%1ZZ").arg(nEq++);
                placeholders << ph;
                backups << QStringLiteral("\\=\\=");
                result += ph;
                ++i;
            } else if (!inTag && c == '&' && i + 3 < processed.size()
                       && processed[i + 1] == 'l' && processed[i + 2] == 't'
                       && processed[i + 3] == ';') {
                QString ph = QString("ZZLT%1ZZ").arg(nLt++);
                placeholders << ph;
                backups << QStringLiteral("\\<");
                result += ph;
                i += 3;
            } else if (!inTag && c == '&' && i + 3 < processed.size()
                       && processed[i + 1] == 'g' && processed[i + 2] == 't'
                       && processed[i + 3] == ';') {
                QString ph = QString("ZZGT%1ZZ").arg(nGt++);
                placeholders << ph;
                backups << QStringLiteral("\\>");
                result += ph;
                i += 3;
            } else {
                result += c;
            }
        }
        if (nDs > 0 || nSt > 0 || nEq > 0 || nLt > 0 || nGt > 0) processed = result;
    }

    // E) 裸顶层 <img> 强制为独立块。
    //    TipTap 的 Image 节点为块级原子节点（inline:false），getHTML() 将其序列化
    //    为不包 <p> 的裸 <img> 顶层元素。QTextDocument::setHtml 会把这种游离的内联
    //    <img> 并入前一个块（标题/段落），导致 toMarkdown 输出 "# 标题![](...)"、
    //    块边界丢失。此处用轻量深度扫描，把"文档直接子节点"（深度 0）的 <img> 包进
    //    <p>，使其成为独立块；已在 <p>/<a>/<hN> 等容器内的内联图片不受影响。
    {
        static const QRegularExpression tagRe(QStringLiteral(
            "<(/?)([a-zA-Z][a-zA-Z0-9]*)((?:[^>\"']|\"[^\"]*\"|'[^']*')*)>"));
        // HTML void 元素：无结束标签，不改变深度
        static const QSet<QString> voidTags = {
            "area", "base", "br", "col", "embed", "hr", "img", "input",
            "keygen", "link", "meta", "param", "source", "track", "wbr"
        };
        QString result;
        result.reserve(processed.size());
        int depth = 0;          // 当前所在块/内联容器的嵌套层数
        int pos = 0;
        bool changed = false;
        for (auto m = tagRe.match(processed, pos); m.hasMatch();
             m = tagRe.match(processed, pos)) {
            result += processed.mid(pos, m.capturedStart() - pos);
            const QString full = m.captured(0);
            const bool isClosing = !m.captured(1).isEmpty();
            const QString name = m.captured(2).toLower();
            const QString attrs = m.captured(3);
            const bool selfClosed = attrs.trimmed().endsWith('/');

            if (isClosing) {
                if (depth > 0) --depth;
                result += full;
            } else if (name == "img" && depth == 0) {
                // 裸顶层图片 → 包进 <p>，强制独立块（img 为 void，深度不变）
                result += "<p>" + full + "</p>";
                changed = true;
            } else if (voidTags.contains(name) || selfClosed) {
                result += full;   // void / 自闭合标签，深度不变
            } else {
                ++depth;
                result += full;
            }
            pos = m.capturedStart() + m.capturedLength();
        }
        result += processed.mid(pos);   // 末尾剩余文本
        if (changed) processed = result;
    }

    // F) 保护 <p> 行首的字面列表标记（数字./数字)/-/+），避免 toMarkdown 在
    //    Qt 6.4.2 上不转义，导致重载时 "1. xxx" 被误解析为有序/无序列表
    //    （违反往返保真：用户写入的段落须保持段落）。真正的列表为 <ol>/<ul>/<li>，
    //    由 Qt 正常转换为列表语法，此处只处理 <p> 文本开头的字面标记。
    //    标记用占位符保护后穿过 setHtml/toMarkdown（占位符为字母数字，无列表形态），
    //    逆序还原阶段还原为转义形式（1\. / 1\) / \- / \+）。
    {
        QRegularExpression pListMarkerRe(
            R"((<p\b[^>]*>)\s*(\d{1,9}[.)]|[+\-])(?=\s))");
        int n = 0;
        int offset = 0;
        while (true) {
            auto m = pListMarkerRe.match(processed, offset);
            if (!m.hasMatch()) break;
            QString marker = m.captured(2);
            // 转义标记中的标点字符：1.→1\. / 1)→1\) / -→\- / +→\+
            QString escaped;
            if (marker.size() == 2)
                escaped = marker.left(1) + QStringLiteral("\\") + marker.right(1);
            else
                escaped = QStringLiteral("\\") + marker;
            QString ph = QString("ZZLM%1ZZ").arg(n++);
            placeholders << ph;
            backups << escaped;
            processed.replace(m.capturedStart(2), m.capturedLength(2), ph);
            offset = m.capturedStart(2) + ph.length();
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

    // 反换行：QTextDocument::toMarkdown() 硬编码 80 列换行（ColumnLimit = 80，
    // 无公开 API 可关闭），把含空格的长段落/列表项/引用块在词边界折成多行
    // （段落 \n、有序列表 \n····、无序列表 \n··、引用块 \n> ）。这些折行会把
    // 列表项拆成多块、把段落拆成多行（再次加载时被 BRMARKER 提升为 <br>），
    // 破坏往返保真。合法软换行（<br>/<br data-brk>）已在前用 ZZ* 占位符保护，
    // 故此处的单 \n 只能是块边界或折行；逐行把折行拼回单行，仅保留块边界。
    {
        QStringList lines = md.split('\n');
        QStringList out;
        for (int i = 0; i < lines.size(); ++i) {
            QString cur = lines[i];
            // 内容行后若紧跟折行续行，则一直拼回（续行缩进用单个空格还原）。
            // 块引用空行（">"）是段落分隔，不参与拼接。
            while (!cur.isEmpty()
                   && cur.trimmed() != QLatin1String(">")
                   && i + 1 < lines.size()
                   && isWrapContinuation(lines[i + 1])) {
                cur += QLatin1Char(' ');
                cur += stripWrapIndent(lines[++i]);
            }
            out << cur;
        }
        md = out.join(QLatin1Char('\n'));
    }

    // 3) 修复被合并进同一段落的块级占位符（必须在还原前）：
    //    setHtml 中相邻块级元素的占位符之间 \n 是软换行，QTextDocument 将其
    //    放入同一段落，toMarkdown 输出为同一行以空格分隔（"ZZCB0ZZ ZZBM0ZZ"）。
    //    多行备份（代码块围栏、块级公式）还原后会与相邻内容粘连
    //    （如闭合围栏与 $$ 粘成 "```$$"，围栏看似丢失）。
    //    处理：备份含换行的占位符必须独占一行——前后若有同行内容，插入空行分隔。
    for (int i = 0; i < placeholders.size(); ++i) {
        if (!backups[i].contains('\n')) continue;
        const QString &ph = placeholders[i];
        // 前方有同行内容（前一字符非换行）→ 前插空行
        md.replace(QRegularExpression("([^\\n])" + ph), "\\1\n\n" + ph);
        // 后方有同行内容（后一字符非换行）→ 后插空行
        md.replace(QRegularExpression(ph + "([^\\n])"), ph + "\n\n\\1");
    }

    // 4) 还原占位符（逆序：后入先还原，防止嵌套占位符丢失）
    for (int i = placeholders.size() - 1; i >= 0; --i)
        md.replace(placeholders[i], backups[i]);

    // 4b) 还原 <br data-brk>（编辑器软换行）：按所在行上下文决定缩进，
    //     段落/引用 → \n；有序列表项 → \n····（4 空格，对齐两位数序号）；
    //     无序列表项 → \n··（2 空格）。缩进 = 行首前导空格 + 列表项续行缩进，
    //     保证嵌套列表项续行仍留在原项内。
    //     必须放在反换行之后：此刻列表项已拼回单行，行首列表标记可直接判定。
    for (const QString &ph : brSoftPlaceholders) {
        int idx = 0;
        while ((idx = md.indexOf(ph, idx)) >= 0) {
            const int lineStart = md.lastIndexOf(QLatin1Char('\n'), idx - 1) + 1;
            int lineEnd = md.indexOf(QLatin1Char('\n'), idx);
            if (lineEnd < 0) lineEnd = md.size();
            const QString line = md.mid(lineStart, lineEnd - lineStart);
            const QString t = line.trimmed();

            int leadingWs = 0;
            while (leadingWs < line.size()
                   && line[leadingWs] == QLatin1Char(' '))
                ++leadingWs;

            QString repl = QStringLiteral("\n");
            static const QRegularExpression orderedRe(R"(^\d{1,9}[.)]\s)");
            static const QRegularExpression unorderedRe(R"(^[-+*]\s)");
            static const QRegularExpression hrRe(R"(^[-*_]{3,}\s*$)");
            if (orderedRe.match(t).hasMatch()) {
                repl += QString(leadingWs + 4, QLatin1Char(' '));
            } else if (unorderedRe.match(t).hasMatch()
                       && !hrRe.match(t).hasMatch()) {
                repl += QString(leadingWs + 2, QLatin1Char(' '));
            }
            md.replace(idx, ph.size(), repl);
            idx += repl.size();
        }
    }

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

    // 5) 防御：剥离 <div> 内残留的 heading font-span + <strong> 污染
    //    toHtml 重组可能因 Toolbar 反复操作后 TipTap getHTML() 的非常规格式
    //    而漏过；未剥离的 font-weight:700 被 TipTap Bold parseHTML 解析为
    //    <strong> mark，后续往返持续污染。此处检测并剥离两种模式：
    //    a) 已有标题前缀：剥离 span/strong，并按 font-size 重新定级前缀
    //    b) 无标题前缀：剥离后按 font-size 恢复标题前缀（xx-large→#, x-large→##, large→###）
    {
        // 匹配 <span style="...font-size:(xx-large|x-large|large)...font-weight:700...">text</span>
        // 使用前瞻断言确保同时存在 font-size 和 font-weight:700，不限属性顺序
        auto makeSpanRe = []() {
            return QRegularExpression(
                R"re(<span\s+style\s*=\s*"(?=.*\bfont-size\s*:\s*(xx-large|x-large|large)\b)(?=.*\bfont-weight\s*:\s*700\b)[^"]*"[^>]*>(.*?)</span>)re");
        };
        auto headingForSize = [](const QString &fs) -> QString {
            if (fs == "xx-large") return "# ";
            if (fs == "x-large") return "## ";
            return "### ";
        };

        // 模式 a：已有标题前缀 — 剥离 span/strong，并按 font-size 重新定级前缀。
        //    降解输入的前缀 # 可能与 font-size 隐含级别不符（如 "# <div>..x-large..>"
        //    实为 h2）；font-size 是渲染时真正使用的级别，以其为准重新定级，
        //    使结果与 Qt 版本无关（6.4.2 不转义前导 #，6.8.2 会转义为 \#）。
        //    捕获组：1=旧前缀(丢弃) 2=div 属性 3=font-size 4=内容
        {
            auto processPrefixed = [&](const QRegularExpression &re) {
                int pos = 0;
                while (true) {
                    auto m = re.match(md, pos);
                    if (!m.hasMatch()) break;
                    QString divAttrs = m.captured(2).trimmed();
                    QString fontSize = m.captured(3);
                    QString content = m.captured(4);
                    QString prefix = headingForSize(fontSize);
                    QString replacement;
                    if (divAttrs.isEmpty())
                        replacement = prefix + content;
                    else
                        replacement = prefix + "<div " + divAttrs + ">" + content + "</div>";
                    md.replace(m.capturedStart(), m.capturedLength(), replacement);
                    pos = m.capturedStart() + replacement.length();
                }
            };
            static const QRegularExpression prefixedStrongSpan(
                R"re(^(#{1,6}\s+)<div\b([^>]*)>\s*<strong>\s*<span\s+style\s*=\s*"(?=.*\bfont-size\s*:\s*(xx-large|x-large|large)\b)(?=.*\bfont-weight\s*:\s*700\b)[^"]*"[^>]*>(.*?)</span>\s*</strong>\s*</div>)re",
                QRegularExpression::MultilineOption);
            static const QRegularExpression prefixedSpan(
                R"re(^(#{1,6}\s+)<div\b([^>]*)>\s*<span\s+style\s*=\s*"(?=.*\bfont-size\s*:\s*(xx-large|x-large|large)\b)(?=.*\bfont-weight\s*:\s*700\b)[^"]*"[^>]*>(.*?)</span>\s*</div>)re",
                QRegularExpression::MultilineOption);
            processPrefixed(prefixedStrongSpan);
            processPrefixed(prefixedSpan);
        }

        // 模式 b：无标题前缀 — 剥离后恢复 # 前缀
        {
            auto process = [&](const QRegularExpression &re) {
                int pos = 0;
                while (true) {
                    auto m = re.match(md, pos);
                    if (!m.hasMatch()) break;
                    QString divAttrs = m.captured(1).trimmed();
                    QString fontSize = m.captured(2);
                    QString content = m.captured(3);
                    QString prefix = headingForSize(fontSize);
                    QString replacement;
                    if (divAttrs.isEmpty())
                        replacement = prefix + content;
                    else
                        replacement = prefix + "<div " + divAttrs + ">" + content + "</div>";
                    md.replace(m.capturedStart(), m.capturedLength(), replacement);
                    pos = m.capturedStart() + replacement.length();
                }
            };
            static const QRegularExpression bareStrongSpan(
                R"re(<div\b([^>]*)>\s*<strong>\s*<span\s+style\s*=\s*"(?=.*\bfont-size\s*:\s*(xx-large|x-large|large)\b)(?=.*\bfont-weight\s*:\s*700\b)[^"]*"[^>]*>(.*?)</span>\s*</strong>\s*</div>)re");
            static const QRegularExpression bareSpan(
                R"re(<div\b([^>]*)>\s*<span\s+style\s*=\s*"(?=.*\bfont-size\s*:\s*(xx-large|x-large|large)\b)(?=.*\bfont-weight\s*:\s*700\b)[^"]*"[^>]*>(.*?)</span>\s*</div>)re");
            process(bareStrongSpan);
            process(bareSpan);
        }
    }

    // 6) 规范化表格格式：QTextDocument::toMarkdown（md4c）对表格输出
    //    |cell|（无空格）且数据行尾部用空格做列对齐，非标准 GFM 风格。
    //    统一为 | cell |（单元格内容前后各一个空格），分隔行统一为 | --- |。
    {
        QStringList lines = md.split('\n');
        for (int i = 0; i < lines.size(); ++i) {
            QString line = lines[i];
            QString t = line.trimmed();
            // 仅处理表格行（以 | 开头且以 | 结尾，非 blockquote 前缀 > |）
            if (!t.startsWith('|') || !t.endsWith('|')) continue;

            // 去掉首尾 | → 拆分为单元格
            QString inner = t.mid(1, t.length() - 2);
            QStringList cells;
            // 简单拆分（不处理行内代码含 | 的极端情况——极少见且拆分会破坏）
            if (inner.contains('`'))
                continue;
            cells = inner.split('|');
            bool isSep = false;
            for (auto &cell : cells) {
                cell = cell.trimmed();
                if (QRegularExpression("^:?-{3,}:?$").match(cell).hasMatch())
                    isSep = true;
            }
            if (isSep) {
                // 分隔行：每格统一为 ---
                QStringList sepCells;
                for (int k = 0; k < cells.size(); ++k)
                    sepCells << "---";
                lines[i] = "| " + sepCells.join(" | ") + " |";
            } else {
                // 数据/表头行：每格 trim + 前后各一个空格
                QStringList padded;
                for (const auto &cell : cells)
                    padded << (" " + cell + " ");
                lines[i] = "|" + padded.join('|') + "|";
            }
        }
        md = lines.join('\n');
    }

    // 7) 标题与图片之间的空行：保留（往返保真）。
    //    toMarkdown 在块间默认输出空行，标题→图片亦然。曾在此折叠为单换行，但那会
    //    删掉用户外部编辑插入的空行（违反往返保真核心原则），且该空行不影响 TipTap
    //    渲染（toHtml 已把 <p><img/></p> 解包为裸 <img>，渲染为紧邻块）。故不再折叠，
    //    与"段落→图片需空行"保持一致。
    //    （注：段落→图片的空行是结构必需，否则图片被并入段落成内联图片。）

    while (md.endsWith('\n'))
        md.chop(1);
    return md;
}
