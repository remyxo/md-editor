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

    // 有 mermaid / 公式：分别离屏渲染，全部完成后替换占位再写 DOCX（异步）。
    // 渲染器为进程级单例（视图+库常驻，导出免 ~500ms 冷启动）；连接以本导出器
    // 为 receiver，导出器 deleteLater 后自动断开
    m_mermaidPending = !m_mermaidSources.isEmpty();
    m_mathPending = !m_mathSources.isEmpty();
    if (m_mermaidPending) {
        m_renderer = MermaidRenderer::instance();
        m_mermaidConn = connect(m_renderer, &MermaidRenderer::finished,
                                this, &DocxExporter::onMermaidRendered);
        m_renderer->renderAll(m_mermaidSources);
    }
    if (m_mathPending) {
        m_mathRenderer = MathMlRenderer::instance();
        m_mathConn = connect(m_mathRenderer, &MathMlRenderer::finished,
                             this, &DocxExporter::onMathRendered);
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
    // 单例渲染器长于本导出器：防止被后续导出轮的 finished 重复唤醒
    //（重写旧文件、重发 exportFinished、重复 deleteLater 崩溃）
    if (m_emitDone) return;
    m_emitDone = true;
    disconnect(m_mermaidConn);
    disconnect(m_mathConn);
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
        // 目录围栏（与 HtmlExporter::inlineImages 同型）：canonical 化（解 ../ 与
        // 符号链接）后必须仍在文档目录内。曾无校验——![](../../x) 经导出可把
        // 目录外文件字节读入（虽被内容级 MIME 拒绝嵌入，读字节本身应受约束）；
        // 未保存文档（docDir 空/不存在）不读本地文件，相对路径等于无围栏
        const QString canonDocDir = QFileInfo(docDir).canonicalFilePath();
        const QString canon = QFileInfo(path).canonicalFilePath();
        if (canonDocDir.isEmpty() || canon.isEmpty()
            || !canon.startsWith(canonDocDir + QLatin1Char('/')))
            return false;
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

// 图片超宽/超高时按比例缩小到页面文本区域内（保持宽高比，只收缩不放大）。
// 宽高约束取更紧的一方联合等比缩放：超宽或超高单独超限各自生效，双超取比例小者。
// maxEmu <= 0 的维度视为不限制。使用 qint64 防乘积溢出（约 240e12，仍在 int64 范围）。
static void clampToPageArea(int &cxEmu, int &cyEmu, int maxWidthEmu, int maxHeightEmu)
{
    if (cxEmu <= 0 || cyEmu <= 0) return;
    const bool overW = maxWidthEmu > 0 && cxEmu > maxWidthEmu;
    const bool overH = maxHeightEmu > 0 && cyEmu > maxHeightEmu;
    if (!overW && !overH) return;
    // 宽超且（高不超或宽约束更紧）→ 按宽缩；否则按高缩。
    // 高度约束缺不得：纵向超长 mermaid 图宽度远小于页宽、高度数倍于页高，
    // Word 不会把内联图片跨页排版，超高部分直接被裁掉（PDF 侧同型 capH 约束）。
    if (overW && (!overH || static_cast<qint64>(cyEmu) * maxWidthEmu
                                   <= static_cast<qint64>(cxEmu) * maxHeightEmu)) {
        cyEmu = static_cast<int>((static_cast<qint64>(cyEmu) * maxWidthEmu) / cxEmu);
        cxEmu = maxWidthEmu;
    } else {
        cxEmu = static_cast<int>((static_cast<qint64>(cxEmu) * maxHeightEmu) / cyEmu);
        cyEmu = maxHeightEmu;
    }
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

// CSS 命名色 → RRGGBB（Word 的 ST_HexColor 只认 6 位十六进制/auto，原样输出
// 命名色或 3 位缩写会产出非法 OOXML，macOS Word 严格校验弹修复框）
static QString cssNamedColorToHex(const QString &name)
{
    static const QHash<QString, QString> named = {
        {"black", "000000"}, {"white", "FFFFFF"}, {"red", "FF0000"},
        {"lime", "00FF00"},  {"green", "008000"}, {"blue", "0000FF"},
        {"yellow", "FFFF00"},{"cyan", "00FFFF"},  {"aqua", "00FFFF"},
        {"magenta", "FF00FF"},{"fuchsia", "FF00FF"},{"gray", "808080"},
        {"grey", "808080"},  {"silver", "C0C0C0"}, {"maroon", "800000"},
        {"olive", "808000"}, {"darkgray", "A9A9A9"},{"darkgrey", "A9A9A9"},
        {"lightgray", "D3D3D3"},{"lightgrey", "D3D3D3"},{"navy", "000080"},
        {"purple", "800080"},{"teal", "008080"},  {"orange", "FFA500"},
        {"pink", "FFC0CB"},  {"brown", "A52A2A"}, {"gold", "FFD700"},
    };
    return named.value(name.toLower());
}

// 从 style 属性中提取颜色值并转为 RRGGBB 六位十六进制；非法值返回空串
//（调用方跳过该颜色，不产出非法 OOXML）
static QString extractColor(const QString &style, const QString &prop)
{
    // (?:^|;\\s*) 确保匹配的属性名在开头或分号后，防止 color 误匹配 background-color 的子串
    QRegularExpression re("(?:^|;\\s*)" + prop + "\\s*:\\s*([^;]+)", QRegularExpression::CaseInsensitiveOption);
    auto m = re.match(style);
    if (!m.hasMatch()) return {};
    QString v = m.captured(1).trimmed();

    // rgb(R, G, B) / rgba(R, G, B, A)：分量支持 0-255 数值与百分比两种写法
    // （从 Word/浏览器粘贴的 HTML 常带 rgb(100%,0%,0%)）；clamp 到 0-255 后
    // 转 6 位十六进制——原样透传超界值会产出 7 位非 ST_HexColor（审查 P2-10：
    // rgb(300,0,0) 曾输出 12C0000，macOS Word 严格校验不过）
    QRegularExpression rgbRe(
        "rgba?\\(\\s*([\\d.]+)(%)?\\s*,\\s*([\\d.]+)(%)?\\s*,\\s*([\\d.]+)(%)?");
    auto rm = rgbRe.match(v);
    if (rm.hasMatch()) {
        auto chan = [](const QString &num, const QString &pct) {
            double d = num.toDouble();
            if (pct == QLatin1String("%")) d = d * 255.0 / 100.0;
            return qBound(0, qRound(d), 255);
        };
        return QString("%1%2%3")
            .arg(chan(rm.captured(1), rm.captured(2)), 2, 16, QChar('0'))
            .arg(chan(rm.captured(3), rm.captured(4)), 2, 16, QChar('0'))
            .arg(chan(rm.captured(5), rm.captured(6)), 2, 16, QChar('0'))
            .toUpper();
    }
    if (v.startsWith('#')) {
        QString hex = v.mid(1).remove(' ');
        // #RGB 3 位缩写展开为 #RRGGBB（每位重复一次，CSS 语义）
        if (hex.size() == 3) {
            QString expanded;
            for (const QChar &c : hex)
                expanded += QString(c) + c;
            return expanded.toUpper();
        }
        if (hex.size() == 6)
            return hex.toUpper();
        return {};
    }
    // CSS 命名色查表；inherit/initial/auto 等关键字与其他未知值返回空（跳过）
    return cssNamedColorToHex(v);
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
    bool smallText = false;
    QStringList textColorStack;   // 嵌套 span 前景色栈（RRGGBB；空串=未设置，取值时继承外层）
    QStringList bgColorStack;     // 嵌套 span 背景色栈
    bool hyperlink = false;

    // 有效颜色 = 最近一层非空设置（CSS 级联语义：内层未设置则继承外层）
    QString textColor() const {
        for (int i = textColorStack.size() - 1; i >= 0; --i)
            if (!textColorStack.at(i).isEmpty()) return textColorStack.at(i);
        return {};
    }
    QString bgColor() const {
        for (int i = bgColorStack.size() - 1; i >= 0; --i)
            if (!bgColorStack.at(i).isEmpty()) return bgColorStack.at(i);
        return {};
    }

    bool operator==(const RunFormat &o) const {
        return bold == o.bold && italic == o.italic && underline == o.underline
            && strike == o.strike && subscript == o.subscript && superscript == o.superscript
            && highlight == o.highlight && monospace == o.monospace
            && smallText == o.smallText
            && textColorStack == o.textColorStack && bgColorStack == o.bgColorStack
            && hyperlink == o.hyperlink;
    }
    bool operator!=(const RunFormat &o) const { return !(*this == o); }

    // fontFamily 非空时覆盖 rFonts（emoji run 换 Noto Color Emoji），并保持
    // rStyle→rFonts 的 schema 前置位置
    QString toPrXml(const char *fontFamily = nullptr) const {
        if (!bold && !italic && !underline && !strike && !subscript && !superscript
            && !highlight && !monospace && !smallText
            && textColor().isEmpty() && bgColor().isEmpty() && !hyperlink
            && !fontFamily)
            return {};
        // CT_RPr 子元素按 schema 序列输出：rStyle → rFonts → b → i → strike →
        // color → sz → highlight → u → shd → vertAlign。原实现顺序违反序列
        // （u 早于 strike、vertAlign 早于 highlight/sz、rFonts 晚于 sz、rStyle
        // 追加在末位；hyperlink 组合还重复输出第二个 w:color/w:u——CT_RPr 各
        // 元素 maxOccurs=1）。显式颜色优先于超链接默认蓝（覆盖 rStyle 的语义）。
        const QString tc = textColor();
        const bool ul = underline || hyperlink;
        QString p;
        if (hyperlink) p += "<w:rStyle w:val=\"Hyperlink\"/>";
        if (fontFamily)
            p += QString("<w:rFonts w:ascii=\"%1\" w:hAnsi=\"%1\" w:eastAsia=\"%1\" w:cs=\"%1\"/>").arg(QLatin1String(fontFamily));
        else if (monospace)
            p += "<w:rFonts w:ascii=\"Consolas\" w:hAnsi=\"Consolas\" w:cs=\"Consolas\"/>";
        if (bold) p += "<w:b/>";
        if (italic) p += "<w:i/>";
        if (strike) p += "<w:strike/>";
        if (!tc.isEmpty()) p += "<w:color w:val=\"" + tc + "\"/>";
        else if (hyperlink) p += "<w:color w:val=\"0563C1\"/>";
        if (smallText) p += "<w:sz w:val=\"18\"/>";   // <small>：11pt 正文 → 9pt（约 0.82×，对齐浏览器 smaller）
        if (highlight) p += "<w:highlight w:val=\"yellow\"/>";
        if (ul) p += "<w:u w:val=\"single\"/>";
        if (!bgColor().isEmpty()) p += "<w:shd w:val=\"clear\" w:color=\"auto\" w:fill=\"" + bgColor() + "\"/>";
        if (subscript) p += "<w:vertAlign w:val=\"subscript\"/>";
        else if (superscript) p += "<w:vertAlign w:val=\"superscript\"/>";
        return "<w:rPr>" + p + "</w:rPr>";
    }
};

struct ParseState {
    QString bodyXml;
    QString relsXml;            // document.xml.rels entries
    int nextRId = 1;
    // 列表编号：每个 <ul>/<ol> 分配独立 numId；无序列表共享 abstractNum 0，
    // 每个有序列表各持一个独立 abstractNum（id 1..N），计数器天然隔离，
    // 使每个列表各自从 1 重新开始编号，无需 startOverride。
    int nextNumId = 1;
    int nextAbstractNumId = 0;    // 有序列表独立 abstractNum 分配器（无序固定为 0）
    QList<int> numAbstractNums;   // 与 numId 一一对应（numId = 下标 + 1），值为该列表引用的 abstractNumId
    bool hasBulletList = false;
    bool hasDecimalList = false;
    int maxListIlvl = 0;    // 文档中出现过的最大嵌套列表层级（决定 numbering.xml 生成的 ilvl 层数）
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
    int maxImageHeightEmu = 0;                            // 图片最大高（EMU），0=不限制；超高按比例缩小（mermaid 长图）
    QString sectPrXml;                                    // 节属性（纸张尺寸+页边距），使 DOCX 页与导出设置一致
    QVector<MermaidRenderer::Result> mermaidResults;      // 渲染结果（按 idx）
    QStringList mermaidSources;                           // 源码（渲染失败回退用）
    QStringList mathSources;                              // 公式 LaTeX 源码（按 idx，失败回退用）
    QStringList mathOMML;                                 // 公式 OMML 片段（按 idx，失败项空）

    // 脚注真导出（审查 P1-7）：引用与定义共用编号分配，定义内容进 footnotes.xml
    QHash<QString, int> footnoteNumById;    // data-id → Word 脚注号（2 起；0/1 为内置分隔符）
    QStringList footnoteOrder;              // data-id 按首次出现（引用或定义）排序
    QHash<int, QString> footnoteRunsByNum;  // 脚注号 → 定义内容 OOXML runs
    QSet<QString> footnoteBodyMarkDone;     // 已发出原生正文标记（+书签）的 data-id

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

// 脚注编号分配：Word 约定 0=separator、1=continuationSeparator，正文脚注从 2 起。
// 引用与定义谁先出现谁分配；同一 data-id 复用同号（Markdown [^x] 多处引用同一定义）。
static int footnoteNumFor(const QString &id, ParseState &s)
{
    auto it = s.footnoteNumById.constFind(id);
    if (it != s.footnoteNumById.constEnd()) return it.value();
    const int num = 2 + s.footnoteNumById.size();
    s.footnoteNumById.insert(id, num);
    s.footnoteOrder.append(id);
    return num;
}

// 脚注定义 runs 的部件级关系清洗：parseInline 产出的 <w:hyperlink r:id> 与图片
// <w:drawing> 引用的是 document.xml.rels 的关系 id，而脚注内容位于独立的
// footnotes.xml 部件（OOXML 关系按部件解析，悬挂 r:id 会被 Word 判「无法读取的
// 内容」）。剥掉超链接外壳（文字保留、不可点）与整块 drawing（图片舍弃，脚注内
// 图片极罕见），保 Word 严格校验通过（docx-word-strict-validation 经验：本机
// WPS/LibreOffice 宽容测不出）。
static QString sanitizeFootnoteRuns(QString runs)
{
    static const QRegularExpression hyperlinkOpenRe("<w:hyperlink\\b[^>]*>");
    static const QRegularExpression drawingRe("<w:drawing>.*?</w:drawing>");
    runs.remove(hyperlinkOpenRe);
    runs.remove(QStringLiteral("</w:hyperlink>"));
    runs.remove(drawingRe);
    return runs;
}

// ============ 内联内容解析 ============

// 在 <w:p>/<w:tc> 等块容器内解析内联内容，返回 OOXML 游程序列
static QString parseInline(QXmlStreamReader &r, const QString &stopTag, ParseState &s)
{
    RunFormat fmt;
    QString textBuf;
    QString out;
    // span 开合与颜色压栈的精确配对表（true=该 span 压了颜色层；数学 span 为
    // false，闭合时不弹）——嵌套/穿插场景下 close 无法从上下文自行判别
    QList<bool> spanColorPushed;

    // 输出一个 run，并根据文本中是否有 emoji 决定是否自动切分
    auto emitRun = [&](const QString &txt, bool isEmoji) {
        if (txt.isEmpty()) return;
        QString pr;
        // emoji run：继承当前格式但换 emoji 字体（rFonts 经 toPrXml 参数注入，
        // 保持 CT_RPr 子元素顺序——原字符串截断拼接会把 rFonts 追加到末尾）
        pr = fmt.toPrXml(isEmoji ? "Noto Color Emoji" : nullptr);
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
            } else if (tag == "small") {
                flushRun(); fmt.smallText = true;
            } else if (tag == "sub") {
                flushRun(); fmt.subscript = true;
            } else if (tag == "sup") {
                // 脚注引用（TipTap 契约 <sup data-type="footnoteRef" data-id="x">x</sup>）：
                // 转 Word 原生脚注引用（FootnoteReference 样式 + footnoteReference 指向
                // footnotes.xml 的定义），Word 自动编号上标；元素内的 id 文本丢弃。
                if (r.attributes().value("data-type").toString() == "footnoteRef") {
                    flushRun();
                    const QString fid = r.attributes().value("data-id").toString();
                    const int num = footnoteNumFor(fid, s);
                    if (!s.footnoteBodyMarkDone.contains(fid)) {
                        // 首个正文引用：原生脚注标记，外裹隐藏书签（_RefFtn<N>）
                        // 供重复引用的 NOTEREF 域定位——Word 交叉引用同款机制
                        s.footnoteBodyMarkDone.insert(fid);
                        out += QStringLiteral(
                                   "<w:bookmarkStart w:id=\"%1\" w:name=\"_RefFtn%1\"/>"
                                   "<w:r><w:rPr><w:rStyle w:val=\"FootnoteReference\"/>"
                                   "<w:vertAlign w:val=\"superscript\"/></w:rPr>"
                                   "<w:footnoteReference w:id=\"%1\"/></w:r>"
                                   "<w:bookmarkEnd w:id=\"%1\"/>").arg(num);
                    } else {
                        // 重复引用：Word 脚注标记一脚注一锚点，同 id 第二个
                        // footnoteReference 会触发「无法读取的内容」修复框
                        // （macOS Word A/B 隔离实证 2026-09-15）——改用 Word 原生
                        // 交叉引用域 NOTEREF（显示同号、可点击、随重编号更新）
                        out += QStringLiteral(
                                   "<w:r><w:fldChar w:fldCharType=\"begin\"/></w:r>"
                                   "<w:r><w:instrText xml:space=\"preserve\">"
                                   " NOTEREF _RefFtn%1 \\r \\h </w:instrText></w:r>"
                                   "<w:r><w:fldChar w:fldCharType=\"separate\"/></w:r>"
                                   "<w:r><w:rPr><w:rStyle w:val=\"FootnoteReference\"/>"
                                   "<w:vertAlign w:val=\"superscript\"/></w:rPr>"
                                   "<w:t>%2</w:t></w:r>"
                                   "<w:r><w:fldChar w:fldCharType=\"end\"/></w:r>")
                                   .arg(num)
                                   .arg(num - 1);   // 显示号：正文脚注按序 1 起
                    }
                    int depth = 1;
                    while (!r.atEnd() && depth > 0) {
                        const auto t2 = r.readNext();
                        if (t2 == QXmlStreamReader::StartElement) ++depth;
                        else if (t2 == QXmlStreamReader::EndElement) --depth;
                    }
                    continue;
                }
                flushRun(); fmt.superscript = true;
            }
            // span（内联数学 / 颜色）
            else if (tag == "span") {
                // 脚注定义里的编号 marker（class=footnote-marker，toHtml 恒输出，
                // 内含返回按钮等 UI 辅件）：Word 原生 footnoteRef 已渲染编号，
                // marker 再输出会双编号——整体消费丢弃
                if (r.attributes().value("class").toString()
                        .split(' ').contains("footnote-marker")) {
                    int depth = 1;
                    while (!r.atEnd() && depth > 0) {
                        const auto t2 = r.readNext();
                        if (t2 == QXmlStreamReader::StartElement) ++depth;
                        else if (t2 == QXmlStreamReader::EndElement) --depth;
                    }
                    continue;
                }
                // 内联公式占位：替换为 <m:oMath> 原生公式（OMML）；渲染失败回退 $...$
                if (r.attributes().hasAttribute("data-math-idx")) {
                    flushRun();
                    int idx = r.attributes().value("data-math-idx").toInt();
                    QString latex = (idx >= 0 && idx < s.mathSources.size()) ? s.mathSources[idx] : QString();
                    QString omm = (idx >= 0 && idx < s.mathOMML.size()) ? s.mathOMML[idx] : QString();
                    if (!omm.isEmpty()) out += "<m:oMath>" + omm + "</m:oMath>";
                    else out += "<w:r><w:t>$" + xmlEscape(latex) + "$</w:t></w:r>";
                    spanColorPushed.append(false);   // 不占颜色栈（闭合时不弹）
                }
                // 内联数学 span 内容为空（KaTeX 渲染），须取 data-latex；
                // 此判断须先于颜色 span，否则 inline-math 被当普通 span 丢弃。
                // 占位替换正常运行时此分支不可达，保留作兜底（替换漏匹配时仍输出 $...$）。
                else if (r.attributes().value("data-type").toString() == "inline-math") {
                    flushRun();
                    QString latex = r.attributes().value("data-latex").toString();
                    out += "<w:r><w:t>$" + xmlEscape(latex) + "$</w:t></w:r>";
                    spanColorPushed.append(false);
                } else {
                    // 颜色 span：恒压一层（未设置的维度压空串 = 继承外层，CSS 级联
                    // 语义，由 textColor()/bgColor() 取最近非空值）；空 style 的穿透
                    // span 同样压空层，保证开合配对精确
                    flushRun();
                    auto attrs = r.attributes();
                    QString sty = attrs.value("style").toString();
                    fmt.textColorStack.append(extractColor(sty, "color"));
                    fmt.bgColorStack.append(extractColor(sty, "background-color"));
                    spanColorPushed.append(true);
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
                    clampToPageArea(cx, cy, s.maxImageWidthEmu, s.maxImageHeightEmu);
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
            // 脚注容器内的定义 div（块级元素穿过内联上下文，footnoteContainer
            // 分支走 parseInline 时到达这里）：收集到 footnotes.xml，正文不输出
            else if (tag == "div"
                     && r.attributes().value("data-type").toString() == "footnoteDef") {
                flushRun();
                const QString fid = r.attributes().value("data-id").toString();
                const int num = footnoteNumFor(fid, s);
                s.footnoteRunsByNum.insert(
                    num, sanitizeFootnoteRuns(parseInline(r, "div", s)));
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
            } else if (tag == "small") {
                flushRun(); fmt.smallText = false;
            } else if (tag == "sub") {
                flushRun(); fmt.subscript = false;
            } else if (tag == "sup") {
                flushRun(); fmt.superscript = false;
            } else if (tag == "span") {
                flushRun();
                // 嵌套 span 颜色栈弹层：仅弹「本层压过颜色」的 span（数学 span
                // 闭合不弹）。曾无条件 clear：内层闭合后外层颜色即丢失
                if (!spanColorPushed.isEmpty() && spanColorPushed.takeLast()) {
                    if (!fmt.textColorStack.isEmpty()) fmt.textColorStack.removeLast();
                    if (!fmt.bgColorStack.isEmpty()) fmt.bgColorStack.removeLast();
                }
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
        // 脚注定义：真导出为 Word 原生脚注（内容进 footnotes.xml，正文不输出）。
        // 旧实现解析出内容后丢弃、正文固定写 "[^id]: ..."（审查 P1-7）。
        QString id = r.attributes().value("data-id").toString();
        const int num = footnoteNumFor(id, s);
        s.footnoteRunsByNum.insert(
            num, sanitizeFootnoteRuns(parseInline(r, tag, s)));
        return {};
    }
    if (dataType == "block-math") {
        QString latex = r.attributes().value("data-latex").toString();
        return "<w:p><w:pPr><w:jc w:val=\"center\"/></w:pPr>"
               "<w:r><w:t>$$" + xmlEscape(latex) + "$$</w:t></w:r></w:p>";
    }
    if (dataType == "footnoteContainer") {
        // 容器本体不产出正文（def 在内联上下文已收集进 footnotes 状态）；
        // def 之间的杂散文本（手写/格式化 HTML 的换行空白）一并丢弃
        parseInline(r, tag, s);
        return {};
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

    // 逐行生成 <w:p>，等宽字体 + 灰底。
    // CT_PPrBase 子元素须按 schema 序列（… → shd → … → spacing → ind → …），
    // ind 不得早于 spacing——顺序违规触发 Word「无法读取的内容」修复框
    QString pPr = "<w:pPr>"
                  "<w:shd w:val=\"clear\" w:color=\"auto\" w:fill=\"F2F2F2\"/>"
                  "<w:spacing w:before=\"0\" w:after=\"0\" w:line=\"240\" w:lineRule=\"auto\"/>"
                  "<w:ind w:left=\"360\" w:right=\"360\"/>"
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

// 引用样式注入：对 blocks 里每个 <w:p> 加左侧蓝竖线 + 层级缩进。
// 左侧竖线对齐 PDF/HTML 的 border-left，颜色 #0078d4 与 PdfExporter 一致；
// 已有 pPr 的段落把属性插到已有内容之前（保持单个 <w:pPr>，否则非法 OOXML，
// 顺序容忍先例见任务列表注入）。level 0 起，每层缩进递增 360
static QString quoteStyled(const QString &blocks, int level)
{
    const QString quotePr = QStringLiteral(
        "<w:pBdr><w:left w:val=\"single\" w:sz=\"18\" w:space=\"4\" w:color=\"0078d4\"/></w:pBdr>"
        "<w:ind w:left=\"%1\" w:right=\"720\"/>").arg(720 + level * 360);
    QString out;
    int pos = 0;
    while (pos < blocks.size()) {
        const int pStart = blocks.indexOf("<w:p>", pos);
        if (pStart < 0) {
            out += blocks.mid(pos);
            break;
        }
        const int afterTag = pStart + 5;   // 跳过 "<w:p>"
        out += blocks.mid(pos, afterTag - pos);
        if (blocks.mid(afterTag, 7) == QLatin1String("<w:pPr>")) {
            out += QLatin1String("<w:pPr>") + quotePr;
            pos = afterTag + 7;
        } else {
            out += QLatin1String("<w:pPr>") + quotePr + QLatin1String("</w:pPr>");
            pos = afterTag;
        }
    }
    return out;
}

// 前置声明：定义位于本函数之后（引用块分派到列表/表格/水平线）。parseList
// 的 ilvl 缺省值放声明处，定义处不再重复（C++ 缺省实参只许出现一次）
static QString parseHorizontalRule();
static QString parseList(QXmlStreamReader &r, bool ordered, ParseState &s, int ilvl = 0);
static QString parseTable(QXmlStreamReader &r, ParseState &s);

static QString parseBlockquote(QXmlStreamReader &r, ParseState &s, int level = 0)
{
    // <blockquote> 内部递归解析块元素。曾只认 p/div/h1-h6 且循环在第一个
    // </blockquote> 即 break：嵌套引用的内层闭合会截断外层（其后段落丢失），
    // 引用内 pre/table 的文本无人消费（内容丢失）、ul/ol 结构退化、标题无
    // 引用样式。现全量分派 + 嵌套递归 + 逐段样式注入
    QString result;
    while (!r.atEnd()) {
        auto tt = r.readNext();
        if (tt == QXmlStreamReader::EndElement
            && r.name().toString().compare(QLatin1String("blockquote"), Qt::CaseInsensitive) == 0)
            break;
        if (tt != QXmlStreamReader::StartElement) continue;
        QString tag = r.name().toString().toLower();
        // 递归解析内部块
        QString inner;
        if (tag == "p" || tag == "div") {
            inner = parseParagraph(r, tag, s);
        } else if (tag == "blockquote") {
            inner = parseBlockquote(r, s, level + 1);
        } else if (tag == "h1") inner = parseHeading(r, 1, s);
        else if (tag == "h2") inner = parseHeading(r, 2, s);
        else if (tag == "h3") inner = parseHeading(r, 3, s);
        else if (tag == "h4") inner = parseHeading(r, 4, s);
        else if (tag == "h5") inner = parseHeading(r, 5, s);
        else if (tag == "h6") inner = parseHeading(r, 6, s);
        else if (tag == "pre") inner = parseCodeBlock(r, s);
        else if (tag == "ul") inner = parseList(r, false, s);
        else if (tag == "ol") inner = parseList(r, true, s);
        else if (tag == "table") inner = parseTable(r, s);
        else if (tag == "hr") inner = parseHorizontalRule();
        else continue;
        result += quoteStyled(inner, level);
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

static QString parseList(QXmlStreamReader &r, bool ordered, ParseState &s, int ilvl)
{
    int numId = s.nextNumId++;
    if (ordered) {
        s.numAbstractNums.append(++s.nextAbstractNumId);  // 有序列表独立 abstractNum（id 1..N）
        s.hasDecimalList = true;
    } else {
        s.numAbstractNums.append(0);                      // 无序列表共享 abstractNum 0
        s.hasBulletList = true;
    }
    if (ilvl > s.maxListIlvl) s.maxListIlvl = ilvl;

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

                // 嵌套列表：递归解析，ilvl 递增；结果自带 numPr（独立 numId + 当前 ilvl），
                // 直接追加，不得再套父列表的 numPr（否则仍落回 ilvl=0 与父项同列）。
                if (innerTag == "ul" || innerTag == "ol") {
                    result += parseList(r, innerTag == "ol", s, ilvl + 1);
                    continue;
                }

                QString inner;
                if (innerTag == "p") {
                    inner = parseParagraph(r, "p", s);
                } else if (innerTag == "div") {
                    // 任务项内容容器 <div>（TipTap 渲染为 <li><label>…</label><div>…</div></li>）：
                    // 迭代其子块。<p> 用 parseParagraph，嵌套 <ul>/<ol> 递归（ilvl+1、自带 numPr）。
                    // 曾因直接 parseParagraph 让 parseInline 吞掉嵌套列表文字，子任务并入父段、
                    // 嵌套列表整体消失（「无缩进」与「文字合并」两个症状同源）。
                    inner.clear();
                    while (!r.atEnd()) {
                        auto dt = r.readNext();
                        if (dt == QXmlStreamReader::EndElement
                            && r.name().toString().compare(QLatin1String("div"), Qt::CaseInsensitive) == 0)
                            break;
                        if (dt != QXmlStreamReader::StartElement) continue;
                        QString childTag = r.name().toString().toLower();
                        if (childTag == "p") {
                            inner += parseParagraph(r, "p", s);
                        } else if (childTag == "ul" || childTag == "ol") {
                            inner += parseList(r, childTag == "ol", s, ilvl + 1);
                        }
                        // 其他子元素忽略
                    }
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
                // 分离首个 <w:p> 段落与其余子块：复选框前缀 + 列表 numPr 只作用于首个段落，
                // 嵌套列表等其余子块（递归已自带 numPr）原样保留，不再被二次包装。
                QString firstP, rest;
                if (inner.startsWith("<w:p>")) {
                    int blockEnd = inner.indexOf("</w:p>");
                    if (blockEnd >= 0) {
                        firstP = inner.left(blockEnd + 6);
                        rest = inner.mid(blockEnd + 6);
                    }
                }
                if (!firstP.isEmpty()) {
                    // 任务列表复选框前缀（仅首个段落；已带 numPr 的嵌套列表段落不注入）
                    if (!taskPrefix.isEmpty() && !firstP.contains("<w:numPr>")) {
                        // 复选框 run 显式指定符号字体：Arial/黑体 无 U+2610/U+2612 字形，
                        // 缺省回退在部分 Word/WPS 环境不可靠，☐/☒ 会渲染成空白。
                        QString ckRun = "<w:r><w:rPr><w:rFonts w:ascii=\"Segoe UI Symbol\" w:hAnsi=\"Segoe UI Symbol\" w:cs=\"Segoe UI Symbol\"/></w:rPr><w:t xml:space=\"preserve\">" + taskPrefix + "</w:t></w:r>";
                        int pPrEnd = firstP.indexOf("</w:pPr>");
                        if (pPrEnd >= 0) {
                            firstP = firstP.left(pPrEnd + 8) + ckRun + firstP.mid(pPrEnd + 8);
                        } else {
                            // <w:p> 共 5 个字符，mid(5) 才跳过整个开标签；mid(4) 会残留一个
                            // 孤立的 ">" 文本节点（<w:p> 内不允许裸文本），Word 打开时可能
                            // 丢弃该段或触发修复，导致任务列表复选框丢失、退化为普通列表。
                            firstP = QStringLiteral("<w:p>") + ckRun + firstP.mid(5);
                        }
                        taskPrefix.clear();
                    }
                    // 列表 numPr（仅首个段落；ilvl 取当前嵌套深度 0=顶层，子列表由递归承载）
                    if (!firstP.contains("<w:numPr>")) {
                        QString listPPr = "<w:pStyle w:val=\"ListParagraph\"/>"
                                          "<w:numPr>"
                                          "<w:ilvl w:val=\"" + QString::number(ilvl) + "\"/>"
                                          "<w:numId w:val=\"" + QString::number(numId) + "\"/>"
                                          "</w:numPr>";
                        int pPrStart = firstP.indexOf("<w:pPr>");
                        int pPrEnd   = firstP.indexOf("</w:pPr>");
                        if (pPrStart >= 0 && pPrEnd > pPrStart) {
                            // 已有 pPr：合并列表属性到已有 pPr 开头（保持单个 <w:pPr>，否则非法）
                            QString existing = firstP.mid(pPrStart + 7, pPrEnd - pPrStart - 7);
                            firstP = firstP.left(pPrStart + 7) + listPPr + existing + firstP.mid(pPrEnd);
                        } else {
                            // 无 pPr：在首个 <w:p> 后插入。不可用 QString::replace（会替换所有
                            // <w:p>，嵌套列表段落将被二次包装成双 numPr），须按 index 精确定位首个。
                            firstP = QStringLiteral("<w:p><w:pPr>") + listPPr + QStringLiteral("</w:pPr>") + firstP.mid(5);
                        }
                    } else {
                        // 首个块已是嵌套列表段落（父项无自身文本的边界情形）：丢弃悬空复选框
                        taskPrefix.clear();
                    }
                    result += firstP;
                }
                result += rest;
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
    int headerCols = 0;   // 表头行列数（无 colgroup 时默认 tblGrid 用，含 colspan 求和）

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
                    // 属性名锚定（(?:^|;)）：TipTap 序列化表格无 colwidth 时每列
                    // 只有 min-width:25px，裸 width 子串匹配会把 25 当列宽
                    QRegularExpression re("(?:^|;)\\s*width\\s*:\\s*(\\d+)px");
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
                    // 检查对齐与合并跨列（属性须在 parseInline 消费元素前读取）
                    QString cellSty = r.attributes().value("style").toString();
                    QString cellAlign = extractAlign(cellSty);
                    QString cellSpan = r.attributes().value("colspan").toString();
                    const int span = cellSpan.toInt();
                    if (firstRow)
                        headerCols += (span > 1 ? span : 1);
                    result += "<w:tc>";
                    // 单元格属性：tcPr 无 w:jc 子元素（CT_TcPr 只认 tcW/shd/vAlign 等），
                    // 水平对齐是段落属性，写入下方单元格段落的 <w:pPr>
                    result += "<w:tcPr>";
                    result += "<w:tcW w:w=\"2340\" w:type=\"dxa\"/>";
                    if (span > 1) result += "<w:gridSpan w:val=\"" + cellSpan + "\"/>";
                    if (isTh) result += "<w:shd w:val=\"clear\" w:color=\"auto\" w:fill=\"D9E2F3\"/>";
                    result += "</w:tcPr>";

                    // 单元格内容（内联解析）
                    QString cellInner = parseInline(r, cellTag, s);
                    QString pPr = cellAlign.isEmpty() ? QString()
                        : "<w:pPr><w:jc w:val=\"" + cellAlign + "\"/></w:pPr>";
                    result += "<w:p>" + pPr + cellInner + "</w:p>";
                    result += "</w:tc>";
                }
            }
            result += "</w:tr>";
            firstRow = false;
        }
    }

    // 插入 tblGrid（如果有 colgroup，用之；否则用表头列数生成默认列宽）
    if (tblGrid.isEmpty()) {
        QString cols;
        for (int i = 0; i < (headerCols > 0 ? headerCols : 2); ++i)
            cols += "<w:gridCol w:w=\"4680\"/>";
        tblGrid = cols;
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
                            clampToPageArea(cx, cy, s.maxImageWidthEmu, s.maxImageHeightEmu);
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
                clampToPageArea(cx, cy, s.maxImageWidthEmu, s.maxImageHeightEmu);
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

// 页眉部件：单段居中文档标题（9pt 灰 #808080），段距紧凑（覆盖 docDefaults 1.15 行距）
static QString makeHeaderXml(const QString &title)
{
    const QString rpr = "<w:rPr><w:color w:val=\"808080\"/><w:sz w:val=\"18\"/></w:rPr>";
    return QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<w:hdr xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\">\n"
        "  <w:p>"
        "<w:pPr><w:spacing w:after=\"0\" w:line=\"240\" w:lineRule=\"auto\"/>"
        "<w:jc w:val=\"center\"/></w:pPr>"
        "<w:r>%1<w:t xml:space=\"preserve\">%2</w:t></w:r>"
        "</w:p>\n"
        "</w:hdr>\n").arg(rpr, xmlEscape(xmlSanitize(title)));
}

// 页脚部件：右对齐「第 X 页 / 共 Y 页」，X/Y 为 PAGE/NUMPAGES 简单字段——
// Word 打开时按字段重算（活页码，用户续编辑自动更新）；字段内 run 文本是占位值
static QString makeFooterXml()
{
    const QString rpr = "<w:rPr><w:color w:val=\"808080\"/><w:sz w:val=\"18\"/></w:rPr>";
    const QString pPr = "<w:pPr><w:spacing w:after=\"0\" w:line=\"240\" w:lineRule=\"auto\"/>"
                        "<w:jc w:val=\"right\"/></w:pPr>";
    return QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<w:ftr xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\">\n"
        "  <w:p>%1"
        "<w:r>%2<w:t xml:space=\"preserve\">第 </w:t></w:r>"
        "<w:fldSimple w:instr=\" PAGE   \\* MERGEFORMAT \"><w:r>%2<w:t>1</w:t></w:r></w:fldSimple>"
        "<w:r>%2<w:t xml:space=\"preserve\"> 页 / 共 </w:t></w:r>"
        "<w:fldSimple w:instr=\" NUMPAGES   \\* MERGEFORMAT \"><w:r>%2<w:t>1</w:t></w:r></w:fldSimple>"
        "<w:r>%2<w:t xml:space=\"preserve\"> 页</w:t></w:r>"
        "</w:p>\n"
        "</w:ftr>\n").arg(pPr, rpr);
}

static QString makeStylesXml()
{
    return QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<w:styles xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\">\n"
        "  <w:docDefaults>"
        // rPr 子元素同样受 CT_RPr 序列约束：rFonts 必须早于 sz
        "<w:rPrDefault><w:rPr><w:rFonts w:ascii=\"Arial\" w:hAnsi=\"Arial\" w:eastAsia=\"黑体\" w:cs=\"Arial\"/><w:sz w:val=\"22\"/></w:rPr></w:rPrDefault>"
        "<w:pPrDefault><w:pPr><w:spacing w:line=\"276\" w:lineRule=\"auto\"/></w:pPr></w:pPrDefault>"
        "</w:docDefaults>\n"
        "  <w:style w:type=\"paragraph\" w:styleId=\"Normal\" w:default=\"1\"><w:name w:val=\"Normal\"/><w:rPr><w:sz w:val=\"22\"/></w:rPr></w:style>\n"
        // Heading 样式 pPr：CT_PPrBase 序列中 spacing 早于 outlineLvl
        "  <w:style w:type=\"paragraph\" w:styleId=\"Heading1\"><w:name w:val=\"heading 1\"/><w:basedOn w:val=\"Normal\"/><w:next w:val=\"Normal\"/><w:pPr><w:spacing w:before=\"240\" w:after=\"120\"/><w:outlineLvl w:val=\"0\"/></w:pPr><w:rPr><w:b/><w:sz w:val=\"32\"/></w:rPr></w:style>\n"
        "  <w:style w:type=\"paragraph\" w:styleId=\"Heading2\"><w:name w:val=\"heading 2\"/><w:basedOn w:val=\"Normal\"/><w:next w:val=\"Normal\"/><w:pPr><w:spacing w:before=\"200\" w:after=\"100\"/><w:outlineLvl w:val=\"1\"/></w:pPr><w:rPr><w:b/><w:sz w:val=\"28\"/></w:rPr></w:style>\n"
        "  <w:style w:type=\"paragraph\" w:styleId=\"Heading3\"><w:name w:val=\"heading 3\"/><w:basedOn w:val=\"Normal\"/><w:next w:val=\"Normal\"/><w:pPr><w:spacing w:before=\"160\" w:after=\"80\"/><w:outlineLvl w:val=\"2\"/></w:pPr><w:rPr><w:b/><w:sz w:val=\"26\"/></w:rPr></w:style>\n"
        "  <w:style w:type=\"paragraph\" w:styleId=\"Heading4\"><w:name w:val=\"heading 4\"/><w:basedOn w:val=\"Normal\"/><w:next w:val=\"Normal\"/><w:pPr><w:spacing w:before=\"120\" w:after=\"60\"/><w:outlineLvl w:val=\"3\"/></w:pPr><w:rPr><w:b/><w:sz w:val=\"24\"/></w:rPr></w:style>\n"
        "  <w:style w:type=\"paragraph\" w:styleId=\"Heading5\"><w:name w:val=\"heading 5\"/><w:basedOn w:val=\"Normal\"/><w:next w:val=\"Normal\"/><w:pPr><w:spacing w:before=\"100\" w:after=\"40\"/><w:outlineLvl w:val=\"4\"/></w:pPr><w:rPr><w:b/><w:sz w:val=\"22\"/></w:rPr></w:style>\n"
        "  <w:style w:type=\"paragraph\" w:styleId=\"Heading6\"><w:name w:val=\"heading 6\"/><w:basedOn w:val=\"Normal\"/><w:next w:val=\"Normal\"/><w:pPr><w:spacing w:before=\"80\" w:after=\"40\"/><w:outlineLvl w:val=\"5\"/></w:pPr><w:rPr><w:b/><w:sz w:val=\"21\"/></w:rPr></w:style>\n"
        "  <w:style w:type=\"paragraph\" w:styleId=\"ListParagraph\"><w:name w:val=\"List Paragraph\"/><w:basedOn w:val=\"Normal\"/><w:pPr><w:ind w:left=\"720\"/></w:pPr></w:style>\n"
        // 表格引用的 TableGrid 与 Hyperlink 的 basedOn 目标都补上定义：
        // 悬空引用 Word 会忽略（不致修复框），但定义齐备才与 Word 自产文档一致
        "  <w:style w:type=\"table\" w:styleId=\"TableGrid\" w:default=\"1\"><w:name w:val=\"Table Grid\"/><w:tblPr/></w:style>\n"
        "  <w:style w:type=\"character\" w:styleId=\"DefaultParagraphFont\" w:default=\"1\"><w:name w:val=\"Default Paragraph Font\"/></w:style>\n"
        "  <w:style w:type=\"character\" w:styleId=\"Hyperlink\"><w:name w:val=\"Hyperlink\"/><w:basedOn w:val=\"DefaultParagraphFont\"/><w:rPr><w:color w:val=\"0563C1\"/><w:u w:val=\"single\"/></w:rPr></w:style>\n"
        // 脚注真导出：Word 内置样式名（footnote text / footnote reference），
        // 页脚区 9pt + 编号上标；引用处另带显式 vertAlign 兜底（样式缺失时不丢上标）
        "  <w:style w:type=\"paragraph\" w:styleId=\"FootnoteText\"><w:name w:val=\"footnote text\"/><w:basedOn w:val=\"Normal\"/><w:pPr><w:spacing w:after=\"0\" w:line=\"240\" w:lineRule=\"auto\"/></w:pPr><w:rPr><w:sz w:val=\"18\"/></w:rPr></w:style>\n"
        "  <w:style w:type=\"character\" w:styleId=\"FootnoteReference\"><w:name w:val=\"footnote reference\"/><w:rPr><w:vertAlign w:val=\"superscript\"/></w:rPr></w:style>\n"
        "</w:styles>\n");
}

// word/footnotes.xml：0=separator、1=continuationSeparator（Word 约定内置，缺省
// 也能工作但补齐更规范），2..=各脚注定义。定义段落挂 FootnoteText 样式，
// w:footnoteRef 由 Word 自动渲染为该脚注的编号上标；其后接一个空格 run 再接
// 定义内容（与 Word 自身产物一致）。namespace 集与 document.xml 根元素对齐
// （定义 runs 可能含 m:oMath 等前缀）。
static QByteArray makeFootnotesXml(const ParseState &s)
{
    QString xml = QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<w:footnotes"
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
        "  <w:footnote w:type=\"separator\" w:id=\"0\"><w:p><w:pPr><w:spacing w:after=\"0\" w:line=\"240\" w:lineRule=\"auto\"/></w:pPr><w:r><w:separator/></w:r></w:p></w:footnote>\n"
        "  <w:footnote w:type=\"continuationSeparator\" w:id=\"1\"><w:p><w:pPr><w:spacing w:after=\"0\" w:line=\"240\" w:lineRule=\"auto\"/></w:pPr><w:r><w:continuationSeparator/></w:r></w:p></w:footnote>\n");
    for (const QString &id : s.footnoteOrder) {
        const int num = s.footnoteNumById.value(id);
        const QString runs = s.footnoteRunsByNum.value(num);
        // 编号标记后补一个空格（Word 自身产物形态）；无定义内容时只剩编号
        xml += QStringLiteral(
            "  <w:footnote w:id=\"%1\"><w:p><w:pPr><w:pStyle w:val=\"FootnoteText\"/>"
            "<w:spacing w:after=\"0\" w:line=\"240\" w:lineRule=\"auto\"/></w:pPr>"
            "<w:r><w:rPr><w:rStyle w:val=\"FootnoteReference\"/></w:rPr><w:footnoteRef/></w:r>"
            "%2</w:p></w:footnote>\n")
            .arg(num)
            .arg(runs.isEmpty() ? QString()
                 : QStringLiteral("<w:r><w:t xml:space=\"preserve\"> </w:t></w:r>") + runs);
    }
    xml += QStringLiteral("</w:footnotes>\n");
    return xml.toUtf8();
}

static QString makeNumberingXml(const ParseState &s)
{
    QString xml = QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<w:numbering xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\">\n");

    // 无序列表（bullet）：共享一个 abstractNum（id=0），多层定义 ilvl 0..maxListIlvl，
    // 每层缩进递增 720 twips（0.5 英寸），符号逐层轮换 • ◦ ▪ 以区分层级
    // （hybridMultilevel 支持各层独立 lvlText）。bullet 无可见编号，无跨列表续编问题。
    if (s.hasBulletList) {
        static const char *bulletGlyphs[] = {"&#x2022;", "&#x25E6;", "&#x25AA;"};
        xml += "  <w:abstractNum w:abstractNumId=\"0\">\n"
               "    <w:multiLevelType w:val=\"hybridMultilevel\"/>\n";
        for (int lvl = 0; lvl <= s.maxListIlvl; ++lvl) {
            xml += "    <w:lvl w:ilvl=\"" + QString::number(lvl) + "\">\n"
                   "      <w:start w:val=\"1\"/>\n"
                   "      <w:numFmt w:val=\"bullet\"/>\n"
                   "      <w:lvlText w:val=\"" + QString(bulletGlyphs[lvl % 3]) + "\"/>\n"
                   "      <w:lvlJc w:val=\"left\"/>\n"
                   "      <w:pPr><w:ind w:left=\"" + QString::number(720 + lvl * 720)
                   + "\" w:hanging=\"360\"/></w:pPr>\n"
                   "    </w:lvl>\n";
        }
        xml += "  </w:abstractNum>\n";
    }
    // 有序列表（decimal）：每个列表一个独立 abstractNum（id 1..N），各层保持 N. 十进制
    // （对齐 HTML/PDF 中嵌套 <ol> 的渲染），缩进随层级递增。独立计数器保证各列表天然
    // 从 1 重新编号，无需 startOverride。
    // 关键：lvlText 必须引用「当前层级自己的序号」——%1 是第 1 级（ilvl=0）的计数器、
    // %2 是第 2 级（ilvl=1）、%3 是第 3 级（ilvl=2）。曾统一写 "%1."，深层项（ilvl≥1）
    // 显示的是浅层计数器：嵌套有序列表（ilvl=2）的 numId 从不在 ilvl=0 出现，ilvl=0
    // 计数器恒为 1，导致所有嵌套项都显示 1（WPS/Word 一致，顶层 ilvl=0 恰好正常）。
    for (int i = 0; i < s.numAbstractNums.size(); ++i) {
        const int aid = s.numAbstractNums[i];
        if (aid == 0) continue;   // 无序列表共享 abstractNum 0，已在上方输出
        xml += "  <w:abstractNum w:abstractNumId=\"" + QString::number(aid) + "\">\n"
               "    <w:multiLevelType w:val=\"hybridMultilevel\"/>\n";
        for (int lvl = 0; lvl <= s.maxListIlvl; ++lvl) {
            xml += "    <w:lvl w:ilvl=\"" + QString::number(lvl) + "\">\n"
                   "      <w:start w:val=\"1\"/>\n"
                   "      <w:numFmt w:val=\"decimal\"/>\n"
                   "      <w:lvlText w:val=\"%" + QString::number(lvl + 1) + ".\"/>\n"
                   "      <w:lvlJc w:val=\"left\"/>\n"
                   "      <w:pPr><w:ind w:left=\"" + QString::number(720 + lvl * 720)
                   + "\" w:hanging=\"360\"/></w:pPr>\n"
                   "    </w:lvl>\n";
        }
        xml += "  </w:abstractNum>\n";
    }
    // 每个列表一个独立 <w:num>（numId=1..N 指向各自的 abstractNum）。有序列表各引用
    // 独立 abstractNum，计数器天然隔离，无需 lvlOverride/startOverride。
    for (int i = 0; i < s.numAbstractNums.size(); ++i) {
        xml += QString("  <w:num w:numId=\"%1\"><w:abstractNumId w:val=\"%2\"/></w:num>\n")
                   .arg(i + 1)
                   .arg(s.numAbstractNums[i]);
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
    // DOCX 页眉页脚开关（独立开关默认开，与 PDF 的 pdfHeader/FooterEnabled 互不影响；
    // 标题取导出文件名基名，与 PDF 同源）。开关/标题声明在下方 settings 块之外——
    // 块是 {} 局部作用域，块外的 rels/contentTypes/zip 段还要用这三个值。
    bool headerEnabled = true;
    bool footerEnabled = true;
    QString headerTitle;
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
        // 图片页高容纳上限：与 PDF 导出的 capH 同源（页高 − 上下边距 − 2mm 安全间隙）。
        // Word 不会把内联图片跨页排版，超高直接裁掉；纵向超长 mermaid 图（TB 数十节点）
        // 宽度远小于页宽而高度数倍于页高，必须等比缩进一页内才完整可见。
        const double capHMm = pageH - (mt + mb) * 10.0 - 2.0;
        state.maxImageHeightEmu = capHMm > 0 ? static_cast<int>(capHMm * 36000.0) : 0;
        const auto mm2twips = [](double mm) { return static_cast<int>(qRound(mm * 56.6929134)); };
        headerEnabled = settings.value("export/docxHeaderEnabled", true).toBool();
        footerEnabled = settings.value("export/docxFooterEnabled", true).toBool();
        headerTitle = QFileInfo(outputPath).completeBaseName();
        if (headerTitle.isEmpty()) headerEnabled = false;   // 防御：无标题不产页眉部件
        // CT_SectPr schema 要求 headerReference/footerReference（EG_HdrFtrReferences）
        // 位于 pgSz 之前，故引用前置（全关时 hdrFtrRefs 为空串，产物与改造前等价）
        QString hdrFtrRefs;
        if (headerEnabled)
            hdrFtrRefs += "<w:headerReference w:type=\"default\" r:id=\"rIdHeader1\"/>";
        if (footerEnabled)
            hdrFtrRefs += "<w:footerReference w:type=\"default\" r:id=\"rIdFooter1\"/>";
        state.sectPrXml = QStringLiteral(
            "<w:sectPr>%1<w:pgSz w:w=\"%2\" w:h=\"%3\"/><w:pgMar w:top=\"%4\" w:right=\"%5\" "
            "w:bottom=\"%6\" w:left=\"%7\" w:header=\"720\" w:footer=\"720\" w:gutter=\"0\"/></w:sectPr>")
            .arg(hdrFtrRefs)
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
    if (headerEnabled)
        docRelsXml += "  <Relationship Id=\"rIdHeader1\""
            " Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/header\""
            " Target=\"header1.xml\"/>\n";
    if (footerEnabled)
        docRelsXml += "  <Relationship Id=\"rIdFooter1\""
            " Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/footer\""
            " Target=\"footer1.xml\"/>\n";
    if (!state.footnoteOrder.isEmpty())
        docRelsXml += "  <Relationship Id=\"rIdFootnotes\""
            " Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/footnotes\""
            " Target=\"footnotes.xml\"/>\n";
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
    if (headerEnabled)
        contentTypes += "  <Override PartName=\"/word/header1.xml\""
            " ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.header+xml\"/>\n";
    if (footerEnabled)
        contentTypes += "  <Override PartName=\"/word/footer1.xml\""
            " ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.footer+xml\"/>\n";
    if (!state.footnoteOrder.isEmpty())
        contentTypes += "  <Override PartName=\"/word/footnotes.xml\""
            " ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.footnotes+xml\"/>\n";
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
    // 注意：不得写 ODF 式 mimetype 首条目——DOCX 是 OPC 包，mimetype 部件无扩展名
    // 又未在 [Content_Types].xml 声明，属内容类型无法解析的部件，Word 会弹
    // 「发现无法读取的内容」修复框（WPS/LibreOffice 宽容不报）。
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

    // OPC 包首条目为 [Content_Types].xml，无 ODF 式 mimetype（见上方注释）
    add("[Content_Types].xml", contentTypes, MZ_BEST_COMPRESSION);
    add("_rels/.rels", rootRels, MZ_BEST_COMPRESSION);
    add("word/_rels/document.xml.rels", docRelsXml, MZ_BEST_COMPRESSION);
    add("word/document.xml", docXml, MZ_BEST_COMPRESSION);
    add("word/styles.xml", stylesXml, MZ_BEST_COMPRESSION);
    if (state.hasBulletList || state.hasDecimalList)
        add("word/numbering.xml", numberingXml, MZ_BEST_COMPRESSION);
    if (headerEnabled)
        add("word/header1.xml", makeHeaderXml(headerTitle).toUtf8(), MZ_BEST_COMPRESSION);
    if (footerEnabled)
        add("word/footer1.xml", makeFooterXml().toUtf8(), MZ_BEST_COMPRESSION);
    if (!state.footnoteOrder.isEmpty())
        add("word/footnotes.xml", makeFootnotesXml(state), MZ_BEST_COMPRESSION);
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
