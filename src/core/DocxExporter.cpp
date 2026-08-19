#include "DocxExporter.h"
#include "MathMlConverter.h"
#include "miniz/miniz.h"

#include <QBuffer>
#include <cstring>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QMimeDatabase>
#include <QPageLayout>
#include <QPageSize>
#include <QRegularExpression>
#include <QSet>
#include <QSettings>
#include <QXmlStreamReader>

// 前向声明：实现位于文件后半部（mermaidBlockRe 附近），onMermaidRendered 先于它被定义
static QString substituteMermaidPlaceholders(const QString &html);
static QString substituteMathPlaceholders(const QString &html);

DocxExporter::DocxExporter(QObject *parent)
    : QObject(parent)
{
}

void DocxExporter::exportToDocx(const QString &html, const QString &docDir, const QString &outputPath)
{
    if (html.isEmpty()) {
        emit exportError("导出失败：编辑器内容为空");
        return;
    }
    m_docDir = docDir;
    m_outputPath = outputPath;
    m_lastHtml = html;
    m_mermaidSources = extractMermaidSources(html);
    m_mathSources = extractMathSources(html);

    // 无 mermaid 且无公式：同步直接写入
    if (m_mermaidSources.isEmpty() && m_mathSources.isEmpty()) {
        if (writeDocx(html, docDir, outputPath)) {
            emit exportFinished(true, outputPath);
        } else {
            emit exportError("DOCX 生成失败");
        }
        return;
    }

    // 有 mermaid / 公式：分别离屏渲染，全部完成后替换占位再写 DOCX（异步）
    m_mermaidPending = !m_mermaidSources.isEmpty();
    m_mathPending = !m_mathSources.isEmpty();
    if (m_mermaidPending) {
        if (!m_renderer) {
            m_renderer = new MermaidRenderer(this);
            connect(m_renderer, &MermaidRenderer::finished, this, &DocxExporter::onMermaidRendered);
        }
        m_renderer->renderAll(m_mermaidSources);
    }
    if (m_mathPending) {
        if (!m_mathRenderer) {
            m_mathRenderer = new MathMlRenderer(this);
            connect(m_mathRenderer, &MathMlRenderer::finished, this, &DocxExporter::onMathRendered);
        }
        m_mathRenderer->renderAll(m_mathSources);
    }
}

void DocxExporter::onMermaidRendered(const QVector<MermaidRenderer::Result> &results)
{
    m_mermaidResults = results;
    m_mermaidPending = false;
    maybeFinish();
}

void DocxExporter::onMathRendered(const QVector<QString> &mathmlList)
{
    // MathML → OMML 转换；渲染/转换失败的项留空，解析时回退为 $...$ 源码文本
    m_mathOMML.clear();
    m_mathOMML.reserve(mathmlList.size());
    for (const QString &ml : mathmlList) {
        if (ml.isEmpty()) m_mathOMML.append(QString());
        else m_mathOMML.append(MathMlConverter::toOMML(ml));
    }
    m_mathPending = false;
    maybeFinish();
}

