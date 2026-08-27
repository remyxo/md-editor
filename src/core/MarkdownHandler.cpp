#include "MarkdownHandler.h"
#include <QTextDocument>
#include <QRegularExpression>
#include <QSet>
#include <QVector>
#include <QFontDatabase>

// 行是否仅为 <br> 等硬换行标签（含属性及自闭合写法）
// 用于 BRMARKER 逻辑：<br> 本身即是换行，其前后 \n 不应再触发 BRMARKER
static bool isBrOnlyLine(const QString &line)
{
    static const QRegularExpression brRe(
        R"(^\s*<br\b[^>]*>\s*$)");
    return brRe.match(line).hasMatch();
}

// 等宽字体 span 的字体名匹配模式（单引号内 OR 分支，不含引号与两侧语法）。
// QTextMarkdownImporter 给行内代码设置的字体是
// QFontDatabase::systemFont(QFontDatabase::FixedFont)，其族名平台相关：
// Linux="monospace"、Windows="Courier New"、macOS="Menlo"，QTextDocument::toHtml
// 据此导出 <span style=" font-family:'...';">。硬编码只认 'monospace' 会导致
// Windows/macOS 上加载即丢 code 语义。三大平台已知名全部纳入 + 动态取本平台
// FixedFont 族名兜底（覆盖自定义系统字体配置），保持版本/平台无关。
static QString monoSpanFamilyPattern()
{
    QStringList names;
    const QStringList fixed =
        QFontDatabase::systemFont(QFontDatabase::FixedFont).families();
    for (const QString &f : fixed)
        if (!names.contains(f))
            names << f;
    names << QStringLiteral("monospace")
          << QStringLiteral("Courier New")
          << QStringLiteral("Menlo");
    names.removeDuplicates();
    QStringList escaped;
    for (const QString &n : names)
        escaped << QRegularExpression::escape(n);
    return escaped.join(QLatin1Char('|'));
}

// 反引号感知的表格单元格拆分：行内代码内的 | 不是分隔符（GFM code span 由
// 等长反引号 run 定界）；转义竖线 \| 同样不是。返回按分隔符拆出的原始单元格
// （未 trim，由调用方处理）。无配对闭合 run 的反引号按普通文本处理。
static QStringList splitTableCells(const QString &inner)
{
    QStringList cells;
    QString cur;
    int i = 0;
    while (i < inner.size()) {
        const QChar c = inner.at(i);
        if (c == QLatin1Char('\\') && i + 1 < inner.size()
                && inner.at(i + 1) == QLatin1Char('|')) {
            cur += QStringLiteral("\\|");   // 转义竖线：字面 |，不拆分
            i += 2;
        } else if (c == QLatin1Char('|')) {
            cells << cur;
            cur.clear();
            ++i;
        } else if (c == QLatin1Char('`')) {
            // 反引号 run：寻找等长配对 run，其间内容属于 code span（| 不拆分）
            const int runStart = i;
            while (i < inner.size() && inner.at(i) == QLatin1Char('`'))
                ++i;
            const int runLen = i - runStart;
            int j = i;
            int closeStart = -1;
            while (j < inner.size()) {
                if (inner.at(j) == QLatin1Char('`')) {
                    const int rs = j;
                    while (j < inner.size() && inner.at(j) == QLatin1Char('`'))
                        ++j;
                    if (j - rs == runLen) {
                        closeStart = rs;
                        break;
                    }
                    // 长度不等的 run 属于内容，继续找配对
                } else {
                    ++j;
                }
            }
            if (closeStart >= 0) {
                cur += inner.mid(runStart, closeStart + runLen - runStart);
                i = closeStart + runLen;
            } else {
                // 未闭合：按普通文本继续（后续 | 仍作分隔符）
                cur += inner.mid(runStart, runLen);
            }
        } else {
            cur += c;
            ++i;
        }
    }
    cells << cur;
    return cells;
}

// 块引用内代码围栏行（单层 > 前缀 + 代码围栏）：> ``` / > ~~~，可带语言信息串。
// 返回围栏长度（>=3）；非块引用围栏行返回 0。
// 用途：块引用内的代码围栏行及其内容不是正文，BRMARKER 逻辑不得在围栏内插入
// 软换行——否则 Qt setMarkdown 的块引用+围栏组合解析会错乱，代码块被拆碎。
static int blockquoteCodeFenceLen(const QString &line)
{
    int i = 0;
    while (i < line.size() && line[i] == ' ') ++i;   // 前导空白
    if (i >= line.size() || line[i] != '>') return 0;
    ++i;
    if (i < line.size() && line[i] == ' ') ++i;       // > 后的可选空格
    if (i >= line.size()) return 0;
    const QChar f = line[i];
    if (f != '`' && f != '~') return 0;
    int len = 0;
    while (i < line.size() && line[i] == f) { ++len; ++i; }
    return len >= 3 ? len : 0;
}

// 剥离块引用前缀（前导空白 + > + 可选空格），返回行剩余内容。
// 非块引用行原样返回。用于收集块引用内代码块内容（移除 > 前缀）。
static QString stripBlockquotePrefix(const QString &line)
{
    int i = 0;
    while (i < line.size() && line[i] == ' ') ++i;
    if (i < line.size() && line[i] == '>') {
        ++i;
        if (i < line.size() && line[i] == ' ') ++i;
        return line.mid(i);
    }
    return line;
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
    // setext 一级标题下划线（整行仅由 3 个及以上等号组成）：非正文行，
    // 避免 BRMARKER 在「标题」与「===」之间插入多余 <br>。
    // ==text== 高亮含非等号内容，不满足整行等号，不受影响。
    if (trimmed.length() >= 3
        && trimmed.count(QLatin1Char('=')) == trimmed.length())
        return false;
    // <br> 独占行仍算正文行——其后的 \n 仍需 BRMARKER 表达换行；
    // 但其前的 \n 由调用侧通过 isBrOnlyLine 抑制 BRMARKER
    if (c == '>') {
        // 块引用代码围栏行（> ```）是块边界，非正文行，避免在其前插入软换行
        if (blockquoteCodeFenceLen(line) > 0)
            return false;
        // 块引用空行（仅 ">" 或 "> "）：段落分隔符，非正文行。避免 BRMARKER 把它
        // 当作软换行插入，否则「> 第一段\n>\n> 第二段」两段被合并成一段。
        if (trimmed == QLatin1String(">"))
            return false;
        return true;
    }
    return c != '#' && c != '-' && c != '|'
           && !(c >= '0' && c <= '9' && trimmed.length() > 1
                && trimmed[1] == '.');
}

// 块引用行（> 前缀）。用于 BRMARKER 软换行判定：CommonMark 规定块引用可打断
// 段落（块边界），但普通段落不能打断块引用（后者为 lazy continuation，仍属
// 同一段落）。故仅「非块引用行紧跟块引用行」是块边界，不应插入软换行。
static bool isBlockquoteLine(const QString &line)
{
    return line.trimmed().startsWith(QLatin1Char('>'));
}

// 图片独占行（![alt](src)，可带 title 与首尾空白）：视为块级图片行。
// 用于隔离：避免 md4c 的 lazy-continuation 把图片并入相邻段落成内联图片。
static bool isImageOnlyLine(const QString &line)
{
    static const QRegularExpression re(
        R"(^\s*!\[[^\]]*\]\([^)\n]*\)\s*$)");
    return re.match(line).hasMatch();
}

// 水平线行（仅 --- 连字符，3 个及以上，可带首尾空白）。仅保护连字符水平线：
// Qt setMarkdown 对连续 --- 会折叠成一个 <hr>，且 ---\n\n正文\n\n--- 会被灾难
// 性误解析（--- 兼具 setext 二级标题下划线歧义），导致正文与水平线一并丢失。
// *** / ___ 无此歧义，Qt 处理正常，无需保护。
static bool isDashHrLine(const QString &line)
{
    static const QRegularExpression re(R"(^\s*-{3,}\s*$)");
    return re.match(line).hasMatch();
}

// 表格行（trim 后以 | 开头且以 | 结尾）：表头/分隔行/数据行的统一识别，用于表格
// 区域跟踪。与 fromHtml 步骤 6 的 |...| 启发式一致。
static bool isTableLine(const QString &line)
{
    QString t = line.trimmed();
    return t.size() >= 2 && t.startsWith(QLatin1Char('|'))
           && t.endsWith(QLatin1Char('|'));
}

// 表格行规范化（防 Qt 空单元格合并 + 空单元格占位符保护，替换旧 normalizeTableRowForQt）。
// 背景：QTextDocument::setMarkdown 会把 body 行「尾随空单元格」折叠成 colspan
// （| x | | | | → <td colspan="4">），使 WYSIWYG 把多格行误渲染成合并单元格。
// 旧实现仅用空格填充空单元格，但空格仍被 Qt 视为空（尾随空格行依旧合并），
// 故须用非空白占位符（ZZTC*，toHtml 后处理还原为空 <td>）。
// 规则（与 fromHtml 步骤 6b 对偶）：
//   - 分隔行：记录列数（colCount），原样直通。
//   - 恰 1 个显式单元格的行（| x |）：原样直通 → Qt colspan 合并（语义「占满整行」）。
//   - ≥2 个显式单元格的行：空单元格填 ZZTC 占位符（Qt 视为非空保留为独立单元格），
//     行短于分隔行列数时补占位符到满列（防末格 colspan 补列）。
//     另防「无空格空单元格」（|1||3|）被 Qt 重定位到行尾的数据损坏。
static QString protectTableRowForQt(const QString &line, int &colCount, QStringList &cellBackups)
{
    if (!isTableLine(line))
        return line;
    QString t = line.trimmed();
    QString inner = t.mid(1, t.length() - 2);
    QStringList cells = splitTableCells(inner);

    // 分隔行：记录列数，原样直通
    static const QRegularExpression sepCellRe(QStringLiteral("^:?-+:?$"));
    bool isSep = true;
    for (const QString &c : cells)
        if (!sepCellRe.match(c.trimmed()).hasMatch()) { isSep = false; break; }
    if (isSep) {
        colCount = cells.size();
        return line;
    }

    // 恰 1 个显式单元格：原样直通，Qt colspan 合并（占满整行）
    if (cells.size() == 1)
        return line;

    // ≥2 个显式单元格：无空单元格且不短行则零改动直通
    bool needProtect = false;
    for (const QString &c : cells)
        if (c.trimmed().isEmpty()) { needProtect = true; break; }
    if (!needProtect && cells.size() >= colCount)
        return line;

    QStringList padded;
    for (const QString &c : cells) {
        QString cc = c.trimmed();
        if (cc.isEmpty()) {
            QString ph = QStringLiteral("ZZTC") + QString::number(cellBackups.size()) + QStringLiteral("ZZ");
            cellBackups << ph;
            padded << (" " + ph + " ");
        } else {
            padded << (" " + cc + " ");
        }
    }
    while (padded.size() < colCount) {
        QString ph = QStringLiteral("ZZTC") + QString::number(cellBackups.size()) + QStringLiteral("ZZ");
        cellBackups << ph;
        padded << (" " + ph + " ");
    }
    return "|" + padded.join('|') + "|";
}

