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

        // numbering.xml：bullet + decimal 两个 abstractNum，配三个独立 <w:num> 实例；
        // 两个有序列表须各带 startOverride 重启信号（否则 Word 按 abstractNumId 续编）。
        unzip.start("unzip", QStringList() << "-p" << out << "word/numbering.xml");
        QVERIFY(unzip.waitForFinished(10000));
        QString numXml = QString::fromUtf8(unzip.readAllStandardOutput());
        QVERIFY2(numXml.count("<w:abstractNum ") == 2, qPrintable(numXml));
        QVERIFY2(numXml.count("<w:num ") == 3, qPrintable(numXml));
        QVERIFY2(numXml.count("<w:startOverride") == 2, qPrintable(numXml));
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
        // 三层嵌套（ul→ol→ul 混合）与有序子列表重启：ilvl 随深度递增（0/1/2），
        // 嵌套有序列表的 startOverride 须落在其实际 ilvl（此处为 1），否则 Word/WPS
        // 会按 abstractNumId 续编不重启。
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
        // 有序子列表（numId=2）的 override 命中 ilvl=1
        QVERIFY2(numXml.contains("<w:lvlOverride w:ilvl=\"1\"><w:startOverride w:val=\"1\"/></w:lvlOverride>"),
                 qPrintable(numXml));
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
