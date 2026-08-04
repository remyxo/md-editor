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

    void testAutoSaveSettings() {
        FileManager fm;
        fm.setAutoSaveEnabled(false);
        QVERIFY(!fm.isAutoSaveEnabled());

        fm.setAutoSaveInterval(60);
        QCOMPARE(fm.autoSaveInterval(), 60);

        // 边界测试
        fm.setAutoSaveInterval(5);  // 应被 clamp 到 10
        QCOMPARE(fm.autoSaveInterval(), 10);

        fm.setAutoSaveInterval(500); // 应被 clamp 到 300
        QCOMPARE(fm.autoSaveInterval(), 300);
    }
};

QTEST_MAIN(TestFileManager)
#include "test_filemanager.moc"
