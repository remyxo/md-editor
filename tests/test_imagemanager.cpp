#include <QTest>
#include <QTemporaryDir>
#include <QFile>
#include <QFileInfo>
#include "../src/core/ImageManager.h"

class TestImageManager : public QObject {
    Q_OBJECT
private slots:
    // 带建议名：按建议名写入，返回相对路径，字节一致
    void testSuggestedNameWritesFile() {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        QString docPath = dir.path() + "/doc.md";
        QByteArray bytes("PNGBYTES");

        QString rel = ImageManager::saveImage(docPath, bytes, "photo.png");

        QCOMPARE(rel, QStringLiteral("images/photo.png"));
        QString abs = dir.path() + "/images/photo.png";
        QVERIFY(QFile::exists(abs));
        QFile f(abs); QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), bytes);
    }

    // 自动创建 images/ 子目录
    void testCreatesImagesDir() {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        QString docPath = dir.path() + "/doc.md";
        QVERIFY(!QFileInfo::exists(dir.path() + "/images"));

        QString rel = ImageManager::saveImage(docPath, "X", "a.png");

        QCOMPARE(rel, QStringLiteral("images/a.png"));
        QVERIFY(QFileInfo::exists(dir.path() + "/images"));
    }

    // 重名追加 -2/-3 后缀
    void testCollisionSuffix() {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        QString docPath = dir.path() + "/doc.md";

        QCOMPARE(ImageManager::saveImage(docPath, "1", "dup.png"), QStringLiteral("images/dup.png"));
        QCOMPARE(ImageManager::saveImage(docPath, "2", "dup.png"), QStringLiteral("images/dup-2.png"));
        QCOMPARE(ImageManager::saveImage(docPath, "3", "dup.png"), QStringLiteral("images/dup-3.png"));
        QVERIFY(QFile::exists(dir.path() + "/images/dup-3.png"));
    }

    // 无建议名（剪贴板粘贴）：用 image-<时间戳>.<ext>
    void testEmptySuggestedUsesTimestamp() {
        QTemporaryDir dir; QVERIFY(dir.isValid());

        QString rel = ImageManager::saveImage(dir.path() + "/doc.md", "bytes", "", "png");

        QVERIFY2(rel.startsWith(QStringLiteral("images/image-")),
                 qPrintable("期望以 images/image- 开头，实际: " + rel));
        QVERIFY(rel.endsWith(QStringLiteral(".png")));
        QVERIFY(QFile::exists(dir.path() + "/" + rel));
    }

    // 空字节：返回空，不建目录
    void testEmptyBytesReturnsEmpty() {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        QString rel = ImageManager::saveImage(dir.path() + "/doc.md", "", "x.png");
        QVERIFY(rel.isEmpty());
        QVERIFY(!QFileInfo::exists(dir.path() + "/images"));
    }

    // fallbackExt 由 MIME 决定（jpg）
    void testFallbackExtFromMime() {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        QString rel = ImageManager::saveImage(dir.path() + "/doc.md", "B", "", "jpg");
        QVERIFY(rel.endsWith(QStringLiteral(".jpg")));
    }
};

QTEST_MAIN(TestImageManager)
#include "test_imagemanager.moc"
