#include <QTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QFile>
#include <cstdio>
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

    void testWatchSurvivesExternalReplace() {
        // 替换式外部保存（vim/sed -i/git checkout 等：写临时文件 + 原子 rename 覆盖）
        // 会导致被监控文件的 inode 被替换，QFileSystemWatcher 随之丢失监控。
        // 本测试验证：首次替换触发信号后，监控应自动恢复，后续修改仍能感知。
        FileManager fm;
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        QString path = dir.path() + "/test.md";
        QVERIFY(fm.writeFile(path, "# v1"));

        QSignalSpy spy(&fm, &FileManager::fileExternallyModified);
        fm.watchFile(path);

        // 模拟 vim 式保存：写临时文件后 std::rename 原子覆盖（inode 被替换）
        QString tmpPath = path + ".swx";
        {
            QFile f(tmpPath);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("# v2");
            f.close();
        }
        QCOMPARE(std::rename(tmpPath.toLocal8Bit().constData(),
                             path.toLocal8Bit().constData()), 0);

        // 首次替换应触发信号
        QVERIFY2(spy.wait(3000), "替换式外部修改应触发 fileExternallyModified");

        // 第二次：就地修改（新 inode）。若监控在替换后丢失，此处永远等不到信号
        {
            QFile f(path);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("# v3");
            f.close();
        }
        QVERIFY2(spy.wait(3000),
                 "替换式修改后监控丢失：后续外部修改无法感知（需重新加入监控）");
    }

    void testSaveBurstCoalesced() {
        // 一次外部保存会在毫秒级内产生多个文件系统事件
        // （如 vim：rename 覆盖 + 新文件继续写入/恢复权限 mtime）。
        // 监控恢复后这些事件都会送达，应合并为一次信号，避免重复弹窗。
        FileManager fm;
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        QString path = dir.path() + "/test.md";
        QVERIFY(fm.writeFile(path, "# v1"));

        QSignalSpy spy(&fm, &FileManager::fileExternallyModified);
        fm.watchFile(path);

        // 替换式保存：原子 rename 覆盖
        QString tmpPath = path + ".swx";
        {
            QFile f(tmpPath);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("# v2");
            f.close();
        }
        QCOMPARE(std::rename(tmpPath.toLocal8Bit().constData(),
                             path.toLocal8Bit().constData()), 0);

        // 先让事件循环处理首个事件（监控恢复到新 inode），
        // 再模拟同一次保存的后续写入 → 第二个事件
        QTest::qWait(100);
        {
            QFile f(path);
            QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Append));
            f.write("\nmore");
            f.close();
        }

        // 等待越过去抖窗口：一次保存只应产生一次信号
        QTest::qWait(800);
        QVERIFY2(spy.count() == 1,
                 qPrintable(QString("一次外部保存的多个事件应合并为一次信号，实际 %1 次")
                                .arg(spy.count())));
    }
};

QTEST_MAIN(TestFileManager)
#include "test_filemanager.moc"
