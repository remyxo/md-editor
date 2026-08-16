#include "DocxExporter.h"
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QXmlStreamReader>

DocxExporter::DocxExporter(QObject *parent)
    : QObject(parent)
{
}

void DocxExporter::exportToDocx(const QString &html, const QString &outputPath)
{
    if (html.isEmpty()) {
        emit exportError("导出失败：编辑器内容为空");
        return;
    }
    if (writeDocx(html, outputPath)) {
        emit exportFinished(true, outputPath);
    } else {
        emit exportError("DOCX 生成失败");
    }
}

// ============ 工具函数 ============

static QString xmlEscape(const QString &s)
{
    QString r = s;
    r.replace("&", "&amp;");
    r.replace("<", "&lt;");
    r.replace(">", "&gt;");
    r.replace("\"", "&quot;");
    return r;
}

static QString xmlSanitize(const QString &s)
{
    QString t = s;
    t.remove(QRegularExpression("[\\x00-\\x08\\x0b\\x0c\\x0e-\\x1f]"));
    return t;
}

// 给定 Unicode 码点是否属于 emoji 范围
static bool isEmojiCodepoint(uint cp)
{
    return (cp >= 0x1F600 && cp <= 0x1F64F)    // Emoticons
        || (cp >= 0x1F300 && cp <= 0x1F5FF)    // Misc Symbols & Pictographs
        || (cp >= 0x1F680 && cp <= 0x1F6FF)    // Transport & Map
        || (cp >= 0x1F900 && cp <= 0x1F9FF)    // Supplemental Symbols
        || (cp >= 0x1FA00 && cp <= 0x1FAFF)    // Symbols Extended-A
        || (cp >= 0x2600 && cp <= 0x27BF)      // Misc + Dingbats
        || (cp >= 0x2300 && cp <= 0x23FF)      // Miscellaneous Technical
        || (cp >= 0x2B50 && cp <= 0x2B55)      // Star/Other symbols
        || (cp >= 0x1F000 && cp <= 0x1F02F)    // Mahjong/Domino
        || (cp >= 0x2934 && cp <= 0x2935)      // Arrows
        || (cp >= 0x25AA && cp <= 0x25FE)      // Geometric shapes (■▲►▼…)
        || (cp >= 0x2B05 && cp <= 0x2B07)      // Arrows
        || (cp >= 0x2B1B && cp <= 0x2B1C)      // Squares
        || (cp >= 0x1F1E0 && cp <= 0x1F1FF)    // Flags (regional indicators)
        || (cp == 0x200D || cp == 0xFE0F)      // ZWJ / VS16 (emoji sequence parts)
        || (cp == 0x00A9 || cp == 0x00AE)      // © ®
        || (cp >= 0x2122 && cp <= 0x2139)      // ™ ℠ ℹ …
        || (cp >= 0x231A && cp <= 0x2328)      // ⌚⌛…
        || (cp >= 0x23CF && cp <= 0x23F3)      // ⏏⏰…
        || (cp == 0x24C2)                    // Ⓜ（单个 emoji 码点）
        || (cp >= 0x1F200 && cp <= 0x1F251); // 🈯🉐🉑…（Enclosed Ideographic Supplement）
}

// HTML→XML 预处理：关闭空元素、修复实体
static QString makeXmlSafe(const QString &html)
{
    QString s = html;
    s.replace("&nbsp;", "&#160;");
    // 关闭空元素：<br>（含属性变体如 <br data-brk="">）、<hr>
    s.replace(QRegularExpression("<br\\s*>", QRegularExpression::CaseInsensitiveOption), "<br/>");
    s.replace(QRegularExpression("<br(\\s+[^>]*[^/])>", QRegularExpression::CaseInsensitiveOption), "<br\\1/>");
    s.replace(QRegularExpression("<hr\\s*>", QRegularExpression::CaseInsensitiveOption), "<hr/>");
    // 关闭 <img ...> <input ...> <col ...>（不重复关闭已自闭合的）
    s.replace(QRegularExpression("<img(\\s+[^>]*[^/])>", QRegularExpression::CaseInsensitiveOption), "<img\\1/>");
    s.replace(QRegularExpression("<input(\\s+[^>]*[^/])>", QRegularExpression::CaseInsensitiveOption), "<input\\1/>");
    s.replace(QRegularExpression("<col(\\s+[^>]*[^/])>", QRegularExpression::CaseInsensitiveOption), "<col\\1/>");
    return s;
}

// 从 style 属性中提取颜色值并转为 RRGGBB 六位十六进制
static QString extractColor(const QString &style, const QString &prop)
{
    // (?:^|;\\s*) 确保匹配的属性名在开头或分号后，防止 color 误匹配 background-color 的子串
    QRegularExpression re("(?:^|;\\s*)" + prop + "\\s*:\\s*([^;]+)", QRegularExpression::CaseInsensitiveOption);
    auto m = re.match(style);
    if (!m.hasMatch()) return {};
    QString v = m.captured(1).trimmed();

    // rgb(R, G, B) 或 rgba(R, G, B, A)
    QRegularExpression rgbRe("rgba?\\(\\s*(\\d+)\\s*,\\s*(\\d+)\\s*,\\s*(\\d+)");
    auto rm = rgbRe.match(v);
    if (rm.hasMatch()) {
        return QString("%1%2%3")
            .arg(rm.captured(1).toInt(), 2, 16, QChar('0'))
            .arg(rm.captured(2).toInt(), 2, 16, QChar('0'))
            .arg(rm.captured(3).toInt(), 2, 16, QChar('0'))
            .toUpper();
    }
    // #RRGGBB
    if (v.startsWith('#'))
        return v.mid(1).toUpper();
    // 其他：直接去掉 # 和空格
    return v.remove(' ').remove('#').toUpper();
}

