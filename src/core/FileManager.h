#ifndef MD_EDITOR_FILEMANAGER_H
#define MD_EDITOR_FILEMANAGER_H

#include <QObject>
#include <QTimer>
#include <QFileSystemWatcher>
#include <QSettings>

class FileManager : public QObject {
    Q_OBJECT
public:
    explicit FileManager(QObject *parent = nullptr);

    QString readFile(const QString &path);
    bool writeFile(const QString &path, const QString &content);

    void watchFile(const QString &path);
    void unwatchFile(const QString &path);

    void startAutoSave(const QString &filePath, const QString &content);
    void stopAutoSave();
    void setAutoSaveEnabled(bool enabled);
    void setAutoSaveInterval(int seconds);
    bool isAutoSaveEnabled() const;
    int autoSaveInterval() const;

    void saveSettings();
    void loadSettings();

signals:
    void fileExternallyModified(const QString &path);
    void autoSaveTriggered(const QString &filePath, const QString &content);
    void fileReadError(const QString &path, const QString &error);
    void fileWriteError(const QString &path, const QString &error);

private slots:
    void onFileChanged(const QString &path);
    void onAutoSaveTimeout();

private:
    QFileSystemWatcher *m_watcher;
    QTimer *m_autoSaveTimer;
    QSettings m_settings;
    QString m_autoSavePath;
    QString m_autoSaveContent;
    bool m_autoSaveEnabled;
    int m_autoSaveIntervalSec;
};

#endif // MD_EDITOR_FILEMANAGER_H
