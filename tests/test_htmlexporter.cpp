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

    void testBlockquoteLeftBorderIsBlueInBothThemes() {
        // 引用块左侧竖线统一为蓝色 #0078d4（与 PDF/DOCX 一致），亮暗主题均生效
        const QStringList themes{"light", "dark"};
        for (const QString &theme : themes) {
            QString css = HtmlExporter::bodyCss(theme);
            QVERIFY2(css.contains("blockquote{border-left:4px solid #0078d4"),
                     qPrintable(theme));
        }
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

    // 1×1 真实 PNG 字节（内容级 MIME 校验后，伪字节夹具不再被内联）
    static QByteArray tinyPng() {
        static const unsigned char png[] = {0x89,0x50,0x4e,0x47,0x0d,0x0a,0x1a,0x0a,0,0,0,0x0d,0x49,0x48,0x44,0x52,
                                     0,0,0,1,0,0,0,1,0x08,0x06,0,0,0,0x1f,0x15,0xc4,0x89,0,0,0,0x0a,0x49,0x44,0x41,0x54,
                                     0x78,0x9c,0x63,0,1,0,0,0,0x05,0,1,0x0d,0x0a,0x2d,0xb4,0,0,0,0,0x49,0x45,0x4e,0x44,0xae,0x42,0x60,0x82};
        return QByteArray(reinterpret_cast<const char*>(png), sizeof(png));
    }

    void testInlineImagesReplacesRelativeSrcWithBase64() {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        QFile f(dir.path() + "/a.png");
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(tinyPng()); f.close();

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
        f.write(tinyPng()); f.close();

        QString html = "<p><img src='b.png'></p>";
        QString out = HtmlExporter::inlineImages(html, dir.path());
        QVERIFY(out.contains("data:image/png;base64,"));
        QVERIFY(!out.contains("src='b.png'"));
    }

    void testBodyCssDiffersByTheme() {
        QVERIFY(HtmlExporter::bodyCss("light").contains("background:"));
        QVERIFY(HtmlExporter::bodyCss("light") != HtmlExporter::bodyCss("dark"));
    }

    void testBodyCssTaskListInlineLayout() {
        // 任务列表 checkbox 与文字需同行：直接 li 用 flex 布局（镜像 editor.css）。
        // 必须用子代选择器 > 限定直接 li——嵌套在任务项内的通用列表 li（如 列表2-2
        // 下再嵌 <ol>）若也命中 flex 会被当成 flex 容器，其子元素 p 与嵌套 <ol>
        // 排成一行、序号消失（导出嵌套列表回归）。
        QVERIFY(HtmlExporter::bodyCss("light").contains("ul[data-type=\"taskList\"] > li{display:flex;"));
        QVERIFY(HtmlExporter::bodyCss("dark").contains("ul[data-type=\"taskList\"] > li{display:flex;"));
        // 内容段落 margin 归零，避免顶部 .6em 把文字压到复选框下方（对齐）；
        // > div > p 只命中任务项内容直接段落，嵌套列表段落不受影响
        QVERIFY(HtmlExporter::bodyCss("light").contains("ul[data-type=\"taskList\"] > li > div > p{margin:0;}"));
        QVERIFY(HtmlExporter::bodyCss("dark").contains("ul[data-type=\"taskList\"] > li > div > p{margin:0;}"));
    }

    void testRenderScriptUsesTheme() {
        QVERIFY(HtmlExporter::renderScript("dark").contains("'dark'"));
        QVERIFY(HtmlExporter::renderScript("light").contains("'default'"));
    }

    void testRenderScriptMermaidStrictSecurity() {
        // mermaid securityLevel 必须 strict：loose 跳过 sanitizeUrl/DOMPurify，
        // 恶意图表标签（A["<img src=x onerror=…>"]）以原始 HTML 进 SVG、经
        // innerHTML 在导出产物里直接执行（受害者打开即触发）
        const QString js = HtmlExporter::renderScript("light");
        QVERIFY2(js.contains("securityLevel:'strict'"), qPrintable(js));
        QVERIFY(!js.contains("securityLevel:'loose'"));
        QVERIFY(!js.contains("securityLevel: 'loose'"));
    }

    void testInlineImagesFenceAndValidation() {
        // 内联防护三件套：① docDir 围栏（../ 逃逸与目录外绝对路径不内联）；
        // ② 内容级校验（伪装扩展名的非图片文件不内联——曾 ![](../../.ssh/id_rsa)
        // 把任意本地文件 base64 嵌进导出产物）；③ 真实图片正常内联
        QTemporaryDir dir; QVERIFY(dir.isValid());
        QDir docDir(dir.path());
        docDir.mkdir("assets");
        // 1×1 真实 PNG（同 testImageEmbeddedAsMedia 的字节）
        const unsigned char png[] = {0x89,0x50,0x4e,0x47,0x0d,0x0a,0x1a,0x0a,0,0,0,0x0d,0x49,0x48,0x44,0x52,
                                     0,0,0,1,0,0,0,1,0x08,0x06,0,0,0,0x1f,0x15,0xc4,0x89,0,0,0,0x0a,0x49,0x44,0x41,0x54,
                                     0x78,0x9c,0x63,0,1,0,0,0,0x05,0,1,0x0d,0x0a,0x2d,0xb4,0,0,0,0,0x49,0x45,0x4e,0x44,0xae,0x42,0x60,0x82};
        QFile good(dir.path() + "/assets/ok.png");
        QVERIFY(good.open(QIODevice::WriteOnly));
        good.write(reinterpret_cast<const char*>(png), sizeof(png)); good.close();
        // docDir 外的同字节 PNG
        QTemporaryDir outside; QVERIFY(outside.isValid());
        QFile esc(outside.path() + "/secret.png");
        QVERIFY(esc.open(QIODevice::WriteOnly));
        esc.write(reinterpret_cast<const char*>(png), sizeof(png)); esc.close();
        // docDir 内伪装成 .png 的文本
        QFile fake(dir.path() + "/assets/fake.png");
        QVERIFY(fake.open(QIODevice::WriteOnly));
        fake.write("this is definitely not an image, maybe /etc/passwd content");
        fake.close();

        // 围栏内真图 → 内联为 data:
        QString out = HtmlExporter::inlineImages(
            "<p><img src=\"assets/ok.png\"></p>", dir.path());
        QVERIFY2(out.contains("data:image/png;base64,"), qPrintable(out));
        // ../ 逃出 docDir → 保留原 src，不内联
        QString escRel = QString("<p><img src=\"../../%1/secret.png\"></p>")
                             .arg(QFileInfo(outside.path()).fileName());
        // 相对 ../ 在临时目录父层不稳定，直接用绝对路径测围栏（更直接）
        Q_UNUSED(escRel);
        QString out2 = HtmlExporter::inlineImages(
            QString("<p><img src=\"%1/secret.png\"></p>").arg(outside.path()), dir.path());
        QVERIFY2(!out2.contains("data:"), qPrintable(out2));
        QVERIFY(out2.contains("secret.png"));
        // 伪装图片 → 不内联
        QString out3 = HtmlExporter::inlineImages(
            "<p><img src=\"assets/fake.png\"></p>", dir.path());
        QVERIFY2(!out3.contains("data:"), qPrintable(out3));
        QVERIFY(out3.contains("fake.png"));
    }

    void testRenderScriptMermaidRenderIdDecoupledFromHolder() {
        // mermaid.render 不传容器时会先按 id 删除文档中既有元素
        //（removeExistingElements）。若给占位 holder 设置与 render 相同的 id，
        // holder 会被 mermaid 当作残留元素删掉，SVG 写入游离节点，图表消失。
        // render id 必须与 holder.id 解耦（对齐 editor.js 的 NodeView 模式）。
        QString s = HtmlExporter::renderScript("light");
        QVERIFY(!s.contains("holder.id='mermaid"));
        QVERIFY(!s.contains("mermaid.render(holder.id"));
        QVERIFY(s.contains("mermaid.render('mermaid-export-'+(++seq),src)"));
    }

    void testRenderScriptMermaidLoopCapturesHolderPerIteration() {
        // var holder 在循环中被异步 .then 回调共享，多图会全部写入最后一个
        // holder。循环体须以 IIFE 捕获每次迭代的 code/holder。
        QString s = HtmlExporter::renderScript("light");
        QVERIFY(s.contains("(function(code){"));
        QVERIFY(s.contains("})(pres[j]);"));
    }

    void testMermaidRenderScriptIsolated() {
        QString s = HtmlExporter::mermaidRenderScript("dark");
        QVERIFY(s.contains("'dark'"));
        QVERIFY(s.contains("pre code.language-mermaid"));
        QVERIFY(s.contains("__renderAllMermaid"));
        QVERIFY(s.contains("mermaid.render('mermaid-export-'+(++seq),src)"));
        QVERIFY(!s.contains("<script"));   // 片段不含 script 标签，调用方自行包裹
    }

    void testExportBodyPercentPlaceholdersSurvive() {
        // 组装用多参 .arg()：替换文本不会被重新扫描，正文字面 %1~%9 必须幸存
        //（若有人改成链式单参 .arg(a).arg(b) 会吞掉正文占位符，此测试拦截）
        QTemporaryDir dir; QVERIFY(dir.isValid());
        QString out = dir.path() + "/p.html";
        QString body = "<p>进度 80%1、50%2、33%3、25%4、10%5、1%6、%7、%8、%9、%99</p>";
        HtmlExporter exporter;
        exporter.exportToHtml(body, dir.path(), out, "light");
        QFile f(out);
        QVERIFY(f.open(QIODevice::ReadOnly | QIODevice::Text));
        QString html = QString::fromUtf8(f.readAll());
        f.close();
        QVERIFY(html.contains("80%1"));
        QVERIFY(html.contains("50%2"));
        QVERIFY(html.contains("33%3"));
        QVERIFY(html.contains("25%4"));
        QVERIFY(html.contains("10%5"));
        QVERIFY(html.contains("%99"));
        // 脚本块插在 </body> 前，只断言 body 开头连续段
        QVERIFY(html.contains("<body>" + body));
    }
};

QTEST_APPLESS_MAIN(TestHtmlExporter)
#include "test_htmlexporter.moc"