void DocxExporter::maybeFinish()
{
    if (m_mermaidPending || m_mathPending) return;
    QString html = substituteMermaidPlaceholders(m_lastHtml);   // mermaid 块 → <p data-mermaid-idx>
    html = substituteMathPlaceholders(html);                    // 公式 → <span/p data-math-idx>
    if (writeDocx(html, m_docDir, m_outputPath)) {
        emit exportFinished(true, m_outputPath);
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

// mermaid 块正则：<pre><code class="...language-mermaid..."> 源码。extractMermaidSources 与
// substituteMermaidPlaceholders 必须共用同一正则，保证占位序号与源码列表序号对齐。
static const QRegularExpression &mermaidBlockRe()
{
    static const QRegularExpression re(
        "<pre[^>]*><code[^>]*class=\"[^\"]*language-mermaid[^\"]*\"[^>]*>([\\s\\S]*?)</code></pre>",
        QRegularExpression::CaseInsensitiveOption);
    return re;
}

static QString htmlUnescape(QString s)   // mermaid 源码在 getContent() 中被 HTML 转义
{
    return s.replace("&lt;", "<").replace("&gt;", ">").replace("&amp;", "&").replace("&quot;", "\"");
}

QStringList DocxExporter::extractMermaidSources(const QString &html)
{
    QStringList out;
    auto it = mermaidBlockRe().globalMatch(html);
    while (it.hasNext()) { auto m = it.next(); out << htmlUnescape(m.captured(1)); }
    return out;
}

// 每个 mermaid 块替换为 <p data-mermaid-idx="N"></p> 占位（N 与 extractMermaidSources 顺序一致）
static QString substituteMermaidPlaceholders(const QString &html)
{
    QString out = html; int offset = 0, idx = 0;
    auto it = mermaidBlockRe().globalMatch(html);
    while (it.hasNext()) {
        auto m = it.next();
        QString ph = QString("<p data-mermaid-idx=\"%1\"></p>").arg(idx++);
        out.replace(m.capturedStart(0) + offset, m.capturedLength(0), ph);
        offset += ph.length() - m.capturedLength(0);
    }
    return out;
}

// 公式占位正则：捕获组 1=行内 <span data-type="inline-math">，组 2=块级 <div data-type="block-math">。
// extractMathSources 与 substituteMathPlaceholders 必须共用同一正则，保证占位序号与源码列表序号对齐。
// data-latex 值经 HTML 转义（&lt; 等），提取后由 htmlUnescape 还原；属性顺序不假定（renderHTML
// 中 data-type 与 data-latex 的先后可能随序列化器变化），故按 data-type 定位整元素、再单独取 data-latex。
static const QRegularExpression &mathRe()
{
    static const QRegularExpression re(
        "(<span\\b[^>]*\\bdata-type=\"inline-math\"[^>]*>(?:\\s*</span>)?)"
        "|(<div\\b[^>]*\\bdata-type=\"block-math\"[^>]*>(?:\\s*</div>)?)",
        QRegularExpression::CaseInsensitiveOption);
    return re;
}

// 从匹配到的完整元素中提取 data-latex 属性值
static QString extractLatexAttr(const QString &elementHtml)
{
    QRegularExpression re("\\bdata-latex=\"([^\"]*)\"", QRegularExpression::CaseInsensitiveOption);
    auto m = re.match(elementHtml);
    return m.hasMatch() ? m.captured(1) : QString();
}

QStringList DocxExporter::extractMathSources(const QString &html)
{
    QStringList out;
    auto it = mathRe().globalMatch(html);
    while (it.hasNext()) { auto m = it.next(); out << htmlUnescape(extractLatexAttr(m.captured(0))); }
    return out;
}

// 每个公式替换为占位：行内 <span data-math-idx="N"></span>，块级 <p data-math-idx="N"></p>
// （N 与 extractMathSources 顺序一致）
static QString substituteMathPlaceholders(const QString &html)
{
    QString out = html; int offset = 0, idx = 0;
    auto it = mathRe().globalMatch(html);
    while (it.hasNext()) {
        auto m = it.next();
        const bool isBlock = m.captured(1).isEmpty();   // 组1=行内，组2=块级
        QString ph = isBlock
            ? QString("<p data-math-idx=\"%1\"></p>").arg(idx)
            : QString("<span data-math-idx=\"%1\"></span>").arg(idx);
        out.replace(m.capturedStart(0) + offset, m.capturedLength(0), ph);
        offset += ph.length() - m.capturedLength(0);
        ++idx;
    }
    return out;
}

// 解析图片路径 → 字节 + MIME + 尺寸（EMU）。失败返回 false。
static bool readImageFile(const QString &src, const QString &docDir,
                          QByteArray *bytes, QString *mime, int *cxEmu, int *cyEmu)
{
    if (src.startsWith("data:")) {
        // data URI：解析 base64 字节（mime 不信任 data: 前缀，交由内容判定）
        int comma = src.indexOf(',');
        if (comma < 0) return false;
        QString header = src.left(comma);
        QRegularExpression mre("data:([^;]+);base64");
        auto mm = mre.match(header);
        if (!mm.hasMatch()) return false;
        *bytes = QByteArray::fromBase64(src.mid(comma + 1).toLatin1());
    } else {
        QString path = QDir::isAbsolutePath(src) ? src : QDir(docDir).filePath(src);
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) return false;
        *bytes = f.readAll();
        f.close();
    }
    // 非空校验：零字节文件 / 空 base64 不得嵌入为损坏图片
    if (bytes->isEmpty()) return false;
    // 基于内容判定真实 MIME（不信任扩展名 / data: 前缀），仅接受三种 Word 可直接嵌入的格式；
    // svg/webp/bmp/tiff 等其他 image/* 一律回退 [图片] 占位，避免嵌入声明 image/jpeg 的损坏 jpg
    QMimeDatabase db;
    const QString realMime = db.mimeTypeForData(*bytes).name();
    if (realMime != "image/png" && realMime != "image/jpeg" && realMime != "image/gif")
        return false;
    *mime = realMime;
    // 尺寸：PNG/JPEG 用 QImageReader；SVG 不读尺寸用默认
    QImageReader ir;
    QBuffer buf(bytes);
    buf.open(QIODevice::ReadOnly);
    ir.setDevice(&buf);
    QSize sz = ir.size();
    if (sz.isValid() && sz.width() > 0 && sz.height() > 0) {
        *cxEmu = sz.width() * 9525;
        *cyEmu = sz.height() * 9525;
    } else {
        *cxEmu = 300 * 9525;   // 默认 300×200 px
        *cyEmu = 200 * 9525;
    }
    return true;
}

// 图片超宽时按比例缩小到页面文本宽度（长边对齐 maxWidthEmu，保持宽高比）。
// maxWidthEmu <= 0 视为不限制。使用 qint64 防 cyEmu*maxWidthEmu 溢出（约 240e12，仍在 int64 范围）。
static void clampToMaxWidth(int &cxEmu, int &cyEmu, int maxWidthEmu)
{
    if (maxWidthEmu <= 0 || cxEmu <= maxWidthEmu) return;
    cyEmu = static_cast<int>((static_cast<qint64>(cyEmu) * maxWidthEmu) / cxEmu);
    cxEmu = maxWidthEmu;
}

// 生成 <w:drawing> 内联图片 XML。仅嵌入 PNG（mermaid 的 SVG 依赖 <style> 与 <marker>，
// Word/WPS 的 SVG 渲染器不支持，会丢箭头，故一律用 Chromium 栅格化好的 PNG，不挂 svgBlip）。
static QString buildImageDrawingXml(int docPrId, int pngRId,
                                    int cxEmu, int cyEmu)
{
    const QString blip = QString("<a:blip r:embed=\"rId%1\"/>").arg(pngRId);
    return QString(
        "<w:drawing><wp:inline distT=\"0\" distB=\"0\" distL=\"0\" distR=\"0\">"
        "<wp:extent cx=\"%1\" cy=\"%2\"/><wp:effectExtent l=\"0\" t=\"0\" r=\"0\" b=\"0\"/>"
        "<wp:docPr id=\"%3\" name=\"图片%3\"/>"
        "<wp:cNvGraphicFramePr><a:graphicFrameLocks noChangeAspect=\"1\"/></wp:cNvGraphicFramePr>"
        "<a:graphic><a:graphicData uri=\"http://schemas.openxmlformats.org/drawingml/2006/picture\">"
        "<pic:pic><pic:nvPicPr><pic:cNvPr id=\"%3\" name=\"图片%3\"/><pic:cNvPicPr/></pic:nvPicPr>"
        "<pic:blipFill>%4<a:stretch><a:fillRect/></a:stretch></pic:blipFill>"
        "<pic:spPr><a:xfrm><a:off x=\"0\" y=\"0\"/><a:ext cx=\"%1\" cy=\"%2\"/></a:xfrm>"
        "<a:prstGeom prst=\"rect\"><a:avLst/></a:prstGeom></pic:spPr></pic:pic>"
        "</a:graphicData></a:graphic></wp:inline></w:drawing>")
        .arg(cxEmu).arg(cyEmu).arg(docPrId).arg(blip);
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
    // 列表编号：每个 <ul>/<ol> 分配独立 numId（指向共享的 abstractNum：0=bullet、1=decimal），
    // 使 Word 对每个列表各自重新开始编号；否则同一 numId 的段落会被当作同一序列续编。
    int nextNumId = 1;
    QList<int> numAbstractNums;   // 与 numId 一一对应（numId = 下标 + 1），值为 abstractNumId
    bool hasBulletList = false;
    bool hasDecimalList = false;
    // 图片 media 部件（Task 5：DOCX 图片嵌入 OOXML media）
    struct MediaPart {
        int rId;
        QString fileName;     // "imageN.png" / "imageN.gif" / "imageN.jpg"
        QString contentType;  // "image/png" 等
        QByteArray bytes;
    };
    QList<MediaPart> media;
    int nextDocPrId = 1;
    QString docDir;                                       // 文档所在目录，用于解析相对图片路径
    int maxImageWidthEmu = 0;                             // 图片最大宽（EMU），0=不限制；超宽按比例缩小
    QString sectPrXml;                                    // 节属性（纸张尺寸+页边距），使 DOCX 页与导出设置一致
    QVector<MermaidRenderer::Result> mermaidResults;      // 渲染结果（按 idx）
    QStringList mermaidSources;                           // 源码（渲染失败回退用）
    QStringList mathSources;                              // 公式 LaTeX 源码（按 idx，失败回退用）
    QStringList mathOMML;                                 // 公式 OMML 片段（按 idx，失败项空）

    QString makeRId(const QString &url) {
        QString rid = QString("rId%1").arg(nextRId++);
        relsXml += QString("  <Relationship Id=\"%1\""
            " Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/hyperlink\""
            " Target=\"%2\" TargetMode=\"External\"/>\n").arg(rid, xmlEscape(url));
        return rid;
    }
};

// 新增图片 media 部件，返回 rId
static int addMedia(ParseState &s, const QByteArray &bytes, const QString &contentType,
                    const QString &ext)
{
    int rId = s.nextRId++;
    QString fileName = QString("image%1.%2").arg(s.media.size() + 1).arg(ext);
    s.media.append({rId, fileName, contentType, bytes});
    s.relsXml += QString("  <Relationship Id=\"rId%1\""
        " Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/image\""
        " Target=\"media/%2\"/>\n").arg(rId).arg(fileName);
    return rId;
}

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
                // 内联公式占位：替换为 <m:oMath> 原生公式（OMML）；渲染失败回退 $...$
                if (r.attributes().hasAttribute("data-math-idx")) {
                    flushRun();
                    int idx = r.attributes().value("data-math-idx").toInt();
                    QString latex = (idx >= 0 && idx < s.mathSources.size()) ? s.mathSources[idx] : QString();
                    QString omm = (idx >= 0 && idx < s.mathOMML.size()) ? s.mathOMML[idx] : QString();
                    if (!omm.isEmpty()) out += "<m:oMath>" + omm + "</m:oMath>";
                    else out += "<w:r><w:t>$" + xmlEscape(latex) + "$</w:t></w:r>";
                }
                // 内联数学 span 内容为空（KaTeX 渲染），须取 data-latex；
                // 此判断须先于颜色 span，否则 inline-math 被当普通 span 丢弃。
                // 占位替换正常运行时此分支不可达，保留作兜底（替换漏匹配时仍输出 $...$）。
                else if (r.attributes().value("data-type").toString() == "inline-math") {
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
            // 图片：嵌入 OOXML media；文件缺失时降级为占位文本
            else if (tag == "img") {
                flushRun();
                QString alt = r.attributes().value("alt").toString();
                QByteArray bytes; QString mime; int cx = 0, cy = 0;
                if (readImageFile(r.attributes().value("src").toString(), s.docDir, &bytes, &mime, &cx, &cy)) {
                    clampToMaxWidth(cx, cy, s.maxImageWidthEmu);
                    QString ext = (mime == "image/png") ? "png" : (mime == "image/gif" ? "gif" : "jpg");
                    int rId = addMedia(s, bytes, mime, ext);
                    out += "<w:r>" + buildImageDrawingXml(s.nextDocPrId++, rId, cx, cy) + "</w:r>";
                } else {
                    out += "<w:r><w:t>[图片" + (alt.isEmpty() ? "" : ": " + xmlEscape(alt)) + "]</w:t></w:r>";
                }
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
    QString rPr = "<w:rPr><w:rFonts w:ascii=\"Consolas\" w:hAnsi=\"Consolas\" w:cs=\"Consolas\"/><w:sz w:val=\"22\"/></w:rPr>";

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
            // 缩进 + 左侧蓝色竖线合并进已有 pPr（否则居中/对齐段落会产出两个 <w:pPr>，非法 OOXML）。
            // 左侧竖线对齐 PDF/HTML 的 border-left，颜色 #0078d4 与 PdfExporter 一致。
            const QString blockquotePr = "<w:pBdr><w:left w:val=\"single\" w:sz=\"18\" w:space=\"4\" w:color=\"0078d4\"/></w:pBdr>"
                                         "<w:ind w:left=\"720\" w:right=\"720\"/>";
            if (inner.startsWith("<w:p>")) {
                int pPrStart = inner.indexOf("<w:pPr>");
                int pPrEnd = inner.indexOf("</w:pPr>");
                if (pPrStart >= 0 && pPrEnd > pPrStart) {
                    // 已有 pPr：边框+缩进插到已有属性之前，保持单个 <w:pPr>
                    QString existing = inner.mid(pPrStart + 7, pPrEnd - pPrStart - 7);
                    inner = inner.left(pPrStart + 7) + blockquotePr + existing + inner.mid(pPrEnd);
                } else {
                    // 无 pPr：在 <w:p> 后插入
                    inner.replace("<w:p>", "<w:p><w:pPr>" + blockquotePr + "</w:pPr>");
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
    int numId = s.nextNumId++;
    s.numAbstractNums.append(ordered ? 1 : 0);
    if (ordered) s.hasDecimalList = true;
    else s.hasBulletList = true;

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
                            // ☐/☒（U+2610/U+2612）为 GFM 任务列表的方框/方框带叉符号。
                            // 文档默认字体（西文 Arial/中文黑体）不含这两个码位字形，须在复选框 run 上
                            // 显式指定含该字形的符号字体（见下方 ckRun 的 w:rFonts）。
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
                    // 复选框 run 显式指定符号字体：Arial/黑体 无 U+2610/U+2612 字形，
                    // 缺省回退在部分 Word/WPS 环境不可靠，☐/☒ 会渲染成空白。
                    QString ckRun = "<w:r><w:rPr><w:rFonts w:ascii=\"Segoe UI Symbol\" w:hAnsi=\"Segoe UI Symbol\" w:cs=\"Segoe UI Symbol\"/></w:rPr><w:t xml:space=\"preserve\">" + taskPrefix + "</w:t></w:r>";
                    int pPrEnd = inner.indexOf("</w:pPr>");
                    if (pPrEnd >= 0) {
                        inner = inner.left(pPrEnd + 8) + ckRun + inner.mid(pPrEnd + 8);
                    } else {
                        // <w:p> 共 5 个字符，mid(5) 才跳过整个开标签；mid(4) 会残留一个
                        // 孤立的 ">" 文本节点（<w:p> 内不允许裸文本），Word 打开时可能
                        // 丢弃该段或触发修复，导致任务列表复选框丢失、退化为普通列表。
                        inner = QStringLiteral("<w:p>") + ckRun + inner.mid(5);
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
            // mermaid 占位：<p data-mermaid-idx="N"></p>，替换为渲染图（或源码回退）。
            // 守卫必须 key off mermaidSources（由 extractMermaidSources 从原始 HTML 提取，
            // 始终完整）；mermaidResults 在渲染失败时可能为空/缺项（如 loadFinished !ok
            // 发出空向量），若 key off 它则占位被静默丢弃，mermaid 块从 DOCX 丢失。
            if (r.attributes().hasAttribute("data-mermaid-idx")) {
                int idx = r.attributes().value("data-mermaid-idx").toInt();
                while (!r.atEnd()) {                     // 消费空 <p> 到 </p>
                    auto t = r.readNext();
                    if (t == QXmlStreamReader::EndElement
                        && r.name().toString().compare(QLatin1String("p"), Qt::CaseInsensitive) == 0)
                        break;
                }
                if (idx >= 0 && idx < s.mermaidSources.size()) {
                    if (idx < s.mermaidResults.size()) {
                        const auto &res = s.mermaidResults[idx];
                        if (!res.png.isEmpty()) {
                            int cx = res.cxEmu, cy = res.cyEmu;
                            clampToMaxWidth(cx, cy, s.maxImageWidthEmu);
                            // 仅嵌入 PNG，不挂 <asvg:svgBlip>。mermaid 的时序图箭头依赖
                            // <style> CSS 类（.messageLineN{stroke:#333}）与 <marker>（marker-end
                            // 箭头），二者 Word/WPS 的 SVG 渲染器均不支持（忽略 <style>、
                            // 不渲染 <marker>），会导致箭头丢失；而 Chromium 栅格化的 PNG
                            // 已正确应用 CSS 与 marker，箭头完整。故放弃矢量 SVG，用 PNG 兜底。
                            int pngRId = addMedia(s, res.png, "image/png", "png");
                            s.bodyXml += "<w:p><w:r>" + buildImageDrawingXml(s.nextDocPrId++, pngRId, cx, cy) + "</w:r></w:p>\n";
                            continue;
                        }
                    }
                    // 渲染失败或结果缺失：回退源码文本
                    s.bodyXml += "<w:p><w:r><w:t xml:space=\"preserve\">" + xmlEscape(s.mermaidSources[idx]) + "</w:t></w:r></w:p>\n";
                }
                continue;
            }
            // 公式块占位：<p data-math-idx="N"></p>，替换为居中 <m:oMathPara> 原生公式
            // （渲染失败回退 $$...$$ 源码文本）。
            if (r.attributes().hasAttribute("data-math-idx")) {
                int idx = r.attributes().value("data-math-idx").toInt();
                while (!r.atEnd()) {                     // 消费空 <p> 到 </p>
                    auto t = r.readNext();
                    if (t == QXmlStreamReader::EndElement
                        && r.name().toString().compare(QLatin1String("p"), Qt::CaseInsensitive) == 0)
                        break;
                }
                QString latex = (idx >= 0 && idx < s.mathSources.size()) ? s.mathSources[idx] : QString();
                QString omm = (idx >= 0 && idx < s.mathOMML.size()) ? s.mathOMML[idx] : QString();
                if (!omm.isEmpty()) {
                    s.bodyXml += "<w:p><w:pPr><w:jc w:val=\"center\"/></w:pPr>"
                                 "<m:oMathPara><m:oMath>" + omm + "</m:oMath></m:oMathPara></w:p>\n";
                } else {
                    s.bodyXml += "<w:p><w:pPr><w:jc w:val=\"center\"/></w:pPr>"
                                 "<w:r><w:t>$$" + xmlEscape(latex) + "$$</w:t></w:r></w:p>\n";
                }
                continue;
            }
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
            QByteArray bytes; QString mime; int cx = 0, cy = 0;
            if (readImageFile(r.attributes().value("src").toString(), s.docDir, &bytes, &mime, &cx, &cy)) {
                clampToMaxWidth(cx, cy, s.maxImageWidthEmu);
                QString ext = (mime == "image/png") ? "png" : (mime == "image/gif" ? "gif" : "jpg");
                int rId = addMedia(s, bytes, mime, ext);
                s.bodyXml += "<w:p><w:r>" + buildImageDrawingXml(s.nextDocPrId++, rId, cx, cy) + "</w:r></w:p>\n";
            } else {
                s.bodyXml += "<w:p><w:r><w:t>[图片" + (alt.isEmpty() ? "" : ": " + xmlEscape(alt)) + "]</w:t></w:r></w:p>\n";
            }
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
        "<w:rPrDefault><w:rPr><w:sz w:val=\"22\"/><w:rFonts w:ascii=\"Arial\" w:hAnsi=\"Arial\" w:eastAsia=\"黑体\" w:cs=\"Arial\"/></w:rPr></w:rPrDefault>"
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
               "  </w:abstractNum>\n";
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
               "  </w:abstractNum>\n";
    }
    // 每个列表一个独立 <w:num>（numId=1..N 指向对应 abstractNum）。有序列表额外带
    // <w:lvlOverride>/<w:startOverride>，Word/WPS 才把它当作新序列从 1 重新编号；
    // 否则会按 abstractNumId 续编，后续不相邻列表不重启（MarkMyWord#30 同类问题）。
    for (int i = 0; i < s.numAbstractNums.size(); ++i) {
        const bool ordered = (s.numAbstractNums[i] == 1);
        xml += QString("  <w:num w:numId=\"%1\"><w:abstractNumId w:val=\"%2\"/>%3</w:num>\n")
                   .arg(i + 1)
                   .arg(s.numAbstractNums[i])
                   .arg(ordered ? "<w:lvlOverride w:ilvl=\"0\"><w:startOverride w:val=\"1\"/></w:lvlOverride>" : QString());
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
        " xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\""
        " xmlns:pic=\"http://schemas.openxmlformats.org/drawingml/2006/picture\""
        " xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\""
        " xmlns:wne=\"http://schemas.microsoft.com/office/word/2006/wordml\""
        ">\n"
        "<w:body>\n");
    if (s.bodyXml.isEmpty()) {
        doc += "<w:p><w:r><w:t></w:t></w:r></w:p>\n";
    } else {
        doc += s.bodyXml.toUtf8();
    }
    // 节属性（纸张/页边距）必须作为 <w:body> 的最后一个子元素
    if (!s.sectPrXml.isEmpty())
        doc += s.sectPrXml.toUtf8();
    doc += QByteArrayLiteral("</w:body>\n</w:document>\n");
    return doc;
}

// ============ 文件写入 ============

bool DocxExporter::writeDocx(const QString &html, const QString &docDir, const QString &outputPath)
{
    // 1. 预处理 HTML → XML
    QString xmlSafe = makeXmlSafe(html);
    QString xmlDoc = "<?xml version=\"1.0\" encoding=\"UTF-8\"?><root>" + xmlSafe + "</root>";

    // 2. 解析 HTML 生成 OOXML
    ParseState state;
    state.docDir = docDir;   // 相对图片路径基准
    state.mermaidResults = m_mermaidResults;
    state.mermaidSources = m_mermaidSources;
    state.mathSources = m_mathSources;
    state.mathOMML = m_mathOMML;

    // 图片最大宽：与 PDF 导出一致，取纸张尺寸减左右边距（毫米 → EMU，1mm=36000 EMU）。
    // 超宽图片按比例缩小到页面文本宽度，小图保持自然尺寸（不放大）。
    // 同时生成 sectPr 声明纸张尺寸与页边距（twips），使 DOCX 页与导出设置一致，
    // 否则 Word/WPS 用默认（约 1 英寸边距）排版，图片上限与实际页宽不匹配、超宽图越界。
    {
        QSettings settings("md-editor", "md-editor");
        const int paper = settings.value("export/paperSize", static_cast<int>(QPageSize::A4)).toInt();
        const int orient = settings.value("export/orientation", static_cast<int>(QPageLayout::Portrait)).toInt();
        const double mt = settings.value("export/marginTop", 1.8).toDouble();
        const double mb = settings.value("export/marginBottom", 1.8).toDouble();
        const double ml = settings.value("export/marginLeft", 1.8).toDouble();
        const double mr = settings.value("export/marginRight", 1.8).toDouble();
        QSizeF ps = QPageSize(static_cast<QPageSize::PageSizeId>(paper)).size(QPageSize::Millimeter);
        const bool landscape = (orient == static_cast<int>(QPageLayout::Landscape));
        const double pageW = landscape ? ps.height() : ps.width();
        const double pageH = landscape ? ps.width() : ps.height();
        const double contentMm = pageW - (ml + mr) * 10.0;
        state.maxImageWidthEmu = contentMm > 0 ? static_cast<int>(contentMm * 36000.0) : 0;
        const auto mm2twips = [](double mm) { return static_cast<int>(qRound(mm * 56.6929134)); };
        state.sectPrXml = QStringLiteral(
            "<w:sectPr><w:pgSz w:w=\"%1\" w:h=\"%2\"/><w:pgMar w:top=\"%3\" w:right=\"%4\" "
            "w:bottom=\"%5\" w:left=\"%6\" w:header=\"720\" w:footer=\"720\" w:gutter=\"0\"/></w:sectPr>")
            .arg(mm2twips(pageW)).arg(mm2twips(pageH))
            .arg(mm2twips(mt * 10.0)).arg(mm2twips(mr * 10.0))
            .arg(mm2twips(mb * 10.0)).arg(mm2twips(ml * 10.0));
    }
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
    // 图片 media 部件：按实际出现的扩展名去重追加 Default（否则 Word 打不开图片）
    if (!state.media.isEmpty()) {
        QSet<QString> exts;
        for (const auto &mp : state.media)
            exts.insert(QFileInfo(mp.fileName).suffix());
        for (const QString &e : exts) {
            QString ct = (e == "png") ? "image/png" : (e == "gif") ? "image/gif"
                       : (e == "svg") ? "image/svg+xml" : "image/jpeg";
            contentTypes += (QString("  <Default Extension=\"%1\" ContentType=\"%2\"/>\n")
                             .arg(e, ct)).toUtf8();
        }
    }
    contentTypes += QByteArrayLiteral("</Types>\n");

    // 4. ZIP 打包（miniz：跨平台、无外部 zip 依赖；部件均已在内存，先写堆内存再经
    //    QFile 落盘，规避 Windows 下 fopen 对非 ASCII 路径的处理问题）
    const QByteArray mimetype = QByteArrayLiteral(
        "application/vnd.openxmlformats-officedocument.wordprocessingml.document");
    const QByteArray rootRels = QByteArrayLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">\n"
        "  <Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"word/document.xml\"/>\n"
        "</Relationships>\n");

    QFile::remove(outputPath);

    mz_zip_archive zip;
    std::memset(&zip, 0, sizeof(zip));
    if (!mz_zip_writer_init_heap(&zip, 0, 0)) {
        qDebug() << "[DocxExport] miniz 初始化内存归档失败";
        return false;
    }

    bool ok = true;
    auto add = [&zip, &ok](const char *name, const QByteArray &data, mz_uint level) {
        if (!ok) return;
        if (!mz_zip_writer_add_mem(&zip, name, data.constData(),
                                   static_cast<size_t>(data.size()), level)) {
            qDebug() << "[DocxExport] miniz 写入条目失败:" << name;
            ok = false;
        }
    };

    // OPC 规范：mimetype 必须为首条目且 STORE（不压缩），其余按需 deflate
    add("mimetype", mimetype, MZ_NO_COMPRESSION);
    add("[Content_Types].xml", contentTypes, MZ_BEST_COMPRESSION);
    add("_rels/.rels", rootRels, MZ_BEST_COMPRESSION);
    add("word/_rels/document.xml.rels", docRelsXml, MZ_BEST_COMPRESSION);
    add("word/document.xml", docXml, MZ_BEST_COMPRESSION);
    add("word/styles.xml", stylesXml, MZ_BEST_COMPRESSION);
    if (state.hasBulletList || state.hasDecimalList)
        add("word/numbering.xml", numberingXml, MZ_BEST_COMPRESSION);
    for (const auto &mp : state.media) {
        const QByteArray name = "word/media/" + mp.fileName.toUtf8();
        add(name.constData(), mp.bytes, MZ_BEST_COMPRESSION);
    }

    void *buf = nullptr;
    size_t size = 0;
    const bool finalized = ok && mz_zip_writer_finalize_heap_archive(&zip, &buf, &size);

    bool written = false;
    if (finalized) {
        QFile outFile(outputPath);
        written = outFile.open(QIODevice::WriteOnly)
            && outFile.write(static_cast<const char *>(buf), static_cast<qint64>(size)) == static_cast<qint64>(size);
        outFile.close();
    }

    mz_free(buf);   // NULL 安全
    mz_zip_writer_end(&zip);

    if (!finalized || !written) {
        qDebug() << "[DocxExport] miniz 归档或写文件失败";
        return false;
    }

    QFileInfo fi(outputPath);
    if (!fi.exists() || fi.size() == 0) {
        qDebug() << "[DocxExport] Output file missing or empty";
        return false;
    }
    return true;
}