// 表格中断块：GFM 规定表格在「其他块级结构」处结束。Qt setMarkdown 会错误地把
// 标题（# 或 setext ===）、代码围栏（```/~~~）、水平线（---）、块级公式（$$）吞入
// 表格，故需在这些块前补空行打断；纯文本行按 GFM 继续作为数据行（惰性延续），不在其列。
static bool isTableBreakingBlock(const QString &line)
{
    QString t = line.trimmed();
    if (t.isEmpty()) return false;
    if (t.startsWith(QLatin1Char('#'))) return true;               // ATX 标题
    if (t.startsWith("```") || t.startsWith("~~~")) return true;    // 代码围栏
    if (t.startsWith("$$")) return true;                            // 块级公式
    if (isDashHrLine(line)) return true;                            // --- 水平线
    if (t.length() >= 3 && t.count(QLatin1Char('=')) == t.length()) // setext ===
        return true;
    return false;
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

// 为占位符取不与源文本冲突的串：占位符形如 ZZ<名><n>ZZ，若源文档（markdown）或已
// 产出内容（processed）已含该串（如用户文档字面 "ZZCB0ZZ"），跳过该序号，否则后处理
// html.replace(ph, backup) 会把用户字面文本一并误替换（往返数据损坏）。n 从 base 起
// 递增直至无冲突；返回的占位符串由调用方存储，还原时按存储值回填（序号可非连续）。
static QString collisionFreePlaceholder(const QString &name, int base,
                                        const QString &markdown, const QString &processed)
{
    int n = base;
    QString ph;
    do {
        ph = QString("ZZ%1%2ZZ").arg(name).arg(n++);
    } while (markdown.contains(ph) || processed.contains(ph));
    return ph;
}

// 转义「未闭合的 HTML 标签」为字面实体（<tag> → &lt;tag&gt;）。
// Qt setMarkdown 遇到段落内未闭合的容器标签（<b>、<script>、<hr>、<span> 等，
// 全文无同名 </tag> 配对）时，会把其后全部内容当 HTML 块吞噬到文末，造成数据
// 丢失（往返保真违规）。可正常往返的标签保持原样：
//   - 配对闭合的：<b>x</b>、<div>x</div>、<a href>…</a>（Qt 原生转为 <strong> 等）
//   - void <br>（含 <br/>、<br />）
//   - 自闭合的：<img src="x"/>
// 已转义的形态跳过：`\<tag>`（Qt toMarkdown 对字面 <tag> 的规范转义，须保持稳定
// 不二次处理）与 `&lt;tag&gt;`（实体文本）。负向断言 (?<![\&\\]) 排除 `<` 紧跟
// `&`/`\` 的匹配。块级 <div> 与受支持内联标签（<span style>/<u>/<kbd>/<small>）
// 在此前已占位保护，不经过本函数；本函数须在占位保护之后执行。
static void escapeUnclosedHtmlTags(QString &s)
{
    static const QRegularExpression tagRe("(?<![\\&\\\\])<[a-zA-Z/][^<>]*>");
    int off = 0;
    while (true) {
        auto m = tagRe.match(s, off);
        if (!m.hasMatch()) break;
        const QString tok = m.captured();
        const bool isClosing = tok.startsWith("</");
        // 提取标签名（<b> → b，<a href> → a，</div> → div）
        int nameEnd = tok.indexOf(QRegularExpression("[\\s>]"), 1);
        if (nameEnd < 0) nameEnd = tok.length() - 1;
        const QString name = isClosing ? tok.mid(2, nameEnd - 2) : tok.mid(1, nameEnd - 1);
        const QString nameL = name.toLower();
        const bool isBr = nameL == "br";
        const bool isSelfClosing = tok.contains("/>");
        const bool hasClose = !isClosing && QRegularExpression(
            "</" + QRegularExpression::escape(name) + "\\b")
                .match(s, m.capturedEnd()).hasMatch();
        if (isClosing || isBr || isSelfClosing || hasClose) {
            off = m.capturedEnd();
            continue;
        }
        // 未闭合容器标签 → 转义 < > 为实体，Qt 不再当标签解析
        const QString inner = tok.mid(1, tok.length() - 2);
        s.replace(m.capturedStart(), m.capturedLength(), "&lt;" + inner + "&gt;");
        off = m.capturedStart() + inner.length() + 8;   // &lt;(4) + inner + &gt;(4)
    }
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

// 统一列表行（任务项或通用列表项）解析出的条目
struct MdListItem {
    int depth;      // 缩进层级（每层 2 空格）
    bool isTask;    // 任务项（- [x]）或通用列表项
    bool checked;   // 任务项：是否勾选
    QString text;   // 行内内容（原始文本，待子转换）
    int listType;   // 通用列表：0=ul, 1=ol
    int number;     // 通用有序列表：起始编号
};

// 解析一行 MD 列表行；非列表行返回 false。任务项优先于通用无序（- [x] 形）
static bool parseMdListLine(const QString &line, MdListItem &out)
{
    static const QRegularExpression taskRe(
        "^(\\s*)[-*+]\\s+\\[([ xX])\\](?:\\s+(.*))?$");
    static const QRegularExpression olRe(
        "^(\\s*)(\\d{1,9})[.)]\\s+(\\S.*)$");
    static const QRegularExpression ulRe(
        "^(\\s*)[-*+]\\s+(\\S.*)$");
    auto tm = taskRe.match(line);
    if (tm.hasMatch()) {
        out.depth = tm.captured(1).length() / 2;
        out.isTask = true;
        out.checked = tm.captured(2).toLower() == "x";
        out.text = tm.captured(3).trimmed();
        return true;
    }
    auto om = olRe.match(line);
    if (om.hasMatch()) {
        out.depth = om.captured(1).length() / 2;
        out.isTask = false;
        out.listType = 1;
        out.number = om.captured(2).toInt();
        out.text = om.captured(3).trimmed();
        return true;
    }
    auto um = ulRe.match(line);
    if (um.hasMatch()) {
        out.depth = um.captured(1).length() / 2;
        out.isTask = false;
        out.listType = 0;
        out.text = um.captured(2).trimmed();
        return true;
    }
    return false;
}

// 递归构建一层列表容器（任务或通用）HTML。idx 指向本层首个条目；连续同类
// （同为任务 / 同为同型通用列表）条目组成同一容器；更深的条目递归嵌入上一
// 容器末尾条目的内容中，符合 TipTap 列表项 content 'paragraph block*'
// （nested:true）契约 —— 任务条目 div 可容纳通用列表，通用 li 也可容纳任务列表。
static QString buildMdListContainer(const QList<MdListItem> &items, int &idx,
                                    int depth, QStringList &subContents)
{
    const MdListItem &first = items[idx];
    const bool isTask = first.isTask;
    const int listType = isTask ? -1 : first.listType;
    QString html;
    if (isTask)
        html = QStringLiteral("<ul data-type=\"taskList\">");
    else if (listType == 1)
        html = first.number > 1
            ? QStringLiteral("<ol start=\"%1\">").arg(first.number)
            : QStringLiteral("<ol>");
    else
        html = QStringLiteral("<ul>");

    while (idx < items.size()
           && items[idx].depth == depth
           && items[idx].isTask == isTask
           && (isTask || items[idx].listType == listType)) {
        const MdListItem &it = items[idx++];
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
        if (isTask) {
            html += QStringLiteral("<li data-type=\"taskItem\" data-checked=\"")
                    + (it.checked ? QStringLiteral("true") : QStringLiteral("false"))
                    + QStringLiteral("\"><label><input type=\"checkbox\"")
                    + (it.checked ? QStringLiteral(" checked=\"checked\"") : QString())
                    + QStringLiteral("><span></span></label><div>") + inner;
        } else {
            html += QStringLiteral("<li>") + inner;
        }
        // 嵌套更深条目：可能有多个不同容器的连续块（如任务列表后紧跟通用列表）
        if (idx < items.size() && items[idx].depth > depth) {
            while (idx < items.size() && items[idx].depth > depth) {
                const int childDepth = items[idx].depth;
                html += buildMdListContainer(items, idx, childDepth, subContents);
            }
        }
        if (isTask)
            html += QStringLiteral("</div></li>");
        else
            html += QStringLiteral("</li>");
    }
    html += isTask ? QStringLiteral("</ul>")
                   : (listType == 1 ? QStringLiteral("</ol>")
                                    : QStringLiteral("</ul>"));
    return html;
}

// 一组连续列表行（任务项 + 更深缩进的通用列表项）→ TipTap 契约 HTML
// （2 空格缩进表示嵌套）。绕过 setMarkdown 整体自绘：Qt 的 class=unchecked
// 格式 TipTap 不认，且任务项内嵌通用列表 Qt setMarkdown 无法产出。
static QString buildTaskListHtml(const QStringList &taskLines,
                                 QStringList &subContents)
{
    QList<MdListItem> items;
    int prevDepth = -1; // 首行缩进时钳制到 0，防止归零失败
    for (const QString &l : taskLines) {
        MdListItem it;
        if (!parseMdListLine(l, it)) continue;
        if (it.depth > prevDepth + 1) it.depth = prevDepth + 1; // 防止非法深度跳变
        items.append(it);
        prevDepth = it.depth;
    }
    if (items.isEmpty()) return QString();
    int idx = 0;
    QString html;
    while (idx < items.size())   // 顶层可能含多个独立容器（任务后跟通用列表等）
        html += buildMdListContainer(items, idx, items[idx].depth, subContents);
    return html;
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

// 前向声明：定义在下方（generalListInnerToMd 的辅助，任务项内嵌通用列表也用它）
static int findMatchingListClose(const QString &s, int openTagStart, bool ordered);
static QString generalListInnerToMd(const QString &inner, int indent,
                                    bool ordered, int start);

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
                // 2) 嵌套任务列表/通用列表递归提取（须在剥离标签前进行）。
                //    任务项 div 内可同时含 taskList 与通用 <ul>/<ol>（TipTap
                //    taskItem content 'paragraph block*'）。按出现位置最早者优先，
                //    通用列表复用 generalListInnerToMd（缩进 = 任务层级+1 层）。
                static const QRegularExpression gUlRe(
                    "<ul\\b(?![^>]*\\bdata-type\\s*=\\s*\"taskList\")[^>]*>");
                static const QRegularExpression gOlRe("<ol\\b[^>]*>");
                static const QRegularExpression gOlStartRe(
                    "\\bstart\\s*=\\s*\"?(\\d+)\"?");
                QString nestedMd;
                while (true) {
                    auto um = ulOpenRe.match(content);
                    auto gm = gUlRe.match(content);
                    auto om = gOlRe.match(content);
                    const int tp = um.hasMatch() ? um.capturedStart() : -1;
                    const int gp = gm.hasMatch() ? gm.capturedStart() : -1;
                    const int op = om.hasMatch() ? om.capturedStart() : -1;
                    if (tp < 0 && gp < 0 && op < 0) break;
                    int best = tp, type = 0;   // 0=taskList 1=通用ul 2=通用ol
                    if (gp >= 0 && (best < 0 || gp < best)) { best = gp; type = 1; }
                    if (op >= 0 && (best < 0 || op < best)) { best = op; type = 2; }
                    if (type == 0) {
                        int closeStart = findMatchingUlClose(content, best);
                        if (closeStart < 0) break;
                        nestedMd += taskListInnerToMd(
                            content.mid(um.capturedEnd(),
                                        closeStart - um.capturedEnd()),
                            indent + 1);
                        content.remove(best, closeStart + 5 - best);   // 含 </ul>
                    } else if (type == 1) {
                        int closeStart = findMatchingListClose(content, best, false);
                        if (closeStart < 0) break;
                        nestedMd += generalListInnerToMd(
                            content.mid(gm.capturedEnd(),
                                        closeStart - gm.capturedEnd()),
                            (indent + 1) * 2, false, 1);
                        content.remove(best, closeStart + 5 - best);   // 含 </ul>
                    } else {
                        auto sm = gOlStartRe.match(om.captured());
                        const int s2 = sm.hasMatch() ? sm.captured(1).toInt() : 1;
                        int closeStart = findMatchingListClose(content, best, true);
                        if (closeStart < 0) break;
                        nestedMd += generalListInnerToMd(
                            content.mid(om.capturedEnd(),
                                        closeStart - om.capturedEnd()),
                            (indent + 1) * 2, true, s2);
                        content.remove(best, closeStart + 5 - best);   // 含 </ol>
                    }
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

// fromHtml 方向：从 <ul 或 <ol 开标签位置平衡扫描，返回配对闭合标签起始位置
static int findMatchingListClose(const QString &s, int openTagStart, bool ordered)
{
    const QString tag = ordered ? "ol" : "ul";
    QRegularExpression tagRe("<(/?)" + tag + "\\b[^>]*>");
    int depth = 0;
    int pos = openTagStart;
    while (true) {
        auto m = tagRe.match(s, pos);
        if (!m.hasMatch()) return -1;
        if (m.captured(1).isEmpty()) ++depth;   // <ul ...> / <ol ...>
        else if (--depth == 0) return m.capturedStart();
        pos = m.capturedEnd();
    }
}

// 通用列表（<ul>/<ol>，非任务）HTML → GFM markdown。
// Qt toMarkdown 对 3 层以上嵌套列表的缩进输出不自洽（第三层 8 空格、第四层
// 6 空格，且 8 空格无法被 setMarkdown 重解析，多次往返后深层子列表被拍平），
// 故通用列表不走 setHtml→toMarkdown，而是像任务列表一样自绘。
// 缩进规则：子列表缩进 = 父项内容列（父缩进 + 父 marker 宽，无序 "- "=2、
// 有序 "N. "=数字位数+2），Qt setMarkdown 可稳定重解析该缩进。
// 调用前提：行内代码/上下标/高亮/公式/删除线/span 已为 ZZ* 占位符（原样保留），
// 条目中残留的加粗/斜体/链接由 taskItemInlineText 转 MD 语法。
// indent 为外层列表起始缩进（顶层 0）；ordered 为列表类型；start 为有序起始编号。
static QString generalListInnerToMd(const QString &inner, int indent,
                                    bool ordered, int start)
{
    static const QRegularExpression liTagRe("<(/?)li\\b[^>]*>");
    static const QRegularExpression taskUlRe(
        "<ul\\b[^>]*\\bdata-type\\s*=\\s*\"taskList\"[^>]*>");
    static const QRegularExpression gUlRe(
        "<ul\\b(?![^>]*\\bdata-type\\s*=\\s*\"taskList\")[^>]*>");
    static const QRegularExpression olOpenRe("<ol\\b[^>]*>");
    static const QRegularExpression olStartRe("\\bstart\\s*=\\s*\"?(\\d+)\"?");

    QString md;
    int n = start;
    int depth = 0;
    int pos = 0;
    int itemStart = -1;
    while (true) {
        auto m = liTagRe.match(inner, pos);
        if (!m.hasMatch()) break;
        if (m.captured(1).isEmpty()) {           // <li ...>
            if (depth == 0) itemStart = m.capturedEnd();
            ++depth;
        } else {                                  // </li>
            --depth;
            if (depth == 0 && itemStart >= 0) {
                QString content = inner.mid(itemStart,
                                            m.capturedStart() - itemStart);
                // 当前条目 marker 宽（其内容列 = indent + markerWidth）
                const int markerWidth = ordered
                    ? QString::number(n).size() + 2 : 2;
                // 1) 嵌套列表递归提取（任务列表/通用 <ul>/<ol> 均可、顺序不定；
                //    须在剥离标签前）。任务列表复用 taskListInnerToMd，
                //    其 indent 为层级（每层 2 空格）= 条目内容列换算。
                QString nestedMd;
                while (true) {
                    auto tum = taskUlRe.match(content);
                    auto gum = gUlRe.match(content);
                    auto om = olOpenRe.match(content);
                    const int tp = tum.hasMatch() ? tum.capturedStart() : -1;
                    const int up = gum.hasMatch() ? gum.capturedStart() : -1;
                    const int op = om.hasMatch() ? om.capturedStart() : -1;
                    if (tp < 0 && up < 0 && op < 0) break;
                    int best = tp, type = 0;   // 0=任务列表 1=通用ul 2=通用ol
                    if (up >= 0 && (best < 0 || up < best)) { best = up; type = 1; }
                    if (op >= 0 && (best < 0 || op < best)) { best = op; type = 2; }
                    if (type == 0) {
                        int c = findMatchingUlClose(content, best);
                        if (c < 0) break;
                        nestedMd += taskListInnerToMd(
                            content.mid(tum.capturedEnd(), c - tum.capturedEnd()),
                            (indent + markerWidth) / 2);
                        content.remove(best, c + 5 - best);   // 含 </ul>
                    } else if (type == 1) {
                        int c = findMatchingListClose(content, best, false);
                        if (c < 0) { content.remove(best, 1); continue; }
                        nestedMd += generalListInnerToMd(
                            content.mid(gum.capturedEnd(), c - gum.capturedEnd()),
                            indent + markerWidth, false, 1);
                        content.remove(best, c + 5 - best);   // 含 </ul>
                    } else {
                        auto sm = olStartRe.match(om.captured());
                        const int s2 = sm.hasMatch() ? sm.captured(1).toInt() : 1;
                        int c = findMatchingListClose(content, best, true);
                        if (c < 0) { content.remove(best, 1); continue; }
                        nestedMd += generalListInnerToMd(
                            content.mid(om.capturedEnd(), c - om.capturedEnd()),
                            indent + markerWidth, true, s2);
                        content.remove(best, c + 5 - best);   // 含 </ol>
                    }
                }
                // 2) 行内内容 → MD 文本
                QString text = taskItemInlineText(content);
                QString marker = ordered
                    ? QString("%1. ").arg(n) : QStringLiteral("- ");
                QString pad(indent, ' ');
                md += pad + marker;
                if (!text.isEmpty()) {
                    QStringList textLines = text.split('\n');
                    md += textLines.first();
                    // 续行缩进 = 条目内容列（缩进 + marker 宽）
                    QString contPad(indent + markerWidth, ' ');
                    for (int k = 1; k < textLines.size(); ++k)
                        md += "\n" + contPad + textLines[k];
                }
                md += "\n";
                if (ordered) ++n;
                md += nestedMd;
                itemStart = -1;
            }
        }
        pos = m.capturedEnd();
    }
    return md;
}

// 判断位置 pos 是否处于未闭合的通用列表（<ul>/<ol>，非 taskList）内。
// fromHtml 步骤 5 用它跳过「嵌在通用列表内的任务列表」——那些由步骤 5b 的
// generalListInnerToMd 在序列化通用列表时一并处理（保留嵌套缩进），否则
// 嵌套 taskList 会被步骤 5 提升为顶层占位符，往返缩进/层级失真。
static bool isInsideGeneralList(const QString &s, int pos)
{
    static const QRegularExpression olTagRe("<(/?)ol\\b[^>]*>");
    static const QRegularExpression ulTagRe(
        "<(/?)ul\\b(?![^>]*\\bdata-type\\s*=\\s*\"taskList\")[^>]*>");
    int depth = 0;
    int scan = 0;
    while (scan < pos) {
        auto om = olTagRe.match(s, scan);
        auto um = ulTagRe.match(s, scan);
        const int op = om.hasMatch() ? om.capturedStart() : -1;
        const int up = um.hasMatch() ? um.capturedStart() : -1;
        if (op < 0 && up < 0) break;
        // match 从 scan 开始找，结果可能落在 pos 之后——不计入深度，直接终止
        // （首个标签已越过 pos，后续更不可能在 pos 之前）。
        if (up < 0 || (op >= 0 && op < up)) {
            if (op >= pos) break;
            depth += om.captured(1).isEmpty() ? 1 : -1;
            scan = om.capturedEnd();
        } else {
            if (up >= pos) break;
            depth += um.captured(1).isEmpty() ? 1 : -1;
            scan = um.capturedEnd();
        }
    }
    return depth > 0;
}

QString MarkdownHandler::toHtml(const QString &markdown)
{
    // 预处理：两行正文间单 \n → 行末嵌入软换行占位符 ZZBR<n>ZZ
    // QTextDocument::setMarkdown() 不支持硬换行（<br>、  \n、\ 均被忽略）
    // 用纯文本占位符嵌入行末，后处理时替换为 <br data-brk>。占位符带序号，避免
    // 与用户文档中字面 "BRMARKER" 文本冲突（此前固定字符串会被后处理误替换，
    // 破坏字面文本/行内代码，如 audit 文档中的 `BRMARKER`）。

    QStringList lines = markdown.split('\n');
    QString processed;
    int codeFenceLen = 0; // 当前代码块开启围栏的反引号数（0 = 不在代码块）
    int bqCodeFenceLen = 0; // 当前块引用内代码围栏长度（0 = 不在块引用代码块）
    QString bqCodeFenceLang;    // 当前块引用代码块语言（围栏信息串）
    QStringList bqCodeContent;  // 当前块引用代码块内容行
    QStringList bqCodeBlocks;   // 块引用代码块最终 HTML（<pre margin-left…>，后处理还原）
    bool inMathBlock = false;
    bool inTableRegion = false; // 是否处于表格区域（含被惰性延续吸收的纯文本行）
    int currentTableCols = 0;   // 当前表格分隔行列数（由 protectTableRowForQt 记录，防空单元格合并用）
    QStringList tableCellBackups; // 表格空单元格占位符 ZZTC*（后处理还原为空 <td>）
    QStringList codeBlocks;  // 每个代码块的最终 HTML（<pre><code class="language-X">…</code></pre>）
    QStringList codeBlockPhs; // 每个代码块的占位符串（与 codeBlocks 一一对应，可能被撞号跳过非连续）
    QString codeFenceLang;   // 当前代码块的语言（围栏信息串）
    QStringList codeContent; // 当前代码块的内容行
    QStringList subContents; // 下标内容（~x~），后处理还原为 <sub>
    QStringList taskLists; // 任务列表 HTML（TipTap taskList 契约），后处理还原
    QStringList footnoteRefs;   // 脚注引用 <sup> HTML（后处理还原）
    QStringList footnoteDefs;   // 脚注定义 def HTML（按 id 去重，首次定义顺序）
    QStringList footnoteDefIds; // 已收集的定义 id（去重用）
    QStringList escapedEqBackups;  // 转义等号 \= → ZZES 保护
    QStringList htmlInlineBackups; // 原始 HTML 内联标签（<span style> / <u>）
    QStringList hrMarkers;       // 水平线 --- → 占位符保护（后处理还原为 <hr/>）
    int brSoftCount = 0;         // 软换行占位符 ZZBR<n>ZZ 计数（后处理还原为 <br data-brk>）
    int listBlankCount = 0;      // 列表项间空行注释占位 ZZLB<n>ZZ 计数（强制拆分列表）
    int blankLineCount = 0;      // 段落间连续空行占位 ZZBL<n>ZZ 计数（还原为 <p><br></p>）
    QStringList blankLinePhs;    // 实际占位符串（collisionFree 跳号，序号可非连续）

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

        // 块引用内代码围栏长度（> ``` / > ~~~），0 表示非块引用围栏行
        int bqFenceLen = blockquoteCodeFenceLen(line);

        // 表格区域状态机（GFM Tables 扩展：表格在「首个空行或其他块级结构」处结束）。
        // Qt setMarkdown 会把紧随表格行的纯文本行惰性延续为数据行（GFM 一致），却会
        // 错误地把标题/代码围栏/水平线/块级公式也吞入表格。故：块级结构前补空行打断，
        // 纯文本行不打断（按 GFM 吸收为数据行，保证往返保真）。
        if (codeFenceLen == 0 && bqCodeFenceLen == 0 && !inMathBlock) {
            if (isTableLine(line)) {
                if (!inTableRegion)
                    currentTableCols = 0;   // 新表格开始：重置列数（分隔行时再设）
                inTableRegion = true;
            } else if (line.trimmed().isEmpty()
                       || isTableBreakingBlock(line)
                       || isListItemLine(line)
                       || isBlockquoteLine(line)) {
                if (inTableRegion && isTableBreakingBlock(line)
                    && !processed.isEmpty() && !processed.endsWith("\n\n"))
                    processed += '\n';
                inTableRegion = false;
            }
            // 其余（纯文本/图片等）行：保持 inTableRegion，被惰性延续吸收
        }

        if (codeFenceLen == 0 && line.startsWith("$$")) {
            if (!inMathBlock) {
                // 单行块级公式 $$...$$（开闭定界符在同一行，如 $$E=mc^2$$）。
                // 若按多行逻辑处理，本行内容与闭合 $$ 会被当作"开启围栏"丢弃，
                // 占位符 ZZMB 永远无备份可还原，泄漏为字面文本 ZZMB0ZZ。
                QString trimmedLine = line.trimmed();
                if (trimmedLine.size() > 4 && trimmedLine.endsWith("$$")) {
                    QString latex = trimmedLine.mid(2, trimmedLine.size() - 4).trimmed();
                    mathBlocks << ("<div data-type=\"block-math\" data-latex=\"" + latex + "\"></div>");
                    processed += "ZZMB" + QString::number(mathBlocks.size() - 1) + "ZZ\n";
                    lastBlock = BT_NONE;
                    continue;
                }
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
                    if (latex.contains("\\begin{")) {
                        // 用户显式书写 LaTeX 环境（aligned/matrix/cases 等）：原样
                        // 保留，仅折叠换行为空格（行尾 \\ 已含换行语义），不额外包裹
                        // aligned——否则双重包裹破坏结构。fromHtml 据此还原为多行原样。
                        latex.replace('\n', ' ');
                    } else {
                        // 纯多行（无环境）：包裹 aligned 供 KaTeX 渲染
                        QStringList parts;
                        for (const QString &l : latex.split('\n'))
                            if (!l.trimmed().isEmpty())
                                parts << l.trimmed();
                        latex = "\\begin{aligned}" + parts.join(" \\\\ ") + "\\end{aligned}";
                    }
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
                // 占位符序号须避开源文档/已产出内容中的字面同串（如 audit 文档
                // 里示例的 `ZZCB0ZZ`），否则还原时用户文本被误替换。串存 codeBlockPhs，
                // 与 codeBlocks 一一对应，还原时按存储值回填。
                codeBlockPhs << collisionFreePlaceholder(
                    "CB", codeBlocks.size(), markdown, processed);
                processed += codeBlockPhs.last() + "\n";
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
        } else if (bqFenceLen > 0) {
            // 块引用内代码围栏行（> ``` / > ~~~）：整体收集（剥离 > 前缀），
            // 用占位符保护并保留语言信息串——Qt setMarkdown 不保留代码语言，且
            // BRMARKER 会破坏围栏解析。占位符以块引用行（> ZZBQnZZ）发出并前后
            // 空行隔离，setMarkdown 产出独立 <p margin-left>ZZBQnZZ</p>，后处理
            // 还原为 <pre margin-left>…</pre>，再由块引用重构合并进 <blockquote>。
            QString stripped = stripBlockquotePrefix(line);
            int fenceLen = 0;
            while (fenceLen < stripped.size()
                   && (stripped[fenceLen] == QLatin1Char('`')
                       || stripped[fenceLen] == QLatin1Char('~')))
                ++fenceLen;
            if (bqCodeFenceLen == 0) {
                // 开启：提取语言（信息串为围栏后的文本）
                bqCodeFenceLang = stripped.mid(fenceLen).trimmed();
                bqCodeFenceLen = fenceLen;
                bqCodeContent.clear();
            } else if (fenceLen >= bqCodeFenceLen && stripped.mid(fenceLen).trimmed().isEmpty()) {
                // 闭合：本行为纯围栏且长度 >= 开启围栏
                QString content = bqCodeContent.join('\n');
                QString codeOpen = bqCodeFenceLang.isEmpty()
                    ? QStringLiteral("<code>")
                    : QStringLiteral("<code class=\"language-") + bqCodeFenceLang + "\">";
                bqCodeBlocks << "<pre style=\" margin-left:40px; margin-right:40px;\">"
                                + codeOpen + content.toHtmlEscaped() + "</code></pre>";
                bqCodeFenceLen = 0;
                // 占位符（前后空行隔离，使 setMarkdown 产出独立 <p> 块）
                if (!processed.isEmpty() && !processed.endsWith("\n\n"))
                    processed += '\n';
                processed += "> ZZBQ" + QString::number(bqCodeBlocks.size() - 1) + "ZZ\n\n";
                // 闭合后紧跟空行（块引用结束）：发出边界标记。setMarkdown 会把空行
                // 折叠，扁平 margin-left 块无法区分「同一块引用」与「空行分隔的两个
                // 块引用」，故此标记以顶层段落形态透传，供后处理打破合并。
                if (next.trimmed().isEmpty())
                    processed += "ZZBQSEP\n";
            } else {
                // 块引用代码内容（含以围栏开头但不足以闭合的嵌套围栏行）
                bqCodeContent << stripped;
            }
            lastBlock = BT_NONE;
        } else if (bqCodeFenceLen > 0) {
            // 块引用代码内容行（非围栏行）：剥离 > 前缀收集
            bqCodeContent << stripBlockquotePrefix(line);
            lastBlock = BT_NONE;
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
            const bool prevWasList = (lastBlock == BT_LIST);
            // 段落间连续空行保护（&nbsp; 空行方案）：Qt setMarkdown 会把段落之间
            // 的多个空行（a\n\n\nb）折叠成单个段落分隔，空行信息丢失；且纯空行
            // 不可移植（标准 md 渲染器折叠多空行）。当两个非空块之间有 K 个空行
            // 且 K≥2 时，保留 1 个作段落分隔，其余 K-1 个替换为占位符行，
            // setMarkdown 后还原为 <p></p>（空段落）；fromHtml 把空段落还原为
            // 字面 &nbsp; 行（跨编辑器可移植的空行，见后处理 1.6）。
            // 仅当：前面已有内容 + 上一块非列表 + 下一非空行非列表项。
            // 列表项间空行走下方 ZZLB 注释逻辑（机制不同，互不干扰）。
            int k = 1;
            while (i + k < lines.size() && lines[i + k].trimmed().isEmpty()) ++k;
            const int j = i + k;
            const bool hasPrevContent = !processed.trimmed().isEmpty();
            const bool paraBlank = (k >= 2 && hasPrevContent && !prevWasList
                                    && j < lines.size()
                                    && !isListItemLine(lines[j])
                                    && !isTaskItemLine(lines[j]));
            processed += '\n';
            if (paraBlank) {
                // 每个占位符行独占一个段落：行后必须补段落分隔空行，否则
                // md4c 把「占位符行\n下一行」当作软换行合并进同一段落，
                // 还原后 b 被吸进 <p><br> b</p>（往返数据错乱）。
                for (int b = 0; b < k - 1; ++b) {
                    const QString ph = collisionFreePlaceholder(
                        "BL", blankLineCount, markdown, processed);
                    ++blankLineCount;
                    blankLinePhs << ph;
                    processed += ph + '\n';
                    processed += '\n';
                }
                i += k - 1;   // for 循环 ++i 后落到下一非空行
            } else if (prevWasList) {
                // 列表项间空行保护：Qt setMarkdown 会把「列表项之间用空行分隔」折叠成
                // 同一 <ol>/<ul> 内相邻 <li>，空行信息丢失（往返后列表项变相邻）。
                // 插入 HTML 注释占位强制拆分列表（缩进对齐下一列表项层级）；注释被
                // Qt setMarkdown 丢弃，产出独立的 <ol>/<ul> 块（有序列表自动带 start），
                // fromHtml 的 toMarkdown 还原为列表项间的空行。
                // 仅当：上一块是列表 + 下一非空行是列表项。
                // 列表项后跟段落/标题时 Qt 本身保留空行，不触发。
                // 仅顶层列表项（缩进 0）触发：嵌套项间空行即便拆分成独立嵌套
                // <ul>，Qt toMarkdown 也会把相邻嵌套 <ul> 合并回去（空行仍丢，
                // 且会造成 WYSIWYG 与保存不一致），故不处理嵌套层级。
                if (j < lines.size() && isListItemLine(lines[j])
                    && !lines[j][0].isSpace()) {
                    processed += "<!-- ZZLB" + QString::number(listBlankCount++)
                                 + "ZZ -->\n";
                }
            }
            lastBlock = BT_NONE;
        } else if (isTaskItemLine(line)) {
            // 任务列表：收集连续行（任务项 + 比最近任务行缩进更深的通用列表行，
            // 含缩进嵌套），绕过 setMarkdown 整体转为 TipTap 契约 HTML（Qt 自家
            // class=unchecked/checked 格式 TipTap 不认；任务项内嵌通用列表
            // Qt setMarkdown 也无法产出）。同层/更浅的通用列表行不吸收，
            // 仍由通用列表分支独立处理（避免行为回归）。
            QStringList taskLines;
            int lastTaskIndent = -1;
            while (i < lines.size()) {
                const QString &l = lines[i];
                if (isTaskItemLine(l)) {
                    taskLines << l;
                    int ind = 0;
                    while (ind < l.size() && l.at(ind) == QLatin1Char(' ')) ++ind;
                    lastTaskIndent = ind;
                    ++i;
                } else if (isListItemLine(l) && lastTaskIndent >= 0) {
                    int ind = 0;
                    while (ind < l.size() && l.at(ind) == QLatin1Char(' ')) ++ind;
                    if (ind > lastTaskIndent) { taskLines << l; ++i; }
                    else break;
                } else {
                    break;
                }
            }
            --i; // for 循环会 ++i
            // 前一行非空且未以空行结尾时补空行，避免占位符被并入前一区块
            // （同隔离规则：也覆盖 $$ 占位符后紧跟任务列表的情况）
            if (!processed.isEmpty() && !processed.endsWith("\n\n"))
                processed += '\n';
            taskLists << buildTaskListHtml(taskLines, subContents);
            processed += "ZZTL" + QString::number(taskLists.size() - 1) + "ZZ\n";
            lastBlock = BT_LIST;
        } else if (isListItemLine(line)) {
            // 通用列表（非任务起始块）：前瞻检查本连续列表块是否含任务项。
            // 若含（如「通用有序列表下嵌套任务列表」），Qt setMarkdown 无法在
            // 通用列表下产出 TipTap 任务列表契约（任务行会被拍平/提升），
            // 整块走统一自绘；否则逐行直通既有路径（保持纯通用列表行为不变）。
            int j = i;
            bool hasTask = false;
            while (j < lines.size() && isListItemLine(lines[j])) {
                if (isTaskItemLine(lines[j])) { hasTask = true; break; }
                ++j;
            }
            if (hasTask) {
                QStringList listLines;
                while (i < lines.size() && isListItemLine(lines[i]))
                    listLines << lines[i++];
                --i; // for 循环会 ++i
                if (!processed.isEmpty() && !processed.endsWith("\n\n"))
                    processed += '\n';
                taskLists << buildTaskListHtml(listLines, subContents);
                processed += "ZZTL" + QString::number(taskLists.size() - 1) + "ZZ\n";
                lastBlock = BT_LIST;
            } else {
                // 隔离：正文后紧跟列表时补空行（与 else 分支一致）
                if (lastBlock == BT_PARA) processed += '\n';
                QString cl = line;
                cl = protectTableRowForQt(cl, currentTableCols, tableCellBackups);
                cl = extractEscapedEquals(cl, escapedEqBackups);
                cl = extractSubscripts(cl, subContents);
                cl = extractFootnoteRefs(cl, footnoteRefs);
                cl = extractHtmlInlineElements(cl, htmlInlineBackups);
                processed += cl + '\n';
                lastBlock = BT_LIST;
            }
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
                   && !isImageOnlyLine(next)
                   // 块引用打断段落是块边界，非软换行：正文\n> 引用 不应插入
                   // <br data-brk>（否则往返成多一个空行）。反向（> 引用\n正文）
                   // 是 lazy continuation，仍保留软换行。
                   && !(!isBlockquoteLine(line) && isBlockquoteLine(next))) {
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
            processed += cl + " " + "ZZBR" + QString::number(brSoftCount++) + "ZZ\n";
            lastBlock = BT_PARA;
        } else if (isDashHrLine(line) && lastBlock != BT_PARA) {
            // 水平线 ---：占位保护，避免 Qt setMarkdown 折叠连续 --- 或灾难性
            // 误解析。--- 紧跟前文段落（无空行）时是 setext 二级标题下划线，
            // 非水平线，此时 lastBlock==BT_PARA 不满足条件，落入 else 分支原样直通。
            QString ph = QStringLiteral("ZZHR") + QString::number(hrMarkers.size())
                         + QStringLiteral("ZZ");
            hrMarkers << "<hr/>";
            processed += ph + '\n';
            lastBlock = BT_NONE;
        } else {
            // 隔离：列表后紧跟正文时补空行（同上）
            if (isParagraphLine(line) && lastBlock == BT_LIST)
                processed += '\n';
            // 隔离：正文后紧跟列表时同样补空行 —— CommonMark 规定非 1 起始的
            // 有序列表不能打断段落，md4c 会把列表项当作段落延续行吞并
            if (isListItemLine(line) && lastBlock == BT_PARA)
                processed += '\n';
            QString cl = line;
            // 去缩进：跨空行紧跟列表的缩进段落——Qt setMarkdown 会把带前导空格的
            // 段落当列表项 lazy continuation 吸收进 <ol>（产出 <ol> 内非法 <p>，
            // 位于两列表项之间），fromHtml 的 toMarkdown 丢弃该段落（往返数据
            // 丢失）。剥离前导空格使其成为独立段落；紧邻列表项无空行的缩进行是
            // 真正的续行（同列表项内容），保留缩进。
            if (isParagraphLine(line) && !line.isEmpty() && line[0].isSpace()
                && i > 0 && lines[i - 1].trimmed().isEmpty()) {
                int j = i - 1;
                while (j >= 0 && lines[j].trimmed().isEmpty()) --j;
                if (j >= 0 && isListItemLine(lines[j])) {
                    int ws = 0;
                    while (ws < line.size() && line[ws].isSpace()) ++ws;
                    cl = line.mid(ws);
                }
            }
            cl = protectTableRowForQt(cl, currentTableCols, tableCellBackups);
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

    // 转义未闭合的 HTML 标签（见 escapeUnclosedHtmlTags）：Qt setMarkdown 会把
    // 段落内未闭合的 <tag> 当 HTML 块吞噬其后内容到文末（数据丢失）。须在行内
    // 代码与 <div> 占位保护之后执行——此时受支持的标签已全部占位，剩余未闭合
    // 标签才安全转义为实体。
    escapeUnclosedHtmlTags(processed);

    QTextDocument doc;
    doc.setMarkdown(processed);
    QString html = doc.toHtml();

    // 后处理 1: 还原代码块占位符 ZZCB → <pre><code class="language-X">…</code></pre>
    //   代码块内容在预处理阶段已整体收集并 HTML 转义，未经过 setMarkdown，
    //   因此可正确保留嵌套围栏（如 ```markdown 内嵌 ```mermaid）。
    //   占位符串用 codeBlockPhs（collisionFreePlaceholder 存储值，序号可能非连续）。
    for (int i = 0; i < codeBlocks.size(); ++i) {
        const QString ph = codeBlockPhs[i];
        html.replace(QRegularExpression("<p[^>]*>" + ph + "</p>"), codeBlocks[i]);
        html.replace(ph, codeBlocks[i]); // fallback：占位符未独立成段时
    }

    // 后处理 1.4: 还原块引用代码块占位符 ZZBQ → <pre margin-left:40px>…</pre>
    //   占位符以块引用行发出，setMarkdown 产出 <p margin-left>ZZBQnZZ</p>；还原为
    //   <pre margin-left>…</pre> 后由后处理 2 的块引用重构合并进 <blockquote>。
    for (int i = 0; i < bqCodeBlocks.size(); ++i) {
        QString ph = QString("ZZBQ%1ZZ").arg(i);
        html.replace(QRegularExpression("<p[^>]*>" + ph + "</p>"), bqCodeBlocks[i]);
        html.replace(ph, bqCodeBlocks[i]); // fallback：占位符未独立成段时
    }

    // 后处理 1.5: 还原水平线占位符 ZZHR → <hr/>
    //   连字符水平线在预处理阶段已占位保护（Qt setMarkdown 会折叠/误解析 ---），
    //   此处还原为 <hr/>。占位符独立成段时连同 <p> 一起替换。
    for (int i = 0; i < hrMarkers.size(); ++i) {
        QString ph = QString("ZZHR%1ZZ").arg(i);
        html.replace(QRegularExpression("<p[^>]*>" + ph + "</p>"), "<hr/>");
        html.replace(ph, "<hr/>"); // fallback：占位符未独立成段时
    }

    // 后处理 1.6: 还原段落间连续空行占位符 ZZBL → <p></p>（空段落）
    //   源码 K 个连续空行（不可移植，标准 md 折叠）→ K-1 个占位符段落，此处
    //   还原为空段落，fromHtml 识别空段落并还原为字面 &nbsp; 行（跨编辑器
    //   可移植的空行）。必须还原为 <p></p> 而非 <p><br></p>：<br> 在段内产生
    //   换行，空段落渲染成 2 行高（实测 61px ≈ 2 行），视觉间距翻倍无规律；
    //   <p></p> 与 TipTap Enter 原生空段落一致（1 行高 31px），渲染正确。
    for (const QString &ph : blankLinePhs) {
        html.replace(QRegularExpression("<p[^>]*>" + ph + "</p>"), "<p></p>");
        html.replace(ph, QString()); // fallback：占位符未独立成段时删除
    }

    // 后处理 2: ZZBR<n>ZZ → <br data-brk>（占位符以区分用户手写 <br>）
    // 嵌入处 cl + " " + 占位符 在标记前补了一个空格作为隔离符，防止标记被
    // 前文行尾标点（*、_、` 等）吞入内联结构；setMarkdown 又在标记后（原换行处）
    // 生成一个空格。这两个空格都是标记机制的伪影，替换时必须一并删除，否则
    // 残留为软换行行尾空格，破坏往返保真。故先匹配「空格+占位符+空格」消费两侧
    // 空格，再降级匹配仅一侧有空格的情形。占位符带序号，用户文档中字面
    // "BRMARKER"（含行内代码内）不会被误替换。
    for (int i = 0; i < brSoftCount; ++i) {
        const QString ph = "ZZBR" + QString::number(i) + "ZZ";
        html.replace(" " + ph + " ", "<br data-brk>");
        html.replace(" " + ph, "<br data-brk>");
        html.replace(ph + " ", "<br data-brk>");
        html.replace(ph, "<br data-brk>");
    }

    // 还原任务列表占位符 ZZTL → TipTap taskList 契约 HTML
    //   先于 mark/上下标/行内公式转换还原，条目文本中的相应语法也能被转换；
    //   ZZSB（下标）占位符由后续统一步骤还原
    for (int i = 0; i < taskLists.size(); ++i) {
        QString ph = QString("ZZTL%1ZZ").arg(i);
        html.replace(QRegularExpression("<p[^>]*>" + ph + "</p>"), taskLists[i]);
        html.replace(ph, taskLists[i]); // fallback：占位符未独立成段时
    }

    // 后处理 2: QTextDocument 内联样式 → 语义标签
    //   引用: <p margin-left:40px>... → <blockquote><p>...</p></blockquote>
    //   Qt 的 setMarkdown 将块引用表示为扁平的 margin-left:40px 块（非 <blockquote>
    //   标签），且无法区分「同一块引用内的多段落」与「多个相邻块引用」（空白行
    //   信息被折叠）。折中：仅当连续的 margin-left 块序列中含 <pre>（块引用内
    //   代码块）时才合并为单个 <blockquote>；纯 <p> 序列每个 <p> 独立成
    //   <blockquote>（保持「> a\n\n> b」两个独立块引用的往返）。
    //   注意 <(p|pre)\b 的 \b 必须保留：否则 <p 会误匹配 <pre 的开头（p 是 pre 的
    //   前缀），把 <pre>…</pre> 与其后的 <p>…</p> 吞成一个 <p> 块，代码块被拆碎。
    {
        QRegularExpression blockRe(
            R"(<(p|pre)\b([^>]*margin-left:\s*40px[^>]*margin-right:\s*40px[^>]*)>(.*?)</\1>)",
            QRegularExpression::DotMatchesEverythingOption);

        struct BqBlock { int start = 0; int end = 0; bool isPre = false; };
        QVector<BqBlock> blocks;
        int off = 0;
        while (true) {
            auto m = blockRe.match(html, off);
            if (!m.hasMatch()) break;
            BqBlock b;
            b.start = m.capturedStart();
            b.end = m.capturedEnd();
            b.isPre = (m.captured(1) == QLatin1String("pre"));
            blocks << b;
            off = m.capturedEnd();
        }

        if (!blocks.isEmpty()) {
            // 相邻块之间仅含空白时归为同一 run
            QVector<QVector<int>> runs;
            QVector<int> cur;
            for (int i = 0; i < blocks.size(); ++i) {
                if (cur.isEmpty()) {
                    cur << i;
                } else {
                    QString between = html.mid(blocks[cur.last()].end,
                                               blocks[i].start - blocks[cur.last()].end);
                    bool onlyWs = true;
                    for (QChar ch : between) { if (!ch.isSpace()) { onlyWs = false; break; } }
                    if (onlyWs) cur << i;
                    else { runs << cur; cur.clear(); cur << i; }
                }
            }
            if (!cur.isEmpty()) runs << cur;

            // 从后往前重建，避免位置偏移。相邻 <blockquote> 间无需保留空白——
            // Qt toMarkdown 对相邻 <blockquote> 自动补空行（已实测验证）。
            QString rebuilt = html;
            for (int r = runs.size() - 1; r >= 0; --r) {
                const QVector<int> &run = runs[r];
                bool hasPre = false;
                for (int idx : run) if (blocks[idx].isPre) { hasPre = true; break; }

                int runStart = blocks[run.first()].start;
                int runEnd = blocks[run.last()].end;

                QString out;
                if (hasPre) {
                    // 含 <pre>：整段 run 合并为一个 <blockquote>
                    QString inner;
                    for (int idx : run)
                        inner += html.mid(blocks[idx].start,
                                          blocks[idx].end - blocks[idx].start);
                    out = "<blockquote>" + inner + "</blockquote>";
                } else {
                    // 纯 <p>：每个 <p> 独立成 <blockquote>
                    for (int idx : run) {
                        out += "<blockquote>" + html.mid(blocks[idx].start,
                                                         blocks[idx].end - blocks[idx].start)
                               + "</blockquote>";
                    }
                }
                rebuilt.replace(runStart, runEnd - runStart, out);
            }
            html = rebuilt;
        }
    }

    // 后处理 2.5: 移除块引用代码块的边界标记 ZZBQSEP。
    //   标记以顶层段落透传，用于打破块引用重构的「连续 margin-left 块」合并，
    //   使「> 代码\n\n> 引用」空行分隔的两个块引用不被误合并为一个。此处移除，
    //   相邻 <blockquote>（或 <blockquote> 与正文）由 Qt toMarkdown 自动补空行。
    html.remove(QRegularExpression(R"(<p[^>]*>ZZBQSEP</p>\s*)"));
    //    monospace span → <code>（字体名动态匹配本平台 FixedFont，见 monoSpanFamilyPattern）
    {
        QRegularExpression re(
            QStringLiteral("<span[^>]*font-family:\\s*'(?:%1)'[^>]*>(.*?)</span>")
                .arg(monoSpanFamilyPattern()));
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
        // 内容字符类必须允许 =：高亮内含 HTML 标签时（==<s>删</s>高== 经
        // setMarkdown 后内层变 <span style="…line-through…">）属性里的 = 会被
        // [^=] 排除导致整组不匹配、== 残留为字面量。放宽为 [^\n] 仍以惰性匹配
        // 停在首个闭合 ==，相邻高亮 ==a== ==b== 互不吞噬（内容中确实含 = 的
        // 高亮本就是合法语法）。
        {
            QRegularExpression re("==([^\n]+?)==");
            html.replace(re, "<mark>\\1</mark>");
        }
        // ~~删除线~~ → <s>：Qt setMarkdown 把 ~~x~~ 渲染为
        // <span style="text-decoration:line-through">，但 TipTap Strike mark
        // 契约是 <s>（只认 s/del，不认 span）。用户手写的 line-through span
        // 已在 setMarkdown 前被 ZZHI 占位符保护，此处残留的 line-through 必来自
        // Qt 的 ~~ 转换，可安全转为 <s>。内层 ZZSB（下标）占位符随 <s> 一并保留，
        // 由后续 ZZSB 还原步骤处理（~~H~2~O~~ → <s>H<sub>2</sub>O</s>）。
        // 若 span 同时含 font-weight:700 / font-style:italic（删除线 ⚡ 加粗/斜体
        // 嵌套，如 <strong>粗<s>删</s></strong> 经 setMarkdown 摊平为组合 span），
        // 必须一并保留——否则 <strong>粗<s>删</s></strong> 往返后「删」丢失加粗。
        // 外层用 <strong><s>…</s></strong> / <em><s>…</s></em> 表达，TipTap 直接解析。
        {
            QRegularExpression lineThroughSpanRe(
                R"(<span\b[^>]*\btext-decoration\s*:\s*line-through[^>]*>(.*?)</span>)",
                QRegularExpression::DotMatchesEverythingOption);
            static const QRegularExpression styleAttrRe(
                R"a(\bstyle\s*=\s*"([^"]*)")a");
            int off = 0;
            while (true) {
                auto m = lineThroughSpanRe.match(html, off);
                if (!m.hasMatch()) break;
                QString content = m.captured(1);
                QString replacement = QStringLiteral("<s>%1</s>").arg(content);
                auto sm = styleAttrRe.match(m.captured());
                QString style = sm.hasMatch() ? sm.captured(1) : QString();
                const bool hasBold = style.contains(
                    QRegularExpression("\\bfont-weight\\s*:\\s*700\\b"));
                const bool hasItalic = style.contains(
                    QRegularExpression("\\bfont-style\\s*:\\s*italic\\b"));
                if (hasBold && hasItalic)
                    replacement = QStringLiteral("<strong><em><s>%1</s></em></strong>").arg(content);
                else if (hasBold)
                    replacement = QStringLiteral("<strong><s>%1</s></strong>").arg(content);
                else if (hasItalic)
                    replacement = QStringLiteral("<em><s>%1</s></em>").arg(content);
                html.replace(m.capturedStart(), m.capturedLength(), replacement);
                off = m.capturedStart() + replacement.length();
            }
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
    // 独占段落的占位符须连 <p> 一起替换：Qt setMarkdown 把占位符包在 <p> 中，
    // 若保留 p 壳，产出 <p><div>…</div></p> 非法嵌套，TipTap 解析时把块级
    // divBlock 提升出 <p>、却把空 <p> 壳留在原位，渲染为 div 前后各一条空行
    // （与块级图片解包同类问题，见下方 <p><img/></p> 解包注释）。
    for (int i = 0; i < divBackups.size(); ++i) {
        QString ph = QString("ZZDV%1ZZ").arg(i);
        html.replace(QRegularExpression("<p[^>]*>" + ph + "</p>"), divBackups[i]);
        html.replace(ph, divBackups[i]); // fallback：占位符未独立成段（混排文本/标题内）
    }

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

    // 表格列对齐提升：Qt toHtml 把 GFM 列对齐（:-: / ---:）输出为数据行单元格
    // 内的 <p align="...">（表头行与默认左对齐不带 align 属性），而 TipTap v3
    // TableCell/TableHeader 仅解析 <td>/<th> 自身的 style/align 属性，内层
    // <p align> 在编辑器加载时被丢弃。此处按列聚合对齐并统一写到每个单元格
    // 标签上（含表头行），使整列在编辑器内呈现对齐，且保存时可经 fromHtml
    // 还原为分隔行对齐标记。
    // （左对齐是默认值，Qt 不输出 align —— :--- 与 --- 在 Qt 层同形，
    //  往返后统一规范化为 ---，语义等价。）
    {
        QRegularExpression tableRegionRe(
            QStringLiteral("(<table\\b[^>]*>)(.*?)(</table>)"),
            QRegularExpression::DotMatchesEverythingOption);
        QRegularExpression cellOpenRe(QStringLiteral("<t[dh]\\b[^>]*>"));
        // Qt 输出形态：<td>\n<p align="center" style="...">（\s 覆盖标签间换行）
        QRegularExpression cellPAlignRe(
            QStringLiteral("(<t[dh]\\b[^>]*>)\\s*<p\\b([^>]*?)\\s*\\balign\\s*=\\s*\"(center|right|left)\"([^>]*)>"));
        int off = 0;
        while (true) {
            auto m = tableRegionRe.match(html, off);
            if (!m.hasMatch()) break;
            QString region = m.captured(2);
            // 同行内列号：统计单元格开标签在本行（最近 <tr 之后）中位于其前的数量
            auto columnOf = [&region, &cellOpenRe](int cellStart) {
                int rowStart = region.lastIndexOf(QLatin1String("<tr"), cellStart);
                int col = 0;
                for (int p = rowStart; p >= 0; ) {
                    auto om = cellOpenRe.match(region, p);
                    if (!om.hasMatch() || om.capturedStart() >= cellStart) break;
                    ++col;
                    p = om.capturedEnd();
                }
                return col;
            };
            // 1) 按列聚合对齐（列内首个非空值）
            QStringList colAligns;
            {
                int coff = 0;
                while (true) {
                    auto cm = cellPAlignRe.match(region, coff);
                    if (!cm.hasMatch()) break;
                    int col = columnOf(cm.capturedStart());
                    while (colAligns.size() < col + 1) colAligns << QString();
                    if (colAligns[col].isEmpty()) colAligns[col] = cm.captured(3);
                    coff = cm.capturedEnd();
                }
            }
            const bool hasAlign = colAligns.contains(QLatin1String("center"))
                               || colAligns.contains(QLatin1String("right"))
                               || colAligns.contains(QLatin1String("left"));
            if (hasAlign) {
                // 2) 剥离内层 p 的 align 属性（对齐已上移，避免双份携带）
                region.replace(
                    QRegularExpression(QStringLiteral(
                        "<p\\b([^>]*?)\\s*\\balign\\s*=\\s*\"(?:center|right|left)\"([^>]*)>")),
                    QStringLiteral("<p\\1\\2>"));
                // 3) 给该列有对齐且无 style 的单元格标签注入 text-align
                //    （含表头行 —— GFM 列对齐语义覆盖整列）
                int coff = 0;
                while (true) {
                    auto cm = cellOpenRe.match(region, coff);
                    if (!cm.hasMatch()) break;
                    int col = columnOf(cm.capturedStart());
                    QString a = (col < colAligns.size()) ? colAligns[col] : QString();
                    QString tag = cm.captured();
                    if (!a.isEmpty() && !tag.contains(QLatin1String("style"))) {
                        QString newTag = tag.left(tag.size() - 1)
                                       + QStringLiteral(" style=\"text-align:") + a + QStringLiteral("\">");
                        region.replace(cm.capturedStart(), cm.capturedLength(), newTag);
                        coff = cm.capturedStart() + newTag.size();
                    } else {
                        coff = cm.capturedEnd();
                    }
                }
            }
            QString replacement = m.captured(1) + region + m.captured(3);
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

    // 还原表格空单元格占位符 ZZTC → 空 <td>（≥2 格行防 Qt 合并用，见 protectTableRowForQt）。
    // 占位符独立成单元格内容，Qt 输出 <td><p…>ZZTCnZZ</p></td>，连同 <p> 壳还原为空单元格，
    // 与 Qt 自身空单元格（<td></td>）形态一致，供 TipTap/fromHtml 正常解析。
    html.replace(QRegularExpression(
        QStringLiteral("<td([^>]*)>\\s*<p[^>]*>ZZTC\\d+ZZ</p>\\s*</td>")),
        QStringLiteral("<td\\1></td>"));
    // fallback：占位符未包在 <p> 中时直接清空（防泄漏为字面文本）
    html.replace(QRegularExpression(QStringLiteral("ZZTC\\d+ZZ")), QString());

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

// 行内代码内容 → 定界正确的行内代码 Markdown。
// 定界符取内容中最长连续反引号串 + 1（至少 1），保证内容含反引号时不会
// 提前闭合（与代码块围栏同源问题，见 docs/code-fence-roundtrip.md 坑 2）。
// 内容首/尾为反引号时补空格分隔：否则反引号与定界符并成更长的定界串，
// 内容被吞（如内容 `x` 直接拼 `` 会得到 ``x``，反引号丢失）。
// 块级公式 data-latex → Markdown（$$ ... $$）逐字还原。
// toHtml 对块级公式输出的 data-latex 恒为单行（换行已折叠），分三类：
//   (a) 自动包裹：纯多行（无环境）→ "\begin{aligned}" + 行join(" \\ ") + "\end{aligned}"
//   (b) 显式环境：用户手写 \begin{...}...\end{...} → 折叠换行为空格，保留 \\ 与 &
//   (c) 单行公式：无换行、无环境
// 据此反向区分：
//   - (a) 首尾恰为 \begin{aligned}/\end{aligned}，且中间无嵌套 \begin{ 也无对齐列
//     & → 剥离 aligned，按 \\ 拆行还原为纯多行。
//   - (b) 含 \begin{ → 把 \begin{...}/\end{...} 提为独立行、\\ 后补换行，逐字还原。
//   - (c) 直接单行输出。
static QString blockMathLatexToMarkdown(const QString &latex)
{
    QString t = latex.trimmed();

    // 自动包裹的 aligned（仅当中间既无嵌套环境也无对齐列，才能安全剥离）
    const QString open = QStringLiteral("\\begin{aligned}");
    const QString close = QStringLiteral("\\end{aligned}");
    if (t.startsWith(open) && t.endsWith(close)) {
        QString mid = t.mid(open.size(), t.size() - open.size() - close.size());
        if (!mid.contains("\\begin{") && !mid.contains(QLatin1Char('&'))) {
            QStringList parts;
            for (const QString &p : mid.split(" \\\\ "))
                parts << p.trimmed();
            return "$$\n" + parts.join("\n") + "\n$$";
        }
        // 否则为显式 \begin{aligned} 环境（含对齐列或嵌套环境），落入下方逐字还原
    }

    // 显式 LaTeX 环境（aligned/matrix/pmatrix/cases 等）：逐字还原多行书写形式
    if (t.contains("\\begin{")) {
        QString r = t;
        r.replace(QRegularExpression(R"(\\begin\{([^}]*)\})"), "\n\\begin{\\1}\n");
        r.replace(QRegularExpression(R"(\\end\{([^}]*)\})"), "\n\\end{\\1}\n");
        r.replace(" \\\\ ", " \\\\\n");
        QStringList lines;
        for (const QString &l : r.split('\n')) {
            QString s = l.trimmed();
            if (!s.isEmpty()) lines << s;
        }
        return "$$\n" + lines.join("\n") + "\n$$";
    }

    // 单行公式（无环境）
    return "$$\n" + t + "\n$$";
}

static QString buildInlineCodeMd(const QString &content)
{
    int maxRun = 0, run = 0;
    for (const QChar &c : content) {
        if (c == QLatin1Char('`')) {
            ++run;
            if (run > maxRun) maxRun = run;
        } else {
            run = 0;
        }
    }
    int fenceLen = maxRun + 1;
    if (fenceLen < 1) fenceLen = 1;
    QString fence(fenceLen, QLatin1Char('`'));
    QString body = content;
    if (body.startsWith(QLatin1Char('`'))) body.prepend(QLatin1Char(' '));
    if (body.endsWith(QLatin1Char('`'))) body.append(QLatin1Char(' '));
    return fence + body + fence;
}

// 删除线内联内容还原后若以 ~ 开头或结尾，会与 ~~ 分隔符贴合成 ~~~… 歧义序列，
// Qt setMarkdown 无法区分下标闭合 ~ 与删除线闭合 ~~：~~H~2~~~ 整体按字面量解析、
// ~~~2~H~~ 甚至输出空段落内容全丢（往返断裂）。命中时该 <s>/line-through 应保留
// 字面 HTML 而非转 ~~…~~（TipTap Strike mark 直接解析 <s>/line-through，HTML 兜底
// 保证往返）。检测对象是内联内容中还原为 ~X~ 的下标占位符 ZZSB，或字面 ~ 贴边。
static bool strikeEdgeTildeCollision(const QString &inner)
{
    static const QRegularExpression phRe("ZZ[A-Z]+[0-9]+ZZ");
    const QString t = inner.trimmed();
    if (t.isEmpty()) return false;

    // 首个内容单元：下标占位符 ZZSB，或字面文本以 ~ 开头
    const QRegularExpressionMatch first = phRe.match(t);
    const bool firstIsPh = first.hasMatch() && first.capturedStart() == 0;
    if (firstIsPh) {
        if (first.captured(0).startsWith("ZZSB")) return true;
    } else if (t.at(0) == QLatin1Char('~')) {
        return true;
    }

    // 末个内容单元：下标占位符 ZZSB，或字面文本以 ~ 结尾
    auto it = phRe.globalMatch(t);
    QRegularExpressionMatch last;
    while (it.hasNext()) last = it.next();
    const bool lastIsPh = last.hasMatch()
        && last.capturedStart() + last.capturedLength() == t.length();
    if (lastIsPh) {
        if (last.captured(0).startsWith("ZZSB")) return true;
    } else if (t.endsWith(QLatin1Char('~'))) {
        return true;
    }
    return false;
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
    // 表格列对齐记录（每表每列："center"/"right"/"left"/空）：setHtml 前从
    // TipTap 的 <td>/<th> style 提取，步骤 6 分隔行规范化时注回。
    // Qt toMarkdown 从不输出对齐标记（恒 -），必须在此中转，与 Qt 写出
    // 行为解耦（版本无关，见 docs/qt-642-escaping-compat.md 范式）。
    QList<QStringList> tableAligns;
    // 表格整行合并记录（每表 body 行索引列表，步骤 6b 折叠回单格用）：见下方提取块。
    QList<QList<int>> tableFullMergeRows;
    // 段落空行占位符 ZZBL*（&nbsp; 空行方案）：识别空段落（<p></p> / <p><br></p> /
    // <p>&nbsp;</p> 含 NBSP 字符）替换为占位符段落，toMarkdown 后占位符独占一行，
    // 还原时替换为字面 &nbsp; 行（跨编辑器可移植空行）。与 placeholders 分开记录
    // （还原是整行替换而非文本替换）。
    QStringList blankLinePhs;
    QString processed = html;

    // 块级占位符统一包裹为 <p>：占位符若为裸文本节点，setHtml 会把它与相邻块
    // 之间的换行折成尾随空格（"ZZBQ0ZZ "），或与相邻块级占位符合并为一行
    // （"ZZHR0ZZ ZZCB0ZZ"），破坏往返保真。包裹为 <p> 使其成为真正的块级节点，
    // 块间自然以空行分隔，尾随空格与合并两类问题同时消除。
    auto wrapBlock = [](const QString &ph) {
        return QStringLiteral("<p>") + ph + QStringLiteral("</p>");
    };

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
            QString md = blockMathLatexToMarkdown(latex);
            QString ph = QString("ZZBM%1ZZ").arg(n++);
            placeholders << ph;
            backups << md;
            QString wrapped = wrapBlock(ph);
            processed.replace(m.capturedStart(), m.capturedLength(), wrapped);
            offset = m.capturedStart() + wrapped.length();
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
            // 占位符序号须避开输入 HTML/已产出内容中的字面同串（如文档里示例的
            // `ZZCB0ZZ`），否则还原时用户文本被误替换。序号存 placeholders（可非
            // 连续），还原按存储值回填；n 仍作数组序号递增。
            QString ph = collisionFreePlaceholder("CB", n, html, processed);
            ++n;
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
            QString wrapped = wrapBlock(ph);
            processed.replace(blockStart, blockEnd - blockStart, wrapped);
            searchFrom = blockStart + wrapped.length();
        }
    }

    // 1.5) 水平线 <hr> → ---
    //    QTextDocument::toMarkdown 把 <hr> 输出为 "- - -"（空格分隔），且后随
    //    块边界丢失（与相邻段落粘连）；保护为占位符，还原时输出规范 "---"。
    {
        int n = 0;
        int offset = 0;
        QRegularExpression hrRe(R"(<hr\b[^>]*>)");
        while (true) {
            auto m = hrRe.match(processed, offset);
            if (!m.hasMatch()) break;
            QString ph = QString("ZZHR%1ZZ").arg(n++);
            placeholders << ph;
            backups << "---";
            QString wrapped = wrapBlock(ph);
            processed.replace(m.capturedStart(), m.capturedLength(), wrapped);
            offset = m.capturedStart() + wrapped.length();
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
            QString wrapped = wrapBlock(ph);
            processed.replace(m.capturedStart(), m.capturedLength(), wrapped);
            offset = m.capturedStart() + wrapped.length();
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
            QString wrapped = wrapBlock(ph);
            processed.replace(m.capturedStart(), m.capturedLength(), wrapped);
            offset = m.capturedStart() + wrapped.length();
        }
    }

    // 2.5) 合并 TipTap 规范化拆开的相邻行内标记（还原嵌套源码往返）。
    //   ProseMirror 序列化相邻文本 run 时，若后一 run 相比前一 run 新增外层
    //   mark（如 <strong> 包 <s>、<s> 包 <mark>），会先闭合再重开，把共享外层
    //   拆成兄弟元素：
    //     <s>X</s><strong><s>Y</s></strong>     （X=删除线，Y=加粗+删除线）
    //     <s><mark>X</mark></s><mark>Y</mark>   （X=高亮+删除线，Y=高亮）
    //   这是 WYSIWYG 的规范形态，但直接转 MD 会得到 ~~X~~<strong><s>Y</s></strong>
    //   之类的拆分形式，与用户书写的 <s>X<strong>Y</strong></s> /
    //   <mark><s>X</s>Y</mark> 不一致，源码往返失真。此处按 ProseMirror 的
    //   逆操作把兄弟 run 合并回嵌套形态（仅当外层/内层标记严格相邻时），
    //   使源码在 WYSIWYG 往返后保持原样。置于 mark→== 之前：合并产物中的
    //   <mark> 需在此处仍是原始标签，才能被 step 3 转为 ==…==。
    {
        // 共享外层删除线、内层加粗/斜体：<s>X</s><strong><s>Y</s></strong>
        static const QRegularExpression liftOuterStrike(
            R"(<((?:s|del|strike))>(.*?)</\1><((?:strong|b|em|i))><\1>(.*?)</\1></\3>)");
        // 反向：<strong><s>X</s></strong><s>Y</s>
        static const QRegularExpression liftOuterStrikeRev(
            R"(<((?:strong|b|em|i))><((?:s|del|strike))>(.*?)</\2></\1><\2>(.*?)</\2>)");
        // 共享内层高亮、外层删除线：<s><mark>X</mark></s><mark>Y</mark>
        static const QRegularExpression liftInnerMark(
            R"(<((?:s|del|strike))><(mark)>([^<]*)</\2></\1><\2>([^<]*)</\2>)");
        // 反向：<mark>Y</mark><s><mark>X</mark></s>
        static const QRegularExpression liftInnerMarkRev(
            R"(<(mark)>([^<]*)</\1><((?:s|del|strike))><\1>([^<]*)</\1></\3>)");
        // 有限迭代：一次合并可能暴露新的相邻可合并对（如三 run 链式嵌套）
        for (int iter = 0; iter < 20; ++iter) {
            const int before = processed.size();
            processed.replace(liftOuterStrike, R"(<\1>\2<\3>\4</\3></\1>)");
            processed.replace(liftOuterStrikeRev, R"(<\2><\1>\3</\1>\4</\2>)");
            processed.replace(liftInnerMark, R"(<\2><\1>\3</\1>\4</\2>)");
            processed.replace(liftInnerMarkRev, R"(<\1>\2<\3>\4</\3></\1>)");
            if (processed.size() == before) break;
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
            backups << buildInlineCodeMd(unescapeHtmlEntities(m.captured(1)));
            processed.replace(m.capturedStart(), m.capturedLength(), ph);
            offset = m.capturedStart() + ph.length();
        }
        // <span monospace>（QTextDocument 原始输出；字体名动态匹配本平台
        // FixedFont，Windows="Courier New"/macOS="Menlo" 同样识别）
        QRegularExpression spanRe(
            QStringLiteral("<span[^>]*font-family:\\s*'(?:%1)'[^>]*>(.*?)</span>")
                .arg(monoSpanFamilyPattern()),
            QRegularExpression::DotMatchesEverythingOption);
        n = 0;
        offset = 0;
        while (true) {
            auto m = spanRe.match(processed, offset);
            if (!m.hasMatch()) break;
            QString ph = QString("ZZIC%1ZZ").arg(n++);
            placeholders << ph;
            backups << buildInlineCodeMd(unescapeHtmlEntities(m.captured(1)));
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
            // 嵌在通用列表内的 taskList 不在此提取（提升会丢失嵌套缩进），
            // 留给步骤 5b 的 generalListInnerToMd 一并序列化
            if (isInsideGeneralList(processed, m.capturedStart())) {
                offset = m.capturedStart() + 1;
                continue;
            }
            int closeStart = findMatchingUlClose(processed, m.capturedStart());
            if (closeStart < 0) { offset = m.capturedStart() + 1; continue; }
            QString md2 = taskListInnerToMd(
                processed.mid(m.capturedEnd(), closeStart - m.capturedEnd()), 0);
            while (md2.endsWith('\n')) md2.chop(1);
            QString ph = QString("ZZTL%1ZZ").arg(n++);
            placeholders << ph;
            backups << md2;
            int totalLen = closeStart + 5 - m.capturedStart(); // 含 </ul>
            QString wrapped = wrapBlock(ph);
            processed.replace(m.capturedStart(), totalLen, wrapped);
            offset = m.capturedStart() + wrapped.length();
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
            QString wrapped = wrapBlock(ph);
            processed.replace(om.capturedStart(), totalLen, wrapped);
            offset = om.capturedStart() + wrapped.length();
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

    // 0) 删除线 ⚡ 加粗/斜体 互嵌保护（HTML 兜底）。
    //    ~~ 与 **/* 无合法组合能表达嵌套（**粗~~删~~** 被 Qt 拆成两段、内层样式
    //    丢失），检测 <strong>/<em> 内含 <s|del|strike>、或 <s|del|strike> 内含
    //    <strong>/<em>，整个外层保留字面 HTML（TipTap Bold/Italic/Strike mark
    //    直接解析，与 toHtml 侧组合 span → <strong><s>…</s></strong> 对称往返）。
    //    此时 <sub>/<sup>/<mark> 已为占位符：逆序还原时外层占位符先开、内层后开。
    //    （<mark> 含 <s> 走 ==…== 语法，不在此保护——toHtml 的 ==→<mark> 已放宽。）
    {
        int n = 0;
        int offset = 0;
        // <strong>/<em> 内含删除线 → 整层字面 HTML。
        // 删除线含 <s>/<del>/<strike> 与 <span …line-through…> 两种形态
        // （后者是外部 HTML / 旧 toHtml 输出），均触发保护。
        QRegularExpression strongStrikeRe(
            R"(<((?:strong|b|em|i))\b[^>]*>(.*?)</\1>)",
            QRegularExpression::DotMatchesEverythingOption);
        while (true) {
            auto m = strongStrikeRe.match(processed, offset);
            if (!m.hasMatch()) break;
            const bool hasStrike = m.captured(2).contains(
                QRegularExpression(R"(<(?:s|del|strike)\b|<\s*span\b[^>]*line-through)"));
            if (hasStrike) {
                QString ph = QString("ZZNS%1ZZ").arg(n++);
                placeholders << ph;
                backups << m.captured(0);
                processed.replace(m.capturedStart(), m.capturedLength(), ph);
                offset = m.capturedStart() + ph.length();
            } else {
                offset = m.capturedStart() + m.capturedLength();
            }
        }
        // <s|del|strike> 内含加粗/斜体 → 整层字面 HTML
        offset = 0;
        QRegularExpression strikeStrongRe(
            R"(<((?:s|del|strike))\b[^>]*>(.*?)</\1>)",
            QRegularExpression::DotMatchesEverythingOption);
        while (true) {
            auto m = strikeStrongRe.match(processed, offset);
            if (!m.hasMatch()) break;
            const bool hasFmt = m.captured(2).contains(
                QRegularExpression(R"(<(?:strong|b|em|i)\b)"));
            if (hasFmt) {
                QString ph = QString("ZZNS%1ZZ").arg(n++);
                placeholders << ph;
                backups << m.captured(0);
                processed.replace(m.capturedStart(), m.capturedLength(), ph);
                offset = m.capturedStart() + ph.length();
            } else {
                offset = m.capturedStart() + m.capturedLength();
            }
        }
    }

    // 删除线 <s>/<del>/<strike> 及 <span text-decoration:line-through> → ~~...~~
    //   toHtml 已把 ~~x~~ 转为 <s>（TipTap Strike 契约）；此处对称还原。内层
    //   下标 <sub> 已在上一步转为 ZZSB 占位符，随 <s> 一并装入 ZZST，逆序还原时
    //   ZZST 先开、ZZSB 后开（~~H~2~O~~ 逐字往返）。
    {
        int n = 0;
        int offset = 0;
        QRegularExpression strikeRe(R"(<(s|del|strike)\b[^>]*>(.*?)</\1>)",
            QRegularExpression::DotMatchesEverythingOption);
        while (true) {
            auto m = strikeRe.match(processed, offset);
            if (!m.hasMatch()) break;
            QString ph = QString("ZZST%1ZZ").arg(n++);
            placeholders << ph;
            // 内层下标贴删除线分隔符边缘（~X~ 贴 ~~）时 ~~…~~ 不可解析，
            // 保留字面 <s>…</s>（m.captured(0)，含已占位的内层 ZZSB，逆序还原）
            QString inner = m.captured(2);
            backups << (strikeEdgeTildeCollision(inner)
                        ? m.captured(0) : "~~" + inner + "~~");
            processed.replace(m.capturedStart(), m.capturedLength(), ph);
            offset = m.capturedStart() + ph.length();
        }
        // <span text-decoration:line-through>（兼容外部 HTML / 旧 toHtml 输出）
        offset = 0;
        QRegularExpression lineThroughRe(
            R"(<span\b[^>]*\btext-decoration\s*:\s*line-through[^>]*>(.*?)</span>)",
            QRegularExpression::DotMatchesEverythingOption);
        while (true) {
            auto m = lineThroughRe.match(processed, offset);
            if (!m.hasMatch()) break;
            QString ph = QString("ZZST%1ZZ").arg(n++);
            placeholders << ph;
            QString inner = m.captured(1);
            backups << (strikeEdgeTildeCollision(inner)
                        ? m.captured(0) : "~~" + inner + "~~");
            processed.replace(m.capturedStart(), m.capturedLength(), ph);
            offset = m.capturedStart() + ph.length();
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
        // <div>（块级容器，排除 data-type 内部结构）。
        // TipTap taskItem 的结构性内容容器 <div>（紧跟 </label>）是编辑器契约
        // 结构而非用户手写 div，不保护：保留原始标签，由 taskListInnerToMd 的
        // taskItemInlineText 剥离。否则嵌套任务列表（嵌在通用列表内，经步骤
        // 5b 的 generalListInnerToMd 序列化）拿到 ZZDV 占位符，条目文本变成
        // 原始 <div> HTML。
        offset = 0;
        QRegularExpression divRe(
            R"(<div\b(?![^>]*\bdata-type\s*=)[^>]*>(.*?)</div>)",
            QRegularExpression::DotMatchesEverythingOption);
        while (true) {
            auto m = divRe.match(processed, offset);
            if (!m.hasMatch()) break;
            const int di = m.capturedStart();
            if (di >= 8 && processed.mid(di - 8, 8) == QLatin1String("</label>")) {
                offset = di + 1;
                continue;
            }
            QString ph = QString("ZZDV%1ZZ").arg(n++);
            placeholders << ph;
            backups << m.captured();
            processed.replace(m.capturedStart(), m.capturedLength(), ph);
            offset = m.capturedStart() + ph.length();
        }
        // 表格列对齐：TipTap 在 <td>/<th> 标签上携带 style="text-align:..."。
        // Qt setHtml 能导入但 toMarkdown 从不输出对齐标记（恒 -），故按列提取
        // 对齐（列内首个非空值）存入 tableAligns，由步骤 6 注回分隔行。
        // 同时剥离表格内 td/th 与 p 上的 text-align 声明：p 上的若留存会被
        // 下方 ZZA 对齐保护包成 <div align> 占位符，破坏表格结构（单元格内容
        // 变为内嵌 HTML 字面量）。
        QRegularExpression tableRegionRe(
            QStringLiteral("(<table\\b[^>]*>)(.*?)(</table>)"),
            QRegularExpression::DotMatchesEverythingOption);
        offset = 0;
        while (true) {
            auto tm = tableRegionRe.match(processed, offset);
            if (!tm.hasMatch()) break;
            QString region = tm.captured(2);
            QStringList colAligns;
            {
                QRegularExpression rowRe(
                    QStringLiteral("<tr\\b[^>]*>(.*?)</tr>"),
                    QRegularExpression::DotMatchesEverythingOption);
                QRegularExpression cellAttrRe(QStringLiteral("<t[dh]\\b([^>]*)>"));
                // style 的 text-align 优先，其次 align 属性（与 TipTap parseAlign 同序）
                QRegularExpression styleAlignRe(
                    QStringLiteral("\\btext-align\\s*:\\s*(center|right|left)"));
                QRegularExpression attrAlignRe(
                    QStringLiteral("\\balign\\s*=\\s*\"(center|right|left)\""));
                int roff = 0;
                while (true) {
                    auto rm = rowRe.match(region, roff);
                    if (!rm.hasMatch()) break;
                    QString row = rm.captured(1);
                    int coff = 0, col = 0;
                    while (true) {
                        auto cm = cellAttrRe.match(row, coff);
                        if (!cm.hasMatch()) break;
                        while (colAligns.size() < col + 1) colAligns << QString();
                        if (colAligns[col].isEmpty()) {
                            QString attrs = cm.captured(1);
                            auto sm = styleAlignRe.match(attrs);
                            auto am = attrAlignRe.match(attrs);
                            if (sm.hasMatch()) colAligns[col] = sm.captured(1);
                            else if (am.hasMatch()) colAligns[col] = am.captured(1);
                        }
                        coff = cm.capturedEnd();
                        ++col;
                    }
                    roff = rm.capturedEnd();
                }
            }
            // 剥离 text-align 声明（含由此变空的 style 属性）。仅作用于表格
            // 区域：顶层段落/标题的 text-align 由下方 ZZA 继续处理。
            region.replace(
                QRegularExpression(QStringLiteral(
                    "(style\\s*=\\s*\"[^\"]*)\\s*text-align\\s*:\\s*(?:center|right|left)\\s*;?([^\"]*\")")),
                QStringLiteral("\\1\\2"));
            region.replace(
                QRegularExpression(QStringLiteral("\\s*style\\s*=\\s*\"\\s*;?\\s*\"")),
                QString());
            QString replacement = tm.captured(1) + region + tm.captured(3);
            processed.replace(tm.capturedStart(), tm.capturedLength(), replacement);
            tableAligns << colAligns;
            offset = tm.capturedStart() + replacement.length();
        }
        // 提取 tableFullMergeRows：源码单格行（| x |，toHtml 产 <td colspan="N">
        // 占满所有列）经 Qt toMarkdown 会被展开成 N 格，步骤 6b 据此折叠回单格，
        // 保证合并单元格往返保真。与 toHtml protectTableRowForQt 的「恰 1 个显式
        // 单元格 → colspan」规则对偶。判定：数据行首格 colspan == 表头列数（含
        // colspan 求和），即整行占满。
        {
            QRegularExpression tableRegionRe(QStringLiteral("(<table\\b[^>]*>)(.*?)(</table>)"),
                QRegularExpression::DotMatchesEverythingOption);
            QRegularExpression rowRe(QStringLiteral("<tr\\b[^>]*>(.*?)</tr>"),
                QRegularExpression::DotMatchesEverythingOption);
            QRegularExpression cellRe(QStringLiteral("<t[dh]\\b([^>]*)>(.*?)</t[dh]>"),
                QRegularExpression::DotMatchesEverythingOption);
            static const QRegularExpression colspanRe(
                QStringLiteral("\\bcolspan\\s*=\\s*[\"'](\\d+)[\"']"));
            int off = 0;
            while (true) {
                auto tm = tableRegionRe.match(processed, off);
                if (!tm.hasMatch()) break;
                QString region = tm.captured(2);
                int colCount = 0;
                QList<int> mergedRows;
                int roff = 0, rowIdx = 0;
                while (true) {
                    auto rm = rowRe.match(region, roff);
                    if (!rm.hasMatch()) break;
                    QString row = rm.captured(1);
                    if (rowIdx == 0) {
                        // 表头行：列数 = 各格 colspan 之和（无 colspan 即 1）
                        int coff = 0;
                        while (true) {
                            auto cm = cellRe.match(row, coff);
                            if (!cm.hasMatch()) break;
                            auto cs = colspanRe.match(cm.captured(1));
                            colCount += cs.hasMatch() ? cs.captured(1).toInt() : 1;
                            coff = cm.capturedEnd();
                        }
                    } else {
                        // 数据行：首格 colspan == 列数 → 整行合并（0-based body 索引）
                        auto cm = cellRe.match(row);
                        if (cm.hasMatch()) {
                            auto cs = colspanRe.match(cm.captured(1));
                            if (cs.hasMatch() && cs.captured(1).toInt() == colCount)
                                mergedRows << (rowIdx - 1);
                        }
                    }
                    ++rowIdx;
                    roff = rm.capturedEnd();
                }
                tableFullMergeRows << mergedRows;
                off = tm.capturedEnd();
            }
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
        // 空段落保护（&nbsp; 空行方案）：<p></p> / <p><br></p> / <p>&nbsp;</p>
        // （含 NBSP 字符）→ 占位符段落。
        // WYSIWYG 空行 = 空段落（TipTap getHTML 输出 <p></p>；toHtml 的连续空行
        // 保护还原为 <p><br></p>；源码字面 &nbsp; 行经 setMarkdown 成 NBSP 段落）。
        // Qt setHtml 丢弃空段落（多空行塌缩成 1）、把 &nbsp; 实体转成 NBSP 字符
        // （toMarkdown 输出字符而非可移植的 &nbsp; 实体），故在 setHtml 前统一
        // 识别并替换为占位符段落；toMarkdown 后占位符独占一行，还原时替换为
        // 字面 &nbsp; 行——跨编辑器可移植（Joplin/MarkText 等把 &nbsp; 行渲染为空行）。
        // 必须位于 brRe 之前：<p><br></p> 的 <br> 尚未被替换成 ZZBR。
        // 对齐处理之后：带 text-align 的空段落先被 <div align> 保护，不重复识别。
        // 跳过列表容器内（空列表项由列表自绘路径处理，避免空行数重复计算）。
        {
            auto insideListContainer = [](const QString &s, int pos) {
                int lastOpen = -1, lastClose = -1;
                for (const QString &tag : {QStringLiteral("<li"), QStringLiteral("<ul"),
                                            QStringLiteral("<ol")}) {
                    const int o = s.lastIndexOf(tag, pos);
                    if (o > lastOpen) lastOpen = o;
                }
                for (const QString &tag : {QStringLiteral("</li>"), QStringLiteral("</ul>"),
                                            QStringLiteral("</ol>")}) {
                    const int c = s.lastIndexOf(tag, pos);
                    if (c > lastClose) lastClose = c;
                }
                return lastOpen > lastClose;
            };
            // 内容为「空白/NBSP* + 可选 br + 空白/NBSP*」的段落视为空段落。
            // \s 匹配 Unicode 空白（含 NBSP 字符 U+00A0）；&nbsp; 实体单独列备选。
            QRegularExpression emptyPRe(
                R"(<p\b[^>]*>(?:(?:\s|&nbsp;)*(?:<br\s*/?>)?(?:\s|&nbsp;)*)</p>)");
            int blOff = 0, blN = 0;
            while (true) {
                auto m = emptyPRe.match(processed, blOff);
                if (!m.hasMatch()) break;
                const int p = m.capturedStart();
                if (!insideListContainer(processed, p)) {
                    const QString ph = collisionFreePlaceholder(
                        "BL", blN, html, processed);
                    ++blN;
                    blankLinePhs << ph;
                    const QString replacement = QStringLiteral("<p>%1</p>").arg(ph);
                    processed.replace(p, m.capturedLength(), replacement);
                    blOff = p + replacement.length();
                } else {
                    blOff = m.capturedEnd();
                }
            }
        }
        // 行内连续 NBSP（WYSIWYG 多空格）→ 占位符 → 还原 &nbsp; 实体。
        // 用户 WYSIWYG 输入连续空格（ProseMirror 以 NBSP 保留）序列化为
        // &nbsp; 实体；Qt toMarkdown 把实体转成 NBSP 字符（源码不可见、不可移植），
        // 故在 setHtml 前保护为占位符，还原时输出字面 &nbsp; 实体。仅保护连续
        // 2+ 个（单空格往返安全，无需转义）。空段落保护已消费整段 NBSP 的段落，
        // 此处剩余的 &nbsp; 必为行内（与其他文本混排）。
        {
            QRegularExpression nbspRe(R"((?:&nbsp;|\x{00a0}){2,})");
            int nbspOff = 0, nbspN = 0;
            while (true) {
                auto m = nbspRe.match(processed, nbspOff);
                if (!m.hasMatch()) break;
                const QString cap = m.captured(0);
                int cnt = 0;
                for (const QChar &c : cap)
                    if (c == QChar(0x00a0)) ++cnt;
                cnt += cap.count(QStringLiteral("&nbsp;"));
                const QString ph = collisionFreePlaceholder(
                    "NB", nbspN, html, processed);
                ++nbspN;
                placeholders << ph;
                backups << QStringLiteral("&nbsp;").repeated(cnt);
                processed.replace(m.capturedStart(), m.capturedLength(), ph);
                nbspOff = m.capturedStart() + ph.length();
            }
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

    // 5b) 通用列表 <ul>/<ol>（非 taskList）→ GFM markdown。
    //     Qt toMarkdown 对 3 层以上嵌套列表的缩进输出不自洽（见 generalListInnerToMd
    //     注释），故与任务列表一样在 setHtml 前自绘，占位符 toMarkdown 后还原。
    //     置于 step N（span/u/kbd/small/div/br 占位保护）之后：列表条目内的
    //     <span>/<u> 等已是 ZZ* 占位符，taskItemInlineText 剥离标签时不误伤；
    //     taskList 的 <ul data-type> 已在 step 5 提取，此处剩余 <ul> 均为通用。
    {
        int n = 0;
        int offset = 0;
        QRegularExpression ulRe(
            "<ul\\b(?![^>]*\\bdata-type\\s*=\\s*\"taskList\")[^>]*>");
        QRegularExpression olRe("<ol\\b[^>]*>");
        QRegularExpression startRe("\\bstart\\s*=\\s*\"?(\\d+)\"?");
        while (true) {
            auto um = ulRe.match(processed, offset);
            auto om = olRe.match(processed, offset);
            const int up = um.hasMatch() ? um.capturedStart() : -1;
            const int op = om.hasMatch() ? om.capturedStart() : -1;
            if (up < 0 && op < 0) break;
            bool ordered;
            QRegularExpressionMatch m;
            int p;
            if (op < 0 || (up >= 0 && up < op)) { ordered = false; m = um; p = up; }
            else { ordered = true; m = om; p = op; }
            int close = findMatchingListClose(processed, p, ordered);
            if (close < 0) { offset = p + 1; continue; }
            int start = 1;
            if (ordered) {
                auto sm = startRe.match(m.captured());
                if (sm.hasMatch()) start = sm.captured(1).toInt();
            }
            QString md2 = generalListInnerToMd(
                processed.mid(m.capturedEnd(), close - m.capturedEnd()),
                0, ordered, start);
            while (md2.endsWith('\n')) md2.chop(1);
            QString ph = QString("ZZGL%1ZZ").arg(n++);
            placeholders << ph;
            backups << md2;
            int totalLen = close + 5 - p;   // </ul>/</ol> 均为 5 字符
            QString wrapped = wrapBlock(ph);
            processed.replace(p, totalLen, wrapped);
            offset = p + wrapped.length();
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

    // 3.5) 还原段落空行占位符 ZZBL：占位符独占的行 → 字面 &nbsp; 行。
    //      toMarkdown 输出为「段落 + 空行 + ZZBLnZZ + 空行 + 段落」，占位符行
    //      替换为 &nbsp; 后即成为跨编辑器可移植的空行（Joplin/MarkText 等把
    //      &nbsp; 行渲染为空行；纯空行会被标准 md 渲染器折叠）。M 个空段落 →
    //      M 个 &nbsp; 行，与 WYSIWYG 空段落数一致。
    //      必须位于 \n{3,} 压缩与反换行之后：此时刻占位符均单行文本，替换才
    //      不破坏块结构；若放压缩之前，还原出的多换行会被压缩成 1 空行。
    if (!blankLinePhs.isEmpty()) {
        QStringList lines = md.split('\n');
        QStringList out;
        out.reserve(lines.size());
        for (const QString &ln : lines) {
            if (blankLinePhs.contains(ln.trimmed()))
                out << QStringLiteral("&nbsp;");
            else
                out << ln;
        }
        md = out.join(QLatin1Char('\n'));
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
    //    统一为 | cell |（单元格内容前后各一个空格），分隔行统一为 | --- |，
    //    并按 setHtml 前提取的列对齐（tableAligns）注回 GFM 对齐标记：
    //    居中 :---: / 右对齐 ---: / 显式左 :---（文档顺序逐表对应，列数一致才注入）。
    {
        int tableIdx = 0;
        QStringList lines = md.split('\n');
        for (int i = 0; i < lines.size(); ++i) {
            QString line = lines[i];
            QString t = line.trimmed();
            // 仅处理表格行（以 | 开头且以 | 结尾，非 blockquote 前缀 > |）
            if (!t.startsWith('|') || !t.endsWith('|')) continue;

            // 去掉首尾 | → 拆分为单元格（反引号感知：行内代码内的 | 与
            // 转义 \| 不作为分隔符，故含反引号的行同样参与规范化）
            QString inner = t.mid(1, t.length() - 2);
            QStringList cells = splitTableCells(inner);
            bool isSep = false;
            for (auto &cell : cells) {
                cell = cell.trimmed();
                // Qt toMarkdown 把分隔行输出为单连字符（|-|-|），故匹配 1+ 连字符
                // （含 :---: 对齐写法），而非 3+。
                if (QRegularExpression("^:?-+:?$").match(cell).hasMatch())
                    isSep = true;
            }
            if (isSep) {
                // 分隔行：按提取的列对齐注回标记（列数一致才消费，防误判
                // 分隔行导致与后续表格错位），每格 :---: / ---: / :--- / ---
                QStringList aligns;
                if (tableIdx < tableAligns.size()
                        && tableAligns[tableIdx].size() == cells.size()) {
                    aligns = tableAligns[tableIdx];
                    ++tableIdx;
                }
                QStringList sepCells;
                for (int k = 0; k < cells.size(); ++k) {
                    QString a = (k < aligns.size()) ? aligns[k] : QString();
                    if (a == QLatin1String("center")) sepCells << ":---:";
                    else if (a == QLatin1String("right")) sepCells << "---:";
                    else if (a == QLatin1String("left")) sepCells << ":---";
                    else sepCells << "---";
                }
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

    // 6b) 整行合并单元格还原：Qt toMarkdown 把单格行（| x |）产出的
    //     <td colspan="N"> 展开成 N 格，此处按 setHtml 前记录的 tableFullMergeRows
    //     折叠回单格（| x |），与 toHtml protectTableRowForQt 的「恰 1 个显式
    //     单元格 → colspan」规则对偶，保证合并单元格往返保真。
    //     按连续 |...| 行识别表格区域（与步骤 6 一致），行序表头/分隔/body 一一对应。
    if (!tableFullMergeRows.isEmpty()) {
        int tableIdx = 0;
        QStringList lines = md.split('\n');
        int i = 0;
        while (i < lines.size()) {
            QString t = lines[i].trimmed();
            if (!(t.startsWith('|') && t.endsWith('|'))) { ++i; continue; }
            QList<int> run;   // 当前表格的连续表行索引
            while (i < lines.size()) {
                QString lt = lines[i].trimmed();
                if (!(lt.startsWith('|') && lt.endsWith('|'))) break;
                run << i;
                ++i;
            }
            // 仅真表格（表头 + 分隔行，run[1] 全为 - 等连字符）才消耗 tableIdx；
            // 段落里的孤立 | x | 行（无分隔行）不是表格，跳过以防与 HTML 表计数错位。
            static const QRegularExpression sepCellRe(QStringLiteral("^:?-+:?$"));
            bool isRealTable = run.size() >= 2;
            if (isRealTable) {
                QString sepInner = lines[run[1]].trimmed();
                if (sepInner.startsWith('|') && sepInner.endsWith('|'))
                    sepInner = sepInner.mid(1, sepInner.length() - 2);
                else
                    isRealTable = false;
                if (isRealTable) {
                    const QStringList sepCells = splitTableCells(sepInner);
                    for (const QString &sc : sepCells)
                        if (!sepCellRe.match(sc.trimmed()).hasMatch()) { isRealTable = false; break; }
                }
            }
            if (!isRealTable)
                continue;
            if (tableIdx < tableFullMergeRows.size()) {
                const QList<int> &merged = tableFullMergeRows[tableIdx];
                for (int r = 2; r < run.size(); ++r) {   // run[0]=表头 run[1]=分隔 run[2..]=body
                    if (!merged.contains(r - 2))
                        continue;
                    QString inner = lines[run[r]].trimmed();
                    inner = inner.mid(1, inner.length() - 2);
                    QStringList cells = splitTableCells(inner);
                    lines[run[r]] = "| " + cells.value(0).trimmed() + " |";
                }
            }
            ++tableIdx;
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
    // 清除文档开头的空行伪影：QTextDocument::toMarkdown 对文档首个表格会前置
    // 一个空行（块间分隔），往返后残留为多余首行空行，破坏往返保真。
    while (md.startsWith('\n'))
        md.remove(0, 1);
    return md;
}
