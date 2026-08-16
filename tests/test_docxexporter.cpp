#include <QTest>
#include <QTemporaryDir>
#include <QProcess>
#include <QFile>
#include <QDir>
#include "../src/core/DocxExporter.h"

// DocxExporter 测试：导出 HTML → DOCX，解包 word/document.xml 断言 OOXML 结构。
// 依赖系统 zip/unzip（CI 与开发机均已安装）。
class TestDocxExporter : public QObject {
    Q_OBJECT

    // 导出 HTML 为 docx，返回解包后的 word/document.xml 内容
    static QString exportDocumentXml(const QString &html) {
        QTemporaryDir dir;
        if (!dir.isValid()) return {};
        QString out = dir.path() + "/out.docx";
        DocxExporter ex;
        ex.exportToDocx(html, dir.path(), out);
        if (!QFile::exists(out)) return {};
        QProcess unzip;
        unzip.start("unzip", QStringList() << "-p" << out << "word/document.xml");
        if (!unzip.waitForFinished(10000)) return {};
        return QString::fromUtf8(unzip.readAllStandardOutput());
    }

private slots:
    void testBasicInlineFormatting() {
        QString xml = exportDocumentXml(
            "<h1>标题</h1>"
            "<p><strong>粗</strong> <em>斜</em> <u>下划线</u> "
            "<s>删除</s> <code>代码</code> <mark>高亮</mark> "
            "<sub>下标</sub> <sup>上标</sup></p>");
        QVERIFY(xml.contains("标题"));
        QVERIFY(xml.contains("Heading1"));
        QVERIFY(xml.contains("<w:b/>"));
        QVERIFY(xml.contains("<w:i/>"));
        QVERIFY(xml.contains("<w:u w:val=\"single\"/>"));
        QVERIFY(xml.contains("<w:strike/>"));
        QVERIFY(xml.contains("<w:highlight w:val=\"yellow\"/>"));
        QVERIFY(xml.contains("<w:vertAlign w:val=\"subscript\"/>"));
        QVERIFY(xml.contains("<w:vertAlign w:val=\"superscript\"/>"));
        QVERIFY(xml.contains("<w:rFonts w:ascii=\"Consolas\""));
    }

    void testInlineMathExportedAsLatex() {
        // 行内公式 span 内容为空（KaTeX 渲染），导出应取 data-latex 而非正文
        QString xml = exportDocumentXml(
            "<p>能量 <span data-type=\"inline-math\" data-latex=\"E=mc^2\"></span></p>");
        QVERIFY2(xml.contains("$E=mc^2$"), qPrintable(xml));
    }

    void testBlockMathExportedAsLatex() {
        QString xml = exportDocumentXml(
            "<div data-type=\"block-math\" data-latex=\"x^2+y^2\"></div>");
        QVERIFY2(xml.contains("x^2+y^2"), qPrintable(xml));
        QVERIFY(xml.contains("$$x^2+y^2$$"));
        QVERIFY(xml.contains("<w:jc w:val=\"center\"/>"));
    }

    void testFootnoteReferenceAndDefinition() {
        // 脚注引用导出为上标，定义导出为 [^id]: 内容（不得丢失内容为 "..."）
        QString xml = exportDocumentXml(
            "<p>正文<sup data-type=\"footnoteRef\" data-id=\"1\">1</sup></p>"
            "<div data-type=\"footnoteContainer\">"
            "<div data-type=\"footnoteDef\" data-id=\"1\">"
            "<span class=\"footnote-marker\">1</span>"
            "<span class=\"footnote-content\">这是脚注内容</span>"
            "</div></div>");
        QVERIFY(xml.contains("这是脚注内容"));
    }

    void testHyperlinkCreatesRelationship() {
        QString xml = exportDocumentXml("<p><a href=\"https://example.com\">链接</a></p>");
        QVERIFY(xml.contains("<w:hyperlink"));
        QVERIFY(xml.contains("链接"));
    }

    void testCodeBlockPreservesLines() {
        QString xml = exportDocumentXml("<pre><code class=\"language-cpp\">int a;\nint b;</code></pre>");
        QVERIFY(xml.contains("int a;"));
        QVERIFY(xml.contains("int b;"));
        // 两行应各成一个段落（<w:p> 至少出现 2 次）
        QVERIFY(xml.count("<w:p>") >= 2);
    }

    void testTableProducesGrid() {
        QString xml = exportDocumentXml(
            "<table><thead><tr><th>A</th><th>B</th></tr></thead>"
            "<tbody><tr><td>1</td><td>2</td></tr></tbody></table>");
        QVERIFY(xml.contains("<w:tbl>"));
        QVERIFY(xml.contains("<w:tblGrid>"));
        QVERIFY(xml.contains("<w:gridCol"));
        QVERIFY(xml.contains("A"));
        QVERIFY(xml.contains("2"));
    }