// 从 style 中提取 text-align 值
static QString extractAlign(const QString &style)
{
    QRegularExpression re("text-align\\s*:\\s*([^;]+)", QRegularExpression::CaseInsensitiveOption);
    auto m = re.match(style);
    if (!m.hasMatch()) return {};
    return m.captured(1).trimmed();
}

// ============ 数据模型 ============

struct RunFormat {
    bool bold = false, italic = false, underline = false, strike = false;
    bool subscript = false, superscript = false, highlight = false, monospace = false;
    QString textColor;   // RRGGBB
    QString bgColor;     // RRGGBB
    bool hyperlink = false;

    bool operator==(const RunFormat &o) const {
        return bold == o.bold && italic == o.italic && underline == o.underline
            && strike == o.strike && subscript == o.subscript && superscript == o.superscript
            && highlight == o.highlight && monospace == o.monospace
            && textColor == o.textColor && bgColor == o.bgColor && hyperlink == o.hyperlink;
    }
    bool operator!=(const RunFormat &o) const { return !(*this == o); }

    QString toPrXml() const {
        if (!bold && !italic && !underline && !strike && !subscript && !superscript
            && !highlight && !monospace && textColor.isEmpty() && bgColor.isEmpty()
            && !hyperlink)
            return {};
        QString p;
        if (bold) p += "<w:b/>";
        if (italic) p += "<w:i/>";
        if (underline) p += "<w:u w:val=\"single\"/>";
        if (strike) p += "<w:strike/>";
        if (subscript) p += "<w:vertAlign w:val=\"subscript\"/>";
        if (superscript) p += "<w:vertAlign w:val=\"superscript\"/>";
        if (highlight) p += "<w:highlight w:val=\"yellow\"/>";
        if (monospace) p += "<w:rFonts w:ascii=\"Consolas\" w:hAnsi=\"Consolas\" w:cs=\"Consolas\"/>";
        if (!textColor.isEmpty()) p += "<w:color w:val=\"" + textColor + "\"/>";
        if (!bgColor.isEmpty()) p += "<w:shd w:val=\"clear\" w:color=\"auto\" w:fill=\"" + bgColor + "\"/>";
        if (hyperlink) p += "<w:rStyle w:val=\"Hyperlink\"/><w:color w:val=\"0563C1\"/><w:u w:val=\"single\"/>";
        return "<w:rPr>" + p + "</w:rPr>";
    }
};

struct ParseState {
    QString bodyXml;
    QString relsXml;            // document.xml.rels entries
    int nextRId = 1;
    // 列表编号
    int bulletNumId = 1;
    int decimalNumId = 2;
    int nextAbstractNum = 1;
    QList<int> usedAbstractNums;
    bool hasBulletList = false;
    bool hasDecimalList = false;

    QString makeRId(const QString &url) {
        QString rid = QString("rId%1").arg(nextRId++);
        relsXml += QString("  <Relationship Id=\"%1\""
            " Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/hyperlink\""
            " Target=\"%2\" TargetMode=\"External\"/>\n").arg(rid, xmlEscape(url));
        return rid;
    }
};

// ============ 内联内容解析 ============

