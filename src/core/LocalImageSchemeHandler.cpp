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

void LocalImageSchemeHandler::requestStarted(QWebEngineUrlRequestJob *job)
{
    // localimg:// 的 path 部分为磁盘绝对路径（/home/.../x.jpg）
    QString path = job->requestUrl().path();

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
