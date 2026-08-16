#include <QTest>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include "../src/core/PdfExporter.h"
#include "../src/core/HtmlExporter.h"

class TestPdfExporter : public QObject {
    Q_OBJECT
private slots:
    void testBuildRenderHtmlInlinesImages() {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        QFile f(dir.path() + "/a.png");
        QVERIFY(f.open(QIODevice::WriteOnly)); f.write("PNGDATA"); f.close();

        QString html = "<p><img src=\"a.png\" alt=\"x\"></p>";
        QString out = PdfExporter::buildRenderHtml(html, dir.path(), "", "");
        QVERIFY(out.contains("data:image/png;base64,"));
        QVERIFY(!out.contains("src=\"a.png\""));
    }

    void testBuildRenderHtmlKeepsAbsoluteImageAsIsWhenMissing() {
        // 读不到的文件保留原 src（不抛异常、不报错）
        QString html = "<p><img src=\"/nonexistent/zzz.jpg\"></p>";
        QString out = PdfExporter::buildRenderHtml(html, "/tmp", "", "");
        QVERIFY(out.contains("src=\"/nonexistent/zzz.jpg\""));
    }
};

QTEST_APPLESS_MAIN(TestPdfExporter)
#include "test_pdfexporter.moc"
