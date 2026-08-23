#include "FileManager.h"
#include <QFile>
#include <QTextStream>
#include <QStringConverter>
#include <QStringDecoder>
#include <QFileInfo>
#include <QDir>
#include <QTimer>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

// 外部变更事件去抖窗口：一次保存产生的多个事件在此窗口内合并为一次信号
static const int kExternalChangeDebounceMs = 300;

// UTF-8 BOM 字节（EF BB BF）
static const QByteArray kUtf8Bom = QByteArray::fromHex("efbbbf");

// 系统默认编码名（无 BOM 且非法 UTF-8 时回退所用编码），用于状态栏显示。
// Windows 中文系统 ANSI 代码页为 936（GBK）；Linux/macOS 恒为 UTF-8。
static QString systemEncodingName()
{
#ifdef Q_OS_WIN
    switch (GetACP()) {
    case 936:   return QStringLiteral("GBK");
    case 54936: return QStringLiteral("GB18030");
    case 65001: return QStringLiteral("UTF-8");
    case 932:   return QStringLiteral("Shift-JIS");
    case 949:   return QStringLiteral("EUC-KR");
    default:    return QStringLiteral("CP%1").arg(GetACP());
    }
#else
    return QStringLiteral("UTF-8");
#endif
}

FileManager::FileManager(QObject *parent)
    : QObject(parent)
    , m_watcher(new QFileSystemWatcher(this))
{
    connect(m_watcher, &QFileSystemWatcher::fileChanged,
            this, &FileManager::onFileChanged);
}

QString FileManager::decodeFileContent(const QByteArray &bytes, QString *encoding)
{
    if (encoding) {
        *encoding = QString();
        if (!bytes.isEmpty()) {
            if (bytes.startsWith(kUtf8Bom))
                *encoding = QStringLiteral("UTF-8 BOM");
            else if (bytes.startsWith("\xFF\xFE"))
                *encoding = QStringLiteral("UTF-16 LE");
            else if (bytes.startsWith("\xFE\xFF"))
                *encoding = QStringLiteral("UTF-16 BE");
            else {
                // 无 BOM：合法 UTF-8 记 UTF-8；否则回退系统编码（中文 Windows 为 GBK）
                QStringDecoder utf8(QStringConverter::Utf8);
                utf8(bytes);
                *encoding = utf8.hasError() ? systemEncodingName()
                                            : QStringLiteral("UTF-8");
            }
        }
    }

    if (bytes.isEmpty()) return QString();

    QString content;
    if (bytes.startsWith(kUtf8Bom)) {
        // UTF-8 BOM：剥离后按 UTF-8 解码
        content = QString::fromUtf8(bytes.constData() + 3, bytes.size() - 3);
    } else if (bytes.startsWith("\xFF\xFE") || bytes.startsWith("\xFE\xFF")) {
        // UTF-16 BOM：QStringConverter::Utf16 自动按 BOM 定序并剥离
        content = QStringDecoder(QStringConverter::Utf16)(bytes);
    } else {
        // 无 BOM：先按 UTF-8 解码并校验；无效（如中文 Windows 的 GBK 文件）回退系统编码
        QStringDecoder utf8(QStringConverter::Utf8);
        content = utf8(bytes);
        if (utf8.hasError())
            content = QStringDecoder(QStringConverter::System)(bytes);
    }

    // 换行归一化为 \n（等价于原 QIODevice::Text 模式读入）
    content.replace("\r\n", "\n");
    content.replace('\r', '\n');
    return content;
}

QString FileManager::readFile(const QString &path, QString *encoding)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        emit fileReadError(path, file.errorString());
        return {};
    }
    QByteArray bytes = file.readAll();
    file.close();
    return decodeFileContent(bytes, encoding);
}

bool FileManager::writeFile(const QString &path, const QString &content)
{
    // 临时取消监控，防止自身写入触发外部变更信号
    unwatchFile(path);

    // 保留原文件的 UTF-8 BOM 状态：Windows 记事本等创建的文件带 BOM，跨平台工具多不带。
    // 读原文件头 3 字节判断（原文件不存在——另存为新文件——则不带 BOM）。
    bool writeBom = false;
    if (QFile::exists(path)) {
        QFile orig(path);
        if (orig.open(QIODevice::ReadOnly)) {
            writeBom = (orig.read(3) == kUtf8Bom);
            orig.close();
        }
    }

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
    if (writeBom)
        out.setGenerateByteOrderMark(true);
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
    // 取消该路径待发出去的外部变更通知
    // （关闭标签页 / 自身 writeFile 前后不应再产生提示）
    if (QTimer *timer = m_changeTimers.take(path)) {
        timer->stop();
        timer->deleteLater();
    }
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

    // 去抖合并：一次外部保存（vim 备份 rename、新文件续写、权限/mtime
    // 恢复等）会在毫秒级内产生多个文件系统事件，只应提示一次。
    // 每个事件重启定时器，窗口内无新事件后才发出信号。
    QTimer *timer = m_changeTimers.value(path);
    if (!timer) {
        timer = new QTimer(this);
        timer->setSingleShot(true);
        timer->setInterval(kExternalChangeDebounceMs);
        connect(timer, &QTimer::timeout, this, [this, path]() {
            QTimer *t = m_changeTimers.take(path);
            emit fileExternallyModified(path);
            if (t) {
                t->deleteLater();
            }
        });
        m_changeTimers.insert(path, timer);
    }
    timer->start();
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