// 在 <w:p>/<w:tc> 等块容器内解析内联内容，返回 OOXML 游程序列
static QString parseInline(QXmlStreamReader &r, const QString &stopTag, ParseState &s)
{
    RunFormat fmt;
    QString textBuf;
    QString out;

    // 输出一个 run，并根据文本中是否有 emoji 决定是否自动切分
    auto emitRun = [&](const QString &txt, bool isEmoji) {
        if (txt.isEmpty()) return;
        QString pr;
        if (isEmoji) {
            // emoji run：继承当前格式但使用 emoji 字体
            pr = fmt.toPrXml();
            // emoji run：继承当前格式但使用 emoji 字体
            if (!pr.isEmpty()) {
                pr.chop(8); // 去掉 </w:rPr>
                pr += "<w:rFonts w:ascii=\"Noto Color Emoji\" w:hAnsi=\"Noto Color Emoji\" w:eastAsia=\"Noto Color Emoji\" w:cs=\"Noto Color Emoji\"/>";
                pr += "</w:rPr>";
            } else {
                pr = "<w:rPr><w:rFonts w:ascii=\"Noto Color Emoji\" w:hAnsi=\"Noto Color Emoji\" w:eastAsia=\"Noto Color Emoji\" w:cs=\"Noto Color Emoji\"/></w:rPr>";
            }
        } else {
            pr = fmt.toPrXml();
        }
        out += "<w:r>";
        if (!pr.isEmpty()) out += pr;
        out += "<w:t xml:space=\"preserve\">" + xmlEscape(xmlSanitize(txt)) + "</w:t>";
        out += "</w:r>";
    };

    auto flushRun = [&]() {
        if (textBuf.isEmpty()) return;
        // 扫描文本，emoji 字符自动切分为独立 run（使用 Noto Color Emoji 字体）
        QString seg;
        bool segIsEmoji = false;
        bool first = true;
        for (int i = 0; i < textBuf.length(); ) {
            uint cp = textBuf.at(i).unicode();
            int len = 1;
            if (QChar::isHighSurrogate(cp) && i + 1 < textBuf.length()) {
                uint lo = textBuf.at(i + 1).unicode();
                if (QChar::isLowSurrogate(lo)) {
                    cp = QChar::surrogateToUcs4(cp, lo);
                    len = 2;
                }
            }
            bool isEmoji = isEmojiCodepoint(cp);
            if (first) {
                segIsEmoji = isEmoji;
                first = false;
            }
            if (isEmoji != segIsEmoji) {
                emitRun(seg, segIsEmoji);
                seg.clear();
                segIsEmoji = isEmoji;
            }
            seg += textBuf.mid(i, len);
            i += len;
        }
        emitRun(seg, segIsEmoji);
        textBuf.clear();
    };

    // 检查是否是指定标签
    auto tagIs = [&](const char *name) {
        return r.name().toString().compare(QLatin1String(name), Qt::CaseInsensitive) == 0;
    };

    while (!r.atEnd()) {
        auto tt = r.readNext();
        if (tt == QXmlStreamReader::Characters) {
            textBuf += r.text();
        } else if (tt == QXmlStreamReader::StartElement) {
            QString tag = r.name().toString().toLower();

            // 内联格式标签
            if (tag == "strong" || tag == "b") {
                flushRun(); fmt.bold = true;
            } else if (tag == "em" || tag == "i") {
                flushRun(); fmt.italic = true;
            } else if (tag == "u") {
                flushRun(); fmt.underline = true;
            } else if (tag == "s" || tag == "del" || tag == "strike") {
                flushRun(); fmt.strike = true;
            } else if (tag == "code") {
                flushRun(); fmt.monospace = true;
            } else if (tag == "mark") {
                flushRun(); fmt.highlight = true;
            } else if (tag == "sub") {
                flushRun(); fmt.subscript = true;
            } else if (tag == "sup") {
                flushRun(); fmt.superscript = true;
            }
            // span（内联数学 / 颜色）
            else if (tag == "span") {
                // 内联数学 span 内容为空（KaTeX 渲染），须取 data-latex；
                // 此判断须先于颜色 span，否则 inline-math 被当普通 span 丢弃
                if (r.attributes().value("data-type").toString() == "inline-math") {
                    flushRun();
                    QString latex = r.attributes().value("data-latex").toString();
                    out += "<w:r><w:t>$" + xmlEscape(latex) + "$</w:t></w:r>";
                } else {
                    auto attrs = r.attributes();
                    QString sty = attrs.value("style").toString();
                    QString c = extractColor(sty, "color");
                    QString bg = extractColor(sty, "background-color");
                    if (!c.isEmpty() || !bg.isEmpty()) {
                        flushRun();
                        if (!c.isEmpty()) fmt.textColor = c;
                        if (!bg.isEmpty()) fmt.bgColor = bg;
                    }
                }
            }
            // 链接
            else if (tag == "a") {
                flushRun();
                QString href = r.attributes().value("href").toString();
                if (!href.isEmpty()) {
                    QString rid = s.makeRId(href);
                    out += "<w:hyperlink r:id=\"" + rid + "\" w:history=\"1\">";
                }
                fmt.hyperlink = true;
            }
            // 换行
            else if (tag == "br") {
                flushRun();
                out += "<w:r><w:br/></w:r>";
            }
            // 图片占位
            else if (tag == "img") {
                flushRun();
                QString alt = r.attributes().value("alt").toString();
                out += "<w:r><w:t>[图片" + (alt.isEmpty() ? "" : ": " + xmlEscape(alt)) + "]</w:t></w:r>";
            }
            // 空元素直接跳过
            else if (tag == "input" || tag == "col") {
                // 无操作
            }
            // 其他内联标签当作纯文本穿透
        } else if (tt == QXmlStreamReader::EndElement) {
            QString tag = r.name().toString().toLower();

            if (tag == "strong" || tag == "b") {
                flushRun(); fmt.bold = false;
            } else if (tag == "em" || tag == "i") {
                flushRun(); fmt.italic = false;
            } else if (tag == "u") {
                flushRun(); fmt.underline = false;
            } else if (tag == "s" || tag == "del" || tag == "strike") {
                flushRun(); fmt.strike = false;
            } else if (tag == "code") {
                flushRun(); fmt.monospace = false;
            } else if (tag == "mark") {
                flushRun(); fmt.highlight = false;
            } else if (tag == "sub") {
                flushRun(); fmt.subscript = false;
            } else if (tag == "sup") {
                flushRun(); fmt.superscript = false;
            } else if (tag == "span") {
                flushRun();
                fmt.textColor.clear();
                fmt.bgColor.clear();
            } else if (tag == "a") {
                flushRun();
                if (fmt.hyperlink) {
                    out += "</w:hyperlink>";
                }
                fmt.hyperlink = false;
            } else if (tag == stopTag) {
                flushRun();
                return out;
            }
        }
    }
    flushRun();
    return out;
}

// ============ 块级元素解析 ============

// 查找当前上下文应该停止的块标签
static QString stopTagFor(QXmlStreamReader &r)
{
    // 返回触发当前块结束的标签
    return {}; // 在 dispatch 层面处理
}

static QString parseParagraph(QXmlStreamReader &r, const QString &tag, ParseState &s)
{
    // 提取 text-align（style 属性 + HTML align 属性）
    QString sty = r.attributes().value("style").toString();
    QString align = extractAlign(sty);
    if (align.isEmpty())
        align = r.attributes().value("align").toString().trimmed();

    // 检查 data-type 属性（脚注、数学等非标准块）
    QString dataType = r.attributes().value("data-type").toString();
    if (dataType == "footnoteDef") {
        // 脚注定义：提取内容和编号
        QString id = r.attributes().value("data-id").toString();
        // 跳过到 </div>
        QString inner = parseInline(r, tag, s);
        return "<w:p><w:r><w:t>[^" + xmlEscape(id) + "]: " + xmlEscape(inner.isEmpty() ? "" : "...") + "</w:t></w:r></w:p>";
    }
    if (dataType == "block-math") {
        QString latex = r.attributes().value("data-latex").toString();
        return "<w:p><w:pPr><w:jc w:val=\"center\"/></w:pPr>"
               "<w:r><w:t>$$" + xmlEscape(latex) + "$$</w:t></w:r></w:p>";
    }
    if (dataType == "footnoteContainer") {
        QString inner = parseInline(r, tag, s);
        return inner.isEmpty() ? QString() : inner;
    }

    QString jc;
    if (align == "center") jc = "<w:jc w:val=\"center\"/>";
    else if (align == "right") jc = "<w:jc w:val=\"right\"/>";
    else if (align == "justify") jc = "<w:jc w:val=\"both\"/>";
    QString pPr = jc.isEmpty() ? QString() : "<w:pPr>" + jc + "</w:pPr>";
    QString inner = parseInline(r, tag, s);
    return "<w:p>" + pPr + inner + "</w:p>";
}

