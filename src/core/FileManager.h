#ifndef MD_EDITOR_FILEMANAGER_H
#define MD_EDITOR_FILEMANAGER_H

#include <QObject>
#include <QFileSystemWatcher>

class FileManager : public QObject {
    Q_OBJECT
public:
    explicit FileManager(QObject *parent = nullptr);

    QString readFile(const QString &path);
    bool writeFile(const QString &path, const QString &content);

    void watchFile(const QString &path);
    void unwatchFile(const QString &path);

signals:
    void fileExternallyModified(const QString &path);
    void fileReadError(const QString &path, const QString &error);
    void fileWriteError(const QString &path, const QString &error);

private slots:
    void onFileChanged(const QString &path);

private:
    // 文件暂不存在时的有限次重试监控（删除后重建等场景）
    void retryWatch(const QString &path, int attemptsLeft);

    QFileSystemWatcher *m_watcher;
};

#endif // MD_EDITOR_FILEMANAGER_H
