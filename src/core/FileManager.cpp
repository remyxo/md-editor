#include "FileManager.h"
#include <QFile>
#include <QTextStream>
#include <QStringConverter>
#include <QFileInfo>
#include <QDir>

FileManager::FileManager(QObject *parent)
    : QObject(parent)
    , m_watcher(new QFileSystemWatcher(this))
{
    connect(m_watcher, &QFileSystemWatcher::fileChanged,
            this, &FileManager::onFileChanged);
}

QString FileManager::readFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        emit fileReadError(path, file.errorString());
        return {};
    }
    QTextStream in(&file);
    // 统一按 UTF-8 解码
    in.setEncoding(QStringConverter::Utf8);
    QString content = in.readAll();
    file.close();
    return content;
}

bool FileManager::writeFile(const QString &path, const QString &content)
{
    // 临时取消监控，防止自身写入触发外部变更信号
    unwatchFile(path);

    // 先写入临时文件，成功后重命名（原子写入）
    QString tmpPath = path + ".tmp";
    QFile file(tmpPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        emit fileWriteError(path, file.errorString());
        return false;
    }

    QTextStream out(&file);
    // 统一按 UTF-8 编码
    out.setEncoding(QStringConverter::Utf8);
    out << content;
    file.close();

    // 原子替换
    if (QFile::exists(path)) {
        QFile::remove(path);
    }
    if (!QFile::rename(tmpPath, path)) {
        QFile::remove(tmpPath);
        emit fileWriteError(path, "写入失败：无法替换原文件");
        return false;
    }

    // 保存成功后重新监控
    watchFile(path);
    return true;
}

void FileManager::watchFile(const QString &path)
{
    if (QFile::exists(path)) {
        m_watcher->addPath(path);
    }
}

void FileManager::unwatchFile(const QString &path)
{
    m_watcher->removePath(path);
}

void FileManager::onFileChanged(const QString &path)
{
    emit fileExternallyModified(path);
}
