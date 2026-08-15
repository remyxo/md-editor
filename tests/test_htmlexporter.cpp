#include <QTest>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include "../src/core/HtmlExporter.h"

class TestHtmlExporter : public QObject {
    Q_OBJECT
private slots:
    void testMimeForExt() {
        QCOMPARE(HtmlExporter::mimeForExt("woff2"), QStringLiteral("font/woff2"));
        QCOMPARE(HtmlExporter::mimeForExt("woff"), QStringLiteral("font/woff"));
        QCOMPARE(HtmlExporter::mimeForExt("ttf"), QStringLiteral("font/ttf"));
        QCOMPARE(HtmlExporter::mimeForExt("png"), QStringLiteral("image/png"));
        QCOMPARE(HtmlExporter::mimeForExt("jpg"), QStringLiteral("image/jpeg"));
        QCOMPARE(HtmlExporter::mimeForExt("jpeg"), QStringLiteral("image/jpeg"));
        QCOMPARE(HtmlExporter::mimeForExt("svg"), QStringLiteral("image/svg+xml"));
    }

    void testInlineCssFontsReplacesUrlWithBase64() {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        QString fontsDir = dir.path() + "/fonts";
        QVERIFY(QDir().mkpath(fontsDir));
        QFile f(fontsDir + "/KaTeX_Main-Regular.woff2");
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("FONTDATA"); f.close();

        QString css = "src:url(fonts/KaTeX_Main-Regular.woff2) format('woff2');";
        QString out = HtmlExporter::inlineCssFonts(css, dir.path() + "/fonts");
        QVERIFY(out.contains("data:font/woff2;base64,"));
        QVERIFY(!out.contains("url(fonts/"));
    }

    void testInlineImagesReplacesRelativeSrcWithBase64() {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        QFile f(dir.path() + "/a.png");
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("PNGDATA"); f.close();

        QString html = "<p><img src=\"a.png\" alt=\"x\"></p>";
        QString out = HtmlExporter::inlineImages(html, dir.path());
        QVERIFY(out.contains("data:image/png;base64,"));
        QVERIFY(!out.contains("src=\"a.png\""));
        QVERIFY(out.contains("alt=\"x\"")); // 其他属性保留
    }

    void testInlineImagesSkipsMissingFile() {
        QString html = "<p><img src=\"nope.png\"></p>";
        QString out = HtmlExporter::inlineImages(html, "/nonexistent");
        QVERIFY(out.contains("src=\"nope.png\"")); // 读不到时原样保留
    }

    void testInlineImagesHandlesSingleQuotedSrc() {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        QFile f(dir.path() + "/b.png");
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("PNG2"); f.close();

        QString html = "<p><img src='b.png'></p>";
        QString out = HtmlExporter::inlineImages(html, dir.path());
        QVERIFY(out.contains("data:image/png;base64,"));
        QVERIFY(!out.contains("src='b.png'"));
    }

    void testBodyCssDiffersByTheme() {
        QVERIFY(HtmlExporter::bodyCss("light").contains("background:"));
        QVERIFY(HtmlExporter::bodyCss("light") != HtmlExporter::bodyCss("dark"));
    }

    void testRenderScriptUsesTheme() {
        QVERIFY(HtmlExporter::renderScript("dark").contains("'dark'"));
        QVERIFY(HtmlExporter::renderScript("light").contains("'default'"));
    }
};

QTEST_APPLESS_MAIN(TestHtmlExporter)
#include "test_htmlexporter.moc"