static QString parseHeading(QXmlStreamReader &r, int level, ParseState &s)
{
    QString sty = r.attributes().value("style").toString();
    QString align = extractAlign(sty);
    if (align.isEmpty())
        align = r.attributes().value("align").toString().trimmed();
    QString jc;
    if (align == "center") jc = "<w:jc w:val=\"center\"/>";
    else if (align == "right") jc = "<w:jc w:val=\"right\"/>";
    QString pPr = "<w:pPr><w:pStyle w:val=\"Heading" + QString::number(level) + "\"/>"
                  + jc + "</w:pPr>";
    QString inner = parseInline(r, QString("h%1").arg(level), s);
    return "<w:p>" + pPr + inner + "</w:p>";
}

static QString parseCodeBlock(QXmlStreamReader &r, ParseState &s)
{
    Q_UNUSED(s);
    // 收集 <pre> 内全部文本（逐行生成段落以保留换行）
    QString allText;
    while (!r.atEnd()) {
        auto tt = r.readNext();
        if (tt == QXmlStreamReader::EndElement
            && r.name().toString().compare(QLatin1String("pre"), Qt::CaseInsensitive) == 0)
            break;
        if (tt == QXmlStreamReader::Characters) {
            allText += r.text();
        }
        // 跳过 <code> 起止标签和其他内联标签
    }

    // 逐行生成 <w:p>，等宽字体 + 灰底
    QString pPr = "<w:pPr>"
                  "<w:shd w:val=\"clear\" w:color=\"auto\" w:fill=\"F2F2F2\"/>"
                  "<w:ind w:left=\"360\" w:right=\"360\"/>"
                  "<w:spacing w:before=\"0\" w:after=\"0\" w:line=\"240\" w:lineRule=\"auto\"/>"
                  "</w:pPr>";
    QString rPr = "<w:rPr><w:rFonts w:ascii=\"Consolas\" w:hAnsi=\"Consolas\" w:cs=\"Consolas\"/><w:sz w:val=\"18\"/></w:rPr>";

    QString result;
    const QStringList lines = allText.split('\n');
    for (const QString &line : lines) {
        QString t = xmlSanitize(line);
        if (t.isEmpty()) {
            // 空行仍需显式段落，否则 Word 会折叠
            result += "<w:p>" + pPr + "</w:p>\n";
        } else {
            result += "<w:p>" + pPr + "<w:r>" + rPr
                    + "<w:t xml:space=\"preserve\">" + xmlEscape(t) + "</w:t></w:r></w:p>\n";
        }
    }
    return result;
}

static QString parseBlockquote(QXmlStreamReader &r, ParseState &s)
{
    // <blockquote> 内部递归解析块元素
    QString result;
    while (!r.atEnd()) {
        auto tt = r.readNext();
        if (tt == QXmlStreamReader::EndElement
            && r.name().toString().compare(QLatin1String("blockquote"), Qt::CaseInsensitive) == 0)
            break;
        if (tt != QXmlStreamReader::StartElement) continue;
        QString tag = r.name().toString().toLower();
        // 递归解析内部块
        if (tag == "p" || tag == "div") {
            QString inner = parseParagraph(r, tag, s);
            // 缩进合并进已有 pPr（否则居中/对齐段落会产出两个 <w:pPr>，非法 OOXML）
            const QString indent = "<w:ind w:left=\"720\" w:right=\"720\"/>";
            if (inner.startsWith("<w:p>")) {
                int pPrStart = inner.indexOf("<w:pPr>");
                int pPrEnd = inner.indexOf("</w:pPr>");
                if (pPrStart >= 0 && pPrEnd > pPrStart) {
                    // 已有 pPr：缩进插到已有属性之前，保持单个 <w:pPr>
                    QString existing = inner.mid(pPrStart + 7, pPrEnd - pPrStart - 7);
                    inner = inner.left(pPrStart + 7) + indent + existing + inner.mid(pPrEnd);
                } else {
                    // 无 pPr：在 <w:p> 后插入
                    inner.replace("<w:p>", "<w:p><w:pPr>" + indent + "</w:pPr>");
                }
            }
            result += inner;
        } else if (tag == "h1") result += parseHeading(r, 1, s);
        else if (tag == "h2") result += parseHeading(r, 2, s);
        else if (tag == "h3") result += parseHeading(r, 3, s);
        else if (tag == "h4") result += parseHeading(r, 4, s);
        else if (tag == "h5") result += parseHeading(r, 5, s);
        else if (tag == "h6") result += parseHeading(r, 6, s);
        // 其他忽略
    }
    return result;
}

static QString parseHorizontalRule()
{
    return "<w:p><w:pPr>"
           "<w:pBdr><w:bottom w:val=\"single\" w:sz=\"6\" w:space=\"1\" w:color=\"auto\"/></w:pBdr>"
           "<w:spacing w:after=\"60\"/>"
           "</w:pPr></w:p>";
}

