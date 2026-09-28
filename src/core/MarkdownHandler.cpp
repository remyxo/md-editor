#include "MarkdownHandler.h"
#include <QTextDocument>
#include <QRegularExpression>
#include <QSet>
#include <QVector>
#include <QFontDatabase>
#include <initializer_list>
#include "qt/QtMarkdownWriter.h"

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
    // 进程级缓存：QFontDatabase 查询 + escape + join 曾在每次调用时重建
    // （toHtml/fromHtml 各 1-2 次，fromHtml 的脚注内容递归逐层放大）；
    // 系统固定字体在进程生命周期内不变
    static const QString cached = []() {
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
    }();
    return cached;
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

// 判断是否为独立成行的字面 &nbsp; 实体（用户显式空段标记）：trimmed 后恰为
// 单个 &nbsp;。WYSIWYG 空段落对应源码里的字面 &nbsp; 行（跨编辑器可移植），
// toHtml 须把它当作空行生成占位符（还原为空段落），否则 Qt setMarkdown 把
// 它当同一段落内的软换行（&nbsp; 前无空行分隔时），往返后空段丢失/失真。
// &nbsp;&nbsp; 等多实体是行内连续空格语义，不属于空行，不匹配。
static bool isNbspLiteralLine(const QString &line)
{
    return line.trimmed() == QLatin1String("&nbsp;");
}

// 水平线标签行（<hr> / <hr/> / <hr />，可带首尾空白，大小写不敏感）。
// 与 --- 同语义（帮助文档约定「<hr> 与 --- 等价」），须同样占位保护：
// <hr> 无 </hr> 配对，不占位会被 escapeUnclosedHtmlTags 当未闭合容器标签
// 转义为字面文本（往返变 \<hr\>，帮助文档承诺失效）。行内混排的 <hr>
// （text <hr> text）不在此列，保持现状按文本转义。
static bool isHrTagLine(const QString &line)
{
    static const QRegularExpression re(
        "^\\s*<hr\\s*/?\\s*>\\s*$",
        QRegularExpression::CaseInsensitiveOption);
    return re.match(line).hasMatch();
}

// 顶层围栏行判定（CommonMark 全族）：≤3 空格缩进 + ≥3 个连续同种围栏字符
// （反引号或波浪号）。此前只认零缩进反引号围栏，~~~ 波浪围栏与合法缩进围栏
// 整族漏保护——围栏行与内容行落入 BRMARKER 段落逻辑被注入 <br data-brk>，
// fromHtml 剥离后代码行无分隔拼接、语言信息串被打碎。
// 返回 false = 非围栏行；true 时 *indent/*run/*ch 给出缩进列、围栏长度与字符。
static bool isTopLevelFenceLine(const QString &line, int *indent, int *run, QChar *ch)
{
    int i = 0;
    while (i < line.size() && line[i] == QLatin1Char(' '))
        ++i;
    if (i > 3 || i >= line.size())
        return false;
    const QChar c = line[i];
    if (c != QLatin1Char('`') && c != QLatin1Char('~'))
        return false;
    int r = 0;
    while (i + r < line.size() && line[i + r] == c)
        ++r;
    if (r < 3)
        return false;
    *indent = i;
    *run = r;
    *ch = c;
    return true;
}

// md 行为顶层代码围栏行时更新围栏状态（fromHtml 产物恒为反引号围栏）。
// 调用方循环携带 inFence/fenceLen：进围栏置位，出围栏（纯围栏行、长度 ≥
// 开启围栏）复位。返回 true = 本行是围栏行（跳过其余处理）
static bool updatesFenceState(const QString &trimmed, bool &inFence, int &fenceLen)
{
    if (!trimmed.startsWith("```"))
        return false;
    int len = 0;
    while (len < trimmed.size() && trimmed[len] == QLatin1Char('`'))
        ++len;
    if (!inFence) {
        inFence = true;
        fenceLen = len;
    } else if (len >= fenceLen && trimmed.mid(len).isEmpty()) {
        inFence = false;
    }
    return true;
}

