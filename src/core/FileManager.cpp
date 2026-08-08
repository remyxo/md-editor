#include "FileManager.h"
#include <QFile>
#include <QTextStream>
#include <QStringConverter>
#include <QFileInfo>
#include <QDir>
#include <QTimer>

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
    // 替换式外部保存（vim/sed -i/git checkout 等：写临时文件 + rename 覆盖）
    // 会使被监控文件的 inode 消失，QFileSystemWatcher 随之自动把该路径移出
    // 监控列表。此处必须重新加入监控，否则后续修改将永久无法感知。
    if (QFile::exists(path)) {
        // 守卫：就地修改（inode 不变）时路径仍在监控列表中，
        // 重复 addPath 会触发 qWarning
        if (!m_watcher->files().contains(path)) {
            m_watcher->addPath(path);
        }
    } else {
        // 文件暂不存在（被删除/移走、外部工具重建中）：有限次重试
        retryWatch(path, 25); // 25 × 200ms ≈ 5 秒
    }

    emit fileExternallyModified(path);
}

void FileManager::retryWatch(const QString &path, int attemptsLeft)
{
    if (attemptsLeft <= 0) {
        return; // 超时放弃，不做无限轮询
    }
    QTimer::singleShot(200, this, [this, path, attemptsLeft]() {
        if (QFile::exists(path)) {
            if (!m_watcher->files().contains(path)) {
                m_watcher->addPath(path);
            }
        } else {
            retryWatch(path, attemptsLeft - 1);
        }
    });
}
