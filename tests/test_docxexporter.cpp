#include <QTest>
#include <QTemporaryDir>
#include <QProcess>
#include <QFile>
#include <QDir>
#include <QImage>
#include <QPageLayout>
#include <QPageSize>
#include <QRegularExpression>
#include <QSet>
#include <QSettings>
#include <QMap>
#include <QXmlStreamReader>
#include "../src/core/DocxExporter.h"
#include "../src/core/MermaidRenderer.h"
#include "../src/core/MathMlRenderer.h"
#include "../src/core/MathMlConverter.h"

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

    // 导出 HTML 为 docx 并整包解压，返回 {部件路径 → 内容}；不存在的部件不在 map 中。
    // 注：不得用 `unzip -p out [Content_Types].xml`——unzip 参数是 glob 模式，
    // `[...]` 是字符类匹配不到字面名，必须 -o 解压后按文件读。
    static QMap<QString, QString> exportParts(const QString &html,
                                               const QStringList &partNames,
                                               const QString &outFileName = QStringLiteral("out.docx")) {
        QMap<QString, QString> out;
        QTemporaryDir dir;
        if (!dir.isValid()) return out;
        const QString outPath = dir.path() + "/" + outFileName;
        DocxExporter ex;
        ex.exportToDocx(html, dir.path(), outPath);
        if (!QFile::exists(outPath)) return out;
        QProcess unzip;
        unzip.start("unzip", QStringList() << "-o" << outPath << "-d" << dir.path() + "/x");
        if (!unzip.waitForFinished(10000)) return out;
        for (const QString &p : partNames) {
            QFile f(dir.path() + "/x/" + p);
            if (f.open(QIODevice::ReadOnly))
                out.insert(p, QString::fromUtf8(f.readAll()));
        }
        return out;
    }

