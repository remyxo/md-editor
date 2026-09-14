#include "LocalImageSchemeHandler.h"

#include <QWebEngineUrlRequestJob>
#include <QFile>
#include <QFileInfo>
#include <QMimeDatabase>
#include <QMimeType>
#include <QUrl>
#include <QDebug>

LocalImageSchemeHandler::LocalImageSchemeHandler(QObject *parent)
    : QWebEngineUrlSchemeHandler(parent)
{
}

// 文档目录白名单：localimg:// 曾是全盘任意图片读取通道（相对 src 的 ../ 可
// 逃出 docDir，注入场景可枚举读取任意本地图片）。仅服务已打开文档目录内的
// 文件（canonical 前缀围栏；EditorTab::setFilePath 注册）
QSet<QString> LocalImageSchemeHandler::s_allowedDirs;
QHash<QString, int> LocalImageSchemeHandler::s_dirRefs;

void LocalImageSchemeHandler::addAllowedDir(const QString &dir)
{
    const QString canon = QFileInfo(dir).canonicalFilePath();
    if (!canon.isEmpty()) {
        s_allowedDirs.insert(canon);
        s_dirRefs[canon] = s_dirRefs.value(canon, 0) + 1;
    }
}

void LocalImageSchemeHandler::removeAllowedDir(const QString &dir)
{
    const QString canon = QFileInfo(dir).canonicalFilePath();
    if (canon.isEmpty())
        return;
    const int refs = s_dirRefs.value(canon, 0);
    if (refs <= 1) {
        s_dirRefs.remove(canon);
        s_allowedDirs.remove(canon);
    } else {
        s_dirRefs[canon] = refs - 1;
    }
}

void LocalImageSchemeHandler::requestStarted(QWebEngineUrlRequestJob *job)
{
    // localimg:// 的 path 部分为磁盘路径（URL 以 '/' 为分隔符）
    QString path = job->requestUrl().path();
#ifdef Q_OS_WIN
    // Windows 盘符绝对路径在 editor.js 的 toSlashPath 中前置了 '/'（规避 'C:' 被 QUrl 解析为 host）：
    // localimg:///C:/... 的 path() 为 /C:/...，去掉前置 '/' 还原为 C:/...。
    if (path.size() >= 3 && path[0] == QLatin1Char('/') && path[2] == QLatin1Char(':'))
        path.remove(0, 1);
#endif

    // 目录围栏：解析后（canonical 化解 ../ 与符号链接）必须落在某个已打开
    // 文档的目录内
    const QString canon = QFileInfo(path).canonicalFilePath();
    bool inside = false;
    for (const QString &dir : s_allowedDirs) {
        if (canon.startsWith(dir + QLatin1Char('/'))) {
            inside = true;
            break;
        }
    }
    if (!inside) {
        qWarning() << "LocalImageSchemeHandler: outside allowed doc dirs:" << path;
        job->fail(QWebEngineUrlRequestJob::RequestDenied);
        return;
    }

    QFileInfo info(path);
    // 仅服务已存在的常规文件
    if (!info.exists() || !info.isFile()) {
        qWarning() << "LocalImageSchemeHandler: not a regular file:" << path;
        job->fail(QWebEngineUrlRequestJob::UrlNotFound);
        return;
    }

    // 防御性：仅服务图片，拒绝非图片（避免被当作任意文件读取通道）
    QMimeDatabase db;
    QMimeType mime = db.mimeTypeForFile(info);
    if (!mime.name().startsWith("image/")) {
        qWarning() << "LocalImageSchemeHandler: not image (" << mime.name() << "):" << path;
        job->fail(QWebEngineUrlRequestJob::RequestDenied);
        return;
    }

    QFile *file = new QFile(path);
    if (!file->open(QIODevice::ReadOnly)) {
        qWarning() << "LocalImageSchemeHandler: open failed:" << path;
        delete file;
        job->fail(QWebEngineUrlRequestJob::UrlNotFound);
        return;
    }

    // reply 接管 device 所有权（结束时由 Qt 删除）
    job->reply(mime.name().toUtf8(), file);
}