static QString parseList(QXmlStreamReader &r, bool ordered, ParseState &s)
{
    int numId = ordered ? s.decimalNumId : s.bulletNumId;
    if (ordered) s.hasDecimalList = true;
    else s.hasBulletList = true;
    s.usedAbstractNums.append(ordered ? 1 : 0);

    QString result;
    while (!r.atEnd()) {
        auto tt = r.readNext();
        if (tt == QXmlStreamReader::EndElement
            && (r.name().toString().compare(QLatin1String("ul"), Qt::CaseInsensitive) == 0
             || r.name().toString().compare(QLatin1String("ol"), Qt::CaseInsensitive) == 0))
            break;
        if (tt != QXmlStreamReader::StartElement) continue;
        QString tag = r.name().toString().toLower();

        if (tag == "li") {
            // 列表项：内部是块内容（p, div 等）
            QString taskPrefix; // 任务列表复选框前缀
            while (!r.atEnd()) {
                auto tt2 = r.readNext();
                if (tt2 == QXmlStreamReader::EndElement && r.name().toString().compare(QLatin1String("li"), Qt::CaseInsensitive) == 0)
                    break;
                if (tt2 != QXmlStreamReader::StartElement) continue;
                QString innerTag = r.name().toString().toLower();
                QString inner;
                if (innerTag == "p" || innerTag == "div") {
                    inner = parseParagraph(r, innerTag, s);
                } else if (innerTag == "label") {
                    // 提取 checkbox 状态
                    while (!r.atEnd()) {
                        auto lt = r.readNext();
                        if (lt == QXmlStreamReader::EndElement
                            && r.name().toString().compare(QLatin1String("label"), Qt::CaseInsensitive) == 0)
                            break;
                        if (lt == QXmlStreamReader::StartElement
                            && r.name().toString().compare(QLatin1String("input"), Qt::CaseInsensitive) == 0) {
                            QString c = r.attributes().value("checked").toString();
                            taskPrefix = c.isNull() ? QStringLiteral("☐ ") : QStringLiteral("☒ ");
                        }
                    }
                    continue;
                } else {
                    // skip
                    continue;
                }
                // 任务列表复选框前缀
                if (!taskPrefix.isEmpty() && inner.startsWith("<w:p>")) {
                    QString ckRun = "<w:r><w:t xml:space=\"preserve\">" + taskPrefix + "</w:t></w:r>";
                    int pPrEnd = inner.indexOf("</w:pPr>");
                    if (pPrEnd >= 0) {
                        inner = inner.left(pPrEnd + 8) + ckRun + inner.mid(pPrEnd + 8);
                    } else {
                        inner = QStringLiteral("<w:p>") + ckRun + inner.mid(4);
                    }
                    taskPrefix.clear();
                }
                // 包装为列表段落 — 与已有 pPr 合并
                QString listPPr = "<w:pStyle w:val=\"ListParagraph\"/>"
                                  "<w:numPr>"
                                  "<w:ilvl w:val=\"0\"/>"
                                  "<w:numId w:val=\"" + QString::number(numId) + "\"/>"
                                  "</w:numPr>";
                if (inner.startsWith("<w:p>")) {
                    int pPrStart = inner.indexOf("<w:pPr>");
                    int pPrEnd   = inner.indexOf("</w:pPr>");
                    if (pPrStart >= 0 && pPrEnd > pPrStart) {
                        // 已有 pPr：合并列表属性到已有 pPr 开头
                        QString existing = inner.mid(pPrStart + 7, pPrEnd - pPrStart - 7);
                        inner = inner.left(pPrStart + 7) + listPPr + existing + inner.mid(pPrEnd);
                    } else {
                        // 无 pPr：在 <w:p> 后插入
                        inner.replace("<w:p>", "<w:p><w:pPr>" + listPPr + "</w:pPr>");
                    }
                }
                result += inner;
            }
        }
    }
    return result;
}