    void testTaskListCheckboxPrefix() {
        QString xml = exportDocumentXml(
            "<ul data-type=\"taskList\"><li data-type=\"taskItem\">"
            "<label><input type=\"checkbox\"></label><div><p>事项</p></div>"
            "</li></ul>");
        QVERIFY(xml.contains("☐"));
    }

    void testBlockquoteCenteredParagraphSinglePPr() {
        // 引用块内居中段落：缩进 pPr 与对齐 pPr 必须合并为单个 <w:pPr>
        QString xml = exportDocumentXml(
            "<blockquote><p style=\"text-align:center\">居中引用</p></blockquote>");
        QVERIFY(xml.contains("居中引用"));
        QCOMPARE(xml.count("<w:pPr>"), 1);
        QVERIFY(xml.contains("<w:ind w:left=\"720\" w:right=\"720\"/>"));
        QVERIFY(xml.contains("<w:jc w:val=\"center\"/>"));
    }

    void testCjkTextNotMisclassifiedAsEmoji() {
        // 中文本体不得使用 Noto Color Emoji 字体（应走 eastAsia 字体回退）
        QString xml = exportDocumentXml("<p>中文测试段落</p>");
        QVERIFY(xml.contains("中文测试段落"));
        QVERIFY(!xml.contains("Noto Color Emoji"));
    }

    void testEmojiStillUsesEmojiFont() {
        // emoji 仍应使用 Noto Color Emoji 字体（回归：修复 CJK 误判时不能误伤 emoji）
        QString xml = exportDocumentXml("<p>笑脸😀符号</p>");
        QVERIFY(xml.contains("Noto Color Emoji"));
        QVERIFY(xml.contains("😀"));
    }

    void testImageBecomesPlaceholder() {
        // 图片文件缺失（docDir 下无 a.png）时降级为 [图片: alt] 占位文本
        QString xml = exportDocumentXml("<p><img src=\"a.png\" alt=\"示意图\"></p>");
        QVERIFY(xml.contains("[图片"));
        QVERIFY(xml.contains("示意图")); // alt 保留
    }

    void testImageEmbeddedAsMedia() {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        QFile f(dir.path() + "/a.png");
        QVERIFY(f.open(QIODevice::WriteOnly));
        // 1×1 透明 PNG
        const unsigned char png[] = {0x89,0x50,0x4e,0x47,0x0d,0x0a,0x1a,0x0a,0,0,0,0x0d,0x49,0x48,0x44,0x52,
                                     0,0,0,1,0,0,0,1,0x08,0x06,0,0,0,0x1f,0x15,0xc4,0x89,0,0,0,0x0a,0x49,0x44,0x41,0x54,
                                     0x78,0x9c,0x63,0,1,0,0,0x05,0,1,0x0d,0x0a,0x2d,0xb4,0,0,0,0,0x49,0x45,0x4e,0x44,0xae,0x42,0x60,0x82};
        f.write(reinterpret_cast<const char*>(png), sizeof(png)); f.close();

        QString out = dir.path() + "/o.docx";
        DocxExporter ex;
        ex.exportToDocx("<p><img src=\"a.png\" alt=\"图\"></p>", dir.path(), out);
        QVERIFY(QFile::exists(out));

        // document.xml 含 drawing + blip
        QProcess unzip; unzip.start("unzip", QStringList() << "-p" << out << "word/document.xml");
        QVERIFY(unzip.waitForFinished(10000));
        QString docXml = QString::fromUtf8(unzip.readAllStandardOutput());
        QVERIFY(docXml.contains("<w:drawing>"));
        QVERIFY(docXml.contains("<a:blip r:embed=\"rId"));
        QVERIFY(docXml.contains("<pic:pic>"));

        // media 部件存在
        unzip.start("unzip", QStringList() << "-l" << out);
        QVERIFY(unzip.waitForFinished(10000));
        QString listing = QString::fromUtf8(unzip.readAllStandardOutput());
        QVERIFY(listing.contains("word/media/image1.png"));
    }

    void testMermaidSourceSurvivesAsCodeBlock() {
        // 当前 DOCX 导出把 mermaid 当普通代码块，仅保留源码（图表不渲染，已知限制）
        QString xml = exportDocumentXml(
            "<pre><code class=\"language-mermaid\">graph TD;A--&gt;B;</code></pre>");
        QVERIFY(xml.contains("graph TD"));
    }
};

QTEST_APPLESS_MAIN(TestDocxExporter)
#include "test_docxexporter.moc"
