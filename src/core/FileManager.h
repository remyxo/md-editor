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

    // 读入大小守卫阈值（MainWindow 软警告确认框复用同一常量）：
    // 编辑管道内存放大显著（bytes → QString(UTF-16) → QTextDocument MD→HTML
    // → QWebChannel 序列化 → ProseMirror 文档，合计约 10×），误开大文件/
    // 二进制会 UI 假死甚至 OOM；正常 Markdown 文档（百万字书稿）纯文本 ~2MB。
    static constexpr qint64 kLargeFileWarnBytes = 2 * 1024 * 1024;   // ≥2MB：提示可能卡顿，可继续
    static constexpr qint64 kMaxReadBytes = 16 * 1024 * 1024;        // >16MB：拒绝读入

    QString readFile(const QString &path, QString *encoding = nullptr);
    bool writeFile(const QString &path, const QString &content);

    // 文件内容解码（readFile 与 MainWindow 双击打开共用）：
    //   BOM → UTF-8（校验）→ 系统编码回退；换行归一化为 \n。
    // encoding 非空时回填检测到的编码标签（UTF-8 BOM / UTF-16 LE/BE / UTF-8 / GBK…）。
    static QString decodeFileContent(const QByteArray &bytes, QString *encoding = nullptr);

    // 头部（前 8KB）含 NUL 且非 UTF-16 → 判为二进制（与 readFile 拒读同逻辑）。
    // 打不开（不存在/权限）返回 false，交由 readFile 报具体错误
    static bool isBinaryFile(const QString &path);

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
