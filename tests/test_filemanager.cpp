#include <QTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QFile>
#include "../src/core/FileManager.h"

class TestFileManager : public QObject {
    Q_OBJECT
private slots:
    void testWriteAndRead() {
        FileManager fm;
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        QString path = dir.path() + "/test.md";
        QString content = "# 标题\n\n测试内容。";

        QVERIFY(fm.writeFile(path, content));
        QVERIFY(QFile::exists(path));

        QString read = fm.readFile(path);
        QCOMPARE(read, content);
    }

    void testReadNonExistent() {
        FileManager fm;
        QSignalSpy spy(&fm, &FileManager::fileReadError);
        QString result = fm.readFile("/nonexistent/path.md");
        QVERIFY(result.isEmpty());
        QCOMPARE(spy.count(), 1);
    }

    void testExternalChangeSignal() {
        FileManager fm;
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        QString path = dir.path() + "/test.md";
        QVERIFY(fm.writeFile(path, "# test"));

        QSignalSpy spy(&fm, &FileManager::fileExternallyModified);
        fm.watchFile(path);

        // 外部修改文件
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
        f.write("# modified");
        f.close();

        // QFileSystemWatcher 是异步的，轮询等待
        QTest::qWait(500);
        QVERIFY(spy.count() >= 1);
    }

    void testUnwatchFile() {
        FileManager fm;
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        QString path = dir.path() + "/test.md";
        QVERIFY(fm.writeFile(path, "# test"));

        QSignalSpy spy(&fm, &FileManager::fileExternallyModified);
        fm.watchFile(path);
        fm.unwatchFile(path);

        // 取消监控后修改不应触发信号
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
        f.write("# modified");
        f.close();

        QTest::qWait(500);
        QCOMPARE(spy.count(), 0);
    }

    void testSelfWriteSuppression() {
        // writeFile 内部重新 watchFile，所以自身写入不应触发外部修改信号
        FileManager fm;
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        QString path = dir.path() + "/test.md";
        QVERIFY(fm.writeFile(path, "# test"));

        QSignalSpy spy(&fm, &FileManager::fileExternallyModified);
        fm.watchFile(path);

        // 通过 FileManager 写入
        QVERIFY(fm.writeFile(path, "# self write"));

        QTest::qWait(500);
        // 自身写入应抑制信号（writeFile 内部 unwatch → write → watch）
        QCOMPARE(spy.count(), 0);
    }
};

QTEST_MAIN(TestFileManager)
#include "test_filemanager.moc"
