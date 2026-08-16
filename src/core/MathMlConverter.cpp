#include "MathMlConverter.h"

#include <QXmlStreamReader>

namespace {

// 转义文本内容中的 XML 特殊字符
QString xmlEscape(const QString &s)
{
    QString out;
    out.reserve(s.size());
    for (QChar c : s) {
        switch (c.unicode()) {
        case '<':  out += "&lt;"; break;
        case '>':  out += "&gt;"; break;
        case '&':  out += "&amp;"; break;
        case '"':  out += "&quot;"; break;
        case '\'': out += "&apos;"; break;
        default:   out += c; break;
        }
    }
    return out;
}

// 递归：r 位于某个 MathML 元素的 StartElement，返回其 OMML。
QString convertElement(QXmlStreamReader &r);

// 跳过当前元素整个子树（r 位于 StartElement），用于 annotation 等无需转换的内容。
void skipElement(QXmlStreamReader &r)
{
    int depth = 0;
    while (!r.atEnd()) {
        auto t = r.readNext();
        if (t == QXmlStreamReader::StartElement) ++depth;
        else if (t == QXmlStreamReader::EndElement) {
            if (depth == 0) break; // 匹配的结束标签
            --depth;
        }
    }
}

// 读取下一个子元素并转换（跳过空白与非 StartElement token）；无子元素时返回空。
QString readChild(QXmlStreamReader &r)
{
    while (!r.atEnd()) {
        auto t = r.readNext();
        if (t == QXmlStreamReader::EndElement) return {};   // 当前元素已结束
        if (t == QXmlStreamReader::StartElement) return convertElement(r);
    }
    return {};
}

// 读取当前元素剩余部分直至其结束标签（用于消费固定数量子元素之后残留的结束标签）。
void readToEnd(QXmlStreamReader &r)
{
    while (!r.atEnd()) {
        if (r.readNext() == QXmlStreamReader::EndElement) break;
    }
}

// 透传包装元素（mrow/mstyle/semantics/math/mpadded/mphantom/merror 等）：
// 逐个转换子元素并拼接。
QString convertChildren(QXmlStreamReader &r)
{
    QString out;
    while (!r.atEnd()) {
        auto t = r.readNext();
        if (t == QXmlStreamReader::EndElement) break;
        if (t == QXmlStreamReader::StartElement) out += convertElement(r);
    }
    return out;
}

// 叶子元素（mi/mn/mo/mtext/ms）→ <m:r>[<m:rPr>]<m:t>text</m:t></m:r>。
// mi（变量）默认斜体，mathvariant="normal"（函数名等）正体；数字/运算符/文本正体。
QString leafElement(QXmlStreamReader &r, bool italic)
{
    QString text;
    while (!r.atEnd()) {
        auto t = r.readNext();
        if (t == QXmlStreamReader::Characters) text += r.text();
        else if (t == QXmlStreamReader::EndElement) break;
    }
    if (italic)
        return QStringLiteral("<m:r><m:rPr><m:sty m:val=\"i\"/></m:rPr><m:t>%1</m:t></m:r>")
            .arg(xmlEscape(text));
    return QStringLiteral("<m:r><m:t xml:space=\"preserve\">%1</m:t></m:r>").arg(xmlEscape(text));
}

// 分数 mfrac → <m:f><m:num>…</m:num><m:den>…</m:den></m:f>
QString fracElement(QXmlStreamReader &r)
{
    const QString num = readChild(r);
    const QString den = readChild(r);
    readToEnd(r);
    return QStringLiteral("<m:f><m:num>%1</m:num><m:den>%2</m:den></m:f>").arg(num, den);
}

// 平方根 msqrt → <m:rad><m:radPr><m:degHide m:val=\"1\"/></m:radPr><m:deg/><m:e>…</m:e></m:rad>
QString sqrtElement(QXmlStreamReader &r)
{
    const QString e = readChild(r);
    readToEnd(r);
    return QStringLiteral("<m:rad><m:radPr><m:degHide m:val=\"1\"/></m:radPr><m:deg/><m:e>%1</m:e></m:rad>")
        .arg(e);
}

// n 次根 mroot → <m:rad><m:deg>…</m:deg><m:e>…</m:e></m:rad>。
// MathML 子元素顺序为「基数 根指数」：首子为被开方数（e），次子为根指数（deg）。
QString rootElement(QXmlStreamReader &r)
{
    const QString base = readChild(r);
    const QString deg = readChild(r);
    readToEnd(r);
    return QStringLiteral("<m:rad><m:deg>%1</m:deg><m:e>%2</m:e></m:rad>").arg(deg, base);
}

// 上标 msup → <m:sSup><m:e>基</m:e><m:sup>上</m:sup></m:sSup>
QString supElement(QXmlStreamReader &r)
{
    const QString base = readChild(r);
    const QString sup = readChild(r);
    readToEnd(r);
    return QStringLiteral("<m:sSup><m:e>%1</m:e><m:sup>%2</m:sup></m:sSup>").arg(base, sup);
}

// 下标 msub → <m:sSub><m:e>基</m:e><m:sub>下</m:sub></m:sSub>
QString subElement(QXmlStreamReader &r)
{
    const QString base = readChild(r);
    const QString sub = readChild(r);
    readToEnd(r);
    return QStringLiteral("<m:sSub><m:e>%1</m:e><m:sub>%2</m:sub></m:sSub>").arg(base, sub);
}

// 上下标 msubsup → <m:sSubSup><m:e>基</m:e><m:sub>下</m:sub><m:sup>上</m:sup></m:sSubSup>
QString subSupElement(QXmlStreamReader &r)
{
    const QString base = readChild(r);
    const QString sub = readChild(r);
    const QString sup = readChild(r);
    readToEnd(r);
    return QStringLiteral("<m:sSubSup><m:e>%1</m:e><m:sub>%2</m:sub><m:sup>%3</m:sup></m:sSubSup>")
        .arg(base, sub, sup);
}

// 下置 munder → <m:limLow><m:e>基</m:e><m:lim>下</m:lim></m:limLow>
QString underElement(QXmlStreamReader &r)
{
    const QString base = readChild(r);
    const QString under = readChild(r);
    readToEnd(r);
    return QStringLiteral("<m:limLow><m:e>%1</m:e><m:lim>%2</m:lim></m:limLow>").arg(base, under);
}

// 上置 mover → <m:limUpp><m:e>基</m:e><m:lim>上</m:lim></m:limUpp>
QString overElement(QXmlStreamReader &r)
{
    const QString base = readChild(r);
    const QString over = readChild(r);
    readToEnd(r);
    return QStringLiteral("<m:limUpp><m:e>%1</m:e><m:lim>%2</m:lim></m:limUpp>").arg(base, over);
}

// 上下置 munderover → <m:limLow>+<m:limUpp> 叠套，下置在外、上置在内
QString underOverElement(QXmlStreamReader &r)
{
    const QString base = readChild(r);
    const QString under = readChild(r);
    const QString over = readChild(r);
    readToEnd(r);
    const QString upper =
        QStringLiteral("<m:limUpp><m:e>%1</m:e><m:lim>%2</m:lim></m:limUpp>").arg(base, over);
    return QStringLiteral("<m:limLow><m:e>%1</m:e><m:lim>%2</m:lim></m:limLow>").arg(upper, under);
}

// 定界符 mfenced → <m:d><m:dPr><m:begChr/><m:endChr/></m:dPr><m:e>子元素以分隔符连接</m:e></m:d>
QString fencedElement(QXmlStreamReader &r)
{
    const QString open = r.attributes().value("open").toString();
    const QString close = r.attributes().value("close").toString();
    const QString separators = r.attributes().value("separators").toString();

    QStringList parts;
    QString sep = separators.isEmpty() ? QStringLiteral(",") : separators.left(1);
    while (!r.atEnd()) {
        auto t = r.readNext();
        if (t == QXmlStreamReader::EndElement) break;
        if (t == QXmlStreamReader::StartElement) parts << convertElement(r);
    }
    QString dPr;
    if (!open.isEmpty())
        dPr += QStringLiteral("<m:begChr m:val=\"%1\"/>").arg(xmlEscape(open));
    if (!close.isEmpty())
        dPr += QStringLiteral("<m:endChr m:val=\"%1\"/>").arg(xmlEscape(close));
    return QStringLiteral("<m:d><m:dPr>%1</m:dPr><m:e>%2</m:e></m:d>")
        .arg(dPr, parts.join(sep));
}

// 是否为变量（mi）：mathvariant="normal" 表示函数名等正体，其余按变量斜体处理
bool isItalicIdentifier(QXmlStreamReader &r)
{
    return r.attributes().value("mathvariant").compare(QLatin1String("normal"), Qt::CaseInsensitive) != 0;
}

QString convertElement(QXmlStreamReader &r)
{
    const QString tag = r.name().toString().toLower();

    if (tag == "annotation" || tag == "annotation-xml") {
        skipElement(r);
        return {};
    }
    if (tag == "mi") return leafElement(r, isItalicIdentifier(r));
    if (tag == "mn" || tag == "mo" || tag == "mtext" || tag == "ms")
        return leafElement(r, false);
    if (tag == "mspace") { skipElement(r); return QStringLiteral("<m:r><m:t> </m:t></m:r>"); }

    if (tag == "mfrac") return fracElement(r);
    if (tag == "msqrt") return sqrtElement(r);
    if (tag == "mroot") return rootElement(r);
    if (tag == "msup") return supElement(r);
    if (tag == "msub") return subElement(r);
    if (tag == "msubsup") return subSupElement(r);
    if (tag == "munder") return underElement(r);
    if (tag == "mover") return overElement(r);
    if (tag == "munderover") return underOverElement(r);
    if (tag == "mfenced") return fencedElement(r);

    if (tag == "mtable")
        return QStringLiteral("<m:m>%1</m:m>").arg(convertChildren(r));
    if (tag == "mtr")
        return QStringLiteral("<m:mr>%1</m:mr>").arg(convertChildren(r));
    if (tag == "mtd")
        return QStringLiteral("<m:e>%1</m:e>").arg(convertChildren(r));

    // 默认透传：mrow / mstyle / semantics / math / mpadded / mphantom / merror 等
    return convertChildren(r);
}

} // namespace

QString MathMlConverter::toOMML(const QString &mathml)
{
    QXmlStreamReader r(mathml);
    // 定位到 <math> 根元素，转换其内容（semantics 透传、annotation 跳过）
    while (!r.atEnd()) {
        auto t = r.readNext();
        if (t == QXmlStreamReader::StartElement) return convertChildren(r);
        if (r.hasError()) return {};
    }
    return {};
}