static QString parseTable(QXmlStreamReader &r, ParseState &s)
{
    QString result = "<w:tbl>";
    result += "<w:tblPr>"
              "<w:tblStyle w:val=\"TableGrid\"/>"
              "<w:tblW w:w=\"0\" w:type=\"auto\"/>"
              "<w:tblBorders>"
              "<w:top w:val=\"single\" w:sz=\"4\" w:space=\"0\" w:color=\"auto\"/>"
              "<w:left w:val=\"single\" w:sz=\"4\" w:space=\"0\" w:color=\"auto\"/>"
              "<w:bottom w:val=\"single\" w:sz=\"4\" w:space=\"0\" w:color=\"auto\"/>"
              "<w:right w:val=\"single\" w:sz=\"4\" w:space=\"0\" w:color=\"auto\"/>"
              "<w:insideH w:val=\"single\" w:sz=\"4\" w:space=\"0\" w:color=\"auto\"/>"
              "<w:insideV w:val=\"single\" w:sz=\"4\" w:space=\"0\" w:color=\"auto\"/>"
              "</w:tblBorders>"
              "</w:tblPr>";

    // 收集 colgroup 信息用于 tblGrid
    QString tblGrid;
    int colCount = 0;
    bool firstRow = true;

    while (!r.atEnd()) {
        auto tt = r.readNext();
        if (tt == QXmlStreamReader::EndElement
            && r.name().toString().compare(QLatin1String("table"), Qt::CaseInsensitive) == 0)
            break;
        if (tt != QXmlStreamReader::StartElement) continue;
        QString tag = r.name().toString().toLower();

        if (tag == "colgroup") {
            // 解析 colgroup 生成 tblGrid
            while (!r.atEnd()) {
                auto ctt = r.readNext();
                if (ctt == QXmlStreamReader::EndElement
                    && r.name().toString().compare(QLatin1String("colgroup"), Qt::CaseInsensitive) == 0)
                    break;
                if (ctt == QXmlStreamReader::StartElement
                    && r.name().toString().compare(QLatin1String("col"), Qt::CaseInsensitive) == 0) {
                    QString w = r.attributes().value("style").toString();
                    QRegularExpression re("width:\\s*(\\d+)px");
                    auto m = re.match(w);
                    int dxa = 2340; // 默认宽
                    if (m.hasMatch()) {
                        // px → dxa (1px ≈ 15 dxa at 96dpi)
                        dxa = m.captured(1).toInt() * 15;
                    }
                    tblGrid += "<w:gridCol w:w=\"" + QString::number(dxa) + "\"/>";
                    colCount++;
                }
            }
        } else if (tag == "tbody") {
            continue; // 穿透 tbody
        } else if (tag == "tr") {
            bool isHeader = false;
            result += "<w:tr>";
            while (!r.atEnd()) {
                auto rtt = r.readNext();
                if (rtt == QXmlStreamReader::EndElement
                    && r.name().toString().compare(QLatin1String("tr"), Qt::CaseInsensitive) == 0)
                    break;
                if (rtt != QXmlStreamReader::StartElement) continue;
                QString cellTag = r.name().toString().toLower();

                if (cellTag == "th" || cellTag == "td") {
                    bool isTh = (cellTag == "th");
                    result += "<w:tc>";
                    // 单元格属性
                    result += "<w:tcPr>";
                    result += "<w:tcW w:w=\"2340\" w:type=\"dxa\"/>";
                    if (isTh) result += "<w:shd w:val=\"clear\" w:color=\"auto\" w:fill=\"D9E2F3\"/>";
                    // 检查对齐
                    QString cellSty = r.attributes().value("style").toString();
                    QString cellAlign = extractAlign(cellSty);
                    if (!cellAlign.isEmpty()) result += "<w:jc w:val=\"" + cellAlign + "\"/>";
                    result += "</w:tcPr>";

                    // 单元格内容（内联解析）
                    QString cellInner = parseInline(r, cellTag, s);
                    result += "<w:p>" + cellInner + "</w:p>";
                    result += "</w:tc>";
                }
            }
            result += "</w:tr>";
            firstRow = false;
        }
    }

    // 插入 tblGrid（如果有 colgroup，用之；否则用默认列宽）
    if (tblGrid.isEmpty()) {
        tblGrid = "<w:gridCol w:w=\"4680\"/><w:gridCol w:w=\"4680\"/>";
    }
    int trPos = result.indexOf("<w:tr>");
    if (trPos >= 0) {
        result = result.left(trPos) + "<w:tblGrid>" + tblGrid + "</w:tblGrid>" + result.mid(trPos);
    } else {
        result += "<w:tblGrid>" + tblGrid + "</w:tblGrid>";
    }
    return result + "</w:tbl>";
}

// ============ 主解析入口 ============

static void parseBody(QXmlStreamReader &r, ParseState &s)
{
    while (!r.atEnd()) {
        auto tt = r.readNext();
        if (tt != QXmlStreamReader::StartElement) continue;
        QString tag = r.name().toString().toLower();

        if (tag == "p") {
            s.bodyXml += parseParagraph(r, "p", s) + "\n";
        } else if (tag == "h1") {
            s.bodyXml += parseHeading(r, 1, s) + "\n";
        } else if (tag == "h2") {
            s.bodyXml += parseHeading(r, 2, s) + "\n";
        } else if (tag == "h3") {
            s.bodyXml += parseHeading(r, 3, s) + "\n";
        } else if (tag == "h4") {
            s.bodyXml += parseHeading(r, 4, s) + "\n";
        } else if (tag == "h5") {
            s.bodyXml += parseHeading(r, 5, s) + "\n";
        } else if (tag == "h6") {
            s.bodyXml += parseHeading(r, 6, s) + "\n";
        } else if (tag == "pre") {
            s.bodyXml += parseCodeBlock(r, s) + "\n";
        } else if (tag == "blockquote") {
            s.bodyXml += parseBlockquote(r, s) + "\n";
        } else if (tag == "hr") {
            s.bodyXml += parseHorizontalRule() + "\n";
        } else if (tag == "ul") {
            s.bodyXml += parseList(r, false, s) + "\n";
        } else if (tag == "ol") {
            s.bodyXml += parseList(r, true, s) + "\n";
        } else if (tag == "table") {
            s.bodyXml += parseTable(r, s) + "\n";
        } else if (tag == "div") {
            s.bodyXml += parseParagraph(r, "div", s) + "\n";
        } else if (tag == "img") {
            QString alt = r.attributes().value("alt").toString();
            s.bodyXml += "<w:p><w:r><w:t>[图片" + (alt.isEmpty() ? "" : ": " + xmlEscape(alt)) + "]</w:t></w:r></w:p>\n";
        }
        // mermaid、脚注容器等作为 div 处理
    }
}

// ============ OOXML 部件生成 ============

