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
    QString baseName, ext;
    if (!suggestedName.isEmpty()) {
        QString full = QFileInfo(suggestedName).fileName(); // 防路径穿越
        ext = extFromName(full, fallbackExt);
        baseName = QFileInfo(full).completeBaseName();
        if (baseName.isEmpty()) baseName = QStringLiteral("image");
    } else {
        ext = fallbackExt.isEmpty() ? QStringLiteral("png") : fallbackExt;
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
