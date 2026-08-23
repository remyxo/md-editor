#ifndef MD_EDITOR_FILEMANAGER_H
#define MD_EDITOR_FILEMANAGER_H

#include <QObject>
#include <QFileSystemWatcher>
#include <QHash>
#include <QByteArray>

class QTimer;

class FileManager : public QObject {
    Q_OBJECT
public:
    explicit FileManager(QObject *parent = nullptr);

    QString readFile(const QString &path, QString *encoding = nullptr);
    bool writeFile(const QString &path, const QString &content);

    // 文件内容解码（readFile 与 MainWindow 双击打开共用）：
    //   BOM → UTF-8（校验）→ 系统编码回退；换行归一化为 \n。
    // encoding 非空时回填检测到的编码标签（UTF-8 BOM / UTF-16 LE/BE / UTF-8 / GBK…）。
    static QString decodeFileContent(const QByteArray &bytes, QString *encoding = nullptr);

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
    // 按路径的去抖定时器：一次外部保存产生的事件突发合并为一次信号
    QHash<QString, QTimer *> m_changeTimers;
};

#endif // MD_EDITOR_FILEMANAGER_H