static QString makeStylesXml()
{
    return QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<w:styles xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\">\n"
        "  <w:docDefaults>"
        "<w:rPrDefault><w:rPr><w:sz w:val=\"22\"/><w:rFonts w:ascii=\"Calibri\" w:hAnsi=\"Calibri\" w:eastAsia=\"Calibri\" w:cs=\"Calibri\"/></w:rPr></w:rPrDefault>"
        "<w:pPrDefault><w:pPr><w:spacing w:line=\"276\" w:lineRule=\"auto\"/></w:pPr></w:pPrDefault>"
        "</w:docDefaults>\n"
        "  <w:style w:type=\"paragraph\" w:styleId=\"Normal\" w:default=\"1\"><w:name w:val=\"Normal\"/><w:rPr><w:sz w:val=\"22\"/></w:rPr></w:style>\n"
        "  <w:style w:type=\"paragraph\" w:styleId=\"Heading1\"><w:name w:val=\"heading 1\"/><w:basedOn w:val=\"Normal\"/><w:next w:val=\"Normal\"/><w:pPr><w:outlineLvl w:val=\"0\"/><w:spacing w:before=\"240\" w:after=\"120\"/></w:pPr><w:rPr><w:b/><w:sz w:val=\"32\"/></w:rPr></w:style>\n"
        "  <w:style w:type=\"paragraph\" w:styleId=\"Heading2\"><w:name w:val=\"heading 2\"/><w:basedOn w:val=\"Normal\"/><w:next w:val=\"Normal\"/><w:pPr><w:outlineLvl w:val=\"1\"/><w:spacing w:before=\"200\" w:after=\"100\"/></w:pPr><w:rPr><w:b/><w:sz w:val=\"28\"/></w:rPr></w:style>\n"
        "  <w:style w:type=\"paragraph\" w:styleId=\"Heading3\"><w:name w:val=\"heading 3\"/><w:basedOn w:val=\"Normal\"/><w:next w:val=\"Normal\"/><w:pPr><w:outlineLvl w:val=\"2\"/><w:spacing w:before=\"160\" w:after=\"80\"/></w:pPr><w:rPr><w:b/><w:sz w:val=\"26\"/></w:rPr></w:style>\n"
        "  <w:style w:type=\"paragraph\" w:styleId=\"Heading4\"><w:name w:val=\"heading 4\"/><w:basedOn w:val=\"Normal\"/><w:next w:val=\"Normal\"/><w:pPr><w:outlineLvl w:val=\"3\"/><w:spacing w:before=\"120\" w:after=\"60\"/></w:pPr><w:rPr><w:b/><w:sz w:val=\"24\"/></w:rPr></w:style>\n"
        "  <w:style w:type=\"paragraph\" w:styleId=\"Heading5\"><w:name w:val=\"heading 5\"/><w:basedOn w:val=\"Normal\"/><w:next w:val=\"Normal\"/><w:pPr><w:outlineLvl w:val=\"4\"/><w:spacing w:before=\"100\" w:after=\"40\"/></w:pPr><w:rPr><w:b/><w:sz w:val=\"22\"/></w:rPr></w:style>\n"
        "  <w:style w:type=\"paragraph\" w:styleId=\"Heading6\"><w:name w:val=\"heading 6\"/><w:basedOn w:val=\"Normal\"/><w:next w:val=\"Normal\"/><w:pPr><w:outlineLvl w:val=\"5\"/><w:spacing w:before=\"80\" w:after=\"40\"/></w:pPr><w:rPr><w:b/><w:sz w:val=\"21\"/></w:rPr></w:style>\n"
        "  <w:style w:type=\"paragraph\" w:styleId=\"ListParagraph\"><w:name w:val=\"List Paragraph\"/><w:basedOn w:val=\"Normal\"/><w:pPr><w:ind w:left=\"720\"/></w:pPr></w:style>\n"
        "  <w:style w:type=\"character\" w:styleId=\"Hyperlink\"><w:name w:val=\"Hyperlink\"/><w:basedOn w:val=\"DefaultParagraphFont\"/><w:rPr><w:color w:val=\"0563C1\"/><w:u w:val=\"single\"/></w:rPr></w:style>\n"
        "</w:styles>\n");
}

static QString makeNumberingXml(const ParseState &s)
{
    QString xml = QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<w:numbering xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\">\n");

    // 无序列表（bullet）
    if (s.hasBulletList) {
        xml += "  <w:abstractNum w:abstractNumId=\"0\">\n"
               "    <w:multiLevelType w:val=\"hybridMultilevel\"/>\n"
               "    <w:lvl w:ilvl=\"0\">\n"
               "      <w:start w:val=\"1\"/>\n"
               "      <w:numFmt w:val=\"bullet\"/>\n"
               "      <w:lvlText w:val=\"&#x2022;\"/>\n"
               "      <w:lvlJc w:val=\"left\"/>\n"
               "      <w:pPr><w:ind w:left=\"720\" w:hanging=\"360\"/></w:pPr>\n"
               "    </w:lvl>\n"
               "  </w:abstractNum>\n"
               "  <w:num w:numId=\"1\"><w:abstractNumId w:val=\"0\"/></w:num>\n";
    }
    // 有序列表（decimal）
    if (s.hasDecimalList) {
        xml += "  <w:abstractNum w:abstractNumId=\"1\">\n"
               "    <w:multiLevelType w:val=\"hybridMultilevel\"/>\n"
               "    <w:lvl w:ilvl=\"0\">\n"
               "      <w:start w:val=\"1\"/>\n"
               "      <w:numFmt w:val=\"decimal\"/>\n"
               "      <w:lvlText w:val=\"%1.\"/>\n"
               "      <w:lvlJc w:val=\"left\"/>\n"
               "      <w:pPr><w:ind w:left=\"720\" w:hanging=\"360\"/></w:pPr>\n"
               "    </w:lvl>\n"
               "  </w:abstractNum>\n"
               "  <w:num w:numId=\"2\"><w:abstractNumId w:val=\"1\"/></w:num>\n";
    }
    xml += "</w:numbering>\n";
    return xml;
}

