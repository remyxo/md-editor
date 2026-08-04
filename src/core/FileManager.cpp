#include "FileManager.h"
#include <QFile>
#include <QTextStream>
#include <QStringConverter>
#include <QFileInfo>
#include <QDir>

FileManager::FileManager(QObject *parent)
    : QObject(parent)
    , m_watcher(new QFileSystemWatcher(this))
    , m_autoSaveTimer(new QTimer(this))
    , m_settings("md-editor", "md-editor")
    , m_autoSaveEnabled(true)
    , m_autoSaveIntervalSec(30)
{
    connect(m_watcher, &QFileSystemWatcher::fileChanged,
            this, &FileManager::onFileChanged);
    connect(m_autoSaveTimer, &QTimer::timeout,
            this, &FileManager::onAutoSaveTimeout);
    loadSettings();
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

void FileManager::startAutoSave(const QString &filePath, const QString &content)
{
    m_autoSavePath = filePath;
    m_autoSaveContent = content;
    if (m_autoSaveEnabled && m_autoSaveIntervalSec > 0) {
        m_autoSaveTimer->start(m_autoSaveIntervalSec * 1000);
    }
}

void FileManager::stopAutoSave()
{
    m_autoSaveTimer->stop();
}

void FileManager::setAutoSaveEnabled(bool enabled)
{
    m_autoSaveEnabled = enabled;
    if (!enabled) {
        stopAutoSave();
    }
}

void FileManager::setAutoSaveInterval(int seconds)
{
    m_autoSaveIntervalSec = qBound(10, seconds, 300);
    if (m_autoSaveTimer->isActive()) {
        m_autoSaveTimer->setInterval(m_autoSaveIntervalSec * 1000);
    }
}

bool FileManager::isAutoSaveEnabled() const { return m_autoSaveEnabled; }
int FileManager::autoSaveInterval() const { return m_autoSaveIntervalSec; }

void FileManager::saveSettings()
{
    m_settings.setValue("autosave/enabled", m_autoSaveEnabled);
    m_settings.setValue("autosave/interval", m_autoSaveIntervalSec);
    m_settings.sync();
}

void FileManager::loadSettings()
{
    m_autoSaveEnabled = m_settings.value("autosave/enabled", true).toBool();
    m_autoSaveIntervalSec = m_settings.value("autosave/interval", 30).toInt();
}

void FileManager::onFileChanged(const QString &path)
{
    emit fileExternallyModified(path);
}

void FileManager::onAutoSaveTimeout()
{
    if (!m_autoSavePath.isEmpty()) {
        emit autoSaveTriggered(m_autoSavePath, m_autoSaveContent);
    }
}
