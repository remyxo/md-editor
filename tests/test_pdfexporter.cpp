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

    void testBuildRenderHtmlInjectsMermaid() {
        QString out = PdfExporter::buildRenderHtml("<p>x</p>", "/tmp", "", "");
        QVERIFY(out.contains("mermaid.min.js"));
        QVERIFY(out.contains("__renderAllMermaid"));
    }

    void testBuildRenderHtmlIncludesTaskListCss() {
        // 任务列表复选框与文字须同行：直接 li 用 flex 并排（复选框一行、文字一行）。
        // 必须用子代选择器 > 限定直接 li——嵌套在任务项内的通用列表 li（如 列表2-2
        // 下再嵌 <ol>）若也命中 flex 会被当成 flex 容器，其子元素 p 与嵌套 <ol>
        // 排成一行、序号消失（导出嵌套列表回归）。
        QString out = PdfExporter::buildRenderHtml("<p>x</p>", "/tmp", "", "");
        QVERIFY(out.contains("ul[data-type=\"taskList\"] > li{display:flex;align-items:flex-start;gap:8px;}"));
        QVERIFY(out.contains("ul[data-type=\"taskList\"] > li > div > p{margin:0;}"));
    }

    void testBuildRenderHtmlPagedInjectsPageRules() {
        PdfPageSpec s = specWithHeader();
        QString out = PdfExporter::buildRenderHtml("<p>x</p>", "/tmp", "", "", s);
        QVERIFY(out.contains("@top-center"));
        QVERIFY(out.contains("我的文档"));
        QVERIFY(out.contains("@bottom-right"));
        QVERIFY(out.contains("counter(page)"));
        QVERIFY(out.contains("counter(pages)"));
        QVERIFY(out.contains("paged.polyfill.js"));
        QVERIFY(out.contains("PagedConfig={auto:false}"));
        QVERIFY(out.contains("window.__preparePrint"));
        // paged 激活时不得再有旧 @page{margin:0}（后位级联会覆盖新 margin）
        QVERIFY(!out.contains("@page{margin:0;}"));
    }

    void testBuildRenderHtmlLegacyPathUnchanged() {
        // 页眉页脚全关 → 不引入 paged.js，走原管线（@page{margin:0} 保留）
        QString out = PdfExporter::buildRenderHtml("<p>x</p>", "/tmp", "", "");
        QVERIFY(!out.contains("paged.polyfill.js"));
        QVERIFY(out.contains("@page{margin:0;}"));
        QVERIFY(out.contains("window.__preparePrint"));   // 打印前置入口两种路径都在
    }

    void testBuildRenderHtmlHeaderOnly() {
        PdfPageSpec s = specWithHeader(); s.footerEnabled = false;
        QString out = PdfExporter::buildRenderHtml("<p>x</p>", "/tmp", "", "", s);
        QVERIFY(out.contains("@top-center"));
        QVERIFY(!out.contains("@bottom-right"));
        QVERIFY(out.contains("paged.polyfill.js"));
    }

    void testBuildRenderHtmlPaperOrientationMargins() {
        PdfPageSpec s = specWithHeader();
        s.paper = QPageSize::A5;
        s.orientation = QPageLayout::Landscape;
        s.marginsMm = QMarginsF(15, 20, 15, 22);   // 左上右下（QMarginsF 语义）
        QString out = PdfExporter::buildRenderHtml("<p>x</p>", "/tmp", "", "", s);
        QVERIFY(out.contains("size:A5 landscape"));
        // @page margin 顺序 = 上 右 下 左：20mm 15mm 22mm 15mm
        QVERIFY(out.contains("margin:20mm 15mm 22mm 15mm"));
    }

    void testBuildRenderHtmlTitleEscaped() {
        PdfPageSpec s = specWithHeader();
        s.headerTitle = QStringLiteral("a\"b\\c\nd");
        QString out = PdfExporter::buildRenderHtml("<p>x</p>", "/tmp", "", "", s);
        QVERIFY(out.contains("a\\\"b\\\\c d"));   // " → \"，\ → \\，换行 → 空格
    }

private:
    PdfPageSpec specWithHeader() {
        PdfPageSpec s;
        s.headerEnabled = true; s.footerEnabled = true;
        s.headerTitle = QStringLiteral("我的文档");
        return s;
    }
};

QTEST_APPLESS_MAIN(TestPdfExporter)
#include "test_pdfexporter.moc"