static QByteArray buildDocumentXml(const ParseState &s)
{
    QByteArray doc = QByteArrayLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<w:document"
        " xmlns:wpc=\"http://schemas.microsoft.com/office/word/2010/wordprocessingCanvas\""
        " xmlns:mc=\"http://schemas.openxmlformats.org/markup-compatibility/2006\""
        " xmlns:o=\"urn:schemas-microsoft-com:office:office\""
        " xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\""
        " xmlns:m=\"http://schemas.openxmlformats.org/officeDocument/2006/math\""
        " xmlns:v=\"urn:schemas-microsoft-com:vml\""
        " xmlns:wp=\"http://schemas.openxmlformats.org/drawingml/2006/wordprocessingDrawing\""
        " xmlns:w10=\"urn:schemas-microsoft-com:office:word\""
        " xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\""
        " xmlns:wne=\"http://schemas.microsoft.com/office/word/2006/wordml\""
        ">\n"
        "<w:body>\n");
    if (s.bodyXml.isEmpty()) {
        doc += "<w:p><w:r><w:t></w:t></w:r></w:p>\n";
    } else {
        doc += s.bodyXml.toUtf8();
    }
    doc += QByteArrayLiteral("</w:body>\n</w:document>\n");
    return doc;
}

// ============ 文件写入 ============

bool DocxExporter::writeDocx(const QString &html, const QString &outputPath)
{
    // 1. 预处理 HTML → XML
    QString xmlSafe = makeXmlSafe(html);
    QString xmlDoc = "<?xml version=\"1.0\" encoding=\"UTF-8\"?><root>" + xmlSafe + "</root>";

    // 2. 解析 HTML 生成 OOXML
    ParseState state;
    QXmlStreamReader reader(xmlDoc);
    reader.readNextStartElement(); // <root>
    parseBody(reader, state);

    if (reader.hasError()) {
        qDebug() << "[DocxExport] XML parse error:" << reader.errorString();
    }

    // 解析完成

    // 3. 构建 OOXML 部件
    QByteArray docXml = buildDocumentXml(state);
    QByteArray stylesXml = makeStylesXml().toUtf8();
    QByteArray numberingXml = makeNumberingXml(state).toUtf8();

    // document.xml.rels — 必须声明 styles、numbering 等部件关系，否则 Word 无法加载样式
    QByteArray docRelsXml = QByteArrayLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">\n"
        "  <Relationship Id=\"rIdStyles\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles\" Target=\"styles.xml\"/>\n");
    if (state.hasBulletList || state.hasDecimalList) {
        docRelsXml += "  <Relationship Id=\"rIdNumbering\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/numbering\" Target=\"numbering.xml\"/>\n";
    }
    if (!state.relsXml.isEmpty()) {
        docRelsXml += state.relsXml.toUtf8();
    }
    docRelsXml += QByteArrayLiteral("</Relationships>\n");

    // [Content_Types].xml — 声明所有部件
    QByteArray contentTypes = QByteArrayLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">\n"
        "  <Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>\n"
        "  <Default Extension=\"xml\" ContentType=\"application/xml\"/>\n"
        "  <Override PartName=\"/word/document.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml\"/>\n"
        "  <Override PartName=\"/word/styles.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.styles+xml\"/>\n");
    if (state.hasBulletList || state.hasDecimalList) {
        contentTypes += "  <Override PartName=\"/word/numbering.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.numbering+xml\"/>\n";
    }
    contentTypes += QByteArrayLiteral("</Types>\n");

    // 4. 写入临时目录
    QTemporaryDir tmpDir;
    if (!tmpDir.isValid()) {
        qDebug() << "[DocxExport] Cannot create temp dir";
        return false;
    }
    const QString base = tmpDir.path();

    auto writeFile = [](const QString &path, const QByteArray &data) -> bool {
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly)) {
            qDebug() << "[DocxExport] Cannot write:" << path;
            return false;
        }
        f.write(data);
        f.close();
        return true;
    };

    // mimetype
    if (!writeFile(base + "/mimetype",
        QByteArrayLiteral("application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml")))
        return false;

    // [Content_Types].xml
    if (!writeFile(base + "/[Content_Types].xml", contentTypes)) return false;

    // _rels/.rels
    QDir().mkpath(base + "/_rels");
    if (!writeFile(base + "/_rels/.rels",
        QByteArrayLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">\n"
        "  <Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"word/document.xml\"/>\n"
        "</Relationships>\n")))
        return false;

    // word/_rels/document.xml.rels
    QDir().mkpath(base + "/word/_rels");
    if (!writeFile(base + "/word/_rels/document.xml.rels", docRelsXml)) return false;

    // word/document.xml
    QDir().mkpath(base + "/word");
    if (!writeFile(base + "/word/document.xml", docXml)) return false;

    // word/styles.xml
    if (!writeFile(base + "/word/styles.xml", stylesXml)) return false;

    // word/numbering.xml（仅当有列表时）
    if (state.hasBulletList || state.hasDecimalList) {
        if (!writeFile(base + "/word/numbering.xml", numberingXml)) return false;
    }

    // 5. ZIP 打包
    QFile::remove(outputPath);

    QProcess zip1;
    zip1.setWorkingDirectory(base);
    zip1.start("zip", QStringList() << "-0" << outputPath << "mimetype");
    if (!zip1.waitForFinished(10000)) {
        qDebug() << "[DocxExport] zip step 1 timeout";
        return false;
    }
    if (zip1.exitCode() != 0) {
        qDebug() << "[DocxExport] zip step 1 failed:" << zip1.readAllStandardError();
        return false;
    }

    QProcess zip2;
    zip2.setWorkingDirectory(base);
    zip2.start("zip", QStringList() << "-r" << outputPath
               << "[Content_Types].xml" << "_rels" << "word");
    if (!zip2.waitForFinished(15000)) {
        qDebug() << "[DocxExport] zip step 2 timeout";
        return false;
    }
    if (zip2.exitCode() != 0) {
        qDebug() << "[DocxExport] zip step 2 failed:" << zip2.readAllStandardError();
        return false;
    }

    QFileInfo fi(outputPath);
    if (!fi.exists() || fi.size() == 0) {
        qDebug() << "[DocxExport] Output file missing or empty";
        return false;
    }
    return true;
}
