#include "ImageManager.h"
#include <QFileInfo>
#include <QDir>
#include <QDateTime>
#include <QFile>

// 从文件名取扩展名（小写无点）；无则用 fallback
static QString extFromName(const QString &name, const QString &fallback)
{
    int dot = name.lastIndexOf(QLatin1Char('.'));
    if (dot >= 0 && dot < name.length() - 1)
        return name.mid(dot + 1).toLower();
    return fallback;
}

QString ImageManager::saveImage(const QString &docFilePath, const QByteArray &bytes,
                                const QString &suggestedName, const QString &fallbackExt)
{
    if (bytes.isEmpty()) return QString();

    QFileInfo docInfo(docFilePath);
    QDir imagesDir(docInfo.absolutePath() + QStringLiteral("/images"));
    if (!imagesDir.exists() && !imagesDir.mkpath(QStringLiteral(".")))
        return QString();

    // 决定主名与扩展名
    // 扩展名白名单（ext 可能来自 JS 侧 fallbackExt，注入场景可拼 ../ 逃出
    // images/ 目录；白名单后任何非图片扩展一律回落 png）
    static const QStringList kAllowedExt = {
        QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("jpeg"),
        QStringLiteral("gif"), QStringLiteral("webp"), QStringLiteral("bmp"),
        QStringLiteral("svg"),
    };
    auto sanitizeExt = [](QString e) -> QString {
        e = e.toLower();
        return kAllowedExt.contains(e) ? e : QStringLiteral("png");
    };
    QString baseName, ext;
    if (!suggestedName.isEmpty()) {
        QString full = QFileInfo(suggestedName).fileName(); // 防路径穿越
        ext = sanitizeExt(extFromName(full, fallbackExt));
        baseName = QFileInfo(full).completeBaseName();
        if (baseName.isEmpty()) baseName = QStringLiteral("image");
    } else {
        ext = fallbackExt.isEmpty() ? QStringLiteral("png") : sanitizeExt(fallbackExt);
        baseName = QStringLiteral("image-")
                   + QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz"));
    }

    // 碰撞处理：photo.png → photo-2.png → photo-3.png ...
    QString fileName = baseName + QLatin1Char('.') + ext;
    for (int counter = 2; imagesDir.exists(fileName); ++counter)
        fileName = baseName + QStringLiteral("-%1.").arg(counter) + ext;

    // 原子写：临时文件 + rename（同文件系统内）
    QString tmpPath = imagesDir.absoluteFilePath(fileName) + QStringLiteral(".tmp");
    {
        QFile f(tmpPath);
        if (!f.open(QIODevice::WriteOnly)) return QString();
        if (f.write(bytes) != bytes.size()) { f.close(); QFile::remove(tmpPath); return QString(); }
        f.close();
    }
    QString absPath = imagesDir.absoluteFilePath(fileName);
    if (!QFile::rename(tmpPath, absPath)) { QFile::remove(tmpPath); return QString(); }

    return QStringLiteral("images/") + fileName;
}