private slots:
    // 重定向 QSettings 到临时目录：testLargeImageShrinksToPageWidth 会临时写 export/* 设置，
    // 若不隔离会污染真实用户配置（同 test_languagemanager，须重定向 NativeFormat）。
    void initTestCase() {
        QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, QDir::tempPath());
    }

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

    // === DOCX 页眉页脚（export/docxHeaderEnabled / docxFooterEnabled，默认开）===

    // 双开：两部件齐全 + sectPr 引用先于 pgSz（CT_SectPr schema 顺序）+ rels/Content_Types 接线 + 良构
    void testDocxHeaderFooterBothEnabled() {
        QSettings s("md-editor", "md-editor");
        const QVariant oldH = s.value("export/docxHeaderEnabled");
        const QVariant oldF = s.value("export/docxFooterEnabled");
        s.setValue("export/docxHeaderEnabled", true);
        s.setValue("export/docxFooterEnabled", true);
        s.sync();

        auto parts = exportParts("<p>hello</p>",
            {"word/document.xml", "word/header1.xml", "word/footer1.xml",
             "word/_rels/document.xml.rels", "[Content_Types].xml"},
            QStringLiteral("测试文档.docx"));
        QVERIFY(parts.contains("word/document.xml"));
        QVERIFY(parts.contains("word/header1.xml"));
        QVERIFY(parts.contains("word/footer1.xml"));

        // sectPr 引用存在且位于 pgSz 之前
        const QString doc = parts["word/document.xml"];
        const int hdrPos = doc.indexOf("<w:headerReference w:type=\"default\" r:id=\"rIdHeader1\"/>");
        const int ftrPos = doc.indexOf("<w:footerReference w:type=\"default\" r:id=\"rIdFooter1\"/>");
        const int pgSzPos = doc.indexOf("<w:pgSz");
        QVERIFY(hdrPos >= 0 && ftrPos >= 0);
        QVERIFY(pgSzPos > hdrPos && pgSzPos > ftrPos);

        // 页眉：居中 + 标题 = 导出文件名基名 + 9pt 灰
        const QString hdr = parts["word/header1.xml"];
        QVERIFY(hdr.contains("<w:jc w:val=\"center\"/>"));
        QVERIFY(hdr.contains("测试文档"));
        QVERIFY(hdr.contains("<w:color w:val=\"808080\"/><w:sz w:val=\"18\"/>"));

        // 页脚：右对齐 + PAGE/NUMPAGES 简单字段 + 「第 X 页 / 共 Y 页」文案
        const QString ftr = parts["word/footer1.xml"];
        QVERIFY(ftr.contains("<w:jc w:val=\"right\"/>"));
        QVERIFY(ftr.contains("w:instr=\" PAGE   \\* MERGEFORMAT \""));
        QVERIFY(ftr.contains("w:instr=\" NUMPAGES   \\* MERGEFORMAT \""));
        QVERIFY(ftr.contains("第 ") && ftr.contains(" 页 / 共 ") && ftr.contains(" 页"));

        // 两部件 XML 良构
        for (const QString &x : {hdr, ftr}) {
            QXmlStreamReader r(x);
            while (!r.atEnd()) r.readNext();
            QVERIFY2(!r.hasError(), qPrintable(r.errorString()));
        }

        // rels 与 Content_Types 接线
        QVERIFY(parts["word/_rels/document.xml.rels"]
                    .contains("relationships/header\" Target=\"header1.xml\""));
        QVERIFY(parts["word/_rels/document.xml.rels"]
                    .contains("relationships/footer\" Target=\"footer1.xml\""));
        QVERIFY(parts["[Content_Types].xml"].contains(
            "/word/header1.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.header+xml\""));
        QVERIFY(parts["[Content_Types].xml"].contains(
            "/word/footer1.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.footer+xml\""));

        s.setValue("export/docxHeaderEnabled", oldH);   // null QVariant → 移除键，回落默认
        s.setValue("export/docxFooterEnabled", oldF);
    }

    // 只开页眉：footer 部件/引用/字段全无
    void testDocxHeaderOnly() {
        QSettings s("md-editor", "md-editor");
        const QVariant oldH = s.value("export/docxHeaderEnabled");
        const QVariant oldF = s.value("export/docxFooterEnabled");
        s.setValue("export/docxHeaderEnabled", true);
        s.setValue("export/docxFooterEnabled", false);
        s.sync();

        auto parts = exportParts("<p>x</p>",
            {"word/document.xml", "word/header1.xml", "word/footer1.xml"});
        QVERIFY(parts.contains("word/header1.xml"));
        QVERIFY(!parts.contains("word/footer1.xml"));
        QVERIFY(parts["word/document.xml"].contains("r:id=\"rIdHeader1\""));
        QVERIFY(!parts["word/document.xml"].contains("footerReference"));
        QVERIFY(!parts["word/document.xml"].contains("NUMPAGES"));

        s.setValue("export/docxHeaderEnabled", oldH);
        s.setValue("export/docxFooterEnabled", oldF);
    }

    // 只开页脚：header 部件/引用全无
    void testDocxFooterOnly() {
        QSettings s("md-editor", "md-editor");
        const QVariant oldH = s.value("export/docxHeaderEnabled");
        const QVariant oldF = s.value("export/docxFooterEnabled");
        s.setValue("export/docxHeaderEnabled", false);
        s.setValue("export/docxFooterEnabled", true);
        s.sync();

        auto parts = exportParts("<p>x</p>",
            {"word/document.xml", "word/header1.xml", "word/footer1.xml"});
        QVERIFY(!parts.contains("word/header1.xml"));
        QVERIFY(parts.contains("word/footer1.xml"));
        QVERIFY(!parts["word/document.xml"].contains("headerReference"));
        QVERIFY(parts["word/document.xml"].contains("r:id=\"rIdFooter1\""));

        s.setValue("export/docxHeaderEnabled", oldH);
        s.setValue("export/docxFooterEnabled", oldF);
    }

    // 全关：零注入，sectPr 直接跟 pgSz（与改造前语义等价，回归锁定）
    void testDocxHeaderFooterAllDisabled() {
        QSettings s("md-editor", "md-editor");
        const QVariant oldH = s.value("export/docxHeaderEnabled");
        const QVariant oldF = s.value("export/docxFooterEnabled");
        s.setValue("export/docxHeaderEnabled", false);
        s.setValue("export/docxFooterEnabled", false);
        s.sync();

        auto parts = exportParts("<p>x</p>",
            {"word/document.xml", "word/header1.xml", "word/footer1.xml",
             "word/_rels/document.xml.rels", "[Content_Types].xml"});
        QVERIFY(!parts.contains("word/header1.xml"));
        QVERIFY(!parts.contains("word/footer1.xml"));
        QVERIFY(parts["word/document.xml"].contains("<w:sectPr><w:pgSz"));
        QVERIFY(!parts["word/_rels/document.xml.rels"].contains("header1.xml"));
        QVERIFY(!parts["[Content_Types].xml"].contains("header+xml"));

        s.setValue("export/docxHeaderEnabled", oldH);
        s.setValue("export/docxFooterEnabled", oldF);
    }

    // 标题含 XML 特殊字符：页眉文本经 xmlEscape，无注入
    void testDocxHeaderTitleEscaped() {
        QSettings s("md-editor", "md-editor");
        const QVariant oldH = s.value("export/docxHeaderEnabled");
        const QVariant oldF = s.value("export/docxFooterEnabled");
        s.setValue("export/docxHeaderEnabled", true);
        s.setValue("export/docxFooterEnabled", false);
        s.sync();

        auto parts = exportParts("<p>x</p>", {"word/header1.xml"},
                                 QStringLiteral("a&b<c.docx"));
        const QString hdr = parts.value("word/header1.xml");
        QVERIFY(hdr.contains("a&amp;b&lt;c"));
        QVERIFY(!hdr.contains("a&b<c"));

        s.setValue("export/docxHeaderEnabled", oldH);
        s.setValue("export/docxFooterEnabled", oldF);
    }

    void testMathMlConverterBasic() {
        // E=mc^2 → 变量斜体（m:sty i）+ 等号 + 上标（m:sSup）。annotation 须跳过、LaTeX 源码不泄漏。
        QString omm = MathMlConverter::toOMML(
            "<math><semantics><mrow>"
            "<mi>E</mi><mo>=</mo><mi>m</mi><msup><mi>c</mi><mn>2</mn></msup>"
            "</mrow><annotation>E=mc^2</annotation></semantics></math>");
        QVERIFY2(omm.contains("<m:sSup>"), qPrintable(omm));
        QVERIFY(omm.contains("<m:sty m:val=\"i\"/>"));
        QVERIFY(omm.contains("E"));
        QVERIFY(omm.contains("c"));
        QVERIFY(omm.contains("2"));
        QVERIFY(!omm.contains("annotation"));
        QVERIFY(!omm.contains("E=mc^2"));
    }

    void testMathMlConverterSelfClosingElements() {
        // 自闭合元素（审查 P2-13）：KaTeX 对 \,/\quad 等间距输出 <mspace/>——
        // 其 StartElement token 即结束，skipElement 等再读 token 会吞掉父级
        // 闭合或后续兄弟元素（转换失步、内容丢失）
        QString out = MathMlConverter::toOMML(
            "<math><mrow><mfrac><mi>a</mi><mspace/></mfrac><mi>b</mi></mrow></math>");
        QVERIFY2(out.contains("b"), qPrintable(out));   // 曾被 mspace 后的失步吞掉
        QVERIFY(out.contains("<m:f>"));
        QString out2 = MathMlConverter::toOMML(
            "<math><mrow><mspace/><mi>c</mi></mrow></math>");
        QVERIFY2(out2.contains("c"), qPrintable(out2));
        // 自闭合透传元素（<mrow/>）不产生内容、不吞后续兄弟
        QString out3 = MathMlConverter::toOMML(
            "<math><mrow/><mi>d</mi></math>");
        QVERIFY2(out3.contains("d"), qPrintable(out3));
    }

    void testMathMlConverterFractionAndRadicals() {
        // 分数 → m:f/m:num/m:den
        QString frac = MathMlConverter::toOMML(
            "<math><mrow><mfrac><mi>a</mi><mi>b</mi></mfrac></mrow></math>");
        QVERIFY2(frac.contains("<m:f>"), qPrintable(frac));
        QVERIFY(frac.contains("<m:num>"));
        QVERIFY(frac.contains("<m:den>"));
        QVERIFY(frac.contains("a"));
        QVERIFY(frac.contains("b"));
        // 平方根 → m:rad 且隐藏根指数
        QString sqrt = MathMlConverter::toOMML(
            "<math><msqrt><mi>x</mi></msqrt></math>");
        QVERIFY(sqrt.contains("<m:rad>"));
        QVERIFY(sqrt.contains("<m:degHide m:val=\"1\"/>"));

        // n 次根 → m:rad 带根指数（mroot 子元素顺序：基数在前、根指数在后）
        QString root = MathMlConverter::toOMML(
            "<math><mroot><mi>x</mi><mn>3</mn></mroot></math>");
        QVERIFY(root.contains("<m:deg>"));
        QVERIFY(root.contains("<m:e>"));
    }

    void testMathMlConverterScripts() {
        QVERIFY(MathMlConverter::toOMML("<math><msub><mi>x</mi><mn>1</mn></msub></math>")
                    .contains("<m:sSub>"));
        QVERIFY(MathMlConverter::toOMML("<math><msup><mi>x</mi><mn>2</mn></msup></math>")
                    .contains("<m:sSup>"));
        QVERIFY(MathMlConverter::toOMML(
                    "<math><msubsup><mi>x</mi><mn>1</mn><mn>2</mn></msubsup></math>")
                    .contains("<m:sSubSup>"));
        // 上下标内容顺序：基、下、上
        QString ss = MathMlConverter::toOMML(
            "<math><msubsup><mi>x</mi><mn>1</mn><mn>2</mn></msubsup></math>");
        int e = ss.indexOf("<m:e>"), sub = ss.indexOf("<m:sub>"), sup = ss.indexOf("<m:sup>");
        QVERIFY(e >= 0 && sub > e && sup > sub);
    }

    void testExtractMathSources() {
        // 行内与块级按文档顺序提取，data-latex 的 HTML 转义还原
        QString html = "<p>能量 <span data-type=\"inline-math\" data-latex=\"E=mc^2\"></span></p>"
                       "<div data-type=\"block-math\" data-latex=\"a&amp;b &lt; c\"></div>"
                       "<p>再一个 <span data-type=\"inline-math\" data-latex=\"x^2\"></span></p>";
        QStringList srcs = DocxExporter::extractMathSources(html);
        QCOMPARE(srcs.size(), 3);
        QCOMPARE(srcs[0], QString("E=mc^2"));
        QCOMPARE(srcs[1], QString("a&b < c"));   // &amp; &lt; 还原
        QCOMPARE(srcs[2], QString("x^2"));
    }

    void testExtractMathSourcesNoMatch() {
        // 普通 span / 无公式的文档不提取
        QCOMPARE(DocxExporter::extractMathSources("<p><span>文字</span></p>").size(), 0);
    }

    void testMathRenderScriptUsesMathmlOutput() {
        // 渲染脚本须显式指定 output:'mathml'（返回 MathML 供 OMML 转换），并含 try/catch 兜底
        QString script = MathMlRenderer::buildRenderScript({"E=mc^2"});
        QVERIFY2(script.contains("output:'mathml'"), qPrintable(script));
        QVERIFY(script.contains("katex.renderToString"));
        QVERIFY(script.contains("throwOnError:false"));
    }

    void testFootnoteReferenceAndDefinition() {
        // 脚注真导出（审查 P1-7）：引用 → Word 原生 footnoteReference（自动编号
        // 上标），定义内容进 footnotes.xml 部件（旧实现解析出内容后丢弃、正文
        // 固定写 "[^1]: ..."），包结构同步补 [Content_Types]/rels 声明
        const QString html =
            "<p>正文<sup data-type=\"footnoteRef\" data-id=\"1\">1</sup></p>"
            "<div data-type=\"footnoteContainer\">\n"
            "<div data-type=\"footnoteDef\" data-id=\"1\">"
            "<span class=\"footnote-marker\">1</span>"
            "<span class=\"footnote-content\">这是脚注内容</span>"
            "</div></div>";
        auto parts = exportParts(html, {"word/document.xml", "word/footnotes.xml",
                                        "word/_rels/document.xml.rels",
                                        "[Content_Types].xml"});
        const QString &xml = parts["word/document.xml"];
        // 引用侧：原生脚注引用（0/1 为内置分隔符，正文脚注从 2 起）
        QVERIFY(xml.contains("<w:footnoteReference w:id=\"2\"/>"));
        QVERIFY(xml.contains("FootnoteReference"));
        QVERIFY(!xml.contains("[^1]"));           // 旧字面形态不得残留
        QVERIFY(!xml.contains("这是脚注内容"));    // 定义内容不进正文
        // 定义侧：footnotes.xml 部件齐全
        const QString &footnotes = parts["word/footnotes.xml"];
        QVERIFY(footnotes.contains("<w:footnotes"));
        QVERIFY(footnotes.contains("w:type=\"separator\""));
        QVERIFY(footnotes.contains("w:type=\"continuationSeparator\""));
        QVERIFY(footnotes.contains("<w:footnote w:id=\"2\">"));
        QVERIFY(footnotes.contains("<w:footnoteRef/>"));
        QVERIFY(footnotes.contains("这是脚注内容"));
        // marker 编号不得重复输出：footnoteRef 已渲染编号，footnote-marker span
        // 是编辑器 UI 辅件（内含返回按钮），导出时整体跳过
        QVERIFY(!footnotes.contains("<w:t xml:space=\"preserve\">1</w:t>"));
        // 容器 open 与 def 之间的换行空白（\n）不得泄漏为正文 run
        QVERIFY(!xml.contains("<w:t xml:space=\"preserve\">\n"));
        // 包声明：Content-Types Override + document.xml.rels 关系
        QVERIFY(parts["[Content_Types].xml"].contains("/word/footnotes.xml"));
        QVERIFY(parts["word/_rels/document.xml.rels"].contains("footnotes.xml"));
    }

    void testFootnoteMultipleAndRepeatedRefs() {
        // 多脚注编号连续（2、3）；同一 data-id 二次引用复用同号；无定义的引用
        // 也占号（footnotes.xml 中该号只有编号标记无内容）
        const QString html =
            "<p>a<sup data-type=\"footnoteRef\" data-id=\"x\">x</sup>"
            "b<sup data-type=\"footnoteRef\" data-id=\"y\">y</sup>"
            "c<sup data-type=\"footnoteRef\" data-id=\"x\">x</sup>"
            "d<sup data-type=\"footnoteRef\" data-id=\"z\">z</sup></p>"
            "<div data-type=\"footnoteContainer\">"
            "<div data-type=\"footnoteDef\" data-id=\"x\"><span class=\"footnote-content\">定义甲</span></div>\n"
            "<div data-type=\"footnoteDef\" data-id=\"y\"><span class=\"footnote-content\">定义乙</span></div>"
            "</div>";
        auto parts = exportParts(html, {"word/document.xml", "word/footnotes.xml"});
        const QString &xml = parts["word/document.xml"];
        // Word 脚注标记一脚注一锚点：同 id 第二个 footnoteReference 触发
        // 「无法读取的内容」修复框（macOS Word A/B 隔离实证 2026-09-15）。
        // 首引用 = 原生标记 + 隐藏书签；重复引用 = NOTEREF 交叉引用域（显示同号）
        QCOMPARE(xml.count("<w:footnoteReference w:id=\"2\"/>"), 1);   // x 首引用
        QVERIFY(xml.contains("<w:bookmarkStart w:id=\"2\" w:name=\"_RefFtn2\"/>"));
        QVERIFY(xml.contains("<w:bookmarkEnd w:id=\"2\"/>"));
        QVERIFY(xml.contains(
            "<w:instrText xml:space=\"preserve\"> NOTEREF _RefFtn2 \\r \\h </w:instrText>"));
        // 域缓存结果：上标样式 + 当前显示号（x 为第 1 条内容脚注）
        QVERIFY(xml.contains(
            "<w:rPr><w:rStyle w:val=\"FootnoteReference\"/>"
            "<w:vertAlign w:val=\"superscript\"/></w:rPr><w:t>1</w:t>"));
        QCOMPARE(xml.count("<w:footnoteReference w:id=\"3\"/>"), 1);   // y
        QCOMPARE(xml.count("<w:footnoteReference w:id=\"4\"/>"), 1);   // z（无定义）
        const QString &fn = parts["word/footnotes.xml"];
        QVERIFY(fn.contains("定义甲"));
        QVERIFY(fn.contains("定义乙"));
        // def 之间的换行空白不得泄漏为正文 run
        QVERIFY(!xml.contains("<w:t xml:space=\"preserve\">\n"));
    }

    void testRunPropertiesOrderFollowsSchema() {
        // CT_RPr 子元素须按 schema 序列：rStyle → rFonts → b → i → strike →
        // color → sz → highlight → u → shd → vertAlign（审查 P2-9：原顺序 u 早于
        // strike、vertAlign 早于 highlight/sz、rFonts 晚于 sz、rStyle 追加末位）
        QString xml = exportDocumentXml(
            "<p><span style=\"color:#123456;background-color:#ABCDEF\">"
            "<code><strong><em><u><s><mark><small>全格式</small></mark></s></u></em></strong></code>"
            "</span></p>"
            "<p><sub>下</sub>和<sup>上</sup></p>");
        int rFonts = xml.indexOf("<w:rFonts w:ascii=\"Consolas\"");
        int b = xml.indexOf("<w:b/>");
        int i = xml.indexOf("<w:i/>");
        int strike = xml.indexOf("<w:strike/>");
        int color = xml.indexOf("<w:color w:val=\"123456\"/>");
        int sz = xml.indexOf("<w:sz w:val=\"18\"/>");
        int highlight = xml.indexOf("<w:highlight");
        int u = xml.indexOf("<w:u w:val=\"single\"/>");
        int shd = xml.indexOf("<w:shd w:val=\"clear\" w:color=\"auto\" w:fill=\"ABCDEF\"/>");
        QVERIFY(rFonts >= 0 && b > rFonts && i > b && strike > i && color > strike
                && sz > color && highlight > sz && u > highlight && shd > u);
        int vaSub = xml.indexOf("<w:vertAlign w:val=\"subscript\"/>");
        QVERIFY2(vaSub > shd, qPrintable(xml));   // vertAlign 在 shd 之后
        // 各元素恰好一次（hyperlink 曾重复输出第二个 w:color/w:u）
        QCOMPARE(xml.count("<w:color w:val=\"123456\"/>"), 1);
    }

    void testParagraphPropertiesOrderFollowsSchema() {
        // CT_PPrBase 子元素须按 schema 序列（… → pBdr → shd → … → spacing → ind →
        // … → jc → … → outlineLvl …），rPr 内 rFonts 早于 sz。顺序违规触发 Word
        // 「无法读取的内容」修复框（macOS Word A/B 实证，2026-09-14）：代码块段落
        // 曾 ind 早于 spacing、styles.xml 的 Heading 曾 outlineLvl 早于 spacing、
        // docDefaults 的 rPr 曾 sz 早于 rFonts
        auto parts = exportParts(
            "<pre><code class=\"language-cpp\">int a;</code></pre>"
            "<h2>标题</h2>",
            {"word/document.xml", "word/styles.xml"});
        const QString &doc = parts["word/document.xml"];
        int shd = doc.indexOf("<w:shd w:val=\"clear\" w:color=\"auto\" w:fill=\"F2F2F2\"/>");
        int spacing = doc.indexOf("<w:spacing w:before=\"0\" w:after=\"0\" w:line=\"240\" w:lineRule=\"auto\"/>");
        int ind = doc.indexOf("<w:ind w:left=\"360\" w:right=\"360\"/>");
        QVERIFY2(shd >= 0 && spacing > shd && ind > spacing, qPrintable(doc));

        const QString &styles = parts["word/styles.xml"];
        // docDefaults：rFonts 早于 sz（首个 sz=22 即 docDefaults 中的）
        int rFonts = styles.indexOf("<w:rFonts w:ascii=\"Arial\"");
        int sz = styles.indexOf("<w:sz w:val=\"22\"/>");
        QVERIFY2(rFonts >= 0 && sz > rFonts, qPrintable(styles));
        // Heading2 样式：spacing 早于 outlineLvl
        int h2 = styles.indexOf("w:styleId=\"Heading2\"");
        int sp2 = styles.indexOf("<w:spacing w:before=\"200\"", h2);
        int ol2 = styles.indexOf("<w:outlineLvl w:val=\"1\"/>", h2);
        QVERIFY2(h2 >= 0 && sp2 > h2 && ol2 > sp2, qPrintable(styles));
        // 悬空样式引用清零：document.xml 引用的 TableGrid、Hyperlink 的 basedOn
        // 目标 DefaultParagraphFont 都有定义
        QVERIFY2(styles.contains("w:styleId=\"TableGrid\""), qPrintable(styles));
        QVERIFY2(styles.contains("w:styleId=\"DefaultParagraphFont\""), qPrintable(styles));
    }

    void testExtractColorClampsOutOfRangeAndPercent() {
        // rgb 分量 clamp 0-255（超界曾产出 7 位非 ST_HexColor）+ 百分比写法支持
        QString xml = exportDocumentXml(
            "<p><span style=\"color:rgb(300,0,0)\">甲</span>"
            "<span style=\"color:rgb(100%,0%,0%)\">乙</span>"
            "<span style=\"color:rgb(50%,50%,50%)\">丙</span></p>");
        QVERIFY(xml.contains("<w:color w:val=\"FF0000\"/>"));   // 300→255；100%→255
        QVERIFY(xml.contains("<w:color w:val=\"808080\"/>"));   // 50%→127.5→128=0x80
        QVERIFY(!xml.contains("12C"));                           // 旧泄漏形态
    }

    void testImageOutsideDocDirFallsBackToPlaceholder() {
        // 目录围栏：../ 逃逸出文档目录的图片不读字节，降级 [图片] 占位。
        // 外部文件必须真实存在（否则「文件不存在」也会降级，测不出围栏本身）
        QTemporaryDir dir; QVERIFY(dir.isValid());
        const QString outside = QFileInfo(dir.path()).absolutePath()
                                + "/md-editor-fence-test.png";
        QFile f(outside);
        QVERIFY(f.open(QIODevice::WriteOnly));
        const unsigned char png[] = {0x89,0x50,0x4e,0x47,0x0d,0x0a,0x1a,0x0a,0,0,0,0x0d,0x49,0x48,0x44,0x52,
                                     0,0,0,1,0,0,0,1,0x08,0x06,0,0,0,0x1f,0x15,0xc4,0x89,0,0,0,0x0a,0x49,0x44,0x41,0x54,
                                     0x78,0x9c,0x63,0,1,0,0,0x05,0,1,0x0d,0x0a,0x2d,0xb4,0,0,0,0,0x49,0x45,0x4e,0x44,0xae,0x42,0x60,0x82};
        f.write(reinterpret_cast<const char*>(png), sizeof(png)); f.close();

        QString out = dir.path() + "/o.docx";
        DocxExporter ex;
        ex.exportToDocx("<p><img src=\"../md-editor-fence-test.png\" alt=\"外\"></p>",
                        dir.path(), out);
        f.remove();
        QProcess unzip;
        unzip.start("unzip", QStringList() << "-p" << out << "word/document.xml");
        QVERIFY(unzip.waitForFinished(10000));
        const QString xml = QString::fromUtf8(unzip.readAllStandardOutput());
        QVERIFY(xml.contains("[图片"));          // 降级占位
        QVERIFY(!xml.contains("<w:drawing>"));   // 未嵌入
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

    void testTableColspanGridSpan() {
        // 整行合并单元格（colspan=4）：必须写 <w:gridSpan w:val="4"/> 到 tcPr，
        // 且无 colgroup 时默认 tblGrid 按表头列数生成 4 个 gridCol——
        // 否则 Word 打开是 2 列网格 + 跨 4 列的单元格，结构非法。
        QString xml = exportDocumentXml(
            "<table><thead><tr><th>A</th><th>B</th><th>C</th><th>D</th></tr></thead>"
            "<tbody><tr><td colspan=\"4\">复杂嵌套</td></tr>"
            "<tr><td>1</td><td>2</td><td>3</td><td>4</td></tr></tbody></table>");
        QVERIFY2(xml.contains("<w:gridSpan w:val=\"4\"/>"), qPrintable(xml));
        QCOMPARE(xml.count("<w:gridCol"), 4);   // 默认 grid 按表头 4 列生成
        // 普通行不产生 gridSpan
        QVERIFY(!xml.contains("gridSpan w:val=\"1\""));
    }

    void testTableCellAlignmentMovesToParagraph() {
        // 回归：单元格水平对齐是段落属性（<w:pPr>/<w:jc>），不是单元格属性——
        // CT_TcPr 没有 w:jc 子元素，曾把它写进 <w:tcPr> 属 OOXML 违规，
        // Word 打开弹「发现无法读取的内容」修复框（WPS/LibreOffice 宽容不报）。
        QTemporaryDir dir; QVERIFY(dir.isValid());
        QString out = dir.path() + "/o.docx";
        DocxExporter ex;
        ex.exportToDocx(
            "<table><thead><tr><th style=\"text-align:center\">A</th></tr></thead>"
            "<tbody><tr><td style=\"text-align:right\">1</td></tr></tbody></table>",
            dir.path(), out);
        QVERIFY(QFile::exists(out));

        QProcess unzip; unzip.start("unzip", QStringList() << "-p" << out << "word/document.xml");
        QVERIFY(unzip.waitForFinished(10000));
        QString xml = QString::fromUtf8(unzip.readAllStandardOutput());

        // tcPr 内不得出现 w:jc（逐块检查，块数须为 2：th + td）
        QRegularExpression tcPrRe("<w:tcPr>(.*?)</w:tcPr>");
        auto it = tcPrRe.globalMatch(xml);
        int n = 0;
        while (it.hasNext()) {
            const QString blk = it.next().captured(1);
            QVERIFY2(!blk.contains("<w:jc"), qPrintable(blk));
            ++n;
        }
        QVERIFY2(n == 2, qPrintable(xml));
        // 对齐落在单元格段落的 pPr 里
        QVERIFY2(xml.contains("<w:pPr><w:jc w:val=\"center\"/></w:pPr>"), qPrintable(xml));
        QVERIFY2(xml.contains("<w:pPr><w:jc w:val=\"right\"/></w:pPr>"), qPrintable(xml));
    }

    void testTableColgroupWidthsScaleToPage() {
        // 带列宽 colgroup 的表格（编辑器 getExportHtml 注入）：按页内容宽
        // 等比缩放（A4 纵向 1.8cm 边距 ⇒ 内容 174mm = 9865 twips），
        // 比例 100:300 ⇒ 2466 / 7399（末列吸收取整余量）；tblLayout fixed；
        // 逐格 tcW 与所跨列缩放宽一致
        QSettings s("md-editor", "md-editor");
        s.setValue("export/paperSize", static_cast<int>(QPageSize::A4));
        s.setValue("export/orientation", static_cast<int>(QPageLayout::Portrait));
        s.setValue("export/marginTop", 1.8);
        s.setValue("export/marginBottom", 1.8);
        s.setValue("export/marginLeft", 1.8);
        s.setValue("export/marginRight", 1.8);
        const QString xml = exportDocumentXml(
            "<table><colgroup><col style=\"width:100px\"><col style=\"width:300px\">"
            "</colgroup><tbody><tr><th>A</th><th>B</th></tr>"
            "<tr><td>x</td><td>y</td></tr></tbody></table>");
        // 100px/300px → dxa 1500/4500，sum 6000；f = 9865/6000 ⇒ 2466.25→2466，
        // 末列 9865-2466=7399
        QVERIFY(xml.contains("<w:tblLayout w:type=\"fixed\"/>"));
        QVERIFY(xml.contains("<w:gridCol w:w=\"2466\"/>"));
        QVERIFY(xml.contains("<w:gridCol w:w=\"7399\"/>"));
        QVERIFY(xml.contains("<w:tcW w:w=\"2466\" w:type=\"dxa\"/>"));
        QVERIFY(xml.contains("<w:tcW w:w=\"7399\" w:type=\"dxa\"/>"));
        // 表宽钳到页内容宽
        QVERIFY(xml.contains("<w:tblW w:w=\"9865\" w:type=\"dxa\"/>"));
    }

    void testSeparateOrderedListsRestartNumbering() {
        // 回归：两个不相邻的有序列表曾共享同一 numId，Word 把它们当作同一编号序列，
        // 第二个列表会从上一个列表的序号续编（如 1,2 后接 3,4 而非 1,2）。
        // 修复：每个 <ul>/<ol> 分配独立 numId，各自重新编号。
        QTemporaryDir dir; QVERIFY(dir.isValid());
        QString out = dir.path() + "/o.docx";
        DocxExporter ex;
        ex.exportToDocx(
            "<ol><li><p>一</p></li><li><p>二</p></li></ol>"
            "<ul data-type=\"taskList\">"
            "<li data-type=\"taskItem\"><label><input type=\"checkbox\"></label><div><p>事项</p></div></li>"
            "</ul>"
            "<ol><li><p>甲</p></li><li><p>乙</p></li></ol>", dir.path(), out);
        QVERIFY(QFile::exists(out));

        QProcess unzip; unzip.start("unzip", QStringList() << "-p" << out << "word/document.xml");
        QVERIFY(unzip.waitForFinished(10000));
        QString docXml = QString::fromUtf8(unzip.readAllStandardOutput());

        // 三个列表（有序 + 任务 + 有序）应引用三个不同的 numId
        QRegularExpression re("<w:numId w:val=\"(\\d+)\"/>");
        auto it = re.globalMatch(docXml);
        QSet<QString> ids;
        while (it.hasNext()) ids.insert(it.next().captured(1));
        QVERIFY2(ids.size() == 3, qPrintable(docXml));

        // numbering.xml：共享 bullet abstractNum(0) + 每个有序列表独立 decimal abstractNum，
        // 配三个独立 <w:num> 实例。两个有序列表各引用自己的 abstractNum（1/2），计数器天然
        // 从 1 重新编号，无需 startOverride（startOverride 落在嵌套 ilvl 时部分渲染器逐段重启）。
        unzip.start("unzip", QStringList() << "-p" << out << "word/numbering.xml");
        QVERIFY(unzip.waitForFinished(10000));
        QString numXml = QString::fromUtf8(unzip.readAllStandardOutput());
        QVERIFY2(numXml.count("<w:abstractNum ") == 3, qPrintable(numXml));
        QVERIFY2(numXml.count("<w:num ") == 3, qPrintable(numXml));
        QVERIFY2(!numXml.contains("startOverride"), qPrintable(numXml));
        // 有序列表 1 → abstractNum 1；任务列表 → abstractNum 0；有序列表 2 → abstractNum 2
        QVERIFY2(numXml.contains("<w:num w:numId=\"1\"><w:abstractNumId w:val=\"1\"/></w:num>"), qPrintable(numXml));
        QVERIFY2(numXml.contains("<w:num w:numId=\"2\"><w:abstractNumId w:val=\"0\"/></w:num>"), qPrintable(numXml));
        QVERIFY2(numXml.contains("<w:num w:numId=\"3\"><w:abstractNumId w:val=\"2\"/></w:num>"), qPrintable(numXml));
    }

    void testNestedListIndentation() {
        // 回归：嵌套列表（无序/有序/任务）曾丢失缩进——嵌套子项被当作父列表的
        // ilvl=0 同级项输出（numId 沿用父列表、ilvl 恒为 0），Word 中与父项同列。
        // 修复：嵌套 <ul>/<ol> 递归解析，ilvl 随嵌套深度递增，numbering.xml 按层定义缩进。
        // 任务列表的嵌套 <ul> 在 taskItem 的 <div> 内容容器内部（真实 TipTap 结构），
        // 须同样递归——曾因 parseInline 把子任务文字吞进父段、嵌套列表整体消失。
        QTemporaryDir dir; QVERIFY(dir.isValid());
        QString out = dir.path() + "/o.docx";
        DocxExporter ex;
        ex.exportToDocx(
            "<ul><li><p>父级</p>"
            "<ul><li><p>无序子级</p></li></ul>"
            "</li></ul>"
            "<ol><li><p>第一</p>"
            "<ol><li><p>有序子级</p></li></ol>"
            "</li></ol>"
            "<ul data-type=\"taskList\"><li data-type=\"taskItem\"><label><input type=\"checkbox\"></label>"
            "<div><p>任务</p>"
            "<ul data-type=\"taskList\"><li data-type=\"taskItem\"><label><input type=\"checkbox\"></label>"
            "<div><p>任务子级</p></div></li></ul>"
            "</div></li></ul>",
            dir.path(), out);
        QVERIFY(QFile::exists(out));

        QProcess unzip; unzip.start("unzip", QStringList() << "-p" << out << "word/document.xml");
        QVERIFY(unzip.waitForFinished(10000));
        QString docXml = QString::fromUtf8(unzip.readAllStandardOutput());

        // 取出包含指定文本的 <w:p>...</w:p> 块，用于断言该段落所属列表层级
        auto paraOf = [](const QString &xml, const QString &text) {
            QRegularExpression re("<w:p>([\\s\\S]*?)</w:p>");
            auto it = re.globalMatch(xml);
            while (it.hasNext()) {
                const QString para = it.next().captured(1);
                if (para.contains(text)) return para;
            }
            return QString();
        };

        // 父级项在 ilvl=0，嵌套子级项必须升到 ilvl=1（根因：曾全部落 0）
        QVERIFY2(paraOf(docXml, "父级").contains("<w:ilvl w:val=\"0\"/>"), qPrintable(docXml));
        QVERIFY2(paraOf(docXml, "无序子级").contains("<w:ilvl w:val=\"1\"/>"), qPrintable(docXml));
        QVERIFY2(paraOf(docXml, "有序子级").contains("<w:ilvl w:val=\"1\"/>"), qPrintable(docXml));
        QVERIFY2(paraOf(docXml, "任务子级").contains("<w:ilvl w:val=\"1\"/>"), qPrintable(docXml));
        // 嵌套任务文字不得并入父任务段落（回归：曾因 parseInline 吞掉嵌套列表文字，
        // 父段 "任务" 与子段 "任务子级" 拼在同一 <w:p> 里）
        QString parentPara = paraOf(docXml, "任务");
        QVERIFY2(parentPara.contains("任务"), qPrintable(docXml));
        QVERIFY2(!parentPara.contains("任务子级"), qPrintable(parentPara));
        // 嵌套子任务须保留自己的复选框前缀（不并入父段时复选框也不得丢失）
        QString childPara = paraOf(docXml, "任务子级");
        QVERIFY2(childPara.contains("☐ "), qPrintable(childPara));
        QVERIFY2(childPara.contains("Segoe UI Symbol"), qPrintable(childPara));
        // 三个顶层列表 + 三个嵌套列表 = 六个独立 numId
        QRegularExpression idRe("<w:numId w:val=\"(\\d+)\"/>");
        auto it = idRe.globalMatch(docXml);
        QSet<QString> ids;
        while (it.hasNext()) ids.insert(it.next().captured(1));
        QVERIFY2(ids.size() == 6, qPrintable(docXml));

        // numbering.xml 须为嵌套层级定义递增缩进（ilvl=1 → left=1440）
        unzip.start("unzip", QStringList() << "-p" << out << "word/numbering.xml");
        QVERIFY(unzip.waitForFinished(10000));
        QString numXml = QString::fromUtf8(unzip.readAllStandardOutput());
        QVERIFY2(numXml.contains("<w:lvl w:ilvl=\"1\""), qPrintable(numXml));
        QVERIFY2(numXml.contains("<w:ind w:left=\"1440\" w:hanging=\"360\"/>"), qPrintable(numXml));
    }

    void testDeepNestedAndMixedListIndentation() {
        // 三层嵌套（ul→ol→ul 混合）：ilvl 随深度递增（0/1/2），
        // 嵌套有序列表引用自己的独立 abstractNum（id=1），计数器天然从 1 重启。
        QTemporaryDir dir; QVERIFY(dir.isValid());
        QString out = dir.path() + "/o.docx";
        DocxExporter ex;
        ex.exportToDocx(
            "<ul><li><p>根</p>"
            "<ol><li><p>中间</p>"
            "<ul><li><p>最深层</p></li></ul>"
            "</li></ol>"
            "</li></ul>",
            dir.path(), out);
        QVERIFY(QFile::exists(out));

        QProcess unzip; unzip.start("unzip", QStringList() << "-p" << out << "word/document.xml");
        QVERIFY(unzip.waitForFinished(10000));
        QString docXml = QString::fromUtf8(unzip.readAllStandardOutput());

        auto paraOf = [](const QString &xml, const QString &text) {
            QRegularExpression re("<w:p>([\\s\\S]*?)</w:p>");
            auto it = re.globalMatch(xml);
            while (it.hasNext()) {
                const QString para = it.next().captured(1);
                if (para.contains(text)) return para;
            }
            return QString();
        };
        QVERIFY2(paraOf(docXml, "根").contains("<w:ilvl w:val=\"0\"/>"), qPrintable(docXml));
        QVERIFY2(paraOf(docXml, "中间").contains("<w:ilvl w:val=\"1\"/>"), qPrintable(docXml));
        QVERIFY2(paraOf(docXml, "最深层").contains("<w:ilvl w:val=\"2\"/>"), qPrintable(docXml));

        unzip.start("unzip", QStringList() << "-p" << out << "word/numbering.xml");
        QVERIFY(unzip.waitForFinished(10000));
        QString numXml = QString::fromUtf8(unzip.readAllStandardOutput());
        // 有序子列表引用独立 abstractNum（id=1），无需 startOverride
        QVERIFY2(numXml.contains("<w:num w:numId=\"2\"><w:abstractNumId w:val=\"1\"/></w:num>"),
                 qPrintable(numXml));
        QVERIFY2(!numXml.contains("startOverride"), qPrintable(numXml));
        // 三层缩进定义齐备
        QVERIFY2(numXml.contains("<w:ind w:left=\"1440\" w:hanging=\"360\"/>"), qPrintable(numXml));
        QVERIFY2(numXml.contains("<w:ind w:left=\"2160\" w:hanging=\"360\"/>"), qPrintable(numXml));
    }

    void testTaskListCheckboxPrefix() {
        // 任务列表：无序列表（bullet）+ ☐/☒ 前缀 + 文本。复选框 run 显式指定
        // Segoe UI Symbol 字体（Calibri 无 U+2610/U+2612 字形，缺省回退不可靠）。
        // 回归：曾因 <w:p> 开标签长度误算（mid(4)）残留孤立 ">" 文本节点，Word 打开
        // 可能丢弃复选框段落、退化为普通无序列表。
        QString xml = exportDocumentXml(
            "<ul data-type=\"taskList\">"
            "<li data-type=\"taskItem\"><label><input type=\"checkbox\"></label><div><p>事项</p></div></li>"
            "<li data-type=\"taskItem\" data-checked=\"true\"><label><input type=\"checkbox\" checked=\"checked\"></label><div><p>完成</p></div></li>"
            "</ul>");
        QVERIFY(xml.contains("☐"));
        QVERIFY(xml.contains("☒"));
        QVERIFY(xml.contains("<w:numPr>"));             // 任务列表用无序列表（bullet）表示
        QVERIFY(xml.contains("Segoe UI Symbol"));       // 复选框 run 指定符号字体
        QVERIFY(!xml.contains("</w:r>>"));              // 无孤立 ">" 残留
    }

    void testTaskUlOlNestedNumbering() {
        // 回归：任务列表 → 普通无序列表 → 普通有序列表 三级嵌套（真实 TipTap 结构）。
        // 三级有序列表两个 item 必须共享同一 numId、引用独立 decimal abstractNum，
        // Word 才连续编号 1/2；曾因共享 abstractNum + startOverride 落在嵌套 ilvl，
        // 部分渲染器（LibreOffice/WPS）逐段重启，两项全显示 1。
        QTemporaryDir dir; QVERIFY(dir.isValid());
        QString out = dir.path() + "/o.docx";
        DocxExporter ex;
        ex.exportToDocx(
            "<ul data-type=\"taskList\">"
            "<li data-type=\"taskItem\" data-checked=\"false\">"
            "<label><input type=\"checkbox\"><span></span></label><div><p>任务1</p></div></li>"
            "<li data-type=\"taskItem\" data-checked=\"true\"><label><input type=\"checkbox\" checked=\"checked\">"
            "<span></span></label><div><p>任务2</p><ul><li><p>列表2-1</p></li>"
            "<li><p>列表2-2</p><ol><li><p>已完成XXX工作，合格率100%</p></li>"
            "<li><p>已完成YYY工作，成功率99.9%，符合预期</p></li></ol></li></ul></div></li></ul>",
            dir.path(), out);
        QVERIFY(QFile::exists(out));

        QProcess unzip; unzip.start("unzip", QStringList() << "-p" << out << "word/document.xml");
        QVERIFY(unzip.waitForFinished(10000));
        QString docXml = QString::fromUtf8(unzip.readAllStandardOutput());

        // 三级有序列表两个 item 共享 numId=3、ilvl=2（连续编号 1/2 的前提）
        auto paraOf = [](const QString &xml, const QString &text) {
            QRegularExpression re("<w:p>([\\s\\S]*?)</w:p>");
            auto it = re.globalMatch(xml);
            while (it.hasNext()) {
                const QString para = it.next().captured(1);
                if (para.contains(text)) return para;
            }
            return QString();
        };
        const QString paraXXX = paraOf(docXml, "已完成XXX工作");
        const QString paraYYY = paraOf(docXml, "已完成YYY工作");
        QVERIFY2(paraXXX.contains("<w:ilvl w:val=\"2\"/><w:numId w:val=\"3\"/>"), qPrintable(paraXXX));
        QVERIFY2(paraYYY.contains("<w:ilvl w:val=\"2\"/><w:numId w:val=\"3\"/>"), qPrintable(paraYYY));
        QRegularExpression re("<w:numId w:val=\"(\\d+)\"/>");
        auto it = re.globalMatch(docXml);
        QSet<QString> ids;
        while (it.hasNext()) ids.insert(it.next().captured(1));
        QCOMPARE(ids.size(), 3);   // taskList + 嵌套 ul + 嵌套 ol = 3 个 numId

        // numbering.xml：共享 bullet(0) + 唯一有序列表的独立 abstractNum(1)
        unzip.start("unzip", QStringList() << "-p" << out << "word/numbering.xml");
        QVERIFY(unzip.waitForFinished(10000));
        QString numXml = QString::fromUtf8(unzip.readAllStandardOutput());
        QVERIFY2(numXml.contains("<w:num w:numId=\"3\"><w:abstractNumId w:val=\"1\"/></w:num>"),
                 qPrintable(numXml));
        QVERIFY2(!numXml.contains("startOverride"), qPrintable(numXml));
        // lvlText 须引用当前层级自己的序号：%1=ilvl0、%2=ilvl1、%3=ilvl2。
        // 曾统一写 "%1."，ilvl=2 的嵌套有序项显示 ilvl=0 计数器（该 numId 从不在
        // ilvl=0 出现，恒为 1），导致所有嵌套项都显示 1。
        QRegularExpression lvlRe("<w:lvl w:ilvl=\"2\">\\s*<w:start w:val=\"1\"/>"
                                 "\\s*<w:numFmt w:val=\"decimal\"/>"
                                 "\\s*<w:lvlText w:val=\"([^\"]+)\"/>");
        const auto lvl2 = lvlRe.match(numXml);
        QVERIFY2(lvl2.hasMatch(), qPrintable(numXml));
        QVERIFY2(lvl2.captured(1) == "%3.", qPrintable(lvl2.captured(1)));
    }

    void testBlockquoteNestedAndBlocksPreserved() {
        // D1：嵌套引用——内层 blockquote 闭合曾触发外层循环 break，其后同层段落
        // 全部丢失；引用内 pre/table 文本曾无人消费而丢失；列表退化为普通段落
        QString xml = exportDocumentXml(
            "<blockquote><p>外层</p><blockquote><p>内层</p></blockquote><p>内层闭合后</p></blockquote>");
        QVERIFY2(xml.contains("外层"), qPrintable(xml));
        QVERIFY2(xml.contains("内层"), qPrintable(xml));
        QVERIFY2(xml.contains("内层闭合后"), qPrintable(xml));

        QString xml2 = exportDocumentXml(
            "<blockquote><pre><code>code-in-quote</code></pre></blockquote>");
        QVERIFY2(xml2.contains("code-in-quote"), qPrintable(xml2));

        QString xml3 = exportDocumentXml(
            "<blockquote><table><tbody><tr><td>cell-in-quote</td></tr></tbody></table></blockquote>");
        QVERIFY2(xml3.contains("cell-in-quote"), qPrintable(xml3));

        QString xml4 = exportDocumentXml(
            "<blockquote><ul><li><p>item-in-quote</p></li></ul></blockquote>");
        QVERIFY2(xml4.contains("item-in-quote"), qPrintable(xml4));
        QVERIFY2(xml4.contains("<w:numPr>"), qPrintable(xml4));
    }

    void testExtractColorValidatesHex() {
        // D2：CSS 命名色/3 位缩写/非法值不得产出非法 ST_HexColor（Word 严格校验
        // 弹修复框）；rgb()/6 位 hex 正常
        QString xml = exportDocumentXml(
            "<p><span style=\"color:red\">红</span>"
            "<span style=\"color:#f00\">缩写</span>"
            "<span style=\"color:#FF0000\">正常</span>"
            "<span style=\"color:inherit\">非法</span>"
            "<span style=\"background-color:rgb(255,200,0)\">背景</span></p>");
        // 6 位 hex 与 rgb() 正常输出
        QVERIFY2(xml.contains("w:val=\"FF0000\""), qPrintable(xml));
        QVERIFY2(xml.contains("w:fill=\"FFC800\""), qPrintable(xml));
        // 命名色 red → FF0000（转换而非原样大写 RED）
        QVERIFY2(!xml.contains("w:val=\"RED\""), qPrintable(xml));
        QVERIFY2(!xml.contains("w:val=\"F00\""), qPrintable(xml));
        QVERIFY2(!xml.contains("w:val=\"INHERIT\""), qPrintable(xml));
    }

    void testNestedSpanColorsStacked() {
        // D3：嵌套 span 颜色栈——内层闭合后外层颜色须恢复（曾无条件 clear）
        QString xml = exportDocumentXml(
            "<p><span style=\"color:#FF0000\">A"
            "<span style=\"background-color:#FFFF00\">B</span>C</span></p>");
        // A 纯红；B 红字黄底；C 内层闭合后恢复纯红（无黄底）
        QVERIFY2(xml.contains(
            "<w:color w:val=\"FF0000\"/></w:rPr><w:t xml:space=\"preserve\">A"),
            qPrintable(xml));
        QVERIFY2(xml.contains(
            "<w:color w:val=\"FF0000\"/><w:shd w:val=\"clear\" w:color=\"auto\" w:fill=\"FFFF00\"/></w:rPr><w:t xml:space=\"preserve\">B"),
            qPrintable(xml));
        QVERIFY2(xml.contains(
            "<w:color w:val=\"FF0000\"/></w:rPr><w:t xml:space=\"preserve\">C"),
            qPrintable(xml));
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

    void testBlockquoteHasLeftBorder() {
        // 引用块须有左侧蓝色竖线（对齐 PDF/HTML 的 border-left），
        // 且边框与缩进共存于同一个 <w:pPr>，不得拆成两个。
        QString xml = exportDocumentXml("<blockquote><p>引用文本</p></blockquote>");
        QVERIFY(xml.contains("引用文本"));
        QVERIFY(xml.contains("<w:pBdr>"));
        QVERIFY(xml.contains("<w:left w:val=\"single\""));
        QVERIFY(xml.contains("w:color=\"0078d4\""));
        QCOMPARE(xml.count("<w:pPr>"), 1);
    }

    void testCjkTextNotMisclassifiedAsEmoji() {
        // 中文本体不得使用 Noto Color Emoji 字体（应走 eastAsia 字体回退）
        QString xml = exportDocumentXml("<p>中文测试段落</p>");
        QVERIFY(xml.contains("中文测试段落"));
        QVERIFY(!xml.contains("Noto Color Emoji"));
    }

    void testDefaultFontIsChineseFriendly() {
        // 默认正文（docDefaults）字体须为中文黑体 + 西文 Arial，而非无中文字形的 Calibri。
        // 字体定义在 word/styles.xml（docDefaults），不在 document.xml。
        QTemporaryDir dir; QVERIFY(dir.isValid());
        QString out = dir.path() + "/o.docx";
        DocxExporter ex;
        ex.exportToDocx("<p>中文测试 English 123</p>", dir.path(), out);
        QVERIFY(QFile::exists(out));

        QProcess unzip;
        unzip.start("unzip", QStringList() << "-p" << out << "word/styles.xml");
        QVERIFY(unzip.waitForFinished(10000));
        QString styles = QString::fromUtf8(unzip.readAllStandardOutput());
        QVERIFY2(styles.contains("w:eastAsia=\"黑体\""), qPrintable(styles));
        QVERIFY(styles.contains("w:ascii=\"Arial\""));
        QVERIFY(!styles.contains("w:eastAsia=\"Calibri\""));
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
        // 图片一律仅嵌 PNG，不挂 <asvg:svgBlip>：Word/WPS 的 SVG 渲染器忽略 <style> 与
        // <marker>，mermaid 时序图箭头会丢失，故统一走 Chromium 栅格化的 PNG 兜底
        QVERIFY(!docXml.contains("svgBlip"));
        QVERIFY(!docXml.contains("asvg:"));

        // media 部件存在
        unzip.start("unzip", QStringList() << "-l" << out);
        QVERIFY(unzip.waitForFinished(10000));
        QString listing = QString::fromUtf8(unzip.readAllStandardOutput());
        QVERIFY(listing.contains("word/media/image1.png"));
    }

    void testSvgImageFallsBackToPlaceholder() {
        // SVG 图片（非 mermaid）无 DOCX 嵌入方案，应回退 [图片] 占位而非嵌入成损坏 jpg
        QTemporaryDir dir; QVERIFY(dir.isValid());
        QFile f(dir.path() + "/x.svg");
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("<svg xmlns=\"http://www.w3.org/2000/svg\"></svg>"); f.close();

        QString out = dir.path() + "/o.docx";
        DocxExporter ex;
        ex.exportToDocx("<p><img src=\"x.svg\" alt=\"矢量图\"></p>", dir.path(), out);
        QVERIFY(QFile::exists(out));

        QProcess unzip; unzip.start("unzip", QStringList() << "-p" << out << "word/document.xml");
        QVERIFY(unzip.waitForFinished(10000));
        QString docXml = QString::fromUtf8(unzip.readAllStandardOutput());
        QVERIFY(!docXml.contains("<w:drawing>"));   // 不嵌入
        QVERIFY(docXml.contains("[图片"));
        QVERIFY(docXml.contains("矢量图"));           // alt 保留
    }

    void testEmptyImageFileFallsBackToPlaceholder() {
        // 零字节图片文件应回退 [图片] 占位，不得嵌入为损坏图片
        QTemporaryDir dir; QVERIFY(dir.isValid());
        QFile f(dir.path() + "/empty.png");
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.close(); // 零字节文件

        QString out = dir.path() + "/o.docx";
        DocxExporter ex;
        ex.exportToDocx("<p><img src=\"empty.png\" alt=\"空图\"></p>", dir.path(), out);
        QVERIFY(QFile::exists(out));

        QProcess unzip; unzip.start("unzip", QStringList() << "-p" << out << "word/document.xml");
        QVERIFY(unzip.waitForFinished(10000));
        QString docXml = QString::fromUtf8(unzip.readAllStandardOutput());
        QVERIFY(!docXml.contains("<w:drawing>"));   // 不嵌入
        QVERIFY(docXml.contains("[图片"));
        QVERIFY(docXml.contains("空图"));             // alt 保留
    }

    void testExtractMermaidSources() {
        // mermaid 源码提取是纯函数（不构造 MermaidRenderer，无 WebEngine 依赖）。
        // HTML 中源码被转义（&gt;），提取后应还原为原始 mermaid 语法。
        QString html = "<pre><code class=\"language-mermaid\">graph TD;A--&gt;B;</code></pre>";
        QStringList srcs = DocxExporter::extractMermaidSources(html);
        QCOMPARE(srcs.size(), 1);
        QVERIFY(srcs[0].contains("graph TD"));
        QVERIFY(srcs[0].contains("-->"));
    }

    void testExtractMermaidSourcesMultipleAndOrder() {
        // 多个 mermaid 块按文档顺序提取，序号与占位替换对齐
        QString html = "<p>前文</p>"
                       "<pre><code class=\"language-mermaid\">graph LR;A--&gt;B</code></pre>"
                       "<p>中间</p>"
                       "<pre data-x=\"1\"><code class=\"language-mermaid\">pie title 占比\n\"A\": 30</code></pre>";
        QStringList srcs = DocxExporter::extractMermaidSources(html);
        QCOMPARE(srcs.size(), 2);
        QVERIFY(srcs[0].contains("graph LR"));
        QVERIFY(srcs[1].contains("pie title"));
    }

    void testExtractMermaidSourcesNoMatch() {
        // 非 mermaid 代码块 / 普通文本不应被提取
        QString html = "<pre><code class=\"language-cpp\">int a;</code></pre><p>正文</p>";
        QCOMPARE(DocxExporter::extractMermaidSources(html).size(), 0);
    }

    void testMermaidRenderScriptReturnsPromise() {
        // 回归：渲染脚本的 IIFE 必须显式 return Promise.all(...)。若漏掉 return，
        // runJavaScript 立即拿到 undefined（异步渲染未完成即回传），SVG/PNG 为空，
        // DOCX 导出回退为 mermaid 源码文本而非图片。
        QString script = MermaidRenderer::buildRenderScript({"graph TD;A-->B"});
        QVERIFY2(script.contains("return Promise.all("), qPrintable(script));
    }

    void testLargeImageShrinksToPageWidth() {
        // 超宽图片（2000px）须按比例缩小到页面文本宽度（A4 纵向 1.8cm 边距 → 174mm）。
        QTemporaryDir dir; QVERIFY(dir.isValid());
        QImage big(2000, 1000, QImage::Format_ARGB32);
        big.fill(Qt::red);
        QVERIFY(big.save(dir.path() + "/big.png", "PNG"));

        // 固定纸张/边距使上限确定；测后恢复原值，避免污染用户设置
        QSettings s("md-editor", "md-editor");
        const QVariant oldPaper = s.value("export/paperSize");
        const QVariant oldOrient = s.value("export/orientation");
        const QVariant oldMl = s.value("export/marginLeft");
        const QVariant oldMr = s.value("export/marginRight");
        s.setValue("export/paperSize", static_cast<int>(QPageSize::A4));
        s.setValue("export/orientation", static_cast<int>(QPageLayout::Portrait));
        s.setValue("export/marginLeft", 1.8);
        s.setValue("export/marginRight", 1.8);

        QString out = dir.path() + "/o.docx";
        DocxExporter ex;
        ex.exportToDocx("<p><img src=\"big.png\"></p>", dir.path(), out);
        QVERIFY(QFile::exists(out));

        QProcess unzip; unzip.start("unzip", QStringList() << "-p" << out << "word/document.xml");
        QVERIFY(unzip.waitForFinished(10000));
        QString docXml = QString::fromUtf8(unzip.readAllStandardOutput());
        // 2000px→19,050,000 EMU，超上限；上限 174mm×36000=6,264,000 EMU，高按比例 3,132,000
        QVERIFY2(docXml.contains("cx=\"6264000\""), qPrintable(docXml));
        QVERIFY2(docXml.contains("cy=\"3132000\""), qPrintable(docXml));

        // sectPr 声明纸张/页边距（A4 纵向 1.8cm 边距），使 Word 排版与图片上限一致
        QVERIFY2(docXml.contains("<w:sectPr>"), qPrintable(docXml));
        QVERIFY2(docXml.contains("<w:pgSz w:w=\"11906\" w:h=\"16838\"/>"), qPrintable(docXml));
        QVERIFY2(docXml.contains("w:left=\"1020\""), qPrintable(docXml));

        if (oldPaper.isValid()) s.setValue("export/paperSize", oldPaper); else s.remove("export/paperSize");
        if (oldOrient.isValid()) s.setValue("export/orientation", oldOrient); else s.remove("export/orientation");
        if (oldMl.isValid()) s.setValue("export/marginLeft", oldMl); else s.remove("export/marginLeft");
        if (oldMr.isValid()) s.setValue("export/marginRight", oldMr); else s.remove("export/marginRight");
    }





    void testTallImageShrinksToPageHeight() {
        // 超高图片（纵向超长 mermaid 图同路径）须等比缩到页面内容高度内：
        // <wp:extent cy> 超页高时 Word 不会把内联图片跨页排版，超出部分直接裁掉。
        // 上限与 PDF 导出同源：A4 纵向 297mm − 上下边距 36mm − 2mm 安全间隙 = 259mm
        // = 9,324,000 EMU。400×4000px 图：高 38,100,000 EMU 超限，等比缩放后
        // cy=9,324,000、cx=3,810,000×9,324,000/38,100,000=932,400。
        QTemporaryDir dir; QVERIFY(dir.isValid());
        QImage tall(400, 4000, QImage::Format_ARGB32);
        tall.fill(Qt::blue);
        QVERIFY(tall.save(dir.path() + "/tall.png", "PNG"));

        QSettings s("md-editor", "md-editor");
        const QVariant oldPaper = s.value("export/paperSize");
        const QVariant oldOrient = s.value("export/orientation");
        const QVariant oldMt = s.value("export/marginTop");
        const QVariant oldMb = s.value("export/marginBottom");
        s.setValue("export/paperSize", static_cast<int>(QPageSize::A4));
        s.setValue("export/orientation", static_cast<int>(QPageLayout::Portrait));
        s.setValue("export/marginTop", 1.8);
        s.setValue("export/marginBottom", 1.8);

        QString out = dir.path() + "/o.docx";
        DocxExporter ex;
        ex.exportToDocx("<p><img src=\"tall.png\"></p>", dir.path(), out);
        QVERIFY(QFile::exists(out));

        QProcess unzip; unzip.start("unzip", QStringList() << "-p" << out << "word/document.xml");
        QVERIFY(unzip.waitForFinished(10000));
        QString docXml = QString::fromUtf8(unzip.readAllStandardOutput());
        QVERIFY2(docXml.contains("cy=\"9324000\""), qPrintable(docXml));
        QVERIFY2(docXml.contains("cx=\"932400\""), qPrintable(docXml));

        if (oldPaper.isValid()) s.setValue("export/paperSize", oldPaper); else s.remove("export/paperSize");
        if (oldOrient.isValid()) s.setValue("export/orientation", oldOrient); else s.remove("export/orientation");
        if (oldMt.isValid()) s.setValue("export/marginTop", oldMt); else s.remove("export/marginTop");
        if (oldMb.isValid()) s.setValue("export/marginBottom", oldMb); else s.remove("export/marginBottom");
    }

    void testSmallTagUsesSmallerFontSize() {
        // <small> 小号字：DOCX 须输出缩小字号（正文 11pt → 9pt，w:sz=18），
        // 与编辑器/PDF 的浏览器 small 默认缩放（约 0.83×）对齐。
        // 曾无 small 分支，穿透后继承 docDefaults 11pt，与周围文字一样大。
        QString xml = exportDocumentXml(
            "<p>正常文字<small>小号补充</small>恢复正常</p>");
        QVERIFY(xml.contains("正常文字"));
        QVERIFY2(xml.contains("<w:sz w:val=\"18\"/></w:rPr><w:t xml:space=\"preserve\">小号补充"),
                 qPrintable(xml));
        // small 外的文字不得带 sz=18
        QVERIFY(!xml.contains("sz w:val=\"18\"/></w:rPr><w:t xml:space=\"preserve\">正常"));
        QVERIFY(!xml.contains("sz w:val=\"18\"/></w:rPr><w:t xml:space=\"preserve\">恢复正常"));
    }

    void testZipStructureOpcCompliant() {
        // 回归：DOCX 是 OPC 包，不是 ODF——不得写 ODF 式 mimetype 首条目。
        // mimetype 部件无扩展名、[Content_Types].xml 又未 Override 它，属
        // 「内容类型无法解析的部件」，Word 打开弹「发现无法读取的内容」修复框
        // （WPS/LibreOffice 宽容不报）。直接校验原始字节，不依赖外部 zip/unzip。
        QTemporaryDir dir; QVERIFY(dir.isValid());
        QString out = dir.path() + "/o.docx";
        DocxExporter ex;
        ex.exportToDocx("<p>结构校验</p>", dir.path(), out);
        QVERIFY(QFile::exists(out));

        QFile f(out);
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QByteArray raw = f.readAll();

        // 本地文件头签名 "PK\x03\x04"
        QVERIFY2(raw.startsWith("PK\x03\x04"), qPrintable(raw.left(8).toHex()));
        QVERIFY2(raw.size() >= 30, "zip 文件过小");
        // 首条目文件名（文件名长度 offset 26-27；文件名起始 offset 30）
        // 必须是 [Content_Types].xml，不得是 ODF 式 mimetype
        const quint16 nameLen = static_cast<quint8>(raw[26]) | (static_cast<quint8>(raw[27]) << 8);
        QCOMPARE(QString::fromLatin1(raw.mid(30, nameLen)), QString("[Content_Types].xml"));

        // 全包不得含 mimetype 部件
        QProcess unzip; unzip.start("unzip", QStringList() << "-l" << out);
        QVERIFY(unzip.waitForFinished(10000));
        const QString listing = QString::fromUtf8(unzip.readAllStandardOutput());
        QVERIFY2(!listing.contains(" mimetype"), qPrintable(listing));
    }
};

QTEST_APPLESS_MAIN(TestDocxExporter)
#include "test_docxexporter.moc"
