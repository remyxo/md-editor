#ifndef MD_EDITOR_EXPORTCOMMON_H
#define MD_EDITOR_EXPORTCOMMON_H

#include <QByteArray>
#include <QRegularExpression>
#include <QString>
#include <QStringList>

// PDF / HTML / DOCX 三份导出器共用的最小工具集与常量。
//
// 收录原则：**语义完全一致、与媒介无关**的才收进来。打印侧的差异（分页、
// 媒体盒、Qt 边距模拟）、屏幕侧的差异（`max-width` 页容器、主题色）、Word
// 侧的差异（OOXML 单位、可嵌入格式白名单）各自留在原实现里——不为统一而
// 统一。改这里的任何东西都同时影响三份产物，必须三产物回归。
namespace ExportCommon {

// 资源读取（qrc/磁盘，UTF-8 文本）。失败返回空串——调用方按各自语义降级
//（HTML/PDF 视为「该库不内联」，产物仍可用）
QString readResource(const QString &path);

// HTML 属性值转义（& < > "）。拼进属性位置的文本必须过这里；实体解码的对侧
// 在 MarkdownHandler::unescapeHtmlEntities（往返保真的同一套规则）
QString htmlEscapeAttr(const QString &s);

// 剥离自定义 CSS 中的 </style> 序列：本地设置携带它会把 <style> 块提前终结、
// 向产物注入任意标记（自注入面）。大小写不敏感、容忍空白
QString stripStyleClose(const QString &css);

// 图片内联白名单：**两条导出路径的能力不同**（HTML 产物可嵌 svg/webp，Word
// 只认 png/jpeg/gif），策略由调用方给；体积上限两侧同值
struct ImagePolicy {
    QStringList mimeWhitelist;      // 走 QMimeDatabase 内容判定时允许的 MIME
    qint64 maxBytes = 50 * 1024 * 1024;
    // svg 无可靠 magic（文本 XML），按 <svg 文本前缀判定。开启后**只认**这条
    // 路径判出的 svg（与既有行为一致：DB 判成 svg 而文本前缀不符者仍拒）
    bool svgByTextPrefix = false;
};

// 内容嗅探：非空 + 体积上限内 + 真实 MIME（不信任扩展名与 data: 前缀）+
// 策略白名单。通过则写出 mime 并返回 true
bool sniffImage(const QByteArray &bytes, const ImagePolicy &policy, QString *mime);

// 本地图片读取：canonical 围栏（解析 ../ 与符号链接后必须仍在 docDir 内；
// docDir 空/不存在 = 未保存文档，一律拒绝——相对路径将落到 CWD，等于无围栏）
// → 读取 → sniffImage
bool loadLocalImage(const QString &src, const QString &docDir,
                    const ImagePolicy &policy, QByteArray *bytes, QString *mime);

// 扩展名 → MIME（内联字体/图片 data: 头用；未知名回退 application/octet-stream）
QString mimeForExt(const QString &ext);

// ── 页码文案（「第 X 页 / 共 Y 页」）单一来源 ─────────────────────────
// 四处由这三段派生：paged 页边距盒（CSS content）、nativeHf 覆盖层（JS
// textContent）、成品校验正则、DOCX 页脚字段。**改文案而不改校验正则** →
// nativeHf 每份产物判「页码漂移」→ 静默回退 paged.js（慢但正确，且无报错指向
// 真因）。test_pdfexporter 有「正则必须匹配文案」的漂移锁，改文案即红。
inline constexpr const char *kPageTextPrefix = "第 ";       // 前接页码数字
inline constexpr const char *kPageTextMid = " 页 / 共 ";    // 中接总页数
inline constexpr const char *kPageTextSuffix = " 页";       // 尾
// 运行时文案（C++ 侧与测试用；JS 侧按同三段拼接）：「第 3 页 / 共 11 页」
QString pageNumberText(int page, int total);
// 成品校验正则：匹配**剥离空白后**的提取文本。页码用 \d+；「页」用通配而非
// 定字——字体 cmap 可能把该字提取成部首码位（视觉同形）
QRegularExpression pageNumberVerifyPattern();

// 标度常量：单一定义，勿再各处裸写
constexpr qreal kEmuPerPx = 9525.0;      // 96dpi：1px = 9525 EMU（OOXML 图尺寸）
constexpr qreal kMmToPx   = 96.0 / 25.4; // mm → CSS px
constexpr qreal kMmToPt   = 72.0 / 25.4; // mm → pt

// ── DOCX 页面几何（表格列宽归一基准）──────────────────────────────
// 页内容宽（twips）= 纸张宽 − 左右边距。QSettings 键与 DocxExporter writeDocx
// 的 sectPr 声明同源（export/paperSize、export/orientation、export/marginLeft、
// export/marginRight，默认 A4 竖版 1.8cm）。TableColumnNormalizer 的 avail 与
// DocxExporter 的 tblW/tblGrid 共用本值——改导出设置两侧同步变
int pageContentWidthTwips();

} // namespace ExportCommon

#endif // MD_EDITOR_EXPORTCOMMON_H