static bool isParagraphLine(const QString &line)
{
    if (line.isEmpty()) return false;
    // 字面 &nbsp; 空行（用户显式空段）：非正文行。否则相邻正文行会把它当
    // 下一段落行触发软换行（ZZBR → <br data-brk>）——toHtml 空行分支已把
    // &nbsp; 行当空行处理生成占位符，此处须排除避免重复/错位标记。
    if (isNbspLiteralLine(line)) return false;
    QString trimmed = line.trimmed();
    // 纯空白行（仅空格/制表符）＝空行（块边界），非正文行——
    // CommonMark 视其为 blank line；且 trimmed 为空时取 [0] 越界触发
    // Q_ASSERT（Debug 构建直接 abort，commonmark 语料 111/112/117/227 即此）
    if (trimmed.isEmpty()) return false;
    QChar c = trimmed[0];
    if (c == '`') {
        if (!trimmed.startsWith("```"))
            return true;
        return false;
    }
    // ~~~ 波浪围栏行：与 ``` 对称排除（段落行不应把后续围栏行当延续——
    // 顶层围栏是块边界，非软换行）
    if (c == '~') {
        if (!trimmed.startsWith("~~~"))
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
    // 水平线标签行（<hr>）：非正文行。否则「正文\n<hr>\n正文」会把 <hr> 行当
    // 段落延续拼软换行标记，<hr> 不再走占位保护（escapeUnclosedHtmlTags
    // 转义为文本，帮助文档「<hr> 与 --- 等价」失效）。
    if (isHrTagLine(line)) return false;
    // 多位有序列表编号（10. / 999. / 10)）：与 isListItemLine 的 \d{1,9}[.)]
    // 对齐——曾只查 trimmed[1]=='.'，10 起的编号被判为正文行，段落边界插入
    // 软换行标记（与单位编号行为分叉）。编号后须空白或行尾（1.5 小数仍是正文）
    static const QRegularExpression ordListRe("^\\d{1,9}[.)](?:\\s|$)");
    return c != '#' && c != '-' && c != '|'
           && !(c >= '0' && c <= '9'
                && ordListRe.match(trimmed).hasMatch());
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
// 前向声明：定义见下方（占位符碰撞检测生成器，collisions 为调用方维护的索引集）
static QString collisionFreePlaceholder(QSet<QString> &collisions,
                                        const QString &name, int base);

static QString protectTableRowForQt(const QString &line, int &colCount,
                                    QStringList &cellBackups, QStringList &cellPhs,
                                    QSet<QString> &collisions)
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
            // 占位符序号须避开用户字面同串（碰撞检测），实际串存 cellPhs，
            // 还原时按存储值精确回填（不再用 ZZTC\d+ 通配正则误伤字面文本）
            QString ph = collisionFreePlaceholder(collisions, "TC", cellPhs.size());
            cellPhs << ph;
            cellBackups << ph;
            padded << (" " + ph + " ");
        } else {
            padded << (" " + cc + " ");
        }
    }
    while (padded.size() < colCount) {
        QString ph = collisionFreePlaceholder(collisions, "TC", cellPhs.size());
        cellPhs << ph;
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

// HTML 属性值 / 元素文本转义，与 unescapeHtmlEntities 成对。
// 用于把**来自文档的文本**拼进 HTML 属性或元素内容（块级公式 latex、代码围栏
// 信息串、脚注 id）。此前这些位置直接裸拼 + 拼接，缺转义造成两类破坏：
//   ① 值含 " → 属性提前终结，后续内容变成游离属性/文本：`$$\text{"hi"}$$` 产出
//      data-latex="\text{"hi"}"，浏览器解析出的 latex 只剩 `\text{`，公式被截断；
//      脚注 [^a"b] 的 id 被截断成 a（引用与定义同时改名）。
//   ② 值含 & / < / > → 经 TipTap getHTML() 的 innerHTML 序列化二次转义（&→&amp;），
//      与 fromHtml 侧的实体解码不对称，往返逐轮膨胀（&amp;→&amp;amp;）。
// 顺序：& 必须先转，否则会二次转义后续插入的实体起始符。
static QString escapeHtmlAttr(const QString &s)
{
    QString r = s;
    r.replace(QLatin1Char('&'), QStringLiteral("&amp;"));
    r.replace(QLatin1Char('"'), QStringLiteral("&quot;"));
    r.replace(QLatin1Char('<'), QStringLiteral("&lt;"));
    r.replace(QLatin1Char('>'), QStringLiteral("&gt;"));
    return r;
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
// collisions 集由调用方预置：源文档（markdown/html）中全部 ZZ\w+ZZ 形态子串
// （buildPlaceholderCollisionSet 单次 O(n) 扫描），并在每次生成后 insert 本次串，
// 使碰撞检测降为 O(1)。旧实现每次对 markdown/processed 各做一次全串 contains ——
// 10k 行文档约 4000 次调用 × 2 × ~1MB ≈ 8GB 字符扫描，占往返耗时大头。
static QString collisionFreePlaceholder(QSet<QString> &collisions,
                                        const QString &name, int base)
{
    int n = base;
    QString ph;
    do {
        ph = QString("ZZ%1%2ZZ").arg(name).arg(n++);
    } while (collisions.contains(ph));
    collisions.insert(ph);
    return ph;
}

// 收集源文档中全部 ZZ\w+ZZ 形态子串，作为占位符碰撞检测的索引。
// 语义等价于对每个候选占位符串做 source.contains(ph)（集合成员判断 O(1)）。
// 占位符形如 ZZ<名><n>ZZ（名为字母数字、序号为数字），\w 覆盖之；用户文档中
// 字面的同形态文本同样被收录，恰好用于避免与之碰撞。
static QSet<QString> buildPlaceholderCollisionSet(const QString &source)
{
    static const QRegularExpression zzRe("ZZ[A-Za-z0-9_]+ZZ");
    QSet<QString> set;
    auto it = zzRe.globalMatch(source);
    while (it.hasNext())
        set.insert(it.next().captured());
    return set;
}

#ifndef QT_NO_DEBUG
// 占位符残留金丝雀（仅 debug 构建）：还原完成后，本次转换生成的每个占位符
// 都必须已从输出中消失。
// 性能：输出串只扫一遍（通用 token 模式 globalMatch，总成本 O(n)），命中候选
// 查集合（O(1)/个）——替代旧「逐占位符 output.contains」的 O(K·n)：3MB 文档
// 2.4 万占位符时旧法要烧 20+ 秒主线程 CPU，直接顶满 GNOME「应用无响应」阈值。
// 语义不变：用户文档可合法地字面包含 ZZ* 文本（见上方 collisionFreePlaceholder
// 注释），只有「呈 ZZ token 形态 且 属于本次生成集合」才判泄漏，用户字面不误报
static void assertNoPlaceholderLeak(
    const QString &output,
    std::initializer_list<const QStringList *> phLists)
{
    QSet<QString> generated;
    for (const QStringList *list : phLists)
        for (const QString &ph : *list)
            generated.insert(ph);
    if (generated.isEmpty())
        return;
    static const QRegularExpression tokenRe(R"(ZZ[A-Z]+[0-9]+ZZ)");
    auto it = tokenRe.globalMatch(output);
    while (it.hasNext()) {
        const QString tok = it.next().captured();
        Q_ASSERT_X(!generated.contains(tok), "MarkdownHandler",
                   qPrintable(QStringLiteral("占位符残留泄漏: ") + tok));
    }
}
#endif

// 单遍重建：把 s 中 re 的全部匹配替换为 fn(match) 的返回串，总成本 O(n)。
// 替代「循环内 re.match(s, off) + s.replace(pos, len)」的反模式——Qt 的
// match(s, offset) 每次调用都从 offset 重新扫描整个字符串（O(n)/次），K 个
// 匹配就是 O(K·n)；s.replace(pos, len) 每次又做 O(n) 尾部移位。本函数在原始
// 串上先行全局匹配（globalMatch 总成本 O(n)），替换串不参与二次扫描，一次
// 拼接完成重建。行为与逐次 match+replace 等价（匹配序列一致、替换文本不互扰）。
template <typename F>
static void globalReplaceWith(QString &s, const QRegularExpression &re, F fn)
{
    QString result;
    result.reserve(s.size());
    int last = 0;
    auto it = re.globalMatch(s);
    while (it.hasNext()) {
        auto m = it.next();
        result += s.mid(last, m.capturedStart() - last);
        result += fn(m);
        last = m.capturedEnd();
    }
    result += s.mid(last);
    s = result;
}

// 单遍重建（区域消费版）：适用于「匹配到开标签后由平衡扫描确定整个消费区域」
// 的提取循环（任务列表/脚注容器/style span 等）。fn(m, replacement, consumedTo)
// 返回 true 表示消费 [m.capturedStart(), consumedTo) 并以 replacement 替换；
// 返回 false 表示跳过——保留原文，消费至 m.capturedEnd()。
// 与旧 while(true){ re.match(s,off); s.replace(...) } 循环的等价性依据：
// ① 匹配在原始串一次收集（globalMatch O(n)），替换串/占位符不参与后续扫描
//   （占位符形态 ZZxxNNZZ 不含标签语法，不会命中）；
// ② 消费区域必为平衡结构（开标签..配对闭标签），落在区域内的后续匹配在旧
//   流程中该区域已被占位符替换、不可能再命中，此处以 capturedStart() < last
//   跳过，语义一致；
// ③ fn 内的上下文/平衡扫描读原始串：旧流程中先前消费区域已替换为占位符
//   （无标签），本流程是原始标签——但消费区域内各标签自平衡，对深度/配对
//   判定无净影响。
template <typename F>
static void globalExtractWith(QString &s, const QRegularExpression &re, F fn)
{
    QString result;
    result.reserve(s.size());
    int last = 0;
    auto it = re.globalMatch(s);
    while (it.hasNext()) {
        const auto m = it.next();
        if (m.capturedStart() < last)
            continue;   // 已消费区域内
        QString replacement;
        int consumedTo = m.capturedEnd();
        if (fn(m, replacement, consumedTo)) {
            result += s.mid(last, m.capturedStart() - last);
            result += replacement;
            last = consumedTo;
        }
    }
    result += s.mid(last);
    s = result;
}

// toHtml 侧 <div>（非 data-type）占位暂存的单遍重建（递归）。
// 旧循环的跳过分支（捕获文本含 \x01 行内代码占位符时 offset=start+1 重搜）
// 具重叠语义：本正则无 DotMatchesEverything（行限定）且内层为懒匹配，被跳过
// 捕获必以 </div> 结尾、其中重命中的配对闭标签不会晚于该结尾，故重命中必
// 落于被跳过捕获的内部——在 mid(1) 上递归单遍处理即可，发现序与占位符
// 编号与旧循环一致。
static QString stashPlainDivs(const QString &seg, QSet<QString> &collisions,
                              QStringList &divBackups, QStringList &divPhs)
{
    static const QRegularExpression divRe(
        R"(<div\b(?![^>]*\bdata-type\s*=)[^>]*>(.*?)</div>)");
    QString out;
    out.reserve(seg.size());
    int last = 0;
    auto it = divRe.globalMatch(seg);
    while (it.hasNext()) {
        const auto m = it.next();
        if (m.capturedStart() < last)
            continue;   // 已消费区域内（嵌套 div 已随外层入备份）
        out += seg.mid(last, m.capturedStart() - last);
        if (m.captured().contains(QChar(1))) {
            // 跳过：保留首字符，段内（去首字符）递归重搜（保旧 +1 重叠语义）
            out += m.captured().left(1)
                 + stashPlainDivs(m.captured().mid(1), collisions, divBackups, divPhs);
        } else {
            divPhs << collisionFreePlaceholder(collisions, "DV", divPhs.size());
            divBackups << m.captured();
            out += divPhs.last();
        }
        last = m.capturedEnd();
    }
    out += seg.mid(last);
    return out;
}
// 空 alt 图片保形（toHtml 侧，全文档范围）。
// Qt setMarkdown 对 alt 为空/空白的图片**整条丢弃**（实测 `![](a.png)` 只产出
// 空段落，`![x](a.png)` 正常），图片在「打开→保存」一次往返后从文档消失
// （.md 原语法被写成 &nbsp;）。此处把空 alt 换成占位符再交给 setMarkdown，
// 产出 <img alt="ZZIA0ZZ" …> 后由后处理 -1 清空 alt 还原语义（图片保住、
// alt 语义仍为空）。覆盖独占行/段落内/引用内/列表内全部位置。
// 行内代码段内的 `![](x)` 是字面文本（CommonMark 规定不解码、不解析标签），
// 先按 escapeUnclosedHtmlTags 同款规则暂存，避免被当成真图片改写。
static void protectEmptyAltImages(QString &processed,
                                  QSet<QString> &collisions,
                                  QStringList &sentinels)
{
    static const QRegularExpression csRe("(`+)([^`\n]+)\\1(?!`)");
    QStringList codes;
    globalReplaceWith(processed, csRe,
        [&codes](const QRegularExpressionMatch &m) {
            codes << m.captured(0);
            return "\x01EUI" + QString::number(codes.size() - 1) + "\x01";
        });
    static const QRegularExpression imgRe(
        R"(!\[([^\]\n]*)\]\(([^)\n]*)\))");
    globalReplaceWith(processed, imgRe,
        [&collisions, &sentinels](const QRegularExpressionMatch &m) {
            if (!m.captured(1).trimmed().isEmpty())
                return m.captured(0);   // 非空 alt：Qt 正常转换，不动
            const QString ph = collisionFreePlaceholder(
                collisions, "IA", sentinels.size());
            sentinels << ph;
            return "![" + ph + "](" + m.captured(2) + ")";
        });
    for (int i = 0; i < codes.size(); ++i)
        processed.replace("\x01EUI" + QString::number(i) + "\x01", codes[i]);
}

// 空 alt 图片的 alt 占位注入（fromHtml 侧，与 toHtml 的 ZZIA 保形对偶）。
// vendored Qt writer 对空 alt 的图片输出 ![image](src)——凭空造出 alt 文本，
// 使 ![](src) 无法字节往返。此处给「无 alt 或 alt 为空」的 <img> 补占位 alt，
// toMarkdown 之后由调用侧统一替换回 ![](...)。
// full 为完整标签串，attrs 为其属性部分（tagRe 的第 3 组，含首空格与可能的
// 结尾 '/'）。非空 alt 原样返回。
static QString injectEmptyAltSentinel(const QString &full, const QString &attrs,
                                      QSet<QString> &collisions,
                                      QStringList &sentinels)
{
    static const QRegularExpression altRe(
        QStringLiteral("\\balt\\s*=\\s*\"([^\"]*)\""));
    const auto am = altRe.match(attrs);
    if (am.hasMatch() && !am.captured(1).isEmpty())
        return full;   // 已有非空 alt：不动
    const QString ph = collisionFreePlaceholder(collisions, "IB", sentinels.size());
    sentinels << ph;
    if (am.hasMatch()) {
        // alt="" → 就地替换属性值。base 为属性段在 full 中的起点（tagRe 的
        // 第 3 组紧接标签名，full 比 attrs 多标签名与结尾 '>'）
        const int base = full.indexOf(attrs);
        QString out = full;
        out.replace(base + am.capturedStart(1), am.capturedLength(1), ph);
        return out;
    }
    // 无 alt 属性：在标签名之后插入
    const int nameEnd = full.indexOf(QLatin1Char(' '), 1);
    const int insertAt = (nameEnd < 0) ? full.indexOf(QLatin1Char('>'), 1)
                                       : nameEnd;
    if (insertAt < 0) return full;
    QString out = full;
    out.insert(insertAt, QStringLiteral(" alt=\"") + ph + QLatin1Char('"'));
    return out;
}


// 批量还原占位符为备份内容（phs 与 backups 一一对应，序号可非连续）：
// 两遍单遍重建，替代「逐占位符全串 replace」的 O(K·n) 模式。
//   第 1 遍：剥除 <p …> 包裹（toHtml 占位符由 setMarkdown 独立成段）后替换；
//   第 2 遍：兜底还原未独立成段（混排文本/标题内）的裸占位符。
// 第 2 遍不会误伤第 1 遍已还原的占位符（已随 <p> 一起消失），backups 中也不含
// 占位符形态的文本。
// 正则用通用占位符模式 ZZ[A-Z]+[0-9]+ZZ + 哈希查表（同 fromHtml 侧多趟还原
// 范式），不按占位符个数拼接 alternation：约 3000+ 个占位符时巨型多选正则
// 超出 PCRE2 编译限制，构造出 invalid QRegularExpression，还原空转致占位符
// 泄漏（Debug 构建触发泄漏断言 abort）。碰撞检测保证真占位符不与用户文本
// 同串，通用模式多匹配到的必然是用户字面 ZZ 形态文本——查表未命中须原样保留。
static void batchRestorePlaceholders(QString &s, const QStringList &phs,
                                     const QStringList &backups)
{
    if (phs.isEmpty())
        return;
    QHash<QString, QString> map;
    map.reserve(phs.size());
    for (int i = 0; i < phs.size(); ++i)
        map.insert(phs[i], backups[i]);

    static const QRegularExpression pWrapRe(
        R"(<p[^>]*>(ZZ[A-Z]+[0-9]+ZZ)</p>)");
    static const QRegularExpression phRe(R"(ZZ[A-Z]+[0-9]+ZZ)");
    globalReplaceWith(s, pWrapRe,
        [&map](const QRegularExpressionMatch &m) {
            auto it = map.constFind(m.captured(1));
            return it != map.constEnd() ? *it : m.captured(0);
        });
    globalReplaceWith(s, phRe,
        [&map](const QRegularExpressionMatch &m) {
            auto it = map.constFind(m.captured());
            return it != map.constEnd() ? *it : m.captured(0);
        });
}

// 批量裸串精确替换（不剥 <p> 包裹）：search/repl 一一对应，单遍重建 O(n)。
// 供内联占位符（ZZHI/ZZES/ZZFR/ZZSB 等）还原——这些占位符嵌在行内文本中，
// 还原时应保留其所在段落结构，不做 <p> 包裹剥离。
// 契约：search 必须全部为 ZZ 占位符 token 形态（ZZ<大写字母><序号>ZZ），
// 还原用通用 token 正则 + 哈希查表（不按个数拼 alternation，原因同
// batchRestorePlaceholders），查表未命中的用户字面 ZZ 形态文本原样保留。
static void batchReplaceExact(QString &s, const QStringList &search,
                              const QStringList &repl)
{
    if (search.isEmpty())
        return;
    QHash<QString, QString> map;
    map.reserve(search.size());
    for (int i = 0; i < search.size(); ++i)
        map.insert(search[i], repl[i]);
    static const QRegularExpression phRe(R"(ZZ[A-Z]+[0-9]+ZZ)");
    globalReplaceWith(s, phRe,
        [&map](const QRegularExpressionMatch &m) {
            auto it = map.constFind(m.captured());
            return it != map.constEnd() ? *it : m.captured(0);
        });
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
// 从 from 起找 </name>（词边界：后随字符非字母数字/_/-）。indexOf 线性扫替代
// 正则：一是免每标签现构造 QRegularExpression（含 JIT 编译），二是无闭合时
// 正则 match 会扫到串尾、indexOf 的实现远快于正则引擎（曾 O(K·n) 主热点）
static bool hasClosingTag(const QString &s, const QString &nameL, int from)
{
    const QString needle = QStringLiteral("</") + nameL;
    int pos = from;
    while ((pos = s.indexOf(needle, pos, Qt::CaseInsensitive)) >= 0) {
        const int after = pos + needle.length();
        const bool boundary = after >= s.length()
            || !(s.at(after).isLetterOrNumber() || s.at(after) == QLatin1Char('_')
                 || s.at(after) == QLatin1Char('-'));
        if (boundary)
            return true;
        pos = after;
    }
    return false;
}

static void escapeUnclosedHtmlTags(QString &s)
{
    // 行内代码段先暂存：code span 内是字面文本——CommonMark 规定实体不解码、
    // 标签不解析，`<tag>` 不是 HTML 标签，不得转义。否则 `<hr>` 等行内代码
    // 显示为字面 &lt;hr&gt;（md4c 不解码 code 内实体，转义产物直通显示；帮助
    // 文档语法指南的「`<hr>` 与 --- 等价」曾因此显示错误）。哨兵用独立前缀
    // \x01EUT——\x01CODE* 是强调包裹 code 机制跨函数传递的占位符（后处理 0
    // 消费），此处不得撞号。分隔符判定须与 md4c 的 CommonMark run 规则一致
    // （开/闭 run 等长、闭 run 后不紧跟反引号）：简单成对 `` `x` `` 会在
    // `` ``` ```<script> ``` `` 这类多反引号串中错配内层，把 md4c 判定为裸露
    // 正文的 <script> 暂存起来，转义循环看不见 → Qt 吞噬其后全文。
    QStringList codes;
    static const QRegularExpression csRe("(`+)([^`\n]+)\\1(?!`)");
    int csOff = 0;
    while (true) {
        const auto cm = csRe.match(s, csOff);
        if (!cm.hasMatch()) break;
        codes << cm.captured(0);   // 含反引号的整个 span（还原时原样回填）
        const QString ph = "\x01EUT" + QString::number(codes.size() - 1) + "\x01";
        s.replace(cm.capturedStart(), cm.capturedLength(), ph);
        csOff = cm.capturedStart() + ph.length();
    }

    static const QRegularExpression tagRe("(?<![\\&\\\\])<[a-zA-Z/][^<>]*>");
    static const QRegularExpression nameEndRe("[\\s>]");
    int off = 0;
    while (true) {
        auto m = tagRe.match(s, off);
        if (!m.hasMatch()) break;
        const QString tok = m.captured();
        const bool isClosing = tok.startsWith("</");
        // 提取标签名（<b> → b，<a href> → a，</div> → div）
        int nameEnd = tok.indexOf(nameEndRe, 1);
        if (nameEnd < 0) nameEnd = tok.length() - 1;
        const QString name = isClosing ? tok.mid(2, nameEnd - 2) : tok.mid(1, nameEnd - 1);
        const QString nameL = name.toLower();
        const bool isBr = nameL == "br";
        const bool isSelfClosing = tok.contains("/>");
        // 短路序：isBr/isSelfClosing 先于闭合检测——曾对每个 <br>/<img> 也
        // 现构造 QRegularExpression（编译 + JIT）并全尾扫描，粘贴大 HTML 平方级
        if (isClosing || isBr || isSelfClosing) {
            off = m.capturedEnd();
            continue;
        }
        const bool hasClose = hasClosingTag(s, nameL, m.capturedEnd());
        if (hasClose) {
            off = m.capturedEnd();
            continue;
        }
        // 未闭合容器标签 → 转义 < > 为实体，Qt 不再当标签解析
        const QString inner = tok.mid(1, tok.length() - 2);
        s.replace(m.capturedStart(), m.capturedLength(), "&lt;" + inner + "&gt;");
        off = m.capturedStart() + inner.length() + 8;   // &lt;(4) + inner + &gt;(4)
    }

    // 还原行内代码段（内容未参与转义）
    for (int i = 0; i < codes.size(); ++i)
        s.replace("\x01EUT" + QString::number(i) + "\x01", codes[i]);
}

// 提取下标 ~x~（Pandoc 语法）为占位符。
// Qt 的 markdown 解析器把单个 ~x~ 当作删除线（text-decoration:line-through），
// 必须在 setMarkdown 前替换为占位符，后处理时再还原为 <sub>。
// 负向断言 (?<!~)~(?!~) 排除删除线 ~~x~~；内容存入 out，后处理时 HTML 转义还原。
// 行内代码 `...` 内的 ~x~ 是字面文本（如 `H~2~O` 展示写法），先暂存代码段
// 再转换（与 extractFootnoteRefs 的 \x01CODE 保护一致），避免被误转为下标。
// 占位符经碰撞检测生成（实际串存 phs），还原时按存储值回填。
static QString extractSubscripts(QString line, QStringList &out, QStringList &phs,
                                 QSet<QString> &collisions)
{
    // 静态缓存正则：每正文行调用一次，现构造会累积 O(行数×构造开销)
    static const QRegularExpression codeSpanRe("`([^`]+)`");
    // 内容允许 ~~ 双波浪单元（如 ~**~~x~~**~ = 下标包删除线）：md4c 会吞掉
    // 无法配对的外层 ~（sub 层整层丢失），故必须在此先行提取。单元化配对
    // 保证纯删除线 ~~x~~ 不受影响（pos0 被 (?!~) 拦截、pos1 被 (?<!~) 拦截）；
    // 闭侧 (?<!~)/(?!~) 守卫与内容 ~~ 单元消费的组合使 `~~H~2~O~~` 的内层
    // ~2~ 提取行为与旧版 [^~]+? 完全一致（~ 后紧跟 ~ 的位置永不成为开/闭符）。
    // 注意：奇数长度 ~ run（~a~~~b~）无法被单元配对消化，闭侧守卫拦截 →
    // 不提取（fromHtml 侧 escapeSubDelimsForBackup 对奇数 run 转义，对称防失配）。
    static const QRegularExpression re("(?<!~)~(?!~)((?:[^~\\n]|~~)+?)(?<!~)~(?!~)");
    // 1) 暂存行内代码片段（反引号内的 ~x~ 原样保留）
    QStringList codes;
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
    int offset = 0;
    while (true) {
        auto m = re.match(line, offset);
        if (!m.hasMatch()) break;
        // 下标内容含行内代码（~`code`~）：把代码占位符还原回反引号原样存入备份，
        // 否则还原时 <sub>\x01CODE0\x01</sub> 令牌泄漏（行内代码已先行还原）。
        // 还原后 <sub>`code`</sub> 经 TipTap 解析为下标+字面反引号，往返稳定
        // （fromHtml 对 <sub> 内容剥离标签但保留反引号 → ~`code`~）。
        QString content = m.captured(1);
        for (int i = 0; i < codes.size(); ++i)
            content.replace("\x01CODE" + QString::number(i) + "\x01", codes[i]);
        out << content;
        phs << collisionFreePlaceholder(collisions, "SB", out.size() - 1);
        QString ph = phs.last();
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
// 占位符经碰撞检测生成（实际串存 phs），还原时按存储值回填。
static QString extractEscapedEquals(QString line, QStringList &out, QStringList &phs,
                                    QSet<QString> &collisions)
{
    // 静态缓存正则：每正文行调用一次，现构造会累积 O(行数×构造开销)
    static const QRegularExpression re("\\\\=");
    int offset = 0;
    while (true) {
        auto m = re.match(line, offset);
        if (!m.hasMatch()) break;
        out << "=";
        phs << collisionFreePlaceholder(collisions, "ES", out.size() - 1);
        QString ph = phs.last();
        line.replace(m.capturedStart(), m.capturedLength(), ph);
        offset = m.capturedStart() + ph.length();
    }
    return line;
}

// 提取转义扩展定界符 \~~、\~、\^ → ZZED 占位符（toHtml 末尾还原为字面 ~~ / ~ / ^）。
// md4c 会消费反斜杠转义（\^ → 字面 ^），使转义与未转义形态在 HTML 层不可区分：
// 后处理 ^x^→<sup> 把转义字面量重新解释为上标（语义丢失）；extractSubscripts
// 不查前导 \，把转义 ~ 当下标定界符（反斜杠残留累积 \\~）；md4c 对 \~~ 产生
// ~+code+<s>~</s> 混合形态（不可稳定序列化）。提前占位使转义定界符以字面
// 文本穿过 setMarkdown 与全部后处理转换（还原置于 toHtml 末尾，晚于 sub/
// emphCode 等全部还原——ZZED 可能内嵌在它们的备份里，早还原会泄漏 token）。
// 行内代码 `...` 内的 \~/\^ 是字面文本（code span 原样保留），先暂存再提取。
// 反斜杠 run 奇偶校验：\\~ 是「字面 \ + 裸 ~」而非转义 ~，不得误配。
static QString extractEscapedExtDelims(QString line, QStringList &out, QStringList &phs,
                                       QSet<QString> &collisions)
{
    // 静态缓存正则：每正文行调用一次，现构造会累积 O(行数×构造开销)
    static const QRegularExpression codeSpanRe("`([^`]+)`");
    static const QRegularExpression re(R"(\\(~{1,2}|\^))");
    // 1) 暂存行内代码片段（反引号内的 \~/\^ 原样保留）
    QStringList codes;
    int off = 0;
    while (true) {
        auto m = codeSpanRe.match(line, off);
        if (!m.hasMatch()) break;
        codes << m.captured(0);
        QString cph = "\x01CODE" + QString::number(codes.size() - 1) + "\x01";
        line.replace(m.capturedStart(), m.capturedLength(), cph);
        off = m.capturedStart() + cph.length();
    }
    // 2) 保护 \~~ / \~ / \^（~{1,2} 贪婪：~~ 优先于 ~）
    int offset = 0;
    while (true) {
        auto m = re.match(line, offset);
        if (!m.hasMatch()) break;
        // 匹配的 \ 前有奇数个 \ → 它本身是被转义的字面反斜杠，本次非转义定界符，
        // 越过该字符重试（后续 \~ 仍可命中，如 \\\~ = 字面 \ + 转义 ~）
        int bs = 0;
        for (int j = m.capturedStart() - 1;
             j >= 0 && line.at(j) == QLatin1Char('\\'); --j)
            ++bs;
        if (bs % 2 == 1) {
            offset = m.capturedStart() + 1;
            continue;
        }
        out << m.captured(1);
        phs << collisionFreePlaceholder(collisions, "ED", out.size() - 1);
        QString ph = phs.last();
        line.replace(m.capturedStart(), m.capturedLength(), ph);
        offset = m.capturedStart() + ph.length();
    }
    // 3) 还原行内代码
    for (int i = 0; i < codes.size(); ++i)
        line.replace("\x01CODE" + QString::number(i) + "\x01", codes[i]);
    return line;
}

// 提取 markdown 行中的原始 HTML 内联标签（<span style>/<u>），
// 替换为占位符 ZZHI{n}ZZ。这些标签无法用 Markdown 语法表达，
// 需保护后直通 Qt setMarkdown，后处理时还原为原始 HTML。
// 不用 DotMatchesEverythingOption——markdown 源码逐行处理，<span>/<u>
// 作为内联元素不会跨行（与 fromHtml() 侧不同，那里处理的是完整 HTML 文档）。
// 行内代码 `...` 内的标签是字面文本（如 `H<sub>2</sub>O` 展示写法），
// 先暂存代码段再提取（与 extractFootnoteRefs 的 \x01CODE 保护一致）。
// 占位符经碰撞检测生成（实际串存 phs），还原时按存储值回填。
// 前置声明（定义于 ~1150 行，脚注路径主用）：嵌套 span 平衡扫描
static int findMatchingSpanClose(const QString &s, int openTagStart);

static QString extractHtmlInlineElements(const QString &line,
                                         QStringList &backups, QStringList &phs,
                                         QSet<QString> &collisions)
{
    // 静态缓存正则：本函数每个正文行调用一次（大文档数千次），每次现构造
    // QRegularExpression 要重新编译模式（含 JIT），累积 O(行数×构造开销)，
    // 实测成为 toHtml 行循环的最大成本（10k 行 ~1.5s）。
    static const QRegularExpression codeSpanRe("`([^`]+)`");
    // span 开标签（配 findMatchingSpanClose 平衡扫描，见下方提取块）
    static const QRegularExpression spanOpenRe(
        R"(<span\b[^>]*\bstyle\s*=\s*"[^"]*"[^>]*>)");
    static const QRegularExpression uRe(R"(<u>(.*?)</u>)");
    static const QRegularExpression kbdRe(R"(<kbd>(.*?)</kbd>)");
    static const QRegularExpression smallRe(R"(<small>(.*?)</small>)");
    static const QRegularExpression markRe(R"(<mark[^>]*>(.*?)</mark>)");
    static const QRegularExpression subRe(R"(<sub[^>]*>(.*?)</sub>)");
    static const QRegularExpression supRe(
        R"(<sup(?![^>]*\bdata-type\s*=)[^>]*>(.*?)</sup>)");
    static const QRegularExpression brRe(R"(<br\b[^>]*>)");
    QString r = line;
    // 1) 暂存行内代码片段（反引号内的 HTML 标签原样保留）
    QStringList codes;
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
    // <span style="...">text</span>（嵌套感知：平衡扫描到配对 </span>——
    // 非贪婪 (.*?)</span> 停在第一个闭合处，嵌套 span 的备份不平衡、尾部
    // 文本连同孤立 </span> 残留为字面输出，结构逐轮退化）
    {
        int offset = 0;
        while (true) {
            auto m = spanOpenRe.match(r, offset);
            if (!m.hasMatch()) break;
            const int closeStart = findMatchingSpanClose(r, m.capturedStart());
            if (closeStart < 0) {
                // 未闭合：跳过该开标签，交 escapeUnclosedHtmlTags 兜底
                offset = m.capturedEnd();
                continue;
            }
            const int end = closeStart + 7;   // 跳过 "</span>"
            backups << r.mid(m.capturedStart(), end - m.capturedStart());
            phs << collisionFreePlaceholder(collisions, "HI", n++);
            QString ph = phs.last();
            r.replace(m.capturedStart(), end - m.capturedStart(), ph);
            offset = m.capturedStart() + ph.length();
        }
    }
    // <u>text</u>
    {
        int offset = 0;
        while (true) {
            auto m = uRe.match(r, offset);
            if (!m.hasMatch()) break;
            backups << m.captured();
            phs << collisionFreePlaceholder(collisions, "HI", n++);
            QString ph = phs.last();
            r.replace(m.capturedStart(), m.capturedLength(), ph);
            offset = m.capturedStart() + ph.length();
        }
    }
    // <kbd>text</kbd>（内联键盘元素，与 <u> 一样复用 ZZHI 占位符体系）
    {
        int offset = 0;
        while (true) {
            auto m = kbdRe.match(r, offset);
            if (!m.hasMatch()) break;
            backups << m.captured();
            phs << collisionFreePlaceholder(collisions, "HI", n++);
            QString ph = phs.last();
            r.replace(m.capturedStart(), m.capturedLength(), ph);
            offset = m.capturedStart() + ph.length();
        }
    }
    // <small>text</small>（内联小字元素）
    {
        int offset = 0;
        while (true) {
            auto m = smallRe.match(r, offset);
            if (!m.hasMatch()) break;
            backups << m.captured();
            phs << collisionFreePlaceholder(collisions, "HI", n++);
            QString ph = phs.last();
            r.replace(m.capturedStart(), m.capturedLength(), ph);
            offset = m.capturedStart() + ph.length();
        }
    }
    // <mark>text</mark>（高亮，保护原始 HTML 不被 QTextDocument 丢弃）
    {
        int offset = 0;
        while (true) {
            auto m = markRe.match(r, offset);
            if (!m.hasMatch()) break;
            backups << m.captured();
            phs << collisionFreePlaceholder(collisions, "HI", n++);
            QString ph = phs.last();
            r.replace(m.capturedStart(), m.capturedLength(), ph);
            offset = m.capturedStart() + ph.length();
        }
    }
    // <sub>text</sub>（下标，保护原始 HTML 不被 QTextDocument 丢弃）
    {
        int offset = 0;
        while (true) {
            auto m = subRe.match(r, offset);
            if (!m.hasMatch()) break;
            backups << m.captured();
            phs << collisionFreePlaceholder(collisions, "HI", n++);
            QString ph = phs.last();
            r.replace(m.capturedStart(), m.capturedLength(), ph);
            offset = m.capturedStart() + ph.length();
        }
    }
    // <sup>text</sup>（上标，不含 data-type 属性——脚注 sup 另有专门处理）
    {
        int offset = 0;
        while (true) {
            auto m = supRe.match(r, offset);
            if (!m.hasMatch()) break;
            backups << m.captured();
            phs << collisionFreePlaceholder(collisions, "HI", n++);
            QString ph = phs.last();
            r.replace(m.capturedStart(), m.capturedLength(), ph);
            offset = m.capturedStart() + ph.length();
        }
    }
    // <br>（硬换行——自闭合 void 元素，保护不被 QTextDocument 丢弃）
    {
        int offset = 0;
        while (true) {
            auto m = brRe.match(r, offset);
            if (!m.hasMatch()) break;
            backups << m.captured();
            phs << collisionFreePlaceholder(collisions, "HI", n++);
            QString ph = phs.last();
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
// 占位符经碰撞检测生成（实际串存 phs），还原时按存储值回填。
static QString extractFootnoteRefs(QString line, QStringList &refs, QStringList &phs,
                                   QSet<QString> &collisions, bool enabled = true)
{
    if (!enabled) return line;   // 嵌套层：[^x] 按字面穿过
    // 静态缓存正则：每正文行调用一次，现构造会累积 O(行数×构造开销)
    static const QRegularExpression codeSpanRe("`([^`]+)`");
    static const QRegularExpression refRe("\\[\\^([^\\]]+?)\\]");
    // 1) 暂存行内代码片段（含 [^x] 的反引号内容不转换）
    QStringList codes;
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
    off = 0;
    while (true) {
        auto m = refRe.match(line, off);
        if (!m.hasMatch()) break;
        // 实体解码：修复历史损坏文件（与定义侧同款，见 buildFootnoteDefHtml 调用点）
        QString id = unescapeHtmlEntities(m.captured(1).trimmed());
        // id 来自文档正文（[^id] 的 id 部分），须转义后才可进属性/元素文本
        refs << ("<sup data-type=\"footnoteRef\" data-id=\"" + escapeHtmlAttr(id) + "\">"
                 + escapeHtmlAttr(id) + "</sup>");
        phs << collisionFreePlaceholder(collisions, "FR", refs.size() - 1);
        QString ph = phs.last();
        line.replace(m.capturedStart(), m.capturedLength(), ph);
        off = m.capturedStart() + ph.length();
    }
    // 3) 还原行内代码
    for (int i = 0; i < codes.size(); ++i)
        line.replace("\x01CODE" + QString::number(i) + "\x01", codes[i]);
    return line;
}

// 脚注定义内容（MD，可多行块级）→ footnoteDef 契约 HTML（marker + 块级 content）。
// 内容经完整 MD→HTML 主管线嵌套转换（数学/任务列表/高亮/上下标等占位符全套生效，
// 嵌套层禁用脚注提取：内层 [^x] 按字面处理，嵌套脚注不支持）。
static QString buildFootnoteDefHtml(const QString &id, const QString &contentMd)
{
    QString inner;
    if (contentMd.trimmed().isEmpty()) {
        inner = QStringLiteral("<p></p>");   // 空定义：block+ schema 最小内容
    } else {
        QString h = MarkdownHandler::toHtml(contentMd, false);
        static const QRegularExpression bodyRe(
            QStringLiteral("<body[^>]*>(.*)</body>"),
            QRegularExpression::DotMatchesEverythingOption);
        auto bm = bodyRe.match(h);
        inner = bm.hasMatch() ? bm.captured(1).trimmed() : h.trimmed();
        // Qt toHtml 携带的片段注释不进编辑器内容
        inner.remove(QStringLiteral("<!--StartFragment-->"));
        inner.remove(QStringLiteral("<!--EndFragment-->"));
    }
    return "<div data-type=\"footnoteDef\" data-id=\"" + escapeHtmlAttr(id) + "\">"
         + "<span class=\"footnote-marker\">" + escapeHtmlAttr(id) + "</span>"
         + "<div class=\"footnote-content\">" + inner + "</div></div>";
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

// 判断某行是否为「80 列折行」产生的续行——该判定及其消费方（反换行合并
// 循环）已随折行根治退役：序列化改用 vendored QtMarkdownWriter 并关闭
// 折行（serializeMarkdownNoWrap），不再产生折行续行。历史机制详见
// docs/vendor-qt-markdown-writer.md。

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
                                    int depth, QStringList &subContents,
                                    QStringList &subPhs,
                                    QSet<QString> &collisions)
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
            d.setMarkdown(extractSubscripts(it.text, subContents, subPhs,
                                            collisions));
            QString h = d.toHtml();
            // static：列表每项都构造一次（10k 项 = 10k 次编译）
            static const QRegularExpression bodyRe(
                "<body[^>]*>(.*)</body>",
                QRegularExpression::DotMatchesEverythingOption);
            auto bm = bodyRe.match(h);
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
                html += buildMdListContainer(items, idx, childDepth, subContents,
                                             subPhs, collisions);
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
                                 QStringList &subContents, QStringList &subPhs,
                                 QSet<QString> &collisions)
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
        html += buildMdListContainer(items, idx, items[idx].depth, subContents,
                                     subPhs, collisions);
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

// fromHtml 方向：从 <blockquote 开标签平衡扫描配对闭标签；失败返回 -1。
// 嵌套引用必须配对到同层闭合——惰性首闭会把内层开标签卷进捕获、留下不平衡
// 片段（曾使二级引用经递归 + Qt 未闭合引用序列化多加一级前缀）。
static int findMatchingBlockquoteClose(const QString &s, int openTagStart)
{
    static const QRegularExpression bqTagRe("<(/?)blockquote\\b[^>]*>");
    int depth = 0;
    int pos = openTagStart;
    while (true) {
        auto m = bqTagRe.match(s, pos);
        if (!m.hasMatch()) return -1;
        if (m.captured(1).isEmpty()) ++depth;   // <blockquote ...>
        else if (--depth == 0) return m.capturedStart();
        pos = m.capturedEnd();
    }
}

// 任务条目内容 → 纯文本：行内富文本转为 markdown 语法，其余标签剥离。
// 调用前提：嵌套任务列表已提取，行内代码/上下标/高亮/公式已是 ZZ* 占位符（原样保留）。
static QString taskItemInlineText(QString content)
{
    // 静态缓存正则：本函数每个条目调用一次（大文档数千次），每次现构造
    // QRegularExpression 要重新编译模式（含 JIT），累积 O(条目数×构造开销)。
    static const QRegularExpression strongRe(
        "<(strong|b)\\b[^>]*>(.*?)</\\1>",
        QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression emRe(
        "<(em|i)\\b[^>]*>(.*?)</\\1>",
        QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression strikeRe(
        "<(s|del)\\b[^>]*>(.*?)</\\1>",
        QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression linkRe(
        "<a\\b[^>]*href\\s*=\\s*\"([^\"]*)\"[^>]*>(.*?)</a>",
        QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression brRe("<br\\b[^>]*>");
    static const QRegularExpression pCloseRe("</p>");
    static const QRegularExpression imgRe(
        "<img\\b([^>]*)>", QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression srcRe("\\bsrc\\s*=\\s*\"([^\"]*)\"");
    static const QRegularExpression altRe("\\balt\\s*=\\s*\"([^\"]*)\"");
    static const QRegularExpression tagRe("<[^>]*>");
    content.replace(strongRe, "**\\2**");
    content.replace(emRe, "*\\2*");
    content.replace(strikeRe, "~~\\2~~");
    content.replace(linkRe, "[\\2](\\1)");
    // <img> → ![alt](src)（图片是内容，不是无意义标签）。
    // 此前 <img> 随 tagRe 一并被剥离：列表项内的图片（编辑器块级 Image 节点
    // 落在 li 内时产 <li><p>x</p><img><p>y</p></li>）保存即从文档消失。
    // 含图片的条目最终并入单行（见下方 kept.join，理由同处注释）。
    // 置于 linkRe 之后：<a href><img></a> 已变 [<img …>](href)，此处再展开为
    // [![alt](src)](href) ✓ 与既有 linkRe 展开顺序一致。置于 remove(tagRe) 之前
    // 是必须的——被剥掉就再也拿不回来了。
    bool hasImage = false;
    globalReplaceWith(content, imgRe,
        [&hasImage](const QRegularExpressionMatch &m) {
            const QString attrs = m.captured(1);
            const auto sm = srcRe.match(attrs);
            if (!sm.hasMatch()) return QString();   // 无 src：与既有剥离行为一致（丢弃）
            hasImage = true;
            const auto am = altRe.match(attrs);
            return QStringLiteral("![")
                + (am.hasMatch() ? unescapeHtmlEntities(am.captured(1)) : QString())
                + "](" + unescapeHtmlEntities(sm.captured(1)) + ")";
        });
    // 块边界 → 换行（条目内多段落用缩进续行表达）
    content.replace(brRe, "\n");
    content.replace(pCloseRe, "\n");
    content.remove(tagRe);
    content = unescapeHtmlEntities(content);
    QStringList kept;
    for (const QString &l : content.split('\n')) {
        QString t = l.trimmed();
        if (!t.isEmpty()) kept << t;
    }
    // 含图片的条目合并为单行：Qt 的 markdown 导入把列表项的续行解析成**列表外**
    // 的独立段落（实测 `- 前\n  后` 即如此，续行机制对块级内容普遍不可用），
    // 图片会被挤出条目、下一轮再漂移。单行形态 `- 前 ![x](a.png) 后` 才是
    // Qt 回读的稳定不动点（图片仍留在 li 内，TipTap 归一到 <p>前</p><img><p>后</p>）。
    return hasImage ? kept.join(QLatin1Char(' ')) : kept.join(QLatin1Char('\n'));
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
    // 手写 indexOf 平衡扫描替代逐次 QRegularExpression::match(s, offset)——
    // match() 每次从 offset 扫到整个字符串末尾（实测耗时 ∝ 剩余长度，300KB
    // 串首部单次 ~768µs、尾部 ~0µs），K 次调用即 O(K·n) 退化；
    // QString::indexOf 对字面模式 SIMD 定位，成本只随实际匹配区间增长。
    // 语义对齐正则 <(/?)ul\b[^>]*>：ul/ol 后须非单词字符（> 或空白）才算标签。
    const QString openTag  = ordered ? QStringLiteral("<ol")  : QStringLiteral("<ul");
    const QString closeTag = ordered ? QStringLiteral("</ol") : QStringLiteral("</ul");
    const auto isWordChar = [](QChar ch) {
        const ushort u = ch.unicode();
        return (u >= 'a' && u <= 'z') || (u >= 'A' && u <= 'Z')
            || (u >= '0' && u <= '9') || u == '_';
    };
    const auto isTag = [&s, &isWordChar](int i, int tagLen) {
        // `<ul` 后是 i+3、`</ul` 后是 i+4（closeTag 多一个 '/'）
        return i >= 0 && i + tagLen < s.size() && !isWordChar(s.at(i + tagLen));
    };
    int depth = 0;
    int pos = openTagStart;
    while (true) {
        const int o = s.indexOf(openTag, pos);
        const int c = s.indexOf(closeTag, pos);
        const bool oOk = isTag(o, openTag.size());
        const bool cOk = isTag(c, closeTag.size());
        if (!oOk && !cOk) return -1;
        if (cOk && (!oOk || c <= o)) {
            if (--depth == 0) return c;
            pos = c + closeTag.size();
        } else {
            ++depth;
            pos = o + openTag.size();
        }
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
    // 单遍双流归并：两个 globalMatch 迭代器按位置序推进（Qt 的 match(s, scan)
    // 每次从 scan 全串扫描到末尾，循环即 O(标签数×n)；globalMatch 单遍 O(n)）。
    // 遇首个 start >= pos 的标签即停——后续标签位置更靠后，不可能再计入。
    auto nextOf = [](QRegularExpressionMatchIterator &it, QRegularExpressionMatch &m) {
        if (!it.hasNext()) return false;
        m = it.next();
        return true;
    };
    QRegularExpressionMatchIterator oi = olTagRe.globalMatch(s);
    QRegularExpressionMatchIterator ui = ulTagRe.globalMatch(s);
    QRegularExpressionMatch om, um;
    bool hasO = nextOf(oi, om);
    bool hasU = nextOf(ui, um);
    int depth = 0;
    while (hasO || hasU) {
        // 取位置较小者（<ol 与 <ul 开头互斥，不存在同起点）
        const bool takeO = hasO && (!hasU || om.capturedStart() < um.capturedStart());
        const QRegularExpressionMatch &m = takeO ? om : um;
        if (m.capturedStart() >= pos) break;
        depth += m.captured(1).isEmpty() ? 1 : -1;
        if (takeO) hasO = nextOf(oi, om);
        else       hasU = nextOf(ui, um);
    }
    return depth > 0;
}

// 链式上下文中被 md4c「整对丢弃」的直接包裹 code span 补救（2026-08-29）。
// 背景：md4c 强调配对是位置序级联——closer 逐个处理、向左找最近活 opener；
// 当最近 opener 与 closer 之间只夹一个 code span（纯代码直接包裹，如 `**`c`**`）时，
// 这对标记被「整对丢弃」：HTML 里无 strong、无字面 `*` 残留（信息在 setMarkdown
// 层已丢，fromHtml 无法恢复）。与之交织且往返完好的两类形态必须严格区分：
//   ① 被征用：opener 已被更早的 closer 配掉 → html 中 code 一侧紧贴 </strong> 边界；
//   ② 成字面：closer 无 opener 可配 → html 中 code 一侧紧贴字面 `*`。
// 策略：不模拟配对算法、不改 setMarkdown 输入（级联零扰动），在 doc.toHtml() 之后、
// 占位符还原之前做外科手术式观察补结构：MD 侧（processed，即 setMarkdown 实际输入）
// 找「code span 两侧紧贴等长 run 的 `**`/`*`」候选；html 侧验证对应 mono span 两侧
// 「既非强调边界（</span> / <span）也无字面 `*`」——双条件交集即整对丢弃铁证，
// 补包 <strong>/<em>。产物形态与既有 emphCode 保护还原的 <strong><code> 完全
// 一致，fromHtml/TipTap 侧零新逻辑（该链路已被既有测试覆盖）。
// 防御（fail-safe）：MD 与 html 的 code span 数量、内容（CommonMark 首尾各剥一空格
// + &<> 转义归一）逐一比对，任何不一致（跨行 span、多反引号 run、字面 ZZ 文本等
// 复杂形态）整体放弃本次补救，保持现状不 worse。无候选时直接返回，常规文档零成本
// （仅一次 processed 反引号扫描，globalMatch 单遍 O(n)）。
static void wrapDiscardedEmphCode(QString &html, const QString &processed)
{
    // --- MD 侧：收集 setMarkdown 可见的单反引号 code span ---
    // ZZ 占位符 token 内的 span 经备份还原产生 html，不经 md4c，须排除（否则数量
    // 对不上会误伤整个文档的补救机会）。用通用 ZZ[A-Z]+[0-9]+ZZ 模式扫 token 区间
    // （真占位符经碰撞检测不与用户字面 ZZ 文本同串，误吞的只可能是用户字面文本，
    // 而字面文本内的 span 本就与 html 侧无对应关系，排除同样安全）。
    struct MdSpan {
        int s;      // 内容起始
        int e;      // 内容结束（不含）
        uchar kind; // 0 普通；1 strong 候选；2 em 候选
    };
    static const QRegularExpression spanRe(QStringLiteral("`([^`\n]+)`"));
    static const QRegularExpression phRe(QStringLiteral("ZZ[A-Z]+[0-9]+ZZ ?"));
    QVector<MdSpan> mdSpans;
    {
        // 反斜杠 run 奇偶判定：pos 前奇数个连续 \ 则该处字符是被转义的字面量
        auto isEscaped = [&processed](int pos) {
            int bs = 0;
            for (int j = pos - 1; j >= 0 && processed.at(j) == QLatin1Char('\\'); --j)
                ++bs;
            return (bs % 2) == 1;
        };
        auto phIt = phRe.globalMatch(processed);
        QRegularExpressionMatch phM;
        bool hasPh = phIt.hasNext() ? (phM = phIt.next(), true) : false;
        auto it = spanRe.globalMatch(processed);
        while (it.hasNext()) {
            const QRegularExpressionMatch m = it.next();
            // 越过已结束的占位符 token，保留首个可能包含本 span 的 token
            while (hasPh && phM.capturedEnd() <= m.capturedStart())
                hasPh = phIt.hasNext() ? (phM = phIt.next(), true) : false;
            if (hasPh && m.capturedStart() >= phM.capturedStart()
                && m.capturedStart() < phM.capturedEnd())
                continue; // span 在占位符内，不经 md4c
            uchar kind = 0;
            const int ts = m.capturedStart(); // 开反引号位置
            const int te = m.capturedEnd();   // 闭反引号后一位置
            // 左侧紧贴 run==2 的 **（strong 候选）或 run==1 的 *（em 候选），
            // run>=3（***）不判——嵌套形态复杂，保持现状。
            if (ts >= 1 && processed.at(ts - 1) == QLatin1Char('*')) {
                int leftRun = 1, leftStart = ts - 1;
                while (leftStart > 0 && processed.at(leftStart - 1) == QLatin1Char('*')) {
                    ++leftRun;
                    --leftStart;
                }
                if ((leftRun == 1 || leftRun == 2) && !isEscaped(leftStart)) {
                    int rightRun = 0;
                    while (te + rightRun < processed.size()
                           && processed.at(te + rightRun) == QLatin1Char('*'))
                        ++rightRun;
                    // 右侧 run 与左侧精确相等且未转义 → 直接包裹形态
                    if (rightRun == leftRun && !isEscaped(te))
                        kind = (leftRun == 2) ? 1 : 2;
                }
            }
            mdSpans.append({int(m.capturedStart(1)), int(m.capturedEnd(1)), kind});
        }
    }
    bool anyCand = false;
    for (const MdSpan &sp : mdSpans)
        if (sp.kind) { anyCand = true; break; }
    if (!anyCand)
        return; // 绝大多数文档零候选：html 侧零扫描零成本

    // --- html 侧：收集 mono span（动态字体族名，跨平台见 monoSpanFamilyPattern）---
    // 必须从 <body> 后开始扫：QTextDocument 输出的 <head><style> 可能含字体 CSS。
    static const QRegularExpression monoRe(
        QStringLiteral("<span[^>]*font-family:\\s*'(?:%1)'[^>]*>(.*?)</span>")
            .arg(monoSpanFamilyPattern()));
    struct Hs { int s, e; QString content; }; // s: 起始 < 位置；e: </span> 结束后一位置
    QVector<Hs> hspans;
    int bodyStart = html.indexOf(QLatin1String("<body"));
    bodyStart = bodyStart < 0 ? 0 : html.indexOf(QLatin1Char('>'), bodyStart) + 1;
    {
        auto it = monoRe.globalMatch(html, bodyStart);
        while (it.hasNext()) {
            const QRegularExpressionMatch m = it.next();
            hspans.append({int(m.capturedStart()), int(m.capturedEnd()), m.captured(1)});
        }
    }
    // 数量与内容逐一核对；不一致 → 放弃（复杂形态整体 fail-safe，见函数头注释）
    if (hspans.size() != mdSpans.size())
        return;
    for (int k = 0; k < mdSpans.size(); ++k) {
        QString expect = processed.mid(mdSpans[k].s, mdSpans[k].e - mdSpans[k].s);
        // CommonMark：内容首尾同时为空格时各剥一个
        if (expect.size() >= 2 && expect.startsWith(QLatin1Char(' '))
            && expect.endsWith(QLatin1Char(' ')))
            expect = expect.mid(1, expect.size() - 2);
        expect.replace(QLatin1Char('&'), QStringLiteral("&amp;"))
            .replace(QLatin1Char('<'), QStringLiteral("&lt;"))
            .replace(QLatin1Char('>'), QStringLiteral("&gt;"));
        if (expect != hspans[k].content)
            return;
    }

    // --- 命中判定 + 单遍重建包裹（不逐个 replace，避免 O(K·n) 尾部搬移）---
    QString out;
    out.reserve(html.size() + mdSpans.size() * 18);
    int last = bodyStart;
    for (int k = 0; k < mdSpans.size(); ++k) {
        if (!mdSpans[k].kind)
            continue;
        const Hs &h = hspans[k];
        // 左裸：前一字符非 `*`（非字面残留）、且非紧贴 </span>（非加粗/斜体边界=征用）
        const bool leftBare = html.at(h.s - 1) != QLatin1Char('*')
            && !(h.s >= 7 && html.mid(h.s - 7, 7) == QLatin1String("</span>"));
        // 右裸：后一字符非 `*`、且不紧贴 <span（非强调重开=征用/其它内联格式）
        const bool rightBare = (h.e >= html.size() || html.at(h.e) != QLatin1Char('*'))
            && html.mid(h.e, 5) != QLatin1String("<span");
        if (!(leftBare && rightBare))
            continue; // 征用/字面形态：md4c 已正确处理，不干预
        const bool strong = (mdSpans[k].kind == 1);
        out += html.mid(last, h.s - last);
        out += strong ? QStringLiteral("<strong>") : QStringLiteral("<em>");
        out += html.mid(h.s, h.e - h.s);
        out += strong ? QStringLiteral("</strong>") : QStringLiteral("</em>");
        last = h.e;
    }
    out += html.mid(last);
    html = out;
}

// 就地展开 s 中的占位符 token（通用 ZZ 模式 + 查表；嵌套占位符经有界多趟
// 消解，趟数 = 嵌套深度上限 8）。未命中表的是用户字面 ZZ 形态文本，原样保留
// （碰撞检测保证真占位符不与用户文本同串）。供脚注定义 / 块引用内容等
// 「递归产出 MD 后需在父级上下文展开外层占位符」的场景共用——多行备份必须
// 先展开再逐行加前缀/缩进，否则只有首行获得前缀（围栏/多行内容逃出容器）。
static void expandPlaceholdersInPlace(QString &s, const QStringList &phs,
                                      const QStringList &backups)
{
    static const QRegularExpression anyTokRe(QStringLiteral("ZZ[A-Z]+[0-9]+ZZ"));
    QHash<QString, QString> phMap;
    phMap.reserve(phs.size());
    for (int i = 0; i < phs.size(); ++i)
        phMap.insert(phs[i], backups[i]);
    for (int pass = 0; pass < 8; ++pass) {
        if (!anyTokRe.match(s).hasMatch())
            break;
        QString rebuilt;
        int pos = 0;
        while (true) {
            const auto tm = anyTokRe.match(s, pos);
            if (!tm.hasMatch()) {
                rebuilt += s.mid(pos);
                break;
            }
            rebuilt += s.mid(pos, tm.capturedStart() - pos);
            const auto itr = phMap.constFind(tm.captured());
            rebuilt += (itr != phMap.constEnd()) ? itr.value() : tm.captured();
            pos = tm.capturedEnd();
        }
        s = rebuilt;
    }
}

// 阶段 4（审查 P2-8）：复杂块引用预抽取（toHtml 侧）。
// Qt setMarkdown 对引用内的列表/hr 会弹出引用、二级引用展平为双倍缩进段落、
// 围栏需专门通道——信息在 Qt 层已丢失，后处理无从恢复。此处在主管线之前把
// 「剥一层 > 前缀后含 列表/嵌套引用/围栏/hr」的引用块整体抽出：内层递归
// toHtml（其自身的后处理 2 已把 margin 块重构为 <blockquote>），包裹为字面
// <blockquote>…</blockquote> 走 ZZDV 字面还原通道（与手写 <div> 同型，独占
// 段落时连 <p> 壳一并替换）；TipTap 原生解析 blockquote 节点，fromHtml 侧
// 递归还原 > 前缀。仅含普通段落的引用块不走此路径（既有 Qt→margin→重构
// 链路已验证，行为不变）。
static void protectComplexQuoteBlocks(QStringList &lines, QSet<QString> &collisions,
                                      QStringList &divPhs, QStringList &divBackups,
                                      bool extractFootnotes)
{
    static const QRegularExpression quoteLineRe("^\\s*>");
    // 剥前缀后判定「Qt 会破坏结构」的构造：无序/有序列表、嵌套引用、围栏、hr
    static const QRegularExpression complexRe(
        "^\\s*(?:[-*+]\\s|\\d{1,9}[.)]\\s|>|`{3,}|~{3,}|(?:-{3,}|\\*{3,}|_{3,})\\s*$)");
    for (int i = 0; i < lines.size();) {
        if (!quoteLineRe.match(lines[i]).hasMatch()) { ++i; continue; }
        int j = i;
        QStringList inner;
        bool complex = false;
        while (j < lines.size() && quoteLineRe.match(lines[j]).hasMatch()) {
            const QString stripped = stripBlockquotePrefix(lines[j]);
            if (complexRe.match(stripped).hasMatch()) complex = true;
            inner << stripped;
            ++j;
        }
        if (complex) {
            // 递归转换内层（footnotes 标志透传，嵌套引用在递归内再抽再递归）
            const QString innerHtml = MarkdownHandler::toHtml(
                inner.join('\n'), extractFootnotes);
            int b0 = innerHtml.indexOf(QLatin1String("<body"));
            b0 = (b0 >= 0 ? innerHtml.indexOf('>', b0) : -1) + 1;
            const int b1 = innerHtml.lastIndexOf(QLatin1String("</body>"));
            const QString body = (b0 > 0 && b1 > b0)
                ? innerHtml.mid(b0, b1 - b0).trimmed() : innerHtml.trimmed();
            divBackups << "<blockquote>" + body + "</blockquote>";
            divPhs << collisionFreePlaceholder(collisions, "DV", divBackups.size() - 1);
            lines.replace(i, divPhs.last());
            for (int k = j - 1; k > i; --k)
                lines.removeAt(k);
        }
        i = j;
    }
}

QString MarkdownHandler::toHtml(const QString &markdown, bool extractFootnotes)
{
    // 占位符碰撞索引：源文档中全部 ZZ\w+ZZ 形态子串，供 collisionFreePlaceholder
    // O(1) 碰撞检测（单次构建，随生成 insert 本次占位符）。
    QSet<QString> collisions = buildPlaceholderCollisionSet(markdown);
    // 预处理：两行正文间单 \n → 行末嵌入软换行占位符 ZZBR<n>ZZ
    // QTextDocument::setMarkdown() 不支持硬换行（<br>、  \n、\ 均被忽略）
    // 用纯文本占位符嵌入行末，后处理时替换为 <br data-brk>。占位符带序号，避免
    // 与用户文档中字面 "BRMARKER" 文本冲突（此前固定字符串会被后处理误替换，
    // 破坏字面文本/行内代码，如 audit 文档中的 `BRMARKER`）。

    QStringList lines = markdown.split('\n');
    QString processed;
    int codeFenceLen = 0; // 当前代码块开启围栏的字符数（0 = 不在代码块；``` 或 ~~~）
    QChar codeFenceChar = QLatin1Char('`');   // 开启围栏的字符（闭合须同字符，CommonMark）
    int tlFenceIndent = 0, tlFenceRun = 0;    // isTopLevelFenceLine 出参（当前行）
    QChar tlFenceChar = QLatin1Char('`');
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
    QStringList escapedDelimBackups; // 转义扩展定界符 \~~/\~/\^ → ZZED 保护（还原为字面定界符）
    QStringList htmlInlineBackups; // 原始 HTML 内联标签（<span style> / <u>）
    QStringList hrMarkers;       // 水平线 --- → 占位符保护（后处理还原为 <hr/>）
    int brSoftCount = 0;         // 软换行占位符 ZZBR<n>ZZ 计数（后处理还原为 <br data-brk>）
    int listBlankCount = 0;      // 列表项间空行注释占位 ZZLB<n>ZZ 计数（强制拆分列表）
    int blankLineCount = 0;      // 段落间连续空行占位 ZZBL<n>ZZ 计数（还原为 <p><br></p>）
    QStringList blankLinePhs;    // 实际占位符串（collisionFree 跳号，序号可非连续）
    // 其余各前缀的实际占位符串列表（均经 collisionFreePlaceholder 跳号生成，
    // 与对应备份列表一一对应，序号可非连续；还原时按存储值回填，避免用户
    // 文档字面同串被误替换——往返保真）
    QStringList mathBlockPhs;    // ZZMB（块级公式，与 mathBlocks 对应）
    QStringList bqCodeBlockPhs;  // ZZBQ（块引用代码块，与 bqCodeBlocks 对应）
    QStringList bqSepPhs;        // 块引用边界标记（原固定串 ZZBQSEP，现带序号）
    QStringList taskListPhs;     // ZZTL（任务列表，与 taskLists 对应）
    QStringList brSoftPhs;       // ZZBR（软换行）
    QStringList hrPhs;           // ZZHR（水平线，与 hrMarkers 对应）
    QStringList divPhs;          // ZZDV（手写 <div>，与 divBackups 对应）
    QStringList divBackups;      // ZZDV 备份（函数级：复杂块引用预抽取在行循环前写入）
    QStringList escapedEqPhs;    // ZZES（转义等号，与 escapedEqBackups 对应）
    QStringList escapedDelimPhs; // ZZED（转义扩展定界符，与 escapedDelimBackups 对应）
    QStringList subPhs;          // ZZSB（下标，与 subContents 对应）
    QStringList footnoteRefPhs;  // ZZFR（脚注引用，与 footnoteRefs 对应）
    QStringList emptyAltSentinels; // ZZIA（空 alt 图片的 alt 占位，见 protectEmptyAltImages）
    QStringList htmlInlinePhs;   // ZZHI（HTML 内联标签，与 htmlInlineBackups 对应）
    QStringList tableCellPhs;    // ZZTC（表格空单元格）
    QStringList emphCodePhs;     // ZZEC/ZZEM（强调包裹行内代码，与 emphCodeHtml 对应）
    QStringList emphCodeHtml;    // 最终 <strong>/<em> 包 <code> HTML，后处理还原

    QStringList mathBlocks;  // 收集 $$ 块的内容
    QStringList mathLines;    // 当前 $$ 块的行
    QString mathOpenLine;     // 当前 $$ 块的开启行原文（EOF 未闭合时还原用）

    // 上一处理行的块类型。隔离规则：列表后紧跟正文（无空行）时补空行，
    // 否则 md4c 的 lazy-continuation 会把正文吞入列表项、后续列表被合并。
    // （有意偏离严格 CommonMark：编辑区语义优先，同 blockquote 先例）
    enum BlockType { BT_NONE, BT_LIST, BT_PARA };
    BlockType lastBlock = BT_NONE;

    // 阶段 4：复杂块引用预抽取（列表/嵌套引用/围栏/hr 在 Qt 层必丢结构）
    protectComplexQuoteBlocks(lines, collisions, divPhs, divBackups, extractFootnotes);

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
                    // 读取侧实体解码（unescapeHtmlEntities）：修复历史损坏文件。
                    // 6.8.2 及以前 fromHtml 不解属性实体，把 aligned 的对齐列 `&`
                    // 写成了字面 `&amp;` 存进 .md；若载入时不解码，toHtml 会把
                    // `&amp;` 再次转义成 `&amp;amp;`，编辑器只解一层 → 公式显示成
                    // 字面「x&amp;= a + b」且成为稳定不动点，永不自愈。
                    // 解码后：编辑器显示正确，下次保存回写干净的 `&`（一次性修复）。
                    mathBlocks << ("<div data-type=\"block-math\" data-latex=\""
                                   + escapeHtmlAttr(unescapeHtmlEntities(latex))
                                   + "\"></div>");
                    mathBlockPhs << collisionFreePlaceholder(collisions,
                        "MB", mathBlocks.size() - 1);
                    processed += mathBlockPhs.last() + "\n";
                    lastBlock = BT_NONE;
                    continue;
                }
                mathLines.clear();
                mathOpenLine = line;
                inMathBlock = true;
                // 占位符（块在闭合 $$ 时才入 mathBlocks，此处先占序号；
                // 碰撞跳号时 mathBlockPhs 与 mathBlocks 仍一一对应）。
                // EOF 未闭合时由循环后的收尾逻辑弹出并还原原文（见 toHtml
                // 末段「未闭合块级公式」注释），两列表恢复一致
                mathBlockPhs << collisionFreePlaceholder(collisions,
                    "MB", mathBlocks.size());
                processed += mathBlockPhs.last() + "\n";
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
                // 实体解码见上方单行块级公式注释（修复历史损坏文件）
                mathBlocks << ("<div data-type=\"block-math\" data-latex=\""
                               + escapeHtmlAttr(unescapeHtmlEntities(latex))
                               + "\"></div>");
                inMathBlock = false;
            }
            lastBlock = BT_NONE;
        } else if (inMathBlock) {
            mathLines << line;
        } else if (isTopLevelFenceLine(line, &tlFenceIndent, &tlFenceRun, &tlFenceChar)) {
            // 顶层围栏行（``` / ~~~，≤3 空格缩进，CommonMark 全族）
            if (codeFenceLen == 0) {
                // 开启新代码块：提取语言（信息串为围栏后的文本），整块用占位符保护，
                // 内容不经 setMarkdown（其围栏解析同样朴素，嵌套围栏会二次错乱）。
                // 内容行保留原始缩进（CommonMark 的去缩进归一化由往返收敛完成）
                // 实体解码：修复历史损坏文件（旧版 fromHtml 不解属性实体，
                // 信息串含 & 时被写成字面 &amp; 存进 .md）
                codeFenceLang = unescapeHtmlEntities(
                    line.mid(tlFenceIndent + tlFenceRun).trimmed());
                codeFenceLen = tlFenceRun;
                codeFenceChar = tlFenceChar;
                codeContent.clear();
                // 占位符序号须避开源文档/已产出内容中的字面同串（如 audit 文档
                // 里示例的 `ZZCB0ZZ`），否则还原时用户文本被误替换。串存 codeBlockPhs，
                // 与 codeBlocks 一一对应，还原时按存储值回填。
                codeBlockPhs << collisionFreePlaceholder(collisions,
                    "CB", codeBlocks.size());
                processed += codeBlockPhs.last() + "\n";
            } else if (tlFenceChar == codeFenceChar && tlFenceRun >= codeFenceLen
                       && line.mid(tlFenceIndent + tlFenceRun).trimmed().isEmpty()) {
                // 闭合代码块：同种围栏字符、纯围栏行、长度 >= 开启围栏
                //（异种围栏行（``` 块内的 ~~~）落入下方内容分支，互斥天然成立）
                QString content = codeContent.join('\n');
                QString codeOpen = codeFenceLang.isEmpty()
                    ? QStringLiteral("<code>")
                    : QStringLiteral("<code class=\"language-") + escapeHtmlAttr(codeFenceLang) + "\">";
                codeBlocks << "<pre>" + codeOpen + content.toHtmlEscaped() + "</code></pre>";
                codeFenceLen = 0;
            } else {
                // 代码块内容（含异种围栏行、以围栏开头但不足以闭合的嵌套围栏行）
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
                bqCodeFenceLang = unescapeHtmlEntities(
                    stripped.mid(fenceLen).trimmed());   // 实体解码见顶层围栏同款
                bqCodeFenceLen = fenceLen;
                bqCodeContent.clear();
            } else if (fenceLen >= bqCodeFenceLen && stripped.mid(fenceLen).trimmed().isEmpty()) {
                // 闭合：本行为纯围栏且长度 >= 开启围栏
                QString content = bqCodeContent.join('\n');
                QString codeOpen = bqCodeFenceLang.isEmpty()
                    ? QStringLiteral("<code>")
                    : QStringLiteral("<code class=\"language-") + escapeHtmlAttr(bqCodeFenceLang) + "\">";
                bqCodeBlocks << "<pre style=\" margin-left:40px; margin-right:40px;\">"
                                + codeOpen + content.toHtmlEscaped() + "</code></pre>";
                bqCodeFenceLen = 0;
                // 占位符（前后空行隔离，使 setMarkdown 产出独立 <p> 块）
                if (!processed.isEmpty() && !processed.endsWith("\n\n"))
                    processed += '\n';
                bqCodeBlockPhs << collisionFreePlaceholder(collisions, 
                    "BQ", bqCodeBlocks.size() - 1);
                processed += "> " + bqCodeBlockPhs.last() + "\n\n";
                // 闭合后紧跟空行（块引用结束）：发出边界标记。setMarkdown 会把空行
                // 折叠，扁平 margin-left 块无法区分「同一块引用」与「空行分隔的两个
                // 块引用」，故此标记以顶层段落形态透传，供后处理打破合并。
                // 标记带序号且碰撞检测（原固定串 ZZBQSEP 会与用户字面文本冲突）
                if (next.trimmed().isEmpty()) {
                    bqSepPhs << collisionFreePlaceholder(collisions, 
                        "BQSEP", bqSepPhs.size());
                    processed += bqSepPhs.last() + "\n";
                }
            } else {
                // 块引用代码内容（含以围栏开头但不足以闭合的嵌套围栏行）
                bqCodeContent << stripped;
            }
            lastBlock = BT_NONE;
        } else if (bqCodeFenceLen > 0) {
            // 块引用代码内容行（非围栏行）：剥离 > 前缀收集
            bqCodeContent << stripBlockquotePrefix(line);
            lastBlock = BT_NONE;
        } else if (isFootnoteDef && extractFootnotes) {
            // 脚注定义行 [^id]: 内容（非代码块）—— 抽取归集到文末容器，从正文流移除。
            // 嵌套层（extractFootnotes=false）不进此分支：定义行按字面输出。
            // 块级定义：缩进续行（≥4 空格 / 1 tab）归属定义；空行后仍有缩进行 =
            // 定义内新段落（pandoc 式）；非缩进行（含懒续行）即定义结束留在正文
            // 实体解码：修复历史损坏文件（旧版 fromHtml 不解属性实体，id 含 &
            // 时被写成字面 &amp; 存进 .md，见 escapeHtmlAttr 注释）。解码后与引用
            // 侧（extractFootnoteRefs 同款）保持一致，两端 id 仍配对。
            QString fid = unescapeHtmlEntities(footnoteDefMatch.captured(1).trimmed());
            QString contentMd = footnoteDefMatch.captured(2).trimmed();
            int j = i + 1;
            while (j < lines.size()) {
                const QString &cont = lines[j];
                if (cont.trimmed().isEmpty()) {
                    // 空行不消费：先向后探测，其后首个非空行是缩进行才算定义内段落分隔
                    int k = j;
                    while (k < lines.size() && lines[k].trimmed().isEmpty()) ++k;
                    if (k < lines.size()
                        && (lines[k].startsWith(QStringLiteral("    "))
                            || lines[k].startsWith(QLatin1Char('\t')))) {
                        // 段落分隔只补一个 '\n'：与下方续行统一的 '\n' 前缀合成恰好
                        // 一个空行（contentMd 为 "a\n\nb"），嵌套管线不会触发 K≥2
                        // 空行守卫多出 <p></p> 空段落。多空行（K≥2）折叠为单个
                        // 段落分隔是有意行为（pandoc 兼容）。
                        contentMd += QLatin1Char('\n');
                        j = k;          // 消费中间空行（定义内段落分隔）
                        continue;
                    }
                    break;              // 空行后无缩进行 → 定义结束
                }
                if (!(cont.startsWith(QStringLiteral("    "))
                      || cont.startsWith(QLatin1Char('\t'))))
                    break;              // 非缩进非空 → 定义结束（懒续行留正文）
                contentMd += QLatin1Char('\n');
                contentMd += cont.startsWith(QLatin1Char('\t'))
                                 ? cont.mid(1) : cont.mid(4);   // 去一层缩进
                ++j;
            }
            if (!footnoteDefIds.contains(fid)) {   // 同 id 取首次定义
                footnoteDefIds << fid;
                footnoteDefs << buildFootnoteDefHtml(fid, contentMd);
            }
            processed += '\n';   // 行被移除，留空行维持段落分隔
            i = j - 1;           // 跳过已消费的续行
            lastBlock = BT_NONE;
        } else if (line.trimmed().isEmpty() || isNbspLiteralLine(line)) {
            const bool prevWasList = (lastBlock == BT_LIST);
            // 段落间空行保护（&nbsp; 空行方案）：Qt setMarkdown 会把段落之间的
            // 多个空行（a\n\n\nb）折叠成单个段落分隔，空行信息丢失；且纯空行
            // 不可移植（标准 md 渲染器折叠多空行）。当两个非空块之间有 K 个空行
            // 且 K≥2 时，保留 1 个作段落分隔，其余 K-1 个替换为占位符行，
            // setMarkdown 后还原为 <p></p>（空段落）；fromHtml 把空段落还原为
            // 字面 &nbsp; 行（跨编辑器可移植的空行，见后处理 1.6）。
            // 仅当：前面已有内容 + 上一块非列表 + 下一非空行非列表项。
            // 列表项间空行走下方 ZZLB 注释逻辑（机制不同，互不干扰）。
            // 字面 &nbsp; 行（isNbspLiteralLine）是用户显式空段，恒生成占位符
            // （即使 K=1，否则被 setMarkdown 当作同一段内软换行）；纯空行保留
            // 1 个作段落分隔，其余折叠为占位符。二者混合时 &nbsp; 行优先占位。
            int k = 1;
            int nbspInSeq = isNbspLiteralLine(line) ? 1 : 0;
            while (i + k < lines.size()) {
                const QString &nl = lines[i + k];
                if (nl.trimmed().isEmpty() || isNbspLiteralLine(nl)) {
                    if (isNbspLiteralLine(nl)) ++nbspInSeq;
                    ++k;
                } else break;
            }
            const int j = i + k;
            const bool hasPrevContent = !processed.trimmed().isEmpty();
            const bool paraBlank = ((nbspInSeq > 0 || k >= 2) && hasPrevContent
                                    && !prevWasList
                                    && j < lines.size()
                                    && !isListItemLine(lines[j])
                                    && !isTaskItemLine(lines[j]));
            processed += '\n';
            if (paraBlank) {
                // 占位符数：字面 &nbsp; 行（用户显式空段）各自生成 1 个占位符；
                // 纯空行按连续段折叠——每段长度 L，保留 1 个作段落分隔、L-1 个
                // 折叠为占位符（与原「K 空行 → K-1 占位符」一致；&nbsp; 行两侧
                // 的单空行是段落分隔，不折叠，避免 `a\n\n&nbsp;\n\nb` 多出空段）。
                // 每个占位符行独占一个段落：行后必须补段落分隔空行，否则
                // md4c 把「占位符行\n下一行」当作软换行合并进同一段落，
                // 还原后 b 被吸进 <p><br> b</p>（往返数据错乱）。
                int nPh = 0, pureRun = 0;
                for (int t = 0; t < k; ++t) {
                    if (isNbspLiteralLine(lines[i + t])) {
                        if (pureRun >= 2) nPh += pureRun - 1;
                        pureRun = 0;
                        ++nPh;                     // &nbsp; 行本身 → 1 占位符
                    } else {
                        ++pureRun;
                    }
                }
                if (pureRun >= 2) nPh += pureRun - 1;
                for (int b = 0; b < nPh; ++b) {
                    const QString ph = collisionFreePlaceholder(collisions, 
                        "BL", blankLineCount);
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
            taskLists << buildTaskListHtml(taskLines, subContents, subPhs,
                                           collisions);
            taskListPhs << collisionFreePlaceholder(collisions, 
                "TL", taskLists.size() - 1);
            processed += taskListPhs.last() + "\n";
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
                taskLists << buildTaskListHtml(listLines, subContents, subPhs,
                                               collisions);
                taskListPhs << collisionFreePlaceholder(collisions, 
                    "TL", taskLists.size() - 1);
                processed += taskListPhs.last() + "\n";
                lastBlock = BT_LIST;
            } else {
                // 隔离：正文后紧跟列表时补空行（与 else 分支一致）
                if (lastBlock == BT_PARA) processed += '\n';
                QString cl = line;
                cl = protectTableRowForQt(cl, currentTableCols, tableCellBackups,
                                          tableCellPhs, collisions);
                cl = extractEscapedEquals(cl, escapedEqBackups, escapedEqPhs,
                                          collisions);
                cl = extractEscapedExtDelims(cl, escapedDelimBackups,
                                             escapedDelimPhs, collisions);
                cl = extractSubscripts(cl, subContents, subPhs, collisions);
                cl = extractFootnoteRefs(cl, footnoteRefs, footnoteRefPhs,
                                         collisions, extractFootnotes);
                cl = extractHtmlInlineElements(cl, htmlInlineBackups, htmlInlinePhs,
                                               collisions);
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
            cl = extractEscapedEquals(cl, escapedEqBackups, escapedEqPhs,
                                      collisions);
            cl = extractEscapedExtDelims(cl, escapedDelimBackups,
                                         escapedDelimPhs, collisions);
            cl = extractSubscripts(cl, subContents, subPhs, collisions);
            cl = extractFootnoteRefs(cl, footnoteRefs, footnoteRefPhs,
                                     collisions, extractFootnotes);
            cl = extractHtmlInlineElements(cl, htmlInlineBackups, htmlInlinePhs,
                                           collisions);
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
            cl = extractEscapedEquals(cl, escapedEqBackups, escapedEqPhs,
                                      collisions);
            cl = extractEscapedExtDelims(cl, escapedDelimBackups,
                                         escapedDelimPhs, collisions);
            cl = extractSubscripts(cl, subContents, subPhs, collisions);
            cl = extractFootnoteRefs(cl, footnoteRefs, footnoteRefPhs,
                                     collisions, extractFootnotes);
            cl = extractHtmlInlineElements(cl, htmlInlineBackups, htmlInlinePhs,
                                           collisions);
            brSoftPhs << collisionFreePlaceholder(collisions, 
                "BR", brSoftCount);
            ++brSoftCount;
            processed += cl + " " + brSoftPhs.last() + "\n";
            lastBlock = BT_PARA;
        } else if (isDashHrLine(line) || isHrTagLine(line)) {
            // 水平线 --- / <hr>：占位保护，避免 Qt setMarkdown 折叠连续 --- 或
            // 灾难性误解析；<hr> 标签无 </hr> 配对，不占位会被 escapeUnclosedHtmlTags
            // 转义为字面文本。--- 紧跟前文段落（无空行）时 CommonMark 视为 setext
            // 二级标题下划线；本编辑器按「编辑区语义优先」处理为水平线（同块引用
            // 隔离先例，有意偏离标准——用户写 --- 意图几乎总是分隔线，setext 标题
            // 可用 ## 书写）。占位符是文本段落：与前文段落不隔断会被当同段延续
            //（还原后 <hr/> 与前文同 <p>，非法嵌套）；与后文正文不隔断同理，
            // 两侧仅在紧邻时补空行。
            if (lastBlock == BT_PARA && !processed.endsWith("\n\n"))
                processed += '\n';
            const QString ph = collisionFreePlaceholder(collisions,
                "HR", hrMarkers.size());
            hrPhs << ph;
            hrMarkers << "<hr/>";
            processed += ph + '\n';
            if (!next.isEmpty() && isParagraphLine(next))
                processed += '\n';
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
            cl = protectTableRowForQt(cl, currentTableCols, tableCellBackups,
                                      tableCellPhs, collisions);
            cl = extractEscapedEquals(cl, escapedEqBackups, escapedEqPhs,
                                      collisions);
            cl = extractEscapedExtDelims(cl, escapedDelimBackups,
                                         escapedDelimPhs, collisions);
            cl = extractSubscripts(cl, subContents, subPhs, collisions);
            cl = extractFootnoteRefs(cl, footnoteRefs, footnoteRefPhs,
                                     collisions, extractFootnotes);
            cl = extractHtmlInlineElements(cl, htmlInlineBackups, htmlInlinePhs,
                                           collisions);
            processed += cl + '\n';
            lastBlock = isListItemLine(line) ? BT_LIST
                        : (isParagraphLine(line) ? BT_PARA : BT_NONE);
        }
    }

    // EOF 收尾：块级结构「开启即入占位符列表、闭合才入备份列表」的未闭合状态
    // 处理。行循环耗尽时若结构未闭合，占位符/备份两列表错配——batchRestore
    // Placeholders 按 phs 遍历取 backups[i] 越界直接崩溃；块引用代码围栏虽两
    // 列表均在闭合时生成（不错配），但内容被静默丢弃（往返保真违规）。
    // ① 未闭合块级公式 $$：解析器惯例——未闭合定界符按字面量透传。开启行的
    //    占位符从列表弹出并从 processed 尾部剥离，原文（开启行 + 内容行）回填
    //    为普通文本，由后续管线按正文解析（不得静默丢弃用户内容）。
    if (inMathBlock) {
        const QString ph = mathBlockPhs.takeLast();
        // 开启行追加的占位符必为 processed 末尾（inMathBlock 期间其余行全部
        // 进入 mathLines，无其它分支追加）；防御性 endsWith 校验——不变量被
        // 破坏时保留占位符原文，由 debug 泄漏断言暴露而非静默错删正文
        if (processed.endsWith(ph + QLatin1Char('\n')))
            processed.chop(ph.size() + 1);
        // 相邻行间嵌入 ZZBR 软换行占位符（与正文行对的既有机制一致）：否则
        // setMarkdown 把软换行折叠成空格，行结构在往返中丢失。守卫对齐行内
        // BRMARKER 判定：下一行须为段落延续行（块边界/空行/<br> 行/图片行/
        // 跨块引用边界均不插），本行须为段落行——唯一例外是开符 $$ 行：它在
        // 常规流中是结构行（isParagraphLine 明确排除），但此处已按字面文本
        // 还原，与后续内容行之间的换行是用户结构，须保留
        QStringList restored;
        restored << mathOpenLine << mathLines;
        for (int ri = 0; ri < restored.size(); ++ri) {
            QString rl = restored[ri];
            const QString &nx = (ri + 1 < restored.size())
                ? restored[ri + 1] : QString();
            const bool curOk = !rl.isEmpty()
                && (isParagraphLine(rl) || ri == 0);
            const bool nextOk = !nx.isEmpty() && isParagraphLine(nx)
                && !isBrOnlyLine(nx) && !isImageOnlyLine(nx)
                && !(!isBlockquoteLine(rl) && isBlockquoteLine(nx));
            if (curOk && nextOk) {
                brSoftPhs << collisionFreePlaceholder(collisions,
                    "BR", brSoftCount);
                ++brSoftCount;
                rl += QLatin1Char(' ') + brSoftPhs.last();
            }
            processed += rl + QLatin1Char('\n');
        }
        inMathBlock = false;
    }
    // ② 未闭合代码围栏 ```：CommonMark 惯例——围栏未闭合时内容到文档结束均
    //    为代码块，按闭合同款逻辑补备份（codeContent 已收集到 EOF），
    //    codeBlockPhs 与 codeBlocks 恢复一致
    if (codeFenceLen > 0) {
        QString content = codeContent.join('\n');
        QString codeOpen = codeFenceLang.isEmpty()
            ? QStringLiteral("<code>")
            : QStringLiteral("<code class=\"language-") + escapeHtmlAttr(codeFenceLang) + "\">";
        codeBlocks << "<pre>" + codeOpen + content.toHtmlEscaped() + "</code></pre>";
        codeFenceLen = 0;
    }
    // ③ 未闭合块引用内代码围栏 > ```：占位符在闭合时才生成（不会崩溃），但
    //    EOF 未闭合时已收集内容无处还原（静默丢弃）。按闭合同款逻辑补还原
    //    （EOF 无后续行，不发 bqSep 边界标记）
    if (bqCodeFenceLen > 0) {
        QString content = bqCodeContent.join('\n');
        QString codeOpen = bqCodeFenceLang.isEmpty()
            ? QStringLiteral("<code>")
            : QStringLiteral("<code class=\"language-") + escapeHtmlAttr(bqCodeFenceLang) + "\">";
        bqCodeBlocks << "<pre style=\" margin-left:40px; margin-right:40px;\">"
                        + codeOpen + content.toHtmlEscaped() + "</code></pre>";
        bqCodeFenceLen = 0;
        if (!processed.isEmpty() && !processed.endsWith("\n\n"))
            processed += '\n';
        bqCodeBlockPhs << collisionFreePlaceholder(collisions,
            "BQ", bqCodeBlocks.size() - 1);
        processed += "> " + bqCodeBlockPhs.last() + "\n\n";
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
    {
        // 1) 暂存行内代码片段。注意：processed 是多行 buffer，正则必须排除
        // \n（[^`\n]）——若允许跨行配对，双反引号写法（`` `x` ``）会与后续
        // 行的反引号误配对，把多行文本吞成一个"代码段"，替换占位符后多行
        // 被压成一行，后续还原与解析全部错乱。
        QStringList codes;
        static const QRegularExpression codeSpanRe("`([^`\n]+)`");
        globalReplaceWith(processed, codeSpanRe,
            [&codes](const QRegularExpressionMatch &m) {
                codes << m.captured(0);
                return "\x01CODE" + QString::number(codes.size() - 1) + "\x01";
            });
        // 2) 保护 <div>。捕获文本含 \x01（行内代码占位符）时跳过——说明该
        // <div> 与 </div> 之间的配对不可靠（占位符还原后才是真实文本），
        // 此时不能备份，否则占位符被吞进 divBackups、步骤 3 还原不到，
        // 最终 \x01 泄漏进 HTML。单遍重建（stashPlainDivs：跳过分支在捕获
        // 段内递归，保旧 offset+1 重叠语义）。
        processed = stashPlainDivs(processed, collisions, divBackups, divPhs);
        // 3) 还原行内代码
        for (int i = 0; i < codes.size(); ++i)
            processed.replace("\x01CODE" + QString::number(i) + "\x01", codes[i]);
    }

    // 强调包裹行内代码 → 占位符保护（绕开 md4c 对 `*`/`**` 直接包 code span 的
    // 解析缺陷）。md4c 会把 `**`code`**` 的加粗标记直接丢弃、把 `*…`code`…*`
    // 在 code 处分段重编码，导致 MD→HTML→MD 往返结构漂移。此处把「含 ≥1 个
    // code span、内部无星号」的强调区间整体占位，setMarkdown 后还原为统一
    // <strong>/<em> 包 <code> 的 HTML——fromHtml 侧已正确处理该形态（code
    // 占位符保护 + Qt 序列化），往返闭环。直接包裹（`**`code`**`）与跨句包裹
    // （`*X `code` Y*`）统一覆盖。
    // 必须置于 escapeUnclosedHtmlTags 之前：区间内代码/文本若含 < 已转义，这里
    // 会二次转义。此时代码围栏已抽成 ZZCB 占位符，围栏内字面 `*`code`*` 文本
    // 不受影响。
    {
        // 强调区间内部 → HTML：文本段按纯文本转义；`code` 包 <code>（内容转义）。
        // 区间内若有其它 markdown 语法（链接等）按纯文本处理——已知局限，罕见
        // 且 md4c 原行为同样失真。
        auto emphCodeEscape = [](const QString &content) -> QString {
            QString out;
            int i = 0;
            static const QRegularExpression csRe(R"(`([^`\n]+)`)");
            auto it = csRe.globalMatch(content);
            while (it.hasNext()) {
                auto m = it.next();
                out += content.mid(i, m.capturedStart() - i).toHtmlEscaped();
                out += QStringLiteral("<code>") + m.captured(1).toHtmlEscaped() + "</code>";
                i = m.capturedEnd();
            }
            out += content.mid(i).toHtmlEscaped();
            return out;
        };
        auto protectEmphCode = [&](const QRegularExpression &re, const char *prefix,
                                   const char *tag, bool adjStarIsChain) {
            // 链式结构判定：从强调符起始向前扫描，若在遇到 code span（反引号）
            // 或行首前先遇到 `*`，则该强调符是「code 间文本加粗」链的闭合/开启符
            // （如 `code1`**（约 **`code2`**）——md4c 把 code 间的文本加粗且往返
            // 稳定），不应保护——保护会偷走配对、破坏链条。
            // 仅在「前面没有 `*` 配对、md4c 必然丢弃加粗/分段斜体」的隔离强调上保护。
            // adjStarIsChain：匹配起点紧邻 `*`（开符 run 比本趟正则消费的更长）
            // 时的定性，两趟语义不同——
            //   加粗趟 false：***`x`*** 的复合定界符直包 code，md4c 全部丢弃，
            //     按隔离保护（矩阵回归 bold(italic(code)) 抓获）；
            //   斜体趟 true：加粗趟已先扫过同一 run（保护的已变占位符，剩下的
            //     星号即链式放行），此处内层 * 若再保护会偷走 run 星号破坏链条
            //     （`**（约 **`b`**` 退化为 <em><em><code>，用户链式用例回归）。
            auto isChainDelimiter = [&processed, adjStarIsChain](int pos) -> bool {
                for (int i = pos - 1; i >= 0; --i) {
                    const QChar c = processed.at(i);
                    if (c == QLatin1Char('\n')) return false;   // 行首：新强调上下文
                    if (c == QLatin1Char('`')) return false;    // 前有 code span：隔离
                    if (c == QLatin1Char('*')) {
                        // 转义星号（\*、\*\*）不是强调符，跳过继续扫描：
                        // `*` 前连续反斜杠为奇数个则被转义
                        int bs = 0;
                        int j = i - 1;
                        while (j >= 0 && processed.at(j) == QLatin1Char('\\')) {
                            ++bs;
                            --j;
                        }
                        if (bs % 2 == 1)
                            continue;   // 转义星号，非强调符
                        if (i == pos - 1)
                            // 紧邻匹配起点的真实星号（开符 run 比本趟正则消费的
                            // 更长）：定性交 adjStarIsChain，两趟语义见声明处注释
                            return adjStarIsChain;
                        return true;    // 真实强调符（有文本相隔）：链式
                    }
                }
                return false;  // 串首：隔离
            };
            int n = 0;
            globalReplaceWith(processed, re,
                [&](const QRegularExpressionMatch &m) {
                    // 隔离强调保护；链式（前面还有 `*`）原样保留交 md4c
                    if (isChainDelimiter(m.capturedStart()))
                        return m.captured();
                    // 强调符被反斜杠转义（\* 是字面星号非强调符）：md4c 视其为
                    // 字面量，保护会错误加粗并残留反斜杠——如 \*\*\`\<mark\>`\*\*
                    // （转义字面 **`<mark>`**）会被 em 正则当斜体区间保护，还原
                    // 后 <em>/<code> 间夹带 \ 字面量，反复往返反斜杠指数累积。
                    // 开、闭两侧的 `*` 都须非转义（闭侧 `*` run 定位首字符）。
                    auto isEscaped = [&processed](int pos) -> bool {
                        int bs = 0;
                        for (int j = pos - 1; j >= 0
                             && processed.at(j) == QLatin1Char('\\'); --j)
                            ++bs;
                        return (bs % 2) == 1;
                    };
                    if (isEscaped(m.capturedStart()))
                        return m.captured();
                    {
                        int cs = m.capturedEnd() - 1;          // 闭符最后一颗 *
                        while (cs > 0 && processed.at(cs - 1) == QLatin1Char('*'))
                            --cs;                              // 定位闭符 run 首字符
                        if (isEscaped(cs))
                            return m.captured();
                    }
                    const QString content = m.captured(1);
                    // 强调区间首尾的空白不再跳过保护（2026-08-28 决策）：md4c 按
                    // CommonMark 边界规则把 `** `code`**` 判为字面（** 后空格非
                    // 强调符），转义成 \*\*；但 WYSIWYG 里 TipTap 加粗输入规则
                    // 比 CommonMark 宽松，同样的输入会生成真实加粗——两条路径
                    // 不一致。为对齐 WYSIWYG，此处连空格包裹也保护为强调，
                    // 空格保留在区间内随往返稳定（`** `code`**` → ** `code`**）。
                    QString ph = collisionFreePlaceholder(collisions, prefix, n++);
                    emphCodePhs << ph;
                    emphCodeHtml << QLatin1String("<") + tag + ">"
                                     + emphCodeEscape(content)
                                     + QLatin1String("</") + tag + ">";
                    return ph;
                });
        };
        // 加粗：**…**，内部无 `*`，含 ≥1 个 code span。
        // 文本段排除反引号 [^*`\n]：反引号是 code span 边界，若允许则贪婪/占有
        // 量词会吞掉 code span 反引号导致不匹配。排除后配合占有量词 *+：无 code
        // span 的普通 `**bold**` 在 `[^`\n]+` 处立即失败且不回溯（否则长文本 +
        // 大量 `**` 触发 O(n²) 回溯，见 markdownhandler-okn-perf-fix 同源教训）。
        protectEmphCode(
            QRegularExpression(R"(\*\*([^*`\n]*+(?:`[^`\n]+`[^*`\n]*+)+)\*\*)"),
            "EC", "strong", false);
        // 斜体：*…*，同上（加粗先占位后，剩余 `*` 不构成误匹配）；紧邻星号
        // 视为链式（加粗趟已扫过同一 run，见 protectEmphCode 声明处注释）
        protectEmphCode(
            QRegularExpression(R"(\*([^*`\n]*+(?:`[^`\n]+`[^*`\n]*+)+)\*)"),
            "EM", "em", true);
        // 删除线：~~…~~ 包裹行内代码 → 占位符保护。md4c 对 ~~`code`~~ 直接丢弃
        // 删除线（只留 <span monospace>code</span>）、对 ~~X `code` Y~~ 在 code 处
        // 分段重编码（删除线丢在 code 上），与 `*`/`**` 同源缺陷，故同样整段占位
        // 还原为 <s>…<code>…</code>…</s>（文本段转义、code 包 <code>，复用
        // emphCodeEscape）。与星号不同：md4c 对「含 code span 的删除线区间」从不
        // 稳定保留（`c1`~~X~~`c2` 链式删除线的 ~~X~~ 无 code span，不匹配本正则，
        // 天然豁免），故无需链式判定；仅需防转义（\~~ 是字面 ~~ 非删除线）。
        // 必须置于 emphCode 之后：`**…`code`…**` 先占位后，~~…~~ 区间内的 `*`
        // 属字面文本，交 emphCodeEscape 按纯文本转义（已知局限，同 emphCode）。
        auto protectStrikeCode = [&](const QRegularExpression &re) {
            int n = 0;
            globalReplaceWith(processed, re,
                [&](const QRegularExpressionMatch &m) {
                    // 开、闭 `~~` 被反斜杠转义（\~~ 字面量）时跳过保护，交 md4c
                    // 当字面量处理（保护会错误加删除线并残留反斜杠）
                    auto isEscaped = [&processed](int pos) -> bool {
                        int bs = 0;
                        for (int j = pos - 1; j >= 0
                             && processed.at(j) == QLatin1Char('\\'); --j)
                            ++bs;
                        return (bs % 2) == 1;
                    };
                    if (isEscaped(m.capturedStart()))
                        return m.captured();
                    {
                        int cs = m.capturedEnd() - 1;     // 闭符最后一颗 ~
                        while (cs > 0 && processed.at(cs - 1) == QLatin1Char('~'))
                            --cs;                          // 定位闭符 run 首字符
                        if (isEscaped(cs))
                            return m.captured();
                    }
                    const QString content = m.captured(1);
                    QString ph = collisionFreePlaceholder(collisions, "SC", n++);
                    emphCodePhs << ph;
                    emphCodeHtml << QLatin1String("<s>")
                                     + emphCodeEscape(content)
                                     + QLatin1String("</s>");
                    return ph;
                });
        };
        // 文本段排除 `~` 与反引号 [^~`\n]：`~code~`（单~下标）与 ~~…~~ 不混；
        // 反引号必须排除——否则 `[^~]` 占有量词把 `code` 当文本贪走，code span
        // 分支无法接管、无回溯 → 不匹配（与加粗正则同构，缺反引号排除即失效）。
        // 排除后配合占有量词 *+ 防 O(n²) 回溯（同加粗）。
        protectStrikeCode(
            QRegularExpression(R"(~~([^~`\n]*+(?:`[^`\n]+`[^~`\n]*+)+)~~)"));
    }

    // 转义未闭合的 HTML 标签（见 escapeUnclosedHtmlTags）：Qt setMarkdown 会把
    // 段落内未闭合的 <tag> 当 HTML 块吞噬其后内容到文末（数据丢失）。须在行内
    // 代码与 <div> 占位保护之后执行——此时受支持的标签已全部占位，剩余未闭合
    // 标签才安全转义为实体。
    escapeUnclosedHtmlTags(processed);

    // 空 alt 图片保形（须在任何解析之前）：Qt setMarkdown 会整条丢弃空 alt 图片
    protectEmptyAltImages(processed, collisions, emptyAltSentinels);

    QTextDocument doc;
    doc.setMarkdown(processed);
    QString html = doc.toHtml();

    // 后处理 -1: 清空空 alt 图片的占位 alt（见 protectEmptyAltImages）。
    //   占位 alt 只用于让 Qt 保留图片，此处统一还原为 alt=""。通用 token 正则
    //   + 集合判断（不按个数拼 alternation），未命中集合的是用户字面同形文本，
    //   原样保留。
    if (!emptyAltSentinels.isEmpty()) {
        QSet<QString> sentinelSet(emptyAltSentinels.cbegin(),
                                  emptyAltSentinels.cend());
        globalReplaceWith(html,
            QRegularExpression(QStringLiteral("alt=\"(ZZ[A-Z]+[0-9]+ZZ)\"")),
            [&sentinelSet](const QRegularExpressionMatch &m) {
                return sentinelSet.contains(m.captured(1))
                           ? QStringLiteral("alt=\"\"") : m.captured(0);
            });
    }

    // 后处理 0: 链式级联中被 md4c 整对丢弃的直接包裹 code 补 <strong>/<em>。
    //   必须在一切占位符还原之前：此刻 html 中 mono span 恰为 md4c 从 processed
    //   渲染出的全部行内代码，与 MD 侧候选逐一对应；还原后引入的 <code> 会破坏
    //   对应关系（见 wrapDiscardedEmphCode 头注释）。
    wrapDiscardedEmphCode(html, processed);

    // 后处理 1: 还原代码块占位符 ZZCB → <pre><code class="language-X">…</code></pre>
    //   代码块内容在预处理阶段已整体收集并 HTML 转义，未经过 setMarkdown，
    //   因此可正确保留嵌套围栏（如 ```markdown 内嵌 ```mermaid）。
    //   占位符串用 codeBlockPhs（collisionFreePlaceholder 存储值，序号可能非连续）。
    batchRestorePlaceholders(html, codeBlockPhs, codeBlocks);

    // 后处理 1.4: 还原块引用代码块占位符 ZZBQ → <pre margin-left:40px>…</pre>
    //   占位符以块引用行发出，setMarkdown 产出 <p margin-left>ZZBQnZZ</p>；还原为
    //   <pre margin-left>…</pre> 后由后处理 2 的块引用重构合并进 <blockquote>。
    batchRestorePlaceholders(html, bqCodeBlockPhs, bqCodeBlocks);

    // 后处理 1.5: 还原水平线占位符 ZZHR → <hr/>
    //   连字符水平线在预处理阶段已占位保护（Qt setMarkdown 会折叠/误解析 ---），
    //   此处还原为 <hr/>。占位符独立成段时连同 <p> 一起替换。
    batchRestorePlaceholders(html, hrPhs, hrMarkers);

    // 后处理 1.6: 还原段落间连续空行占位符 ZZBL → <p></p>（空段落）
    //   源码 K 个连续空行（不可移植，标准 md 折叠）→ K-1 个占位符段落，此处
    //   还原为空段落，fromHtml 识别空段落并还原为字面 &nbsp; 行（跨编辑器
    //   可移植的空行）。必须还原为 <p></p> 而非 <p><br></p>：<br> 在段内产生
    //   换行，空段落渲染成 2 行高（实测 61px ≈ 2 行），视觉间距翻倍无规律；
    //   <p></p> 与 TipTap Enter 原生空段落一致（1 行高 31px），渲染正确。
    //   带 <p> 包裹的还原为 <p></p>，裸占位符（混排时）删除。
    //   通用 token 正则 + 集合成员判断（不按个数拼 alternation，防超 PCRE2
    //   编译上限，见 batchRestorePlaceholders 注释）；未命中集合的是用户字面
    //   ZZ 形态文本，原样保留。
    if (!blankLinePhs.isEmpty()) {
        QSet<QString> phSet(blankLinePhs.cbegin(), blankLinePhs.cend());
        globalReplaceWith(html,
            QRegularExpression(QStringLiteral("<p[^>]*>(ZZ[A-Z]+[0-9]+ZZ)</p>")),
            [&phSet](const QRegularExpressionMatch &m) {
                return phSet.contains(m.captured(1))
                           ? QStringLiteral("<p></p>") : m.captured(0);
            });
        globalReplaceWith(html,
            QRegularExpression(QStringLiteral("ZZ[A-Z]+[0-9]+ZZ")),
            [&phSet](const QRegularExpressionMatch &m) {
                return phSet.contains(m.captured()) ? QString() : m.captured(0);
            });
    }

    // 后处理 2: ZZBR<n>ZZ → <br data-brk>（占位符以区分用户手写 <br>）
    // 嵌入处 cl + " " + 占位符 在标记前补了一个空格作为隔离符，防止标记被
    // 前文行尾标点（*、_、` 等）吞入内联结构；setMarkdown 又在标记后（原换行处）
    // 生成一个空格。这两个空格都是标记机制的伪影，替换时必须一并删除，否则
    // 残留为软换行行尾空格，破坏往返保真。故先匹配「空格+占位符+空格」消费两侧
    // 空格，再降级匹配仅一侧有空格的情形。占位符带序号，用户文档中字面
    // "BRMARKER"（含行内代码内）不会被误替换。
    // 单遍重建：正则「可选前空格 + 占位符 + 可选后空格」一次性消费两侧空格
    // （等效原 4 次降级替换，O(K·n) → O(n)）。
    // 占位符用通用 token 模式 + 集合成员判断（不按个数拼 alternation：2 万+
    // 软换行时巨型多选正则超 PCRE2 编译上限变 invalid，还原空转 → 占位符泄漏
    // → Debug 构建泄漏断言 abort）；未命中集合的是用户字面 ZZ 形态文本，
    // 返回整段匹配原样保留（两侧空格不被吞）。
    if (!brSoftPhs.isEmpty()) {
        QSet<QString> phSet(brSoftPhs.cbegin(), brSoftPhs.cend());
        globalReplaceWith(html,
            QRegularExpression(QStringLiteral(" ?(ZZ[A-Z]+[0-9]+ZZ) ?")),
            [&phSet](const QRegularExpressionMatch &m) {
                return phSet.contains(m.captured(1))
                           ? QStringLiteral("<br data-brk>") : m.captured(0);
            });
    }

    // 还原任务列表占位符 ZZTL → TipTap taskList 契约 HTML
    //   先于 mark/上下标/行内公式转换还原，条目文本中的相应语法也能被转换；
    //   ZZSB（下标）占位符由后续统一步骤还原
    batchRestorePlaceholders(html, taskListPhs, taskLists);

    // 还原强调包裹行内代码占位符 ZZEC/ZZEM → <strong>/<em> 包 <code> HTML。
    // 多趟单遍还原（同 fromHtml 占位符还原范式）：backup 可能内嵌其它占位符
    // （如 `*text **`code`** more*` 的 em backup 含加粗 ZZEC），须多趟让内嵌
    // token 逐趟展开；置后于任务列表、先于 sub/math 等后续还原，内嵌的
    // ZZSB/ZZMB 等 token 仍可在后续步骤被还原，避免泄漏。
    if (!emphCodePhs.isEmpty()) {
        QHash<QString, QString> phMap;
        phMap.reserve(emphCodePhs.size());
        for (int i = 0; i < emphCodePhs.size(); ++i)
            phMap.insert(emphCodePhs[i], emphCodeHtml[i]);
        // 通用 token 正则 + 查表（不按个数拼 alternation，见
        // batchRestorePlaceholders 注释）；未命中原样保留，且不置 replaced
        // （否则用户字面 ZZ 文本会让多趟循环空转致死循环）
        static const QRegularExpression allRe(R"(ZZ[A-Z]+[0-9]+ZZ)");
        for (;;) {
            bool replaced = false;
            globalReplaceWith(html, allRe,
                [&phMap, &replaced](const QRegularExpressionMatch &m) {
                    auto it = phMap.constFind(m.captured());
                    if (it == phMap.constEnd())
                        return m.captured(0);   // 用户字面 ZZ 文本，保留
                    replaced = true;
                    return *it;
                });
            if (!replaced) break;
        }
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
        // 两段式扫描（O(n)）：先用无嵌套回溯的廉价标签模式单遍 globalMatch 找出
        // 全部 <p>/<pre> 开标签（[^>]*> 单向扫描到 '>' 即成，无 O(属性长²) 回溯），
        // 再对属性含 margin-left:40px+margin-right:40px 的标签用 indexOf 找同型
        // 闭合标签。原单正则 <([^>]*margin-left…[^>]*margin-right…)>(.*?)</\1>
        // 在每个标签处都要 O(属性长²) 回溯，3MB 文档（数万标签）烧秒级主线程
        // CPU（GNOME「应用无响应」）。行为与原正则一致：闭合取最近同型标签
        // （原懒惰量词 (.*?)</\1> 同样停在首个闭合处）、无闭合则跳过该标签、
        // 后块起点不得落入前块区间（原 off=capturedEnd 同义）。
        // <(p|pre)\b 的 \b 必须保留（见上），\b 与 [^>]* 均无回溯放大。
        static const QRegularExpression tagRe(R"(<(p|pre)\b[^>]*>)");
        static const QRegularExpression marginRe(
            R"(margin-left:\s*40px[^>]*margin-right:\s*40px)");

        struct BqBlock { int start = 0; int end = 0; bool isPre = false; };
        QVector<BqBlock> blocks;
        auto it = tagRe.globalMatch(html);
        while (it.hasNext()) {
            auto m = it.next();
            if (!blocks.isEmpty() && m.capturedStart() < blocks.constLast().end)
                continue;   // 落在前块区间内（原循环 off 起点等价排除）
            const QString tag = m.captured(0);
            // 廉价预检：绝大多数标签无 margin-left，一次 indexOf 短路
            if (!tag.contains(QLatin1String("margin-left")))
                continue;
            if (!marginRe.match(tag).hasMatch())
                continue;
            const bool isPre = (m.captured(1) == QLatin1String("pre"));
            const QLatin1String closeTag(isPre ? "</pre>" : "</p>");
            const int close = html.indexOf(closeTag, m.capturedEnd());
            if (close < 0)
                continue;   // 无同型闭合：原正则回溯后同该起点失败
            BqBlock b;
            b.start = m.capturedStart();
            b.end = close + closeTag.size();
            b.isPre = isPre;
            blocks << b;
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

    // 后处理 2.5: 移除块引用代码块的边界标记 ZZBQSEP<n>。
    //   标记以顶层段落透传，用于打破块引用重构的「连续 margin-left 块」合并，
    //   使「> 代码\n\n> 引用」空行分隔的两个块引用不被误合并为一个。此处移除，
    //   相邻 <blockquote>（或 <blockquote> 与正文）由 Qt toMarkdown 自动补空行。
    //   按存储的实际占位符串移除（带序号 + 碰撞检测，不误删用户字面文本）：
    //   通用 token 正则 + 集合成员判断，未命中集合的用户字面文本原样保留
    //   （不按个数拼 alternation 的原因见 batchRestorePlaceholders 注释）
    if (!bqSepPhs.isEmpty()) {
        QSet<QString> phSet(bqSepPhs.cbegin(), bqSepPhs.cend());
        globalReplaceWith(html,
            QRegularExpression(
                QStringLiteral("<p[^>]*>(ZZ[A-Z]+[0-9]+ZZ)</p>\\s*")),
            [&phSet](const QRegularExpressionMatch &m) {
                return phSet.contains(m.captured(1)) ? QString() : m.captured(0);
            });
    }
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
        // 单遍重建：globalMatch 先行取全部 <pre>/<code>（__CODE<n>__ 令牌不含
        // 标签，不影响后续匹配），替换串不参与二次扫描，O(K·n) → O(n)。
        QRegularExpression codeRe(
            R"(<(pre|code)[^>]*>.*?</\1>)",
            QRegularExpression::DotMatchesEverythingOption);
        globalReplaceWith(html, codeRe,
            [&codeBlocks](const QRegularExpressionMatch &m) {
                codeBlocks << m.captured();
                return QString("__CODE%1__").arg(codeBlocks.size() - 1);
            });
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
        globalReplaceWith(html, mathSpanRe,
            [&mathSpans](const QRegularExpressionMatch &m) {
                mathSpans << m.captured();
                return QString("__IMATH%1__").arg(mathSpans.size() - 1);
            });
        // 上标（Pandoc 语法）：^x^ → <sup>
        //   下标 ~x~ 已在 setMarkdown 前替换为占位符（Qt 会把 ~x~ 当作删除线）。
        //   此时行内公式已被 __IMATH 占位符保护、块级公式已是 ZZMB 占位符，安全。
        html.replace(QRegularExpression("\\^([^\\^\\n]+?)\\^"),
                     "<sup>\\1</sup>");
        // 恢复 math span：__IMATH<n>__ → 原始 span（map 兜底未命中原样保留，
        // 防用户字面 __IMATH\d+__ 越界，与旧逐 token replace 行为一致）
        if (!mathSpans.isEmpty()) {
            QHash<QString, QString> imathMap;
            imathMap.reserve(mathSpans.size());
            for (int i = 0; i < mathSpans.size(); ++i)
                imathMap.insert(QString("__IMATH%1__").arg(i), mathSpans[i]);
            globalReplaceWith(html, QRegularExpression(QStringLiteral("__IMATH\\d+__")),
                [&imathMap](const QRegularExpressionMatch &m) {
                    return imathMap.value(m.captured(), m.captured());
                });
        }
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
            static const QRegularExpression boldStyleRe(
                "\\bfont-weight\\s*:\\s*700\\b");
            static const QRegularExpression italicStyleRe(
                "\\bfont-style\\s*:\\s*italic\\b");
            // static const 正则不可出现在捕获列表（MSVC C3495 / Clang 报错，
            // GCC 扩展容忍——Linux 测不出），静态存储变量体内直接引用即可
            globalReplaceWith(html, lineThroughSpanRe,
                []
                (const QRegularExpressionMatch &m) {
                    QString content = m.captured(1);
                    QString replacement = QStringLiteral("<s>%1</s>").arg(content);
                    auto sm = styleAttrRe.match(m.captured());
                    QString style = sm.hasMatch() ? sm.captured(1) : QString();
                    const bool hasBold = style.contains(boldStyleRe);
                    const bool hasItalic = style.contains(italicStyleRe);
                    if (hasBold && hasItalic)
                        replacement = QStringLiteral("<strong><em><s>%1</s></em></strong>").arg(content);
                    else if (hasBold)
                        replacement = QStringLiteral("<strong><s>%1</s></strong>").arg(content);
                    else if (hasItalic)
                        replacement = QStringLiteral("<em><s>%1</s></em>").arg(content);
                    return replacement;
                });
        }
        // 还原转义等号 ZZES → =（在 ==→<mark> 之后，避免还原的 = 参与匹配）
        batchReplaceExact(html, escapedEqPhs, escapedEqBackups);
        // 恢复 <code>/<pre>：__CODE<n>__ → 原始 <pre>/<code>（map 兜底未命中原样
        // 保留，防用户字面 __CODE\d+__ 越界，与旧逐 token replace 行为一致）
        if (!codeBlocks.isEmpty()) {
            QHash<QString, QString> codeMap;
            codeMap.reserve(codeBlocks.size());
            for (int i = 0; i < codeBlocks.size(); ++i)
                codeMap.insert(QString("__CODE%1__").arg(i), codeBlocks[i]);
            globalReplaceWith(html, QRegularExpression(QStringLiteral("__CODE\\d+__")),
                [&codeMap](const QRegularExpressionMatch &m) {
                    return codeMap.value(m.captured(), m.captured());
                });
        }
    }
    // 还原 <div> 占位符 ZZDV → 原始 <div>
    // 必须先于 ZZHI/ZZFR/ZZSB 等内层占位符还原：div 内部可能含这些占位符
    // （如 <div><span style>…</span></div>），先还原外层 div，内层占位符才能
    // 被后续各自的还原步骤继续处理（与 ZZHI 先于 ZZFR 同理）。
    // 独占段落的占位符须连 <p> 一起替换：Qt setMarkdown 把占位符包在 <p> 中，
    // 若保留 p 壳，产出 <p><div>…</div></p> 非法嵌套，TipTap 解析时把块级
    // divBlock 提升出 <p>、却把空 <p> 壳留在原位，渲染为 div 前后各一条空行
    // （与块级图片解包同类问题，见下方 <p><img/></p> 解包注释）。
    batchRestorePlaceholders(html, divPhs, divBackups);

    // 修复 <hN><span font-size:xx-large><div align="X">text</div></span></hN>
    // → <hN style="text-align:X">text</hN>
    // QTextDocument 把 <div> 放在标题内，但 TipTap divBlock 是 block 节点不
    // 能嵌套在标题中。此处提取 <div align> → <hN style="text-align:">，
    // 利用已有 TextAlign 扩展渲染居中标题，fromHtml 的 ZZA 机制负责往返还原。
    {
        QRegularExpression re(
            R"re(<(h[1-6])\b[^>]*>\s*<span\b[^>]*\bfont-size\s*:\s*xx-large[^>]*>\s*<div\b[^>]*\balign\s*=\s*"([^"]*)"[^>]*>(.*?)</div>\s*</span>\s*</\1>)re",
            QRegularExpression::DotMatchesEverythingOption);
        globalReplaceWith(html, re,
            [](const QRegularExpressionMatch &m) {
                return QString("<%1 style=\"text-align:%2\">%3</%1>")
                    .arg(m.captured(1), m.captured(2), m.captured(3));
            });
    }

    // 兜底：剥离标题内 QTextDocument 注入的 font span（font-size + font-weight）
    // 上面重组 regex 只处理含 <div align> 的标题且限于 h1（xx-large），工具栏
    // 反复操作后 TipTap getHTML() 可能产生非常规格式导致重组不匹配，未剥离的
    // font span（font-weight:700）被 TipTap Bold parseHTML 解析为 <strong> mark，
    // 后续往返持续污染。此处兜底剥离所有 h1-h6 标题内的标题级 font span。
    //
    // 只删该 span 的**开闭标签**、保留内容与其间其余内联标记（平衡扫描配对）。
    // 早前用单正则 <hN…><span font-size…>(.*?)</span></hN> 整体重组，惰性捕获组
    // 在「标题含 ≥2 个内联段」时会吞掉中间标记：`# a **c**` 的 Qt 输出是
    // <span font-size…;font-weight:700>a </span><span font-weight:700>c</span>，
    // 惰性组只能扩张到末尾的 </span> 才能满足末尾的 </hN>，于是产出
    // <h1 …>a </span><span style=" font-weight:700;">c</h1>——**悬空 </span> 的
    // 畸形 HTML**（TipTap 仅靠 DOMParser 容错），且 `**c**` 的强调信息在后续
    // 保存中丢失。仅当 font-size 为标题级关键字（xx-large/x-large/large）才剥离，
    // 用户手写的其它 font-size span 不受影响。
    {
        static const QRegularExpression headingRe(
            R"re(<(h[1-6])\b[^>]*>.*?</\1>)re",
            QRegularExpression::DotMatchesEverythingOption);
        static const QRegularExpression headingFontSpanRe(
            R"re(<span\b[^>]*\bfont-size\s*:\s*(?:xx-large|x-large|large)\b[^>]*>)re");
        globalReplaceWith(html, headingRe,
            [](const QRegularExpressionMatch &hm) {
                const QString h = hm.captured();
                if (!h.contains(QLatin1String("font-size")))
                    return h;   // 无标题级 font span：原样返回
                QString out;
                out.reserve(h.size());
                int pos = 0;
                while (true) {
                    const auto sm = headingFontSpanRe.match(h, pos);
                    if (!sm.hasMatch()) break;
                    const int closeStart = findMatchingSpanClose(h, sm.capturedStart());
                    if (closeStart < 0) { pos = sm.capturedEnd(); continue; }
                    out += h.mid(pos, sm.capturedStart() - pos);   // 开标签之前
                    out += h.mid(sm.capturedEnd(), closeStart - sm.capturedEnd());
                    pos = closeStart + 7;                          // 跳过 </span>
                }
                out += h.mid(pos);
                return out;
            });
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
        static const QRegularExpression fwSpanRe(
            QStringLiteral("<span\\b[^>]*\\bfont-weight\\s*:\\s*700[^>]*>(.*?)</span>"),
            QRegularExpression::DotMatchesEverythingOption);
        globalReplaceWith(html, tableFirstRowRe,
            [](const QRegularExpressionMatch &m) {
                QString rowContent = m.captured(2);
                rowContent.replace(fwSpanRe, "\\1");
                return m.captured(1) + rowContent + m.captured(3);
            });
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
        globalReplaceWith(html, tableRegionRe,
            [&cellOpenRe, &cellPAlignRe](const QRegularExpressionMatch &m) {
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
            return m.captured(1) + region + m.captured(3);
        });
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
        static const QRegularExpression boldStyleRe(
            "\\bfont-weight\\s*:\\s*700\\b");
        static const QRegularExpression italicStyleRe(
            "\\bfont-style\\s*:\\s*italic\\b");
        static const QRegularExpression colorStyleRe("\\bcolor\\s*:");
        static const QRegularExpression bgColorStyleRe(
            "\\bbackground-color\\s*:");
        // 单遍重建：需转义语义标签的 span 替换，其余原样保留（m.captured()），
        // 替代 while(true) match + html.replace（O(K·n) → O(n)）。
        globalReplaceWith(html, spanStyleRe,
            []
            (const QRegularExpressionMatch &m) {
                QString style = m.captured(1);
                QString content = m.captured(2);

                bool hasBold = style.contains(boldStyleRe);
                bool hasItalic = style.contains(italicStyleRe);
                bool hasColor = style.contains(colorStyleRe);
                bool hasBg = style.contains(bgColorStyleRe);

                if (!((hasBold || hasItalic) && !hasColor && !hasBg))
                    return m.captured();   // 含用户样式的 span 原样保留
                QString replacement = content;
                if (hasBold && hasItalic)
                    replacement = QStringLiteral("<strong><em>%1</em></strong>").arg(content);
                else if (hasBold)
                    replacement = QStringLiteral("<strong>%1</strong>").arg(content);
                else
                    replacement = QStringLiteral("<em>%1</em>").arg(content);
                return replacement;
            });
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
        static const QRegularExpression spanOpenRe("<span\\b[^>]*>");
        globalReplaceWith(html, linkTagRe,
            [](const QRegularExpressionMatch &m) {
                QString inner = m.captured(2);
                inner.remove(spanOpenRe);
                inner.remove("</span>");
                return "<a" + m.captured(1) + inner + "</a>";
            });
    }

    // 还原 HTML 内联标签占位符 ZZHI → 原始 <span style> / <u>
    // 必须先于 ZZFR/ZZSB 还原：ZZHI 占位符内容可能含 <span>/<u> 内部的
    // ZZSB/ZZFR 占位符（如 <span>~2~</span>、<u>[^1]</u>），先开外层标签，
    // 内层占位符才能被后续 ZZFR/ZZSB 还原步骤继续处理。
    // 多趟循环：嵌套字面 HTML（如 ==<sub>a<u>u</u>b</sub>== 重载时 uRe 先提
    // 内层 <u>、subRe 再把含 ZZHI0ZZ 的 <sub> 整段提为外层占位符）外层备份
    // 内嵌内层占位符，还原注入后单趟 batchReplaceExact 已扫过——新注入的内层
    // 占位符需下一趟消解。趟数 = 嵌套深度（上限兜底防失控，正常 ≤ 2-3 趟）。
    // 终止判定改单趟 token 扫描：逐占位符 contains 在「全部已还原、确认退出」
    // 的最后一趟须扫 K×全串（3000 占位符 + 3MB 文档 = 9GB 字符扫描/次转换），
    // 与 collisionFreePlaceholder 注释里废除的反模式同型。命中的用户字面
    // token（非本批生成）最多多跑有界趟数，无害
    static const QRegularExpression hiTokenRe(QStringLiteral("ZZHI[0-9]+ZZ"));
    for (int hiPass = 0; hiPass < 8; ++hiPass) {
        if (!hiTokenRe.match(html).hasMatch())
            break;
        batchReplaceExact(html, htmlInlinePhs, htmlInlineBackups);
    }
    // 还原脚注引用占位符 ZZFR → <sup data-type="footnoteRef">
    batchReplaceExact(html, footnoteRefPhs, footnoteRefs);

    // 追加脚注定义容器（仅当存在定义）
    if (!footnoteDefs.isEmpty()) {
        QString container = "<div data-type=\"footnoteContainer\">";
        for (const QString &d : footnoteDefs) container += d;
        container += "</div>";
        int bodyClose = html.indexOf("</body>");
        if (bodyClose >= 0) html.insert(bodyClose, container);
        else html += container;
    }
    // 恢复下标占位符 ZZSB → <sub>。内容来自原始 markdown 文本，须「标记感知
    // 转义」：受支持的内联 HTML 标签（与行级 ZZHI 保护同一约定）与既有
    // &xxx; 实体原样保留，仅转义不构成良构标签的裸 '<'。盲目全量 esc() 有
    // 两个往返失真（矩阵回归 sub(html) 抓获）：
    //   ① 内容中用户手写的 <u> 等标签被转成 &lt;u&gt; 字面文本，语义丢失；
    //   ② fromHtml 侧按原文回传 sub 内容，已实体化的 & 每轮再转义翻倍
    //     （~a&lt;b~ → ~a&amp;lt;b~ → ~a&amp;amp;lt;b~ → …）无界累积，永不收敛。
    // 注意：必须在脚注容器追加之后执行——脚注定义内容可能含 ZZSB 占位符。
    // 下标内容含行内代码（~`code`~，extractSubscripts 已把代码反引号原样还原进
    // 内容）：code span 须包成 <sub><code>…</code></sub>，否则按字面文本存入
    // <sub>`code`</sub>——WYSIWYG 显示字面反引号，且 code 内容含 < 时转义成
    // &lt; 后 fromHtml 侧不再还原（无 <code> 标签触发保护），往返漂移累积 &amp;。
    if (!subContents.isEmpty()) {
        QStringList subBackups;
        subBackups.reserve(subContents.size());
        // 标签家族与行级 extractHtmlInlineElements 的 ZZHI 保护集对齐，另加
        // strong/b/em/i/del/s/strike/a/font（TipTap 富文本序列化常见输出）。
        // '>' 与裸 '&' 对 HTML 文本无害且保往返字节稳定（fromHtml 原文回传，
        // 不做实体解码），不转义；code span 内容仍走全量 esc——fromHtml 的
        // buildInlineCodeMd(unescapeHtmlEntities(...)) 与之成对。
        static const QRegularExpression markupRe(QStringLiteral(
            "(</?(?:span|u|kbd|small|mark|sub|sup|br|strong|b|em|i|code|"
            "del|s|strike|a|font)\\b[^<>]*>"
            "|&(?:[a-zA-Z][a-zA-Z0-9]*|#[0-9]+|#[xX][0-9a-fA-F]+);)"),
            QRegularExpression::CaseInsensitiveOption);
        auto escAware = [](const QString &s) {
            QString e;
            e.reserve(s.size());
            int i = 0;
            auto it = markupRe.globalMatch(s);
            while (it.hasNext()) {
                auto m = it.next();
                e += QString(s.mid(i, m.capturedStart() - i)).replace('<', "&lt;");
                e += m.captured(0);
                i = m.capturedEnd();
            }
            e += s.mid(i).replace('<', "&lt;");
            return e;
        };
        auto esc = [](const QString &s) {
            QString e = s;
            return e.replace('&', "&amp;").replace('<', "&lt;").replace('>', "&gt;");
        };
        // 文本段 ^x^ 成对 → <sup>（与全局后处理 ^x^→<sup> 对称）：sub 内容在
        // 备份中，全局后处理扫不到——不转换则 ~^x^~ 的 sup 语义整层丢失
        // （矩阵回归 mark(sub(sup)) 抓获）。置于 escAware 后按段应用：code span
        // 段仍走全量 esc 保持字面；转义 \^ 已是 ZZED 占位符（无裸 ^）不误触发。
        static const QRegularExpression supPairRe("\\^([^\\^\\n]+?)\\^");
        auto segToHtml = [&escAware](const QString &seg) {
            return escAware(seg).replace(supPairRe, QStringLiteral("<sup>\\1</sup>"));
        };
        static const QRegularExpression codeRe("`([^`]+)`");
        for (const QString &c : subContents) {
            QString e;
            int i = 0;
            auto it = codeRe.globalMatch(c);
            while (it.hasNext()) {
                auto m = it.next();
                e += segToHtml(c.mid(i, m.capturedStart() - i));
                e += QStringLiteral("<code>") + esc(m.captured(1))
                     + QStringLiteral("</code>");
                i = m.capturedEnd();
            }
            e += segToHtml(c.mid(i));
            subBackups << "<sub>" + e + "</sub>";
        }
        batchReplaceExact(html, subPhs, subBackups);
    }
    // 恢复数学公式占位符（前后可能有空 <p>，一并清除）
    batchRestorePlaceholders(html, mathBlockPhs, mathBlocks);
    // 清除紧邻 math div 的空 <p>（占位符产生的伪影，不误删正文空行）
    html.replace(QRegularExpression(
        "<p[^>]*>(<br[^>]*>)?</p>\\s*(<div data-type=\"block-math\")"), "\\2");
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

    // 列表项内的块级图片：补一个空 <p> 挡在图片前，把图片留在条目内。
    // TipTap 的 ListItem 内容式为 paragraph block*，块级 Image（inline:false）作
    // 首子元素时 ProseMirror 无处安放，会把整张图**弹出列表**——`<li><img></li>`
    // 经真实链路（vitest 探针）变成 `<li><p></p></li></ul><img>`，保存后列表里
    // 永远残留一个空条目（用户从未写过）。在前面补空 <p> 即 TipTap 的稳定不动点
    //（`<li><p></p><img></li>` 载入即原样输出），而 fromHtml 侧的空段落会被
    // taskItemInlineText 过滤掉，回写仍是干净的 `- ![x](a.png)`，完整往返。
    // 仅当图片确为条目首个子元素时才补——`<li><p>前</p><img>` 形态 TipTap 本就能
    // 稳定容纳（实测），不得再插入多余空段落。
    {
        // 通用列表项：<li …><img …>
        globalReplaceWith(html,
            QRegularExpression(QStringLiteral("(<li\\b[^>]*>)\\s*(<img\\b)")),
            [](const QRegularExpressionMatch &m) {
                return m.captured(1) + QStringLiteral("<p></p>") + m.captured(2);
            });
        // 任务项内容容器：<label>…</label><div><img …>（TipTap taskItem 契约结构）
        globalReplaceWith(html,
            QRegularExpression(QStringLiteral("(</label>\\s*<div\\b[^>]*>)\\s*(<img\\b)")),
            [](const QRegularExpressionMatch &m) {
                return m.captured(1) + QStringLiteral("<p></p>") + m.captured(2);
            });
    }

    // 还原表格空单元格占位符 ZZTC → 空 <td>（≥2 格行防 Qt 合并用，见 protectTableRowForQt）。
    // 占位符独立成单元格内容，Qt 输出 <td><p…>ZZTCnZZ</p></td>，连同 <p> 壳还原为空单元格，
    // 与 Qt 自身空单元格（<td></td>）形态一致，供 TipTap/fromHtml 正常解析。
    // 按存储的实际占位符串还原：通用 token 正则 + 集合成员判断——仅清空本次
    // 生成的占位符，用户文档字面的同形态文本不在集合内，原样保留（不按个数
    // 拼 alternation 的原因见 batchRestorePlaceholders 注释）
    if (!tableCellPhs.isEmpty()) {
        QSet<QString> phSet(tableCellPhs.cbegin(), tableCellPhs.cend());
        globalReplaceWith(html,
            QRegularExpression(QStringLiteral(
                "<td([^>]*)>\\s*<p[^>]*>(ZZ[A-Z]+[0-9]+ZZ)</p>\\s*</td>")),
            [&phSet](const QRegularExpressionMatch &m) {
                if (!phSet.contains(m.captured(2)))
                    return m.captured(0);
                return QStringLiteral("<td") + m.captured(1) + QStringLiteral("></td>");
            });
        // fallback：占位符未包在 <p> 中时直接清空（防泄漏为字面文本）
        globalReplaceWith(html,
            QRegularExpression(QStringLiteral("ZZ[A-Z]+[0-9]+ZZ")),
            [&phSet](const QRegularExpressionMatch &m) {
                return phSet.contains(m.captured()) ? QString() : m.captured(0);
            });
    }

    // 还原转义扩展定界符 ZZED → 字面 ~~ / ~ / ^。必须放在 toHtml 末尾：
    // ZZED 可能内嵌于 sub/emphCode 等备份所还原的 HTML 文本中（如 ==\^x\^==
    // 的 mark 备份内容），早于它们还原会让 ZZED 串暴露给后续转换或泄漏。
    batchReplaceExact(html, escapedDelimPhs, escapedDelimBackups);

#ifndef QT_NO_DEBUG
    assertNoPlaceholderLeak(html,
        {&codeBlockPhs, &blankLinePhs, &mathBlockPhs, &bqCodeBlockPhs,
         &bqSepPhs, &taskListPhs, &brSoftPhs, &hrPhs, &divPhs,
         &escapedEqPhs, &escapedDelimPhs, &subPhs, &footnoteRefPhs,
         &htmlInlinePhs, &tableCellPhs, &emphCodePhs, &emptyAltSentinels});
#endif

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
// mark/sup 备份内容中的 MD 等价标签 → MD 语法（防备份冻结为字面 HTML）。
// mark/sup 的内容在 toHtml 侧经过 setMarkdown 富解析（==**x**== → <mark><strong>），
// fromHtml 侧若把内层 <strong>/<s>/<sub> 等原样留在备份里，往返一次后用户的
// MD 语法退化为字面 HTML（矩阵回归 mark(bold)/sup(strike) 簇抓获）。对称还原：
// 有 MD 等价语法的标签换回 MD 语法，无等价的（<u>/<span>/<kbd>…）保留字面
// （与 ZZHI 保护约定一致）。selfTag 为 wrapper 自身标签名——同族嵌套
// （====x==== / ^^x^^）定界符粘连歧义，保留字面。code 不转：<code> 在
// fromHtml 更早的行内代码提取步已占位化，不会出现在此处内容中。
static QString mdEquivTagsToSyntax(const QString &s, const char *selfTag)
{
    struct Fam {
        const char *tags;    // 标签 alternation
        const char *open;    // MD 开定界符
        const char *close;   // MD 闭定界符
    };
    static const Fam fams[] = {
        {"strong|b",      "**", "**"},
        {"em|i",          "*",  "*"},
        {"s|del|strike",  "~~", "~~"},
        {"mark",          "==", "=="},
        {"sub",           "~",  "~"},
        {"sup",           "^",  "^"},
    };
    QString r = s;
    for (const Fam &f : fams) {
        if (qstrncmp(f.tags, selfTag, qstrlen(selfTag)) == 0)
            continue;   // 同族嵌套歧义，保留字面
        const QRegularExpression re(
            QStringLiteral("<(%1)\\b[^>]*>(.*?)</\\1>")
                .arg(QLatin1String(f.tags)),
            QRegularExpression::DotMatchesEverythingOption);
        globalReplaceWith(r, re,
            [&f](const QRegularExpressionMatch &m) {
                return QString::fromLatin1(f.open) + m.captured(2)
                       + QString::fromLatin1(f.close);
            });
    }
    return r;
}

// 块引用 inner 的行内标签 → MD 语法：引用步骤先于 Qt 转换抽取（防 > 前缀被
// toMarkdown 折叠），Qt 侧管理的标签族（strong/em/s/mark/sub/sup/a/img）到不了
// toMarkdown，须手动转换；handler 管理的行内元素（mark/code/span/u/kbd/small）
// 在引用抽取前已是 ZZ* 占位符，不在其列。selfTag 传 "blockquote"（不在任何
// 标签族内，六族全部转换）。
static QString blockquoteInnerInlineToMd(const QString &inner)
{
    QString r = mdEquivTagsToSyntax(inner, "blockquote");
    // 链接：<a href="URL">TEXT</a> → [TEXT](URL)。置于 mdEquiv 之后：链接文本
    // 内的 strong/em 先转 **/*，得到 [**b**](u) 嵌套形态。正则内含 )" 序列，
    // 原始字符串须用自定义定界符 re，否则 )" 提前终结字面量
    globalReplaceWith(r,
        QRegularExpression(
            R"re(<a\b[^>]*\bhref\s*=\s*"([^"]*)"[^>]*>(.*?)</a>)re",
            QRegularExpression::DotMatchesEverythingOption),
        [](const QRegularExpressionMatch &m) {
            return "[" + m.captured(2) + "]("
                   + unescapeHtmlEntities(m.captured(1)) + ")";
        });
    // 图片：<img ...> → ![alt](src)；无 src 的畸形 img 保留原样交后续剥离
    globalReplaceWith(r, QRegularExpression(R"(<img\b[^>]*>)"),
        [](const QRegularExpressionMatch &m) {
            const QString tag = m.captured(0);
            auto attr = [&tag](const char *name) {
                QRegularExpression re(
                    QStringLiteral("\\b%1\\s*=\\s*\"([^\"]*)\"")
                        .arg(QLatin1String(name)));
                auto am = re.match(tag);
                return am.hasMatch() ? am.captured(1) : QString();
            };
            const QString src = attr("src");
            if (src.isEmpty()) return tag;
            return "![" + attr("alt") + "](" + unescapeHtmlEntities(src) + ")";
        });
    return r;
}

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

// fromHtml 各备份构造点（mark/strike/sub/sup/N 步）的字面扩展定界符转义：
// 备份在 toMarkdown 之后才还原，绕过了 D 步对 processed 的字面符号转义；
// 备份内容若带裸 ^/~/~~，下一轮 toHtml 的上标/下标/删除线转换会把它重新
// 解释为语法（用户转义写入的字面量语义丢失，如 ~~\^x\^~~ 第二轮变 ~~<sup>x</sup>~~）。
// 与 D 步同一套状态机：跳过 <...> 标签内部（含嵌套标签与属性值），只转义文本。
// escapeCaret=false 供下标备份用：下标内容在 toHtml 行级抽取（早于 sup 后处理
// 转换），字面 ^ 天然惰性——转义反而把 ~a^b~（今日逐字节稳定）归一成 ~a\^b~。
static QString escapeExtDelimsOutsideTags(const QString &s, bool escapeCaret)
{
    QString r;
    r.reserve(s.size());
    bool inTag = false;
    for (int i = 0; i < s.size(); ++i) {
        const QChar c = s.at(i);
        if (c == QLatin1Char('<')) inTag = true;
        else if (c == QLatin1Char('>')) inTag = false;

        if (!inTag && c == QLatin1Char('~') && i + 1 < s.size()
            && s.at(i + 1) == QLatin1Char('~')) {
            r += QStringLiteral("\\~~");
            ++i;
        } else if (!inTag && c == QLatin1Char('~')) {
            r += QStringLiteral("\\~");
        } else if (!inTag && escapeCaret && c == QLatin1Char('^')) {
            r += QStringLiteral("\\^");
        } else {
            r += c;
        }
    }
    return r;
}

// 下标备份的字面 ~ 转义（fromHtml <sub> → ~x~，与 escapeExtDelimsOutsideTags
// 同族但更精细）：extractSubscripts 已支持内容含成对 ~~ 单元（见其注释），
// 内部偶数长度的 ~ run 保持字面即可对称重摄取——~**~~x~~**~ 直接字节保真
// 往返；仅两类 run 须转义：
//   ① 贴内容边缘（首/末单元与定界符 ~ 粘连）：~~ 开头产出 ~~~ 围栏开启行，
//      ~~ 结尾被 extractSubscripts 闭侧 (?<!~) 拦截，均无法重新配对；
//   ② 奇数长度：~~ 单元成对消化后残留单 ~，内容生长在 run 处中断、闭符
//      错位（~a~b~ 的 ~a~ 先配对、b~ 残留——旧版全量转义防的正是这个）。
// 标签内部不转义（同一状态机约定）；字面 ^ 不转义（toHtml 行级抽取早于
// sup 后处理，天然惰性，~a^b~ 逐字节稳定）。
static QString escapeSubDelimsForBackup(const QString &s)
{
    QString r;
    r.reserve(s.size());
    bool inTag = false;
    int i = 0;
    while (i < s.size()) {
        const QChar c = s.at(i);
        if (c == QLatin1Char('<')) inTag = true;
        else if (c == QLatin1Char('>')) inTag = false;
        if (!inTag && c == QLatin1Char('~')) {
            int j = i;
            while (j < s.size() && s.at(j) == QLatin1Char('~')) ++j;   // run [i,j)
            const bool atEdge = (i == 0 || j == s.size());
            if (!atEdge && (j - i) % 2 == 0) {
                r += s.mid(i, j - i);          // 内部偶数 run：字面保真
            } else {
                for (int k = 0; k < j - i; ++k) r += QStringLiteral("\\~");
            }
            i = j;
            continue;
        }
        r += c;
        ++i;
    }
    return r;
}

// QTextDocument → Markdown：vendored QtMarkdownWriter，关闭 80 列折行。
// QTextDocument::toMarkdown() 硬编码折行（无 API 关闭）且稀疏空格场景
// 强切吞字；折行关闭后每块单行输出。默认 GitHub 方言下 vendored writer
// 与 doc.toMarkdown() 逐字节等价（tests/test_qtmarkdownwriter.cpp 回归），
// 动机、改动清单与 LGPL 合规见 docs/vendor-qt-markdown-writer.md。
static QString serializeMarkdownNoWrap(const QTextDocument &doc)
{
    QString md;
    QTextStream ts(&md);
    QtMarkdownWriter writer(ts, QTextDocument::MarkdownDialectGitHub);
    writer.setWrapEnabled(false);
    writer.writeAll(&doc);
    return md;
}

QString MarkdownHandler::fromHtml(const QString &html)
{
    // 占位符碰撞索引：源 HTML 中全部 ZZ\w+ZZ 形态子串，供 collisionFreePlaceholder
    // O(1) 碰撞检测（单次构建，随生成 insert 本次占位符）。
    QSet<QString> collisions = buildPlaceholderCollisionSet(html);
    // QTextDocument::toMarkdown() 不能正确处理 <code> / <pre>
    // 策略：先替换代码为占位符 → toMarkdown() → 还原占位符

    QStringList placeholders, backups;
    // 空 alt 图片的 alt 占位串（ZZIB*，见 injectEmptyAltSentinel）。不参与批次
    // 还原，仅用于把 Qt writer 的 ![image](…) 改回 ![](...)，故单独记录。
    QStringList emptyAltSentinels;
    // <br data-brk>（编辑器软换行）占位符：与 backups 分开记录，因其还原需按
    // 所在行上下文决定缩进（段落 \n / 有序列表 \n···· / 无序列表 \n··），
    // 无法在保护阶段预先确定，统一按行上下文还原。
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

    // 定向愈合「加粗/斜体闭合 + 空格 + 全角标点」的历史损坏空格。
    // 旧版反换行在强调标记边界折行时误补空格，产生 "**审查结论** ：" 形态损坏；
    // 该空格往往是段落内唯一折行点，触发 QTextMarkdownWriter 的 i=j+1 吞字 bug
    // （续行恰满 80 列时，折行逻辑把断点处非空格的 。/token 字符当空格吞掉，
    // 详见 docs/qt-642-escaping-compat.md 同源机制）。中文排版里全角标点前
    // 不加空格，此归一化安全；在 setHtml 前去掉，使 toMarkdown 不再在该处折行。
    // 仅限全角标点，不加粗后接普通 CJK/ASCII 的情况（那是合法空格）。
    static const QRegularExpression emphSpaceRe(
        R"re((</(?:strong|em|b|i)>)\s+([，。！？；：、）】》」』〉〕〗…—・～]))re");
    processed.replace(emphSpaceRe, "\\1\\2");

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
        // 先匹配整个元素，再从中提取 data-latex（属性顺序无关）。
        // 单遍重建：跳过分支（无 data-latex）返回原文——块级/行内公式均为空
        // 元素（<div ...></div> / <span ...></span>，标签内无内容），旧循环
        // 的 offset=+1 重搜不可能在其中再命中，语义等价。
        static const QRegularExpression blockMathTagRe(
            R"re(<div\b[^>]*\bdata-type\s*=\s*"block-math"[^>]*></div>)re");
        static const QRegularExpression latexRe(R"re(data-latex\s*=\s*"([^"]*)")re");
        globalReplaceWith(processed, blockMathTagRe,
            [&](const QRegularExpressionMatch &m) {
                const auto lm = latexRe.match(m.captured());
                if (!lm.hasMatch()) return m.captured();
                // 属性值实体解码：data-latex 的值经 TipTap getHTML() 的 innerHTML
                // 序列化恒被转义（& → &amp;、< → &lt;、" → &quot;），toHtml 侧现在
                // 也走 escapeHtmlAttr。不解码则 & 类公式（aligned/matrix 的对齐列
                // `&`）每保存一轮就多一层转义（&amp; → &amp;amp;），永不收敛。
                QString latex = unescapeHtmlEntities(lm.captured(1));
                QString md = blockMathLatexToMarkdown(latex);
                QString ph = collisionFreePlaceholder(collisions, "BM", n++);
                placeholders << ph;
                backups << md;
                return wrapBlock(ph);
            });
        // 行内公式（同理：属性顺序无关；空元素，跳过分支无重叠语义）
        static const QRegularExpression inlineMathTagRe(
            R"re(<span\b[^>]*\bdata-type\s*=\s*"inline-math"[^>]*></span>)re");
        globalReplaceWith(processed, inlineMathTagRe,
            [&](const QRegularExpressionMatch &m) {
                const auto lm = latexRe.match(m.captured());
                if (!lm.hasMatch()) return m.captured();
                QString latex = unescapeHtmlEntities(lm.captured(1));   // 见上方块级公式注释
                QString md = "$" + latex + "$";
                QString ph = collisionFreePlaceholder(collisions, "IM", n++);
                placeholders << ph;
                backups << md;
                return ph;
            });
    }

    // 1) 代码块：多重 <pre>（每行一个，QTextDocument 特有）→ 合并
    {
        QRegularExpression preRe(
            R"(<pre[^>]*>(.*?)</pre>)",
            QRegularExpression::DotMatchesEverythingOption);
        static const QRegularExpression langRe(
            "class=\"language-([^\"]+)\"");
        static const QRegularExpression stripTagRe("<[^>]*>");
        // 单遍重建：globalMatch 一次取全部 <pre>，仅空白分隔的连续 <pre> 合并为
        // 同一组（同一代码块）；逐组生成占位符并拼接 result。替代 while(true)
        // preRe.match(processed, searchFrom) + processed.replace 的 O(K·n) 模式。
        // 分组判定基于原始串中相邻 <pre> 间的空白（与原实现一致），占位符串由
        // collisionFreePlaceholder 在原始串上碰撞检测（比原「已替换串」检测更
        // 保守但同样安全，仅占位符编号可能不同，还原按存储值回填不受影响）。
        QVector<QRegularExpressionMatch> matches;
        auto preIt = preRe.globalMatch(processed);
        while (preIt.hasNext())
            matches << preIt.next();

        QString result;
        result.reserve(processed.size());
        int last = 0;
        int n = 0;
        int i = 0;
        while (i < matches.size()) {
            int blockStart = matches[i].capturedStart();
            int blockEnd = matches[i].capturedEnd();
            QStringList lines;
            lines << matches[i].captured(1);
            ++i;
            while (i < matches.size()) {
                const QString between = processed.mid(blockEnd,
                    matches[i].capturedStart() - blockEnd);
                if (!between.trimmed().isEmpty())
                    break;
                lines << matches[i].captured(1);
                blockEnd = matches[i].capturedEnd();
                ++i;
            }

            result += processed.mid(last, blockStart - last);

            QString code = lines.join('\n');
            // 提取语言（TipTap codeBlock 输出 <code class="language-xxx">）
            QString lang;
            auto lm = langRe.match(code);
            // 实体解码：toHtml 侧经 escapeHtmlAttr 写入信息串（见该函数注释）
            if (lm.hasMatch()) lang = unescapeHtmlEntities(lm.captured(1));
            code.remove(stripTagRe);
            code = unescapeHtmlEntities(code);
            code = code.trimmed();
            // 占位符序号须避开输入 HTML/已产出内容中的字面同串（如文档里示例的
            // `ZZCB0ZZ`），否则还原时用户文本被误替换。序号存 placeholders（可非
            // 连续），还原按存储值回填；n 仍作数组序号递增。
            QString ph = collisionFreePlaceholder(collisions, "CB", n);
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
            result += wrapBlock(ph);
            last = blockEnd;
        }
        result += processed.mid(last);
        processed = result;
    }

    // 1.5) 水平线 <hr> → ---
    //    QTextDocument::toMarkdown 把 <hr> 输出为 "- - -"（空格分隔），且后随
    //    块边界丢失（与相邻段落粘连）；保护为占位符，还原时输出规范 "---"。
    {
        int n = 0;
        QRegularExpression hrRe(R"(<hr\b[^>]*>)");
        globalReplaceWith(processed, hrRe,
            [&n, &collisions, &placeholders, &backups, &wrapBlock]
            (const QRegularExpressionMatch &m) {
                Q_UNUSED(m);
                QString ph = collisionFreePlaceholder(collisions, "HR", n++);
                placeholders << ph;
                backups << "---";
                return wrapBlock(ph);
            });
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

    // 4) 行内代码 <code>text</code> 及 <span monospace> → 占位符
    {
        // 归一化 code 与包裹标记的嵌套顺序：TipTap/ProseMirror 按 mark rank 渲染，
        // 可能输出 <code><strong>X</strong></code>（code 在外）而非 toHtml 统一的
        // <strong><code>X</code></strong>。交换为包裹标记在外，使下方 code 正则只
        // 提取纯文本内容，否则 <strong> 会被误当 code 内容（还原为字面
        // `<strong>X</strong>`）。
        // 覆盖全部包裹符（strong/b/em/i/s/del/strike/mark/sub/sup/u）：
        // TipTap 对 <mark><code> 归一为 <code><mark>（mark rank 不同），对
        // <s>/<sub>/<sup><code> 同理——缺一即还原为字面 `<s>…</s>` 内嵌标签。
        // 仅处理 code 直接包裹单层标记；多标记/嵌套极罕见，交下方正则兜底。
        globalReplaceWith(processed,
            QRegularExpression(
                R"(<code[^>]*><(strong|b|em|i|s|del|strike|mark|sub|sup|u)>(.*?)</\1></code>)",
                QRegularExpression::DotMatchesEverythingOption),
            [](const QRegularExpressionMatch &m) {
                return QLatin1String("<") + m.captured(1) + "><code>"
                       + m.captured(2) + "</code></" + m.captured(1) + ">";
            });

        // 占位符是否需要追加尾随空格：QTextMarkdownWriter 的 80 列折行在长行
        // 无空格断点时，会拦腰拆 token 并吞掉断点字符（i=j+1 吞字 bug，见
        // docs/qt-642-escaping-compat.md 同源机制）。行内元素占位符密集相邻时
        // （如 `<code>ZZHR</code>/<code>ZZBQ</code>/… 长串）该折行点会落在 token
        // 内部。尾随空格给出合法折行点（空格处折行是预期行为），还原时随 token
        // 一起被匹配消费，不产生多余空格。
        // 但「无脑追加」会在两种情形出错：
        //   a) </code> 后紧跟空白：尾随空格与后随空格合并，QTextDocument 折叠
        //      （裸 HTML/TipTap 无 pre-wrap 时）后 restore 把唯一空格吞掉 →
        //      原空格丢失（`code` Y → `code`Y）。
        //   b) </code> 后紧跟 <标签>：追加空格会在相邻 code 间/段尾插入多余
        //      空格，或段尾空格被 toMarkdown 裁剪导致还原失配。
        // 因此仅当后续紧跟「非空白、非标签」字符时追加：该场景必然无天然折行点
        // （否则 80 列折行仍会落在 token 内），正是需要防拆的密集串。
        auto needsWrapGuardSpace = [&processed](int pos) -> bool {
            if (pos >= processed.size())
                return false;
            const QChar c = processed.at(pos);
            return !c.isSpace() && c != QLatin1Char('<');
        };
        // <code> 标签（TipTap 输出）
        QRegularExpression codeRe(
            R"(<code[^>]*>(.*?)</code>)",
            QRegularExpression::DotMatchesEverythingOption);
        int n = 0;
        globalReplaceWith(processed, codeRe,
            [&n, &collisions, &placeholders, &backups, &needsWrapGuardSpace]
            (const QRegularExpressionMatch &m) {
                QString ph = collisionFreePlaceholder(collisions, "CD", n++);
                if (needsWrapGuardSpace(m.capturedEnd()))
                    ph += QLatin1Char(' ');
                placeholders << ph;
                backups << buildInlineCodeMd(unescapeHtmlEntities(m.captured(1)));
                return ph;
            });
        // <span monospace>（QTextDocument 原始输出；字体名动态匹配本平台
        // FixedFont，Windows="Courier New"/macOS="Menlo" 同样识别）
        QRegularExpression spanRe(
            QStringLiteral("<span[^>]*font-family:\\s*'(?:%1)'[^>]*>(.*?)</span>")
                .arg(monoSpanFamilyPattern()),
            QRegularExpression::DotMatchesEverythingOption);
        n = 0;
        globalReplaceWith(processed, spanRe,
            [&n, &collisions, &placeholders, &backups, &needsWrapGuardSpace]
            (const QRegularExpressionMatch &m) {
                // 同 <code>：仅密集串追加尾随空格防折行拆 token
                QString ph = collisionFreePlaceholder(collisions, "IC", n++);
                if (needsWrapGuardSpace(m.capturedEnd()))
                    ph += QLatin1Char(' ');
                placeholders << ph;
                backups << buildInlineCodeMd(unescapeHtmlEntities(m.captured(1)));
                return ph;
            });
    }


    // 3) 高亮 <mark>text</mark> → ==text==
    {
        QRegularExpression re(
            R"(<mark[^>]*>(.*?)</mark>)",
            QRegularExpression::DotMatchesEverythingOption);
        int n = 0;
        globalReplaceWith(processed, re,
            [&n, &collisions, &placeholders, &backups]
            (const QRegularExpressionMatch &m) {
                QString ph = collisionFreePlaceholder(collisions, "MK", n++);
                placeholders << ph;
                // 高亮内容中的字面 ^/~/~~ 须转义：==\^x\^== 的 mark 内容以字面 ^
                // 还原为备份后，第二轮 toHtml 的 ^x^→<sup> 后处理会在 == 内误触发。
                // 内层 MD 等价标签换回 MD 语法（mdEquivTagsToSyntax，防冻结）
                backups << "==" + mdEquivTagsToSyntax(
                             escapeExtDelimsOutsideTags(m.captured(1), true), "mark")
                             + "==";
                return ph;
            });
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
        globalReplaceWith(processed, supTagRe,
            [&n, &collisions, &placeholders, &backups, &idRe]
            (const QRegularExpressionMatch &m) {
                auto im = idRe.match(m.captured());
                // 实体解码：toHtml 侧经 escapeHtmlAttr 写入，且 TipTap getHTML()
                // 的 innerHTML 序列化会二次转义（见 escapeHtmlAttr 注释）
                QString id = im.hasMatch() ? unescapeHtmlEntities(im.captured(1))
                                           : QString();
                QString ph = collisionFreePlaceholder(collisions, "FR", n++);
                placeholders << ph;
                backups << "[^" + id + "]";
                return ph;
            });
    }

    // 上下标 <sub>/<sup> → Pandoc ~x~/^x^（toMarkdown 会丢弃 sub/sup 标签）
    //   内部富文本标签原样保留（escapeExtDelimsOutsideTags 标签感知，只转义
    //   标签外的 ~/^）：toHtml 侧 ZZSB 备份为标记感知转义（受支持标签直通），
    //   此处对称直通使 ~a<u>u</u>b~ / ^a<u>u</u>b^ 往返字节保真；此前剥离
    //   <[^>]*> 会丢内部标签（^a<u>u</u>b^ → ^aub^，矩阵回归 sup(html) 抓获）
    {
        int n = 0;
        QRegularExpression subRe(R"(<sub[^>]*>(.*?)</sub>)",
            QRegularExpression::DotMatchesEverythingOption);
        globalReplaceWith(processed, subRe,
            [&n, &collisions, &placeholders, &backups]
            (const QRegularExpressionMatch &m) {
                QString text = m.captured(1);
                QString ph = collisionFreePlaceholder(collisions, "SB", n++);
                placeholders << ph;
                // 下标内容：先内层 MD 等价标签换回 MD 语法（mdEquivTagsToSyntax，
                // 与 mark/sup 备份同一防冻结处理；sub 同族跳过），再做贴边/奇数
                // ~ run 转义（escapeSubDelimsForBackup，内部成对 ~~ 保字面使
                // ~**~~x~~**~ 字节保真）。顺序必须「先转换后转义」：<s>x</s> 贴
                // 内容边缘时转换出 ~~ 与定界符 ~ 粘成 ~~~（围栏开启行），转换在
                // 前才能让转义捕获该贴边 run；字面 ^ 惰性（行级抽取早于 sup 后
                // 处理），不转义以保 ~a^b~ 逐字节稳定
                backups << "~" + escapeSubDelimsForBackup(
                             mdEquivTagsToSyntax(text, "sub")) + "~";
                return ph;
            });
        QRegularExpression supRe(R"(<sup[^>]*>(.*?)</sup>)",
            QRegularExpression::DotMatchesEverythingOption);
        n = 0;
        globalReplaceWith(processed, supRe,
            [&n, &collisions, &placeholders, &backups]
            (const QRegularExpressionMatch &m) {
                QString text = m.captured(1);
                QString ph = collisionFreePlaceholder(collisions, "SP", n++);
                placeholders << ph;
                // 上标内容转义字面 ^ 与 ~：^a\^b^ 重载时 ^..^ 会提前在 a 处闭合；
                // ^a\~b~c^ 的成对 ~ 会先被行级 extractSubscripts 吃掉把 sup 拆裂。
                // 内层 MD 等价标签换回 MD 语法（mdEquivTagsToSyntax，防冻结）；
                // 换出的 ~x~ 在重载时被 extractSubscripts 先行提取，往返对称
                backups << "^" + mdEquivTagsToSyntax(
                             escapeExtDelimsOutsideTags(text, true), "sup") + "^";
                return ph;
            });
    }

    // 5) 任务列表 <ul data-type="taskList"> → GFM - [ ] / - [x]
    //    QTextDocument 不认识 data-type/data-checked（保存后复选框丢失），
    //    在 setHtml 前整体提取为占位符，toMarkdown 后还原。
    //    置于行内代码/上下标/高亮/公式占位保护之后：条目文本中的 ZZ* 占位符
    //    被原样保留，最终在逆序还原阶段统一还原。
    {
        static const QRegularExpression ulOpenRe(
            "<ul\\b[^>]*\\bdata-type\\s*=\\s*\"taskList\"[^>]*>");
        int n = 0;
        // 单遍重建（globalExtractWith）：开标签匹配不重叠；跳过分支的旧
        // offset=+1 重搜只能落在本就互斥的开标签内部属性区，实际不可命中，
        // 保留原文即等价。fn 内 isInsideGeneralList/findMatchingUlClose 读
        // 原始串——先前消费区域各标签自平衡，深度判定不受影响。
        globalExtractWith(processed, ulOpenRe,
            [&](const QRegularExpressionMatch &m, QString &wrapped, int &consumedTo) {
                // 嵌在通用列表内的 taskList 不在此提取（提升会丢失嵌套缩进），
                // 留给步骤 5b 的 generalListInnerToMd 一并序列化
                if (isInsideGeneralList(processed, m.capturedStart()))
                    return false;
                const int closeStart = findMatchingUlClose(processed, m.capturedStart());
                if (closeStart < 0) return false;
                QString md2 = taskListInnerToMd(
                    processed.mid(m.capturedEnd(), closeStart - m.capturedEnd()), 0);
                while (md2.endsWith('\n')) md2.chop(1);
                QString ph = collisionFreePlaceholder(collisions, "TL", n++);
                placeholders << ph;
                backups << md2;
                consumedTo = closeStart + 5;   // 含 </ul>
                wrapped = wrapBlock(ph);
                return true;
            });
    }

    // 7) 脚注容器 <div data-type="footnoteContainer"> → [^id]: 内容（多条）
    //    块级定义：content 容器为 div（旧 span 形态兼容）；def 定界与 content
    //    提取均用平衡扫描（taskList 内嵌 <div>、Qt 嵌套 <span> 会破坏非贪婪
    //    匹配）；内容经完整 fromHtml 主管线嵌套转换后按缩进续行输出。
    //    依赖前置的 Step 6：footnoteRef 的 <sup> 已先隔离为 ZZFR。
    {
        static const QRegularExpression contOpenRe(
            R"(<div\b[^>]*\bdata-type\s*=\s*"footnoteContainer"[^>]*>)");
        // 定位每条 def 开标签（定界交给平衡扫描）
        static const QRegularExpression defOpenRe(
            R"(<div\b[^>]*\bdata-type\s*=\s*"footnoteDef"[^>]*>)");
        static const QRegularExpression defIdRe("data-id\\s*=\\s*\"([^\"]*)\"");
        // content 容器：div（块级新形态）| span（内联旧形态）
        static const QRegularExpression contentOpenRe(
            R"(<(div|span)\b[^>]*class\s*=\s*"footnote-content"[^>]*>)");
        int n = 0;
        // 单遍重建（globalExtractWith）：跳过分支（无配对闭标签）保留原文即
        // 等价（开标签互斥，+1 重搜不可命中）；fn 内 findMatchingDivClose 读
        // 原始串，先前消费区域自平衡、深度判定不受影响。
        globalExtractWith(processed, contOpenRe,
            [&](const QRegularExpressionMatch &om, QString &wrapped, int &consumedTo) {
            const int closeStart = findMatchingDivClose(processed, om.capturedStart());
            if (closeStart < 0) return false;
            QString inner = processed.mid(om.capturedEnd(),
                                          closeStart - om.capturedEnd());
            QString mdBlock;
            int doff = 0;
            while (true) {
                auto dm = defOpenRe.match(inner, doff);
                if (!dm.hasMatch()) break;
                int defClose = findMatchingDivClose(inner, dm.capturedStart());
                if (defClose < 0) { doff = dm.capturedEnd(); continue; }
                auto iim = defIdRe.match(dm.captured());
                // 实体解码（与上方脚注引用、escapeHtmlAttr 成对）
                QString id = iim.hasMatch() ? unescapeHtmlEntities(iim.captured(1))
                                            : QString();
                QString defInner = inner.mid(dm.capturedEnd(),
                                             defClose - dm.capturedEnd());
                auto com = contentOpenRe.match(defInner);
                QString contentHtml;
                if (com.hasMatch()) {
                    const bool isDiv = (com.captured(1) == QLatin1String("div"));
                    int contentClose = isDiv
                        ? findMatchingDivClose(defInner, com.capturedStart())
                        : findMatchingSpanClose(defInner, com.capturedStart());
                    contentHtml = (contentClose >= 0)
                        ? defInner.mid(com.capturedEnd(),
                                       contentClose - com.capturedEnd())
                        : defInner.mid(com.capturedEnd());
                } else {
                    contentHtml = defInner;   // 无 content 容器（异常兜底）
                }
                // 块级 HTML → MD：嵌套完整 fromHtml（任务列表/数学/行内代码等
                // 占位保护对称生效；内容内若含嵌套脚注容器，转出的 [^x]: 行
                // 即字面文本，与 toHtml 端字面处理对称）
                QString cm = MarkdownHandler::fromHtml(contentHtml);
                // cm 内的外层占位符（块级公式 ZZBM / 代码块 ZZCB / 任务列表
                // ZZTL 等）若留给末段统一还原，多行备份的内部行不携带本定义的
                // 续行缩进——重载时定义在首个非缩进内部行处断裂（块级公式形态
                // 还会使嵌套 toHtml 遇到未闭合 $$——该形态曾触发 mathBlockPhs/
                // mathBlocks 错配崩溃，现已由 toHtml 的 EOF 收尾按字面透传消解）。
                // 故先在 cm 内就地展开（逆序 = 外层先展开，与末段
                // 多趟还原语义一致），使多行备份的每一行都进入下方缩进续行输出。
                // 碰撞检测保证 cm 中与占位符同串的必为真占位符；漏网 token 由
                // 末段多趟还原兜底（无缩进，与未展开时的旧行为一致）。
                // 就地展开外层占位符（见 expandPlaceholdersInPlace 注释：
                // 多行备份展开后再缩进续行，内部行才携带定义的续行缩进）
                expandPlaceholdersInPlace(cm, placeholders, backups);
                while (cm.endsWith('\n')) cm.chop(1);
                // 缩进续行输出：首行接 [^id]: 之后，其余行补 4 空格（空行不缩进）
                QStringList outLines = cm.split('\n');
                mdBlock += "[^" + id + "]: " + outLines.value(0);
                for (int li = 1; li < outLines.size(); ++li) {
                    mdBlock += '\n';
                    if (!outLines[li].trimmed().isEmpty())
                        mdBlock += QStringLiteral("    ") + outLines[li];
                }
                mdBlock += '\n';
                doff = defClose + 6;   // 跳过 </div>
            }
            const QString ph = collisionFreePlaceholder(collisions, "FD", n++);
            placeholders << ph;
            backups << mdBlock;
            consumedTo = closeStart + 6;   // 含 </div>
            wrapped = wrapBlock(ph);
            return true;
        });
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
        // 模式 a: <th...><span ...font-weight:700...>text</span></th>
        // 单遍重建：替换串 <th…>…</th> 内不含 span/strong 主体，不会再命中。
        static const QRegularExpression thFontSpanRe(
            QStringLiteral("<(th)(\\b[^>]*)>\\s*<span\\b[^>]*\\bfont-weight\\s*:\\s*700[^>]*>(.*?)</span>\\s*</\\1>"),
            QRegularExpression::DotMatchesEverythingOption);
        globalReplaceWith(processed, thFontSpanRe,
            [](const QRegularExpressionMatch &m) {
                return QString("<th%1>%2</th>").arg(m.captured(2), m.captured(3));
            });
        // 模式 b: <th...><strong>text</strong></th>（TipTap Bold mark 产物）
        static const QRegularExpression thStrongRe(
            QStringLiteral("<(th)(\\b[^>]*)>\\s*<strong\\b[^>]*>(.*?)</strong>\\s*</\\1>"),
            QRegularExpression::DotMatchesEverythingOption);
        globalReplaceWith(processed, thStrongRe,
            [](const QRegularExpressionMatch &m) {
                return QString("<th%1>%2</th>").arg(m.captured(2), m.captured(3));
            });
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
        // <strong>/<em> 内含删除线 → 整层字面 HTML。
        // 删除线含 <s>/<del>/<strike> 与 <span …line-through…> 两种形态
        // （后者是外部 HTML / 旧 toHtml 输出），均触发保护。
        QRegularExpression strongStrikeRe(
            R"(<((?:strong|b|em|i))\b[^>]*>(.*?)</\1>)",
            QRegularExpression::DotMatchesEverythingOption);
        globalReplaceWith(processed, strongStrikeRe,
            [&collisions, &placeholders, &backups, &n]
            (const QRegularExpressionMatch &m) {
                static const QRegularExpression strikeCheckRe(
                    R"(<(?:s|del|strike)\b|<\s*span\b[^>]*line-through)");
                if (!m.captured(2).contains(strikeCheckRe))
                    return m.captured(0);   // 无嵌套删除线：原样保留
                QString ph = collisionFreePlaceholder(collisions, "NS", n++);
                placeholders << ph;
                backups << m.captured(0);
                return ph;
            });
        // <s|del|strike> 内含加粗/斜体 → 整层字面 HTML
        QRegularExpression strikeStrongRe(
            R"(<((?:s|del|strike))\b[^>]*>(.*?)</\1>)",
            QRegularExpression::DotMatchesEverythingOption);
        globalReplaceWith(processed, strikeStrongRe,
            [&collisions, &placeholders, &backups, &n]
            (const QRegularExpressionMatch &m) {
                static const QRegularExpression fmtCheckRe(
                    R"(<(?:strong|b|em|i)\b)");
                if (!m.captured(2).contains(fmtCheckRe))
                    return m.captured(0);   // 无嵌套加粗/斜体：原样保留
                QString ph = collisionFreePlaceholder(collisions, "NS", n++);
                placeholders << ph;
                backups << m.captured(0);
                return ph;
            });
    }

    // 标题内强调保护：<strong>/<b>/<em>/<i> → 占位符 + MD 语法备份。
    // Qt 的 toMarkdown **不输出 <hN> 内的强调标记**（<p> 内正常；实测
    // setHtml("<h1><strong>c</strong></h1>").toMarkdown() == "# c"），标题里用户
    // 写的 **粗** / *斜* 在保存时被静默丢弃（编辑器显示粗体、存盘后消失，
    // `# a **c**` → `# a c`、`# a *c*` → `# a c`）。code/mark/s/strike/sub/sup/
    // span 各有占位保护，唯独 strong/em 没有——此步补齐，与它们同款：
    // 占位 + 还原（逆序还原使内层占位符先于外层展开）。
    // 内容含裸标签的（如 <strong><s>x</s></strong>）不在此处理：上方删除线互嵌
    // 步骤已按字面 HTML 保护，与既有约定一致。
    {
        int n = 0;
        static const QRegularExpression headingRe(
            R"re(<(h[1-6])\b[^>]*>.*?</\1>)re",
            QRegularExpression::DotMatchesEverythingOption);
        // 内容限定无裸标签（[^<>]*）——嵌套标记交给其它步骤，避免备份不成立
        static const QRegularExpression emphRe(
            R"re(<((?:strong|b)|(?:em|i))\b[^>]*>([^<>]*)</\1>)re");
        globalReplaceWith(processed, headingRe,
            [&collisions, &placeholders, &backups, &n]
            (const QRegularExpressionMatch &hm) {
                QString h = hm.captured();
                globalReplaceWith(h, emphRe,
                    [&collisions, &placeholders, &backups, &n]
                    (const QRegularExpressionMatch &em) {
                        const QString tag = em.captured(1).toLower();
                        const bool bold = (tag == QLatin1String("strong")
                                           || tag == QLatin1String("b"));
                        const QString inner =
                            escapeExtDelimsOutsideTags(em.captured(2), true);
                        // 空内容或贴边星号：**/* 会与内容并成歧义序列
                        //（***a*** 等），保留字面 HTML（与 ST 步的贴边处理同型）
                        if (inner.isEmpty() || inner.startsWith(QLatin1Char('*'))
                            || inner.endsWith(QLatin1Char('*')))
                            return em.captured(0);
                        const QString ph = collisionFreePlaceholder(
                            collisions, "HE", n++);
                        placeholders << ph;
                        backups << (bold ? QStringLiteral("**") + inner
                                                + QStringLiteral("**")
                                         : QStringLiteral("*") + inner
                                                + QStringLiteral("*"));
                        return ph;
                    });
                return h;
            });
    }

    // 删除线 <s>/<del>/<strike> 及 <span text-decoration:line-through> → ~~...~~
    //   toHtml 已把 ~~x~~ 转为 <s>（TipTap Strike 契约）；此处对称还原。内层
    //   下标 <sub> 已在上一步转为 ZZSB 占位符，随 <s> 一并装入 ZZST，逆序还原时
    //   ZZST 先开、ZZSB 后开（~~H~2~O~~ 逐字往返）。
    {
        int n = 0;
        static const QRegularExpression strikeRe(R"(<(s|del|strike)\b[^>]*>(.*?)</\1>)",
            QRegularExpression::DotMatchesEverythingOption);
        globalReplaceWith(processed, strikeRe,
            [&collisions, &placeholders, &backups, &n](const QRegularExpressionMatch &m) {
                QString ph = collisionFreePlaceholder(collisions, "ST", n++);
                placeholders << ph;
                // 内层下标贴删除线分隔符边缘（~X~ 贴 ~~）时 ~~…~~ 不可解析，
                // 保留字面 <s>…</s>（m.captured(0)，含已占位的内层 ZZSB，逆序还原）
                QString inner = escapeExtDelimsOutsideTags(m.captured(2), true);
                backups << (strikeEdgeTildeCollision(inner)
                            ? m.captured(0) : "~~" + inner + "~~");
                return ph;
            });
        // <span text-decoration:line-through>（兼容外部 HTML / 旧 toHtml 输出）
        static const QRegularExpression lineThroughRe(
            R"(<span\b[^>]*\btext-decoration\s*:\s*line-through[^>]*>(.*?)</span>)",
            QRegularExpression::DotMatchesEverythingOption);
        globalReplaceWith(processed, lineThroughRe,
            [&collisions, &placeholders, &backups, &n](const QRegularExpressionMatch &m) {
                QString ph = collisionFreePlaceholder(collisions, "ST", n++);
                placeholders << ph;
                QString inner = escapeExtDelimsOutsideTags(m.captured(1), true);
                backups << (strikeEdgeTildeCollision(inner)
                            ? m.captured(0) : "~~" + inner + "~~");
                return ph;
            });
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
        // <span style="...">text</span>（嵌套感知：平衡扫描到配对 </span>，
        // 与 toHtml 侧 extractHtmlInlineElements 同型——非贪婪匹配在嵌套
        // span 下备份不平衡、尾部残留）
        {
            // 单遍栈配对预计算每个 span 开标签的配对闭标签（O(n) 总成本）。
            // 替代对每个开标签调用 findMatchingSpanClose 的 O(K·n) 逐次平衡
            // 扫描（match(s,pos) 每次从 pos 全串扫描；实测 3000 span 文档该
            // 扫描独占 ~2s）。栈配对与「从开标签起数深度」同为标准括号配对，
            // 对良构/未闭合/孤闭标签的行为一致。
            static const QRegularExpression spanTagRe("<(/?)span\\b[^>]*>");
            QHash<int, int> closeByOpen;
            {
                QVector<int> openStack;
                auto tit = spanTagRe.globalMatch(processed);
                while (tit.hasNext()) {
                    const auto t = tit.next();
                    if (t.captured(1).isEmpty())
                        openStack.append(t.capturedStart());
                    else if (!openStack.isEmpty())
                        closeByOpen.insert(openStack.takeLast(), t.capturedStart());
                }
            }
            static const QRegularExpression spanOpenRe(
                R"(<span\b[^>]*\bstyle\s*=\s*"[^"]*"[^>]*>)");
            globalExtractWith(processed, spanOpenRe,
                [&](const QRegularExpressionMatch &m, QString &phOut, int &consumedTo) {
                    const int closeStart = closeByOpen.value(m.capturedStart(), -1);
                    if (closeStart < 0)
                        return false;   // 未闭合：留原样交后续宽容解析
                    const int end = closeStart + 7;
                    const QString whole = processed.mid(m.capturedStart(), end - m.capturedStart());
                    QString ph = collisionFreePlaceholder(collisions, "HS", n++);
                    placeholders << ph;
                    // 保留完整原始 HTML，但文本中的字面 ^/~/~~ 转义（重载时
                    // sup/sub/strike 后处理会误触发），标签与属性不受影响
                    backups << escapeExtDelimsOutsideTags(whole, true);
                    consumedTo = end;
                    phOut = ph;
                    return true;
                });
        }
        // <u>text</u>（<u> 可能位于最后一个 <span> 之前，故重新全局匹配）
        QRegularExpression uRe(
            R"(<u>(.*?)</u>)",
            QRegularExpression::DotMatchesEverythingOption);
        globalReplaceWith(processed, uRe,
            [&collisions, &placeholders, &backups, &n]
            (const QRegularExpressionMatch &m) {
                QString ph = collisionFreePlaceholder(collisions, "UL", n++);
                placeholders << ph;
                backups << escapeExtDelimsOutsideTags(m.captured(), true);
                return ph;
            });
        // <kbd>text</kbd>（内联键盘元素，QTextDocument::toMarkdown 会丢弃标签）
        QRegularExpression kbdRe(
            R"(<kbd>(.*?)</kbd>)",
            QRegularExpression::DotMatchesEverythingOption);
        globalReplaceWith(processed, kbdRe,
            [&collisions, &placeholders, &backups, &n]
            (const QRegularExpressionMatch &m) {
                QString ph = collisionFreePlaceholder(collisions, "KD", n++);
                placeholders << ph;
                backups << escapeExtDelimsOutsideTags(m.captured(), true);
                return ph;
            });
        // <small>text</small>（内联小字元素）
        QRegularExpression smallRe(
            R"(<small>(.*?)</small>)",
            QRegularExpression::DotMatchesEverythingOption);
        globalReplaceWith(processed, smallRe,
            [&collisions, &placeholders, &backups, &n]
            (const QRegularExpressionMatch &m) {
                QString ph = collisionFreePlaceholder(collisions, "SM", n++);
                placeholders << ph;
                backups << escapeExtDelimsOutsideTags(m.captured(), true);
                return ph;
            });

        // 2) 引用 → > 前缀。后移至行内占位保护（code/mark/span/u/kbd/small）之后：
        //    曾位于行内转换之前，inner 的无条件标签剥离把引用内所有行内格式连同
        //    标签剥掉——WYSIWYG 对引用内文字加粗，每次保存即丢语义
        //    （> **加粗** → > 加粗）。后移后：handler 管理的行内元素已是 ZZ*
        //    占位符（剥离无伤）；Qt 侧管理的标签族（strong/em/s/mark/sub/sup/
        //    a/img）到不了 Qt 转换，由 blockquoteInnerInlineToMd 手动转 MD 语法。
        {
            int n = 0;
            // <blockquote> 标签先处理。
            // 递归 + 逐行前缀（阶段 4 / 审查 P2-8）：内容经完整 fromHtml 主管线
            // 嵌套转换——列表/hr/围栏/嵌套 blockquote 均保结构（旧实现仅手动转
            // 行内标签、块级全部剥平：引用内列表变裸列表、二级引用塌成一级、
            // 围栏体丢 > 前缀）。外层管线已保护的 ZZ* 占位符在递归结果中字面
            // 透传，须就地展开后再加前缀——曾展开在加前缀之后，多行备份只有
            // 首行得到 > 前缀，围栏重开即逃出引用。嵌套 <blockquote> 由递归
            // 转出内层 "> " 行，外层再统一加一级前缀（> > a）。
            static const QRegularExpression bqOpenRe(R"(<blockquote\b[^>]*>)");
            globalExtractWith(processed, bqOpenRe,
                [&n, &collisions, &placeholders, &backups, &wrapBlock, &processed]
                (const QRegularExpressionMatch &m, QString &outReplacement, int &consumedTo) {
                    // 平衡配对（嵌套引用取同层闭合，见 findMatchingBlockquoteClose）
                    const int closeStart = findMatchingBlockquoteClose(processed, m.capturedStart());
                    if (closeStart < 0)
                        return false;   // 未闭合：留原样交后续宽容解析
                    const QString raw = processed.mid(m.capturedEnd(),
                                                      closeStart - m.capturedEnd());
                    // 内层形态三分支：
                    // ① 含嵌套 <blockquote>（toHtml 复杂引用预抽取产物）：递归已
                    //    转出内层 "> " 行，外层整体再加一级（> > a）；
                    // ② Qt 扁平形态（<p margin-left:40px>，老简单引用路径产物）：
                    //    递归的 bqRe 已加过一级，直通不再加（曾双重前缀）；
                    // ③ 普通内容：递归转块级结构（列表/hr/围栏），逐行加一级。
                    const bool hasNestedBq = raw.contains(QLatin1String("<blockquote"));
                    static const QRegularExpression flatQuoteRe(
                        R"(<(p|pre)\b[^>]*margin-left:\s*40px)");
                    const bool flatQtQuote =
                        !hasNestedBq && flatQuoteRe.match(raw).hasMatch();
                    QString innerMd = MarkdownHandler::fromHtml(raw);
                    expandPlaceholdersInPlace(innerMd, placeholders, backups);
                    QString quoted;
                    if (flatQtQuote) {
                        quoted = innerMd.trimmed();
                    } else {
                        QStringList ls = innerMd.split('\n');
                        while (!ls.isEmpty() && ls.last().trimmed().isEmpty())
                            ls.removeLast();
                        for (const QString &l : ls) {
                            quoted += l.trimmed().isEmpty()
                                ? QStringLiteral(">\n")    // 引用内空行保块边界
                                : "> " + l + QStringLiteral("\n");
                        }
                        quoted = quoted.trimmed();
                    }
                    QString ph = collisionFreePlaceholder(collisions, "BQ", n++);
                    placeholders << ph;
                    backups << quoted;
                    consumedTo = closeStart + 13;   // 含 </blockquote>
                    outReplacement = wrapBlock(ph);
                    return true;
                });
            // 裸 margin-left <p>（未被 <blockquote> 包裹的）
            QRegularExpression bqRe(
                R"(<p[^>]*margin-left:\s*40px[^>]*margin-right:\s*40px[^>]*>(.*?)</p>)",
                QRegularExpression::DotMatchesEverythingOption);
            globalReplaceWith(processed, bqRe,
                [&n, &collisions, &placeholders, &backups, &wrapBlock]
                (const QRegularExpressionMatch &m) {
                    QString text = blockquoteInnerInlineToMd(m.captured(1));
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
                    QString ph = collisionFreePlaceholder(collisions, "BQ", n++);
                    placeholders << ph;
                    backups << quoted;
                    return wrapBlock(ph);
                });
        }
        // <div>（块级容器，排除 data-type 内部结构）。
        // TipTap taskItem 的结构性内容容器 <div>（紧跟 </label>）是编辑器契约
        // 结构而非用户手写 div，不保护：保留原始标签，由 taskListInnerToMd 的
        // taskItemInlineText 剥离。否则嵌套任务列表（嵌在通用列表内，经步骤
        // 5b 的 generalListInnerToMd 序列化）拿到 ZZDV 占位符，条目文本变成
        // 原始 <div> HTML。
        QRegularExpression divRe(
            R"(<div\b(?![^>]*\bdata-type\s*=)[^>]*>(.*?)</div>)",
            QRegularExpression::DotMatchesEverythingOption);
        globalReplaceWith(processed, divRe,
            [&collisions, &placeholders, &backups, &processed, &n]
            (const QRegularExpressionMatch &m) {
                const int di = m.capturedStart();
                if (di >= 8 && processed.mid(di - 8, 8) == QLatin1String("</label>"))
                    return m.captured();  // TipTap taskItem 结构 div：原样保留
                QString ph = collisionFreePlaceholder(collisions, "DV", n++);
                placeholders << ph;
                backups << m.captured();
                return ph;
            });
        // 表格列对齐：TipTap 在 <td>/<th> 标签上携带 style="text-align:..."。
        // Qt setHtml 能导入但 toMarkdown 从不输出对齐标记（恒 -），故按列提取
        // 对齐（列内首个非空值）存入 tableAligns，由步骤 6 注回分隔行。
        // 同时剥离表格内 td/th 与 p 上的 text-align 声明：p 上的若留存会被
        // 下方 ZZA 对齐保护包成 <div align> 占位符，破坏表格结构（单元格内容
        // 变为内嵌 HTML 字面量）。
        //
        // 单趟合并处理：原「对齐提取+防护+剥离」与「合并行提取」两趟
        // tableRegionRe.match(processed, offset) 全串扫描合并为一趟 globalMatch
        // 重建。列对齐在原始 region 上提取、合并行判定在防护/剥离后的 region
        // 上进行——与原两趟的先后次序一致；TC 注入只改空单元格文本、剥离只动
        // style 属性，均不改 colspan/行列结构，判定结果不受影响。行级扫描同样
        // 单遍化（globalMatch 收集全部 <tr>，替代 rowRe.match(region, roff) 的
        // O(行数×|region|) 逐次全串扫描；逐格扫描是行内小串，保留）。
        static const QRegularExpression tableRegionRe(
            QStringLiteral("(<table\\b[^>]*>)(.*?)(</table>)"),
            QRegularExpression::DotMatchesEverythingOption);
        static const QRegularExpression tblRowRe(
            QStringLiteral("<tr\\b[^>]*>(.*?)</tr>"),
            QRegularExpression::DotMatchesEverythingOption);
        static const QRegularExpression tblCellRe(
            QStringLiteral("<t[dh]\\b([^>]*)>(.*?)</t[dh]>"),
            QRegularExpression::DotMatchesEverythingOption);
        static const QRegularExpression colspanRe(
            QStringLiteral("\\bcolspan\\s*=\\s*[\"'](\\d+)[\"']"));
        globalReplaceWith(processed, tableRegionRe,
            [&](const QRegularExpressionMatch &tm) {
                QString region = tm.captured(2);
                QStringList colAligns;
                {
                    static const QRegularExpression cellAttrRe(
                        QStringLiteral("<t[dh]\\b([^>]*)>"));
                    // style 的 text-align 优先，其次 align 属性（与 TipTap parseAlign 同序）
                    static const QRegularExpression styleAlignRe(
                        QStringLiteral("\\btext-align\\s*:\\s*(center|right|left)"));
                    static const QRegularExpression attrAlignRe(
                        QStringLiteral("\\balign\\s*=\\s*\"(center|right|left)\""));
                    auto rit = tblRowRe.globalMatch(region);
                    while (rit.hasNext()) {
                        auto rm = rit.next();
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
                    }
                }
                // 空单元格注入占位符（与 toHtml protectTableRowForQt 的 ZZTC 防护
                // 对偶）：Qt toMarkdown 按列宽生成分隔行连字符，空列 0 宽 → 全空
                // 表格分隔行退化 |||（与数据行无异，重开即丢表格）。注入非空文本
                // 使每列获得 ≥1 连字符；占位符入共享 placeholders/backups（backup
                // 为空串），通用还原（步骤 4）回空后由规范化（步骤 6）补 |  | 形态，
                // 幂等。两种空形态均匹配：裸 <td></td> 与 TipTap 实际产的
                // <td><p></p></td>（vitest 探针实测 insertTable 产物）。位于脚注
                // 容器提取之后 → 天然只作用于正文表格；定义内表格由嵌套 fromHtml
                // 递归获得同款防护。
                {
                    static const QRegularExpression emptyCellRe(
                        QStringLiteral(
                            "(<t[dh]\\b[^>]*>)(\\s*(?:<p>\\s*</p>\\s*)?)(</t[dh]>)"));
                    globalReplaceWith(region, emptyCellRe,
                        [&collisions, &placeholders, &backups]
                        (const QRegularExpressionMatch &m) {
                            const QString ph = collisionFreePlaceholder(
                                collisions, "TC", placeholders.size());
                            placeholders << ph;
                            backups << QString();   // 还原为空
                            return m.captured(1) + ph + m.captured(3);
                        });
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
                // 提取 tableFullMergeRows（防护/剥离后的 region，与原第二趟一致）：
                // 源码单格行（| x |，toHtml 产 <td colspan="N"> 占满所有列）经 Qt
                // toMarkdown 会被展开成 N 格，步骤 6b 据此折叠回单格，保证合并
                // 单元格往返保真。与 toHtml protectTableRowForQt 的「恰 1 个显式
                // 单元格 → colspan」规则对偶。判定：数据行首格 colspan == 表头
                // 列数（含 colspan 求和），即整行占满。
                {
                    int colCount = 0;
                    QList<int> mergedRows;
                    int rowIdx = 0;
                    auto rit = tblRowRe.globalMatch(region);
                    while (rit.hasNext()) {
                        auto rm = rit.next();
                        QString row = rm.captured(1);
                        if (rowIdx == 0) {
                            // 表头行：列数 = 各格 colspan 之和（无 colspan 即 1）
                            int coff = 0;
                            while (true) {
                                auto cm = tblCellRe.match(row, coff);
                                if (!cm.hasMatch()) break;
                                auto cs = colspanRe.match(cm.captured(1));
                                colCount += cs.hasMatch() ? cs.captured(1).toInt() : 1;
                                coff = cm.capturedEnd();
                            }
                        } else {
                            // 数据行：首格 colspan == 列数 → 整行合并（0-based body 索引）
                            auto cm = tblCellRe.match(row);
                            if (cm.hasMatch()) {
                                auto cs = colspanRe.match(cm.captured(1));
                                if (cs.hasMatch() && cs.captured(1).toInt() == colCount)
                                    mergedRows << (rowIdx - 1);
                            }
                        }
                        ++rowIdx;
                    }
                    tableFullMergeRows << mergedRows;
                }
                tableAligns << colAligns;
                return tm.captured(1) + region + tm.captured(3);
            });
        // 对齐：<p style="text-align:..."> / 标题 text-align → <div align="...">
        // TipTap TextAlign 为段落/标题注入 text-align CSS，toMarkdown 丢弃。
        // 提取对齐值并还原为 <div align="...">（已支持往返），内容 ZZ* 占位符
        // 在逆序还原阶段后续解析。
        static const QRegularExpression alignRe(
            R"re(<(p|h[1-6])\b[^>]*\bstyle\s*=\s*"([^"]*\btext-align\s*:\s*(left|center|right)[^"]*)"[^>]*>(.*?)</\1>)re",
            QRegularExpression::DotMatchesEverythingOption);
        globalReplaceWith(processed, alignRe,
            [&collisions, &placeholders, &backups, &n](const QRegularExpressionMatch &m) {
                QString tag = m.captured(1);
                QString fullStyle = m.captured(2);
                QString align = m.captured(3);
                QString content = m.captured(4);
                // 从 style 中剥离 text-align:XXX（含前后分号/空格）
                QString stripped = fullStyle;
                static const QRegularExpression stripAlignRe(
                    R"(\s*text-align\s*:\s*(left|center|right)\s*;?)");
                stripped.remove(stripAlignRe);
                stripped = stripped.trimmed();
                if (stripped.endsWith(';'))
                    stripped.chop(1);
                stripped = stripped.trimmed();
                QString ph = collisionFreePlaceholder(collisions, "A", n++);
                placeholders << ph;
                backups << QString("<div align=\"%1\">%2</div>").arg(align, content);
                if (stripped.isEmpty())
                    return QString("<%1>%2</%1>").arg(tag, ph);
                return QString("<%1 style=\"%2\">%3</%1>").arg(tag, stripped, ph);
            });
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
            // 跳过列表容器内（空列表项由列表自绘路径处理，避免空行数重复计算）。
            // 同样跳过表格容器内：TipTap 把空单元格序列化为 <td><p></p></td>
            // （单元格必须含段落节点），其中的空段落是「空单元格内容」而非空行；
            // 若替换成 ZZBL 占位符，toMarkdown 后占位符进入 | ... | 行，
            // 还原时整行不等于占位符 → 泄漏（如 ZZBL2ZZ 出现在表格行）。
            // 表格外的空段落（表格前后空行）不受影响，正常保护还原为 &nbsp;。
            auto insideListContainer = [](const QString &s, int pos) {
                int lastOpen = -1, lastClose = -1;
                for (const QString &tag : {QStringLiteral("<li"), QStringLiteral("<ul"),
                                            QStringLiteral("<ol"), QStringLiteral("<table"),
                                            QStringLiteral("<td"), QStringLiteral("<th"),
                                            QStringLiteral("<tr"), QStringLiteral("<tbody"),
                                            QStringLiteral("<thead"), QStringLiteral("<tfoot")}) {
                    const int o = s.lastIndexOf(tag, pos);
                    if (o > lastOpen) lastOpen = o;
                }
                for (const QString &tag : {QStringLiteral("</li>"), QStringLiteral("</ul>"),
                                            QStringLiteral("</ol>"), QStringLiteral("</table>"),
                                            QStringLiteral("</td>"), QStringLiteral("</th>"),
                                            QStringLiteral("</tr>"), QStringLiteral("</tbody>"),
                                            QStringLiteral("</thead>"), QStringLiteral("</tfoot>")}) {
                    const int c = s.lastIndexOf(tag, pos);
                    if (c > lastClose) lastClose = c;
                }
                return lastOpen > lastClose;
            };
            // 内容为「空白/NBSP* + 可选 br + 空白/NBSP*」的段落视为空段落。
            // 注意 Qt 正则的 \s 不匹配 NBSP 字符 U+00A0（实测），而空段落内容
            // 既可能是 &nbsp; 实体（TipTap getHTML 的空段落），也可能是 NBSP
            // 字符（toHtml setMarkdown 把字面 &nbsp; 行转成的段落），二者均须
            // 显式列入备选；识别后统一还原为字面 &nbsp; 行（跨编辑器可移植）。
            static const QRegularExpression emptyPRe(
                R"(<p\b[^>]*>(?:(?:\s|&nbsp;|\x{00a0})*(?:<br\s*/?>)?(?:\s|&nbsp;|\x{00a0})*)</p>)");
            int blN = 0;
            globalReplaceWith(processed, emptyPRe,
                [&collisions, &blankLinePhs, &blN, &processed, &insideListContainer]
                (const QRegularExpressionMatch &m) {
                    if (insideListContainer(processed, m.capturedStart()))
                        return m.captured();   // 列表/表格容器内：保留
                    const QString ph = collisionFreePlaceholder(collisions, "BL", blN);
                    ++blN;
                    blankLinePhs << ph;
                    return QStringLiteral("<p>%1</p>").arg(ph);
                });
        }
        // 行内连续 NBSP（WYSIWYG 多空格）→ 占位符 → 还原 &nbsp; 实体。
        // 用户 WYSIWYG 输入连续空格（ProseMirror 以 NBSP 保留）序列化为
        // &nbsp; 实体；Qt toMarkdown 把实体转成 NBSP 字符（源码不可见、不可移植），
        // 故在 setHtml 前保护为占位符，还原时输出字面 &nbsp; 实体。仅保护连续
        // 2+ 个（单空格往返安全，无需转义）。空段落保护已消费整段 NBSP 的段落，
        // 此处剩余的 &nbsp; 必为行内（与其他文本混排）。
        {
            static const QRegularExpression nbspRe(R"((?:&nbsp;|\x{00a0}){2,})");
            int nbspN = 0;
            globalReplaceWith(processed, nbspRe,
                [&collisions, &placeholders, &backups, &nbspN]
                (const QRegularExpressionMatch &m) {
                    const QString cap = m.captured(0);
                    int cnt = 0;
                    for (const QChar &c : cap)
                        if (c == QChar(0x00a0)) ++cnt;
                    cnt += cap.count(QStringLiteral("&nbsp;"));
                    const QString ph = collisionFreePlaceholder(collisions, "NB", nbspN);
                    ++nbspN;
                    placeholders << ph;
                    backups << QStringLiteral("&nbsp;").repeated(cnt);
                    return ph;
                });
        }
        // <br> 硬换行（不含 data-brk 标记——BRMARKER 生成的由 toMarkdown 处理）
        {
            static const QRegularExpression brRe(
                R"re(<br\b(?![^>]*\bdata-brk)[^>]*>)re");
            globalReplaceWith(processed, brRe,
                [&collisions, &placeholders, &backups, &n]
                (const QRegularExpressionMatch &m) {
                    QString ph = collisionFreePlaceholder(collisions, "BR", n++);
                    placeholders << ph;
                    backups << m.captured();  // 保留原样 <br> 标签
                    return ph;
                });
        }
        // <br data-brk>（编辑器软换行）：同样在 setHtml 前保护为占位符，
        // 否则 toMarkdown 把它转成 \n / \n····，软换行语义与块边界不可区分
        // （历史上还须与 80 列折行区分，折行已由 vendored writer 根治）。
        // 占位符存活到序列化之后，再按上下文还原。
        {
            static const QRegularExpression brSoftRe(
                R"re(<br\b[^>]*\bdata-brk[^>]*>)re");
            globalReplaceWith(processed, brSoftRe,
                [&collisions, &brSoftPlaceholders, &n]
                (const QRegularExpressionMatch &m) {
                    QString ph = collisionFreePlaceholder(collisions, "BK", n++);
                    brSoftPlaceholders << ph;
                    return ph;
                });
        }
    }

    // 5b) 通用列表 <ul>/<ol>（非 taskList）→ GFM markdown。
    //     Qt toMarkdown 对 3 层以上嵌套列表的缩进输出不自洽（见 generalListInnerToMd
    //     注释），故与任务列表一样在 setHtml 前自绘，占位符 toMarkdown 后还原。
    //     置于 step N（span/u/kbd/small/div/br 占位保护）之后：列表条目内的
    //     <span>/<u> 等已是 ZZ* 占位符，taskItemInlineText 剥离标签时不误伤；
    //     taskList 的 <ul data-type> 已在 step 5 提取，此处剩余 <ul> 均为通用。
    {
        // 合并 <ul>/<ol> 开标签为单正则以 globalMatch 一次取全部（含负向断言
        // 排除 taskList 的 <ul data-type="taskList">），按位置顺序单遍重建。
        // 替代 while(true) 内 ulRe.match + olRe.match 双全串扫描 + replace 的
        // O(K·n) 模式：每层嵌套列表的 <ul>/<ol> 开标签也会被收集，但处理外层
        // 列表后其整个区域已被占位符替换，内层开标签位置 < last 被跳过。
        QRegularExpression listOpenRe(
            "<(ul|ol)\\b(?![^>]*\\bdata-type\\s*=\\s*\"taskList\")[^>]*>");
        QRegularExpression startRe("\\bstart\\s*=\\s*\"?(\\d+)\"?");

        QString result;
        result.reserve(processed.size());
        int last = 0;
        int n = 0;
        auto openIt = listOpenRe.globalMatch(processed);
        while (openIt.hasNext()) {
            auto m = openIt.next();
            const int p = m.capturedStart();
            if (p < last) continue;   // 已被外层列表替换区域覆盖（内层嵌套）
            const bool ordered = (m.captured(1) == QLatin1String("ol"));
            int close = findMatchingListClose(processed, p, ordered);
            if (close < 0) continue;  // 未闭合：跳过本开标签，留内部可闭合的
            int start = 1;
            if (ordered) {
                auto sm = startRe.match(m.captured());
                if (sm.hasMatch()) start = sm.captured(1).toInt();
            }
            result += processed.mid(last, p - last);
            QString md2 = generalListInnerToMd(
                processed.mid(m.capturedEnd(), close - m.capturedEnd()),
                0, ordered, start);
            while (md2.endsWith('\n')) md2.chop(1);
            QString ph = collisionFreePlaceholder(collisions, "GL", n++);
            placeholders << ph;
            backups << md2;
            result += wrapBlock(ph);
            last = close + 5;   // </ul>/</ol> 均为 5 字符
        }
        result += processed.mid(last);
        processed = result;
    }

    // D) 保护字面 **、*、==、&lt;、&gt;、~~、~、^ 不被 toMarkdown() 重新解释为 Markdown 语法。
    //    用户通过 \*\*、\=\=、\<、\> 转义写入的字面符号，经 toHtml 已转为
    //    纯文本/HTML 实体；QTextDocument::toMarkdown() 不会反转为转义形式，
    //    直接输出裸 **/==，且 &lt;→\< 但 &gt;→>（不加反斜杠），不对称。
    //    此处用简易状态机识别 HTML 标签外的字面符号/实体并替换为占位符，
    //    还原时输出为转义形式，保证 \<、\> 往返保真。
    //    孤立字面 *（非 **）同样需保护：Qt 6.4.2 的 toMarkdown 不会为标题/正文
    //    内联文本中的 * 添加转义，重载时 *A* 会被误解析为强调，破坏往返保真。
    //    字面 ~~（成对）/~（单个）/^ 同理：toHtml 把 \^ 等转义定界符还原为字面
    //    文本后，若不转义直接输出，重载时 ^x^ 后处理会把它变回 <sup>（转义
    //    语义丢失）；两个孤立 ~ 同行还会被 extractSubscripts 配成下标。~~ 分支
    //    必须先于 ~（否则 ~~ 被拆成两个 \~，虽收敛但形态次优）。
    //    ** 的例外：后跟 ASCII 空白或块级边界（</p>、<br 等，重载后等效
    //    行尾）时还原裸 **——flanking 规则使其无法作开启符，md4c 拒配对
    //    残留为字面（链式上下文中 **`code`** 后跟空格+中文即此形态），
    //    裸形式重载同样稳定且渲染一致；转义形态反而造成首轮字节失真。
    //    注意：强调语法 * 来自 <em> 标签、在 setHtml 之后由 Qt 生成，本步在
    //    setHtml 之前运行，故只命中真正的字面 *，不影响 <em>→* 的正常转换。
    //    （mark/strike/sub/sup/N 步备份的内容不经过本步，由
    //    escapeExtDelimsOutsideTags 在各构造点单独转义。）
    {
        QString result;
        result.reserve(processed.size());
        bool inTag = false;
        int nDs = 0; // ** 计数
        int nSt = 0; // 孤立 * 计数
        int nEq = 0; // == 计数
        int nLt = 0; // &lt; 计数
        int nGt = 0; // &gt; 计数
        int nTt = 0; // ~~ 计数
        int nTd = 0; // 孤立 ~ 计数
        int nCr = 0; // ^ 计数
        // ASCII 空白或串尾（CommonMark 空白集不含全角空格等 Unicode 空白，
        // md4c 对其后视作可开启，不得豁免）
        const auto asciiWsOrEof = [&processed](int k) {
            return k >= processed.size() || processed[k] == ' '
                || processed[k] == '\t' || processed[k] == '\n'
                || processed[k] == '\r';
        };
        // 块级闭合/换行标签前缀（TipTap/Qt 输出形态）：** 紧贴其前，
        // 重载后等效行尾；表格单元格边界（td/th/tr）后随同行内容，不豁免
        const auto blockBoundaryFollows = [&processed](int k) {
            if (k >= processed.size() || processed[k] != '<')
                return false;
            const QStringView rest = QStringView(processed).mid(k);
            return rest.startsWith(QLatin1String("</p>"))
                || rest.startsWith(QLatin1String("<br"))
                || rest.startsWith(QLatin1String("</h"))
                || rest.startsWith(QLatin1String("</li"))
                || rest.startsWith(QLatin1String("</ul"))
                || rest.startsWith(QLatin1String("</ol"))
                || rest.startsWith(QLatin1String("</bl"))
                || rest.startsWith(QLatin1String("</pre"))
                || rest.startsWith(QLatin1String("</tab"))
                || rest.startsWith(QLatin1String("</div"));
        };
        for (int i = 0; i < processed.size(); ++i) {
            QChar c = processed[i];
            if (c == '<')
                inTag = true;
            else if (c == '>')
                inTag = false;

            if (!inTag && c == '*' && i + 1 < processed.size()
                && processed[i + 1] == '*') {
                QString ph = collisionFreePlaceholder(collisions, "DS", nDs++);
                placeholders << ph;
                // 后跟 ASCII 空白/块级边界（重载后等效行尾）时还原裸 **：
                // flanking 规则使其无法作开启符，md4c 拒配对残留为字面，
                // 裸形式重载同样稳定且本引擎/CommonMark/GitHub 渲染一致
                //（对比 ~~：裸形式 GitHub 是删除线，故归一为 \~~）。仍走
                // 占位符：裸 ** 留在文本层会被 Qt toMarkdown 版本相关地
                // 转义（6.8 转义/6.4 不转义），占位符保证输出确定性。
                const bool tailLiteral =
                    asciiWsOrEof(i + 2) || blockBoundaryFollows(i + 2);
                backups << (tailLiteral ? QStringLiteral("**")
                                        : QStringLiteral("\\*\\*"));
                result += ph;
                ++i;
            } else if (!inTag && c == '*') {
                // 孤立字面 *（已被上面的 ** 分支排除）：还原为 \*，
                // 使转义与 Qt 版本无关（6.4.2 不转义、6.8.2 转义，统一为转义）。
                QString ph = collisionFreePlaceholder(collisions, "ST", nSt++);
                placeholders << ph;
                backups << QStringLiteral("\\*");
                result += ph;
            } else if (!inTag && c == '=' && i + 1 < processed.size()
                       && processed[i + 1] == '=') {
                QString ph = collisionFreePlaceholder(collisions, "EQ", nEq++);
                placeholders << ph;
                backups << QStringLiteral("\\=\\=");
                result += ph;
                ++i;
            } else if (!inTag && c == '~' && i + 1 < processed.size()
                       && processed[i + 1] == '~') {
                QString ph = collisionFreePlaceholder(collisions, "TT", nTt++);
                placeholders << ph;
                backups << QStringLiteral("\\~~");
                result += ph;
                ++i;
            } else if (!inTag && c == '~') {
                // 孤立字面 ~（已被上面的 ~~ 分支排除）：重载时与行内另一孤立 ~
                // 配成下标的风险，统一转义
                QString ph = collisionFreePlaceholder(collisions, "TD", nTd++);
                placeholders << ph;
                backups << QStringLiteral("\\~");
                result += ph;
            } else if (!inTag && c == '^') {
                QString ph = collisionFreePlaceholder(collisions, "CR", nCr++);
                placeholders << ph;
                backups << QStringLiteral("\\^");
                result += ph;
            } else if (!inTag && c == '&' && i + 3 < processed.size()
                       && processed[i + 1] == 'l' && processed[i + 2] == 't'
                       && processed[i + 3] == ';') {
                QString ph = collisionFreePlaceholder(collisions, "LT", nLt++);
                placeholders << ph;
                backups << QStringLiteral("\\<");
                result += ph;
                i += 3;
            } else if (!inTag && c == '&' && i + 3 < processed.size()
                       && processed[i + 1] == 'g' && processed[i + 2] == 't'
                       && processed[i + 3] == ';') {
                QString ph = collisionFreePlaceholder(collisions, "GT", nGt++);
                placeholders << ph;
                backups << QStringLiteral("\\>");
                result += ph;
                i += 3;
            } else {
                result += c;
            }
        }
        if (nDs > 0 || nSt > 0 || nEq > 0 || nLt > 0 || nGt > 0
            || nTt > 0 || nTd > 0 || nCr > 0)
            processed = result;
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
        // 单遍扫描：tagRe.globalMatch 总成本 O(n)，替代逐次 tagRe.match(s, pos)
        // （Qt 逐次 match 每次都全串扫描，10810 个标签即 O(K·n) → O(n)）
        auto tagIt = tagRe.globalMatch(processed);
        while (tagIt.hasNext()) {
            auto m = tagIt.next();
            result += processed.mid(pos, m.capturedStart() - pos);
            const QString full = m.captured(0);
            const bool isClosing = !m.captured(1).isEmpty();
            const QString name = m.captured(2).toLower();
            const QString attrs = m.captured(3);
            const bool selfClosed = attrs.trimmed().endsWith('/');

            if (isClosing) {
                if (depth > 0) --depth;
                result += full;
            } else if (name == "img") {
                // 空 alt 保形：<img> 无 alt 或 alt="" 时注入占位 alt，令 vendored
                // Qt writer 写出 ![ZZIB0ZZ](src) 而非凭空造出的 ![image](src)
                // （qtextmarkdownwriter.cpp:589 对空 alt 填字面 "image"），
                // toMarkdown 之后统一替换回 ![](...)。
                const QString withAlt = injectEmptyAltSentinel(
                    full, attrs, collisions, emptyAltSentinels);
                if (depth == 0) {
                    // 裸顶层图片 → 包进 <p>，强制独立块（img 为 void，深度不变）
                    result += "<p>" + withAlt + "</p>";
                    changed = true;
                } else {
                    result += withAlt;
                    if (withAlt != full) changed = true;
                }
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
        static const QRegularExpression pListMarkerRe(
            R"((<p\b[^>]*>)\s*(\d{1,9}[.)]|[+\-])(?=\s))");
        int n = 0;
        globalReplaceWith(processed, pListMarkerRe,
            [&collisions, &placeholders, &backups, &n]
            (const QRegularExpressionMatch &m) {
                QString marker = m.captured(2);
                // 转义标记中的标点字符：1.→1\. / 1)→1\) / -→\- / +→\+
                QString escaped;
                if (marker.size() == 2)
                    escaped = marker.left(1) + QStringLiteral("\\") + marker.right(1);
                else
                    escaped = QStringLiteral("\\") + marker;
                QString ph = collisionFreePlaceholder(collisions, "LM", n++);
                placeholders << ph;
                backups << escaped;
                return m.captured(1) + ph;   // 保留 <p> 前缀，只替换标记部分
            });
    }

    QTextDocument doc;
    doc.setHtml(processed);
    QString md = serializeMarkdownNoWrap(doc);

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

    // （反换行已退役）toMarkdown 的 80 列折行由 vendored QtMarkdownWriter
    // 关闭（serializeMarkdownNoWrap，见 docs/vendor-qt-markdown-writer.md），
    // 序列化不再产生折行续行，原「逐行拼回折行」补丁（isWrapContinuation /
    // stripWrapIndent / endsWithEmphMarker + wrappedMarkerRe 转义剥离）随之
    // 删除。合法软换行仍由 ZZBK 占位符保护（见上文 brSoftRe 块），段内
    // 单 \n 不复存在，\n 只可能是块边界。

    // 3) 修复被合并进同一段落的块级占位符（必须在还原前）：
    //    setHtml 中相邻块级元素的占位符之间 \n 是软换行，QTextDocument 将其
    //    放入同一段落，toMarkdown 输出为同一行以空格分隔（"ZZCB0ZZ ZZBM0ZZ"）。
    //    多行备份（代码块围栏、块级公式）还原后会与相邻内容粘连
    //    （如闭合围栏与 $$ 粘成 "```$$"，围栏看似丢失）。
    //    处理：备份含换行的占位符必须独占一行——前后若有同行内容，插入空行分隔。
    // 全部多行备份占位符合集为 QSet，用通用占位符模式 ZZ[A-Z]+[0-9]+ZZ 匹配，
    // 前缀/后缀各一次单遍重建。通用模式比逐占位符 alternation（K 项）或逐占位符
    // 全串 replace（O(K·n)）都快：简单模式 O(n) 匹配 + O(1) 集合判断。
    // 语义与原逐占位符处理一致：相邻多行占位符、前缀后缀同时命中均正确分离。
    QSet<QString> multiSet;
    for (int i = 0; i < placeholders.size(); ++i)
        if (backups[i].contains('\n'))
            multiSet.insert(placeholders[i]);
    if (!multiSet.isEmpty()) {
        static const QRegularExpression phRe(R"(ZZ[A-Z]+[0-9]+ZZ)");
        // 前方有同行内容（前一字符非换行）→ 前插空行
        globalReplaceWith(md,
            QRegularExpression("([^\\n])(" + phRe.pattern() + ")"),
            [&multiSet](const QRegularExpressionMatch &m) {
                const QString ph = m.captured(2);
                return multiSet.contains(ph)
                    ? m.captured(1) + "\n\n" + ph
                    : m.captured(0);
            });
        // 后方有同行内容（后一字符非换行）→ 后插空行
        globalReplaceWith(md,
            QRegularExpression("(" + phRe.pattern() + ")([^\\n])"),
            [&multiSet](const QRegularExpressionMatch &m) {
                const QString ph = m.captured(1);
                return multiSet.contains(ph)
                    ? ph + "\n\n" + m.captured(2)
                    : m.captured(0);
            });
    }

    // 3.5) 还原段落空行占位符 ZZBL：占位符独占的行 → 字面 &nbsp; 行。
    //      toMarkdown 输出为「段落 + 空行 + ZZBLnZZ + 空行 + 段落」，占位符行
    //      替换为 &nbsp; 后即成为跨编辑器可移植的空行（Joplin/MarkText 等把
    //      &nbsp; 行渲染为空行；纯空行会被标准 md 渲染器折叠）。M 个空段落 →
    //      M 个 &nbsp; 行，与 WYSIWYG 空段落数一致。
    //      必须位于 \n{3,} 压缩之后：此时刻占位符均单行文本，替换才
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

    // 4) 还原占位符。多趟单遍还原（O(趟数·n)，替代逐占位符全串 replace 的
    //    O(K·n)）：每趟把 md 中全部剩余占位符 token 一次性替换为备份；嵌套
    //    占位符（外层 backup 含内层 token）由下一趟继续还原，趟数 = 最大嵌套
    //    深度。与原「逆序后入先还原」语义等价——backup 只含更早（更小序）的
    //    token，逆序还原本质即外层先展开、内层随后补全，多趟达成同样结果。
    //    用通用模式 ZZ[A-Z]+[0-9]+ZZ + 哈希查表，不用逐占位符 alternation
    //    （K 项拼接）：约 3000+ 个占位符时巨型多选正则超出 PCRE2 编译限制，
    //    构造出 invalid QRegularExpression，doMatch 直接 qFatal abort（海量
    //    行内代码的大文档真实可触发）。碰撞检测保证真占位符不与用户文本
    //    同串，通用模式多匹配到的必然是用户字面 ZZ 形态文本——查表未命中
    //    须原样保留，不得吞删。
    if (!placeholders.isEmpty()) {
        QHash<QString, QString> phMap;
        phMap.reserve(placeholders.size());
        for (int i = 0; i < placeholders.size(); ++i)
            phMap.insert(placeholders[i], backups[i]);
        static const QRegularExpression allRe(
            R"(ZZ[A-Z]+[0-9]+ZZ ?)");  // 尾随空格：needsWrapGuardSpace 的防折行空格
        for (;;) {
            bool replaced = false;
            globalReplaceWith(md, allRe,
                [&phMap, &replaced](const QRegularExpressionMatch &m) {
                    const QString tok = m.captured();
                    auto it = phMap.constFind(tok);
                    if (it != phMap.constEnd()) {
                        replaced = true;
                        return *it;  // key 含空格：guard 空格随 token 一并消费
                    }
                    if (tok.endsWith(QLatin1Char(' '))) {
                        // 空格也可能属于原文（key 无空格）：去空格再查，
                        // 命中则保留空格（不属于占位符的一部分）
                        it = phMap.constFind(tok.left(tok.size() - 1));
                        if (it != phMap.constEnd()) {
                            replaced = true;
                            return *it + QLatin1Char(' ');
                        }
                    }
                    return m.captured(0);  // 用户字面 ZZ 形态文本，保留
                });
            if (!replaced) break;
        }
    }

    // 4b) 还原 <br data-brk>（编辑器软换行）：按所在行上下文决定缩进，
    //     段落/引用 → \n；有序列表项 → \n····（4 空格，对齐两位数序号）；
    //     无序列表项 → \n··（2 空格）。缩进 = 行首前导空格 + 列表项续行缩进，
    //     保证嵌套列表项续行仍留在原项内。
    //     列表项恒为单行输出（折行已根治），行首列表标记可直接判定。
    // 单趟 token 扫描 + 集合判断重建：逐占位符 indexOf 曾每个 ph 从头扫半串
    // （3 万软换行 × 1MB 文档 ≈ 10^10 级字符比较）。行上下文从原串读取
    //（重建不改变原 md，位置始终有效）
    {
        // 软换行占位符家族是 ZZBK（<br data-brk>）；ZZBR 是普通 <br>，由通用
        // 还原处理，不在此列
        static const QRegularExpression brTokRe(QStringLiteral("ZZBK[0-9]+ZZ"));
        QSet<QString> phSet(brSoftPlaceholders.cbegin(), brSoftPlaceholders.cend());
        QString out;
        int pos = 0;
        while (true) {
            const auto m = brTokRe.match(md, pos);
            if (!m.hasMatch()) {
                out += md.mid(pos);
                break;
            }
            const int idx = m.capturedStart();
            out += md.mid(pos, idx - pos);
            QString repl = m.captured();
            if (phSet.contains(m.captured())) {
                const int lineStart = md.lastIndexOf(QLatin1Char('\n'), idx - 1) + 1;
                int lineEnd = md.indexOf(QLatin1Char('\n'), idx);
                if (lineEnd < 0) lineEnd = md.size();
                const QString line = md.mid(lineStart, lineEnd - lineStart);
                const QString t = line.trimmed();

                int leadingWs = 0;
                while (leadingWs < line.size()
                       && line[leadingWs] == QLatin1Char(' '))
                    ++leadingWs;

                repl = QStringLiteral("\n");
                static const QRegularExpression orderedRe(R"(^\d{1,9}[.)]\s)");
                static const QRegularExpression unorderedRe(R"(^[-+*]\s)");
                static const QRegularExpression hrRe(R"(^[-*_]{3,}\s*$)");
                if (orderedRe.match(t).hasMatch()) {
                    repl += QString(leadingWs + 4, QLatin1Char(' '));
                } else if (unorderedRe.match(t).hasMatch()
                           && !hrRe.match(t).hasMatch()) {
                    repl += QString(leadingWs + 2, QLatin1Char(' '));
                }
            }
            out += repl;
            pos = m.capturedEnd();
        }
        md = out;
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
    //    缩进续行（脚注定义内表格，Task 7 契约）重写时保留原行前导空白：
    //    否则定义在首个非缩进续行处断裂，表格逃逸成正文 pipe 字面文本。
    {
        int tableIdx = 0;
        QStringList lines = md.split('\n');
        static const QRegularExpression leadingWsRe(QStringLiteral("^[ \t]+"));
        // 围栏状态机：代码围栏内的 |...| 行是字面代码内容，不参与规范化，
        // 伪分隔行也不得抢占 tableAligns 对位（围栏行进出翻转）
        bool inFence = false;
        int fenceLen = 0;
        for (int i = 0; i < lines.size(); ++i) {
            QString line = lines[i];
            QString t = line.trimmed();
            if (updatesFenceState(t, inFence, fenceLen)) continue;
            if (inFence) continue;
            // 仅处理表格行（以 | 开头且以 | 结尾，非 blockquote 前缀 > |）
            if (!t.startsWith('|') || !t.endsWith('|')) continue;
            const auto wm = leadingWsRe.match(line);
            const QString prefix = wm.hasMatch() ? wm.captured() : QString();

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
                // 分隔行导致与后续表格错位），每格 :---: / ---: / :--- / ---。
                // 仅第 0 列（无前导空白）的表格参与 tableAligns 消费：
                // tableAligns 按文档序只对应正文表格（提取于脚注容器移除
                // 之后），缩进的定义内表格若参与消费会抢占正文表格的对位
                QStringList aligns;
                if (prefix.isEmpty() && tableIdx < tableAligns.size()
                        && tableAligns[tableIdx].size() == cells.size()) {
                    aligns = tableAligns[tableIdx];
                    ++tableIdx;
                }
                QStringList sepCells;
                for (int k = 0; k < cells.size(); ++k) {
                    QString a = (k < aligns.size()) ? aligns[k] : QString();
                    if (a.isEmpty() && !prefix.isEmpty()) {
                        // 缩进分隔行未被消费时保留既有标记：定义内表格的
                        // 对齐由嵌套 fromHtml 注入，此处不得剥离成 ---
                        const QString c = cells[k].trimmed();
                        if (c.startsWith(':') && c.endsWith(':') && c.size() >= 3)
                            a = QLatin1String("center");
                        else if (c.endsWith(':') && c.size() >= 2)
                            a = QLatin1String("right");
                        else if (c.startsWith(':') && c.size() >= 2)
                            a = QLatin1String("left");
                    }
                    if (a == QLatin1String("center")) sepCells << ":---:";
                    else if (a == QLatin1String("right")) sepCells << "---:";
                    else if (a == QLatin1String("left")) sepCells << ":---";
                    else sepCells << "---";
                }
                lines[i] = prefix + "| " + sepCells.join(" | ") + " |";
            } else {
                // 数据/表头行：每格 trim + 前后各一个空格
                QStringList padded;
                for (const auto &cell : cells)
                    padded << (" " + cell + " ");
                lines[i] = prefix + "|" + padded.join('|') + "|";
            }
        }
        md = lines.join('\n');
    }

    // 6b) 整行合并单元格还原：Qt toMarkdown 把单格行（| x |）产出的
    //     <td colspan="N"> 展开成 N 格，此处按 setHtml 前记录的 tableFullMergeRows
    //     折叠回单格（| x |），与 toHtml protectTableRowForQt 的「恰 1 个显式
    //     单元格 → colspan」规则对偶，保证合并单元格往返保真。
    //     按连续 |...| 行识别表格区域（与步骤 6 一致），行序表头/分隔/body 一一对应。
    //     仅第 0 列（无前导空白）行参与区域跟踪与 tableIdx 消费：缩进的定义内
    //     表格已由嵌套 fromHtml 折叠，tableFullMergeRows 也只记录正文表格
    //     （提取于脚注容器移除之后），缩进区域跳过防错位消费。
    if (!tableFullMergeRows.isEmpty()) {
        int tableIdx = 0;
        QStringList lines = md.split('\n');
        static const QRegularExpression leadingWsRe(QStringLiteral("^[ \t]+"));
        // 与步骤 6 同款围栏状态机：围栏内的 |...| 行不参与区域识别与对位消费
        bool inFence = false;
        int fenceLen = 0;
        int i = 0;
        while (i < lines.size()) {
            QString t = lines[i].trimmed();
            if (updatesFenceState(t, inFence, fenceLen)) { ++i; continue; }
            if (inFence) { ++i; continue; }
            if (leadingWsRe.match(lines[i]).hasMatch()
                    || !(t.startsWith('|') && t.endsWith('|'))) { ++i; continue; }
            QList<int> run;   // 当前表格的连续表行索引
            while (i < lines.size()) {
                QString lt = lines[i].trimmed();
                if (leadingWsRe.match(lines[i]).hasMatch()
                        || !(lt.startsWith('|') && lt.endsWith('|'))) break;
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

    // 空 alt 图片还原：![ZZIB0ZZ](src) → ![](src)（见 injectEmptyAltSentinel）。
    // 通用 token 正则 + 集合判断，未命中集合的是用户字面同形 alt，原样保留。
    if (!emptyAltSentinels.isEmpty()) {
        QSet<QString> sentinelSet(emptyAltSentinels.cbegin(),
                                  emptyAltSentinels.cend());
        globalReplaceWith(md,
            QRegularExpression(QStringLiteral("!\\[(ZZ[A-Z]+[0-9]+ZZ)\\]\\(")),
            [&sentinelSet](const QRegularExpressionMatch &m) {
                return sentinelSet.contains(m.captured(1))
                           ? QStringLiteral("![](") : m.captured(0);
            });
    }

#ifndef QT_NO_DEBUG
    assertNoPlaceholderLeak(md,
        {&placeholders, &brSoftPlaceholders, &blankLinePhs, &emptyAltSentinels});
#endif

    return md;
}
