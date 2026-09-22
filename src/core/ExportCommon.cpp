#include "ExportCommon.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMimeDatabase>
#include <QRegularExpression>

namespace ExportCommon {

QString readResource(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return QString();
    const QString s = QString::fromUtf8(f.readAll());
    f.close();
    return s;
}

QString htmlEscapeAttr(const QString &s)
{
    QString o = s;
    o.replace('&', QStringLiteral("&amp;"));
    o.replace('<', QStringLiteral("&lt;"));
    o.replace('>', QStringLiteral("&gt;"));
    o.replace('"', QStringLiteral("&quot;"));
    return o;
}

QString stripStyleClose(const QString &css)
{
    QString s = css;
    s.remove(QRegularExpression(QStringLiteral("</\\s*style\\s*>"),
                                QRegularExpression::CaseInsensitiveOption));
    return s;
}

bool sniffImage(const QByteArray &bytes, const ImagePolicy &policy, QString *mime)
{
    // 空字节不得内联（零字节文件 / 空 base64 会产出损坏图片）
    if (bytes.isEmpty() || bytes.size() > policy.maxBytes) return false;
    const QByteArray head = bytes.left(512);
    // svg：无可靠 magic，按 <svg 文本前缀判定（<img> 上下文的 svg 不执行脚本，
    // 可安全内联）。开启该策略时只认这条路径判出的 svg
    if (policy.svgByTextPrefix
        && (head.trimmed().startsWith("<svg") || head.contains("<svg"))) {
        if (mime) *mime = QStringLiteral("image/svg+xml");
        return true;
    }
    // 基于内容判定真实 MIME（不信任扩展名 / data: 前缀），再按策略白名单过滤
    QMimeDatabase db;
    const QString real = db.mimeTypeForData(bytes).name();
    if (!policy.mimeWhitelist.contains(real)) return false;
    if (mime) *mime = real;
    return true;
}

bool loadLocalImage(const QString &src, const QString &docDir, const ImagePolicy &policy,
                    QByteArray *bytes, QString *mime)
{
    // 围栏基准：未保存文档（docDir 空/不存在）不做本地读取——相对路径将落到
    // CWD，等于无围栏
    const QString canonDocDir = QFileInfo(docDir).canonicalFilePath();
    if (canonDocDir.isEmpty()) return false;
    const QString path = QDir::isAbsolutePath(src) ? src : QDir(docDir).filePath(src);
    // canonical 化解 ../ 与符号链接后必须仍在文档目录内：曾无任何校验，经
    // ![](../../.ssh/id_rsa) 可把目录外文件字节读入产物
    const QString canon = QFileInfo(path).canonicalFilePath();
    if (canon.isEmpty() || !canon.startsWith(canonDocDir + QLatin1Char('/')))
        return false;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;
    const QByteArray data = f.readAll();
    f.close();
    if (!sniffImage(data, policy, mime)) return false;
    if (bytes) *bytes = data;
    return true;
}

QString pageNumberText(int page, int total)
{
    return QString::fromUtf8(kPageTextPrefix) + QString::number(page)
         + QString::fromUtf8(kPageTextMid) + QString::number(total)
         + QString::fromUtf8(kPageTextSuffix);
}

QRegularExpression pageNumberVerifyPattern()
{
    // 「页」用通配（.）：字体 cmap 可能把该字提取成部首码位，视觉同形；
    // 与 kPageText* 的文案一致性由 test_pdfexporter 的漂移锁保证
    return QRegularExpression(QStringLiteral("第(\\d+)./共(\\d+)"));
}

QString mimeForExt(const QString &ext)
{
    const QString e = ext.toLower();
    if (e == "woff2") return QStringLiteral("font/woff2");
    if (e == "woff")  return QStringLiteral("font/woff");
    if (e == "ttf")   return QStringLiteral("font/ttf");
    if (e == "png")   return QStringLiteral("image/png");
    if (e == "jpg" || e == "jpeg") return QStringLiteral("image/jpeg");
    if (e == "gif")   return QStringLiteral("image/gif");
    if (e == "svg")   return QStringLiteral("image/svg+xml");
    if (e == "webp")  return QStringLiteral("image/webp");
    return QStringLiteral("application/octet-stream");
}

} // namespace ExportCommon
