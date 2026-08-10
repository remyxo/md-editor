#include <QTest>
#include <QRegularExpression>
#include "../src/core/MarkdownHandler.h"

class TestMarkdownHandler : public QObject {
    Q_OBJECT
private slots:
    void testBasicConversion() {
        QString md = "# Hello\n\nWorld";
        QString html = MarkdownHandler::toHtml(md);
        QVERIFY(!html.isEmpty());
        QVERIFY(html.contains("Hello"));
        QVERIFY(html.contains("World"));
    }

    void testPlainText() {
        QString md = "# Hello\n\nWorld";
        QString text = MarkdownHandler::toPlainText(md);
        QVERIFY(text.contains("Hello"));
        QVERIFY(text.contains("World"));
    }

    void testCodeBlock() {
        QString md = "```cpp\nint main() {}\n```";
        QString html = MarkdownHandler::toHtml(md);
        QVERIFY(html.contains("int main() {}"));
    }

    void testEmpty() {
        // 空输入不应崩溃，且纯文本为空
        QVERIFY(MarkdownHandler::toPlainText("").isEmpty());
        QString html = MarkdownHandler::toHtml("");
        QVERIFY(html.isEmpty() || !html.contains("Hello"));
    }

    void testBlockMathNoBlankLine() {
        // 块级公式前无空行：不应插入 <br>
        QString md = "块级公式1：\n$$\nE = mc^2\n$$\n\n继续正文\n";
        QString html = MarkdownHandler::toHtml(md);
        QVERIFY(html.contains("block-math"));
        QVERIFY(html.contains("E = mc^2"));
        // 关键：公式 div 之前不应有 <br>（那会产生多余空行）
        int mathPos = html.indexOf("block-math");
        QString before = mathPos > 0 ? html.mid(0, mathPos) : QString();
        QVERIFY2(!before.contains("<br>"),
                 "块级公式前不应有 <br>（会产生多余空行）");
    }

    void testBlockMathWithBlankLine() {
        // 块级公式前有空行：正常处理
        QString md = "块级公式1：\n\n$$\nE = mc^2\n$$\n\n继续正文\n";
        QString html = MarkdownHandler::toHtml(md);
        QVERIFY(html.contains("block-math"));
        QVERIFY(html.contains("E = mc^2"));
    }

    void testMultiLineBlockMath() {
        // 多行块级公式 → aligned 环境
        QString md = "$$\na = 1\nb = 2\nc = 3\n$$\n";
        QString html = MarkdownHandler::toHtml(md);
        QVERIFY(html.contains("block-math"));
        QVERIFY(html.contains("aligned"));
        QVERIFY(html.contains("a = 1"));
        QVERIFY(html.contains("b = 2"));
    }

    void testInlineMath() {
        // 行内公式 $...$ → inline-math
        QString md = "这是 $E=mc^2$ 行内公式\n";
        QString html = MarkdownHandler::toHtml(md);
        QVERIFY(html.contains("inline-math"));
        QVERIFY(html.contains("E=mc^2"));
    }

    void testInlineMathSumSquare() {
        // 回归测试：$...$ 内含多个 ^ 时，上标正则不应跨区域误匹配
        // Bug：\sum_{i=1}^{n} i^{2} 中 ^{n} 的 ^ 和 ^{2} 的 ^ 被上标正则
        //      跨区域匹配为 ^{n} i^，导致 data-latex 被破坏为
        //      \sum_{i=1}<sup>{n} i</sup>{2}
        QString md = "求和: $\\sum_{i=1}^{n} i^{2}$\n";
        QString html = MarkdownHandler::toHtml(md);
        QVERIFY(html.contains("inline-math"));
        // 关键断言：data-latex 不应被注入 <sup> 标签
        QVERIFY2(!html.contains("<sup>"),
                 qPrintable("上标正则不应破坏行内公式，实际: " + html));
        QVERIFY2(html.contains("\\sum_{i=1}^{n} i^{2}"),
                 qPrintable("data-latex 应保留完整 LaTeX，实际: " + html));
    }

    void testInlineCodeUnaffected() {
        // 行内代码 `...` 不受影响
        QString md = "使用 `printf(\"hello\")` 函数\n";
        QString html = MarkdownHandler::toHtml(md);
        QVERIFY(html.contains("<code>"));
        QVERIFY(html.contains("printf"));
    }

    void testCodeBlockWithDollar() {
        // 代码块内的 $ 不应被转换
        QString md = "```cpp\nint x = $value;\n```\n";
        QString html = MarkdownHandler::toHtml(md);
        QVERIFY(html.contains("$value"));
        QVERIFY(!html.contains("inline-math"));
        QVERIFY(html.contains("<pre"));
    }

    void testMixedInlineCodeAndMath() {
        // 行内代码 + 行内公式混合
        QString md = "`code` 和 $math$ 混合\n";
        QString html = MarkdownHandler::toHtml(md);
        QVERIFY(html.contains("<code>"));
        QVERIFY(html.contains("inline-math"));
    }

    void testMathRoundTrip() {
        // 块级公式往返：MD → HTML → MD
        QString md1 = "$$\nE = mc^2\n$$\n";
        QString html = MarkdownHandler::toHtml(md1);
        QVERIFY(html.contains("block-math"));
        QString md2 = MarkdownHandler::fromHtml(html);
        QVERIFY(md2.contains("$$"));
        QVERIFY(md2.contains("E = mc^2"));
    }

    void testMathTableBlankLinesRT() {
        // 块级公式 + 表格：往返后两者之间应恰好 1 空行（曾因 backup 末尾
        // 冗余 \n 与 md4c 块边界 \n\n\n 叠加而膨胀为 3 空行）
        QString html =
            "<div data-type=\"block-math\" data-latex=\"x = y\"></div>"
            "<table><tr><th>A</th></tr><tr><td>1</td></tr></table>";
        QString md = MarkdownHandler::fromHtml(html);
        QVERIFY2(!md.contains("$$\n\n\n"),
                 qPrintable("公式后不应出现多余空行，实际:\n" + md));
        QVERIFY2(md.startsWith("$$\nx = y\n$$\n\n|"),
                 qPrintable("公式与表格间应恰好 1 空行，实际:\n" + md));
    }

    void testMathParagraphOneBlankRT() {
        // 块级公式 + 段落：往返后两者间应恰好 1 空行（不误伤）
        QString html =
            "<div data-type=\"block-math\" data-latex=\"a = b\"></div>"
            "<p>正文段落</p>";
        QString md = MarkdownHandler::fromHtml(html);
        QVERIFY2(md.startsWith("$$\na = b\n$$\n\n正文段落"),
                 qPrintable("公式与段落间应恰好 1 空行，实际:\n" + md));
    }

    void testCodeBlockInnerBlanksPreserved() {
        // 代码块内部用户有意保留的多空行，往返后不应被块间空行压缩破坏
        QString md1 = "```\nline1\n\n\nline2\n```";
        QString html = MarkdownHandler::toHtml(md1);
        QString md2 = MarkdownHandler::fromHtml(html);
        QVERIFY2(md2.contains("line1\n\n\nline2"),
                 qPrintable("代码块内部 2 空行应原样保留，实际:\n" + md2));
    }

    void testInlineMathRoundTrip() {
        // 行内公式往返：MD → HTML → MD
        QString md1 = "这是 $E=mc^2$ 公式\n";
        QString html = MarkdownHandler::toHtml(md1);
        QVERIFY(html.contains("inline-math"));
        QString md2 = MarkdownHandler::fromHtml(html);
        QVERIFY(md2.contains("$E=mc^2$"));
    }

    void testMultiLineMathRoundTrip() {
        // 多行块级公式往返（含 aligned）
        QString md1 = "$$\na = 1\nb = 2\n$$\n";
        QString html = MarkdownHandler::toHtml(md1);
        QVERIFY(html.contains("aligned"));
        QString md2 = MarkdownHandler::fromHtml(html);
        QVERIFY(md2.contains("$$"));
        QVERIFY(md2.contains("a = 1"));
        QVERIFY(md2.contains("b = 2"));
        // 往返后不应含 aligned
        QVERIFY(!md2.contains("aligned"));
    }

    void testMathRoundTripTipTapOrder() {
        // TipTap Mathematics 扩展输出 data-latex 在 data-type 之前
        // fromHtml() 的正则必须能匹配任意属性顺序
        QString html = "<p>行内 <span data-latex=\"E=mc^2\" data-type=\"inline-math\"></span> 公式</p>"
                       "<div data-latex=\"a = 1\" data-type=\"block-math\"></div>";
        QString md = MarkdownHandler::fromHtml(html);
        QVERIFY(md.contains("$$"));
        QVERIFY(md.contains("a = 1"));
        QVERIFY(md.contains("$E=mc^2$"));
    }

    void testCodeBlockLanguageRoundTrip() {
        // 代码块语言标记往返：MD → HTML → MD，语言应保留
        // TipTap codeBlock 契约：<pre><code class="language-xxx">
        QString md1 = "```cpp\nint main() {}\n```";
        QString html = MarkdownHandler::toHtml(md1);
        QVERIFY2(html.contains("class=\"language-cpp\""),
                 qPrintable("toHtml 应输出 language class，实际: " + html));
        QVERIFY2(html.contains("<pre><code"),
                 qPrintable("toHtml 应输出 <pre><code>，实际: " + html));
        QString md2 = MarkdownHandler::fromHtml(html);
        QVERIFY2(md2.contains("```cpp"),
                 qPrintable("fromHtml 应还原 ```cpp 围栏，实际: " + md2));
        QVERIFY(md2.contains("int main() {}"));
    }

    void testCodeBlockTipTapOutputRoundTrip() {
        // TipTap renderHTML 输出 <pre><code class="language-py"> → fromHtml 还原围栏
        QString html = "<pre><code class=\"language-py\">print(1)</code></pre>";
        QString md = MarkdownHandler::fromHtml(html);
        QVERIFY2(md.contains("```py"),
                 qPrintable("应还原 ```py 围栏，实际: " + md));
        QVERIFY(md.contains("print(1)"));
    }

    void testCodeBlockNoLanguageRoundTrip() {
        // 无语言代码块：往返不应产生多余语言标记
        QString md1 = "```\nplain code\n```";
        QString html = MarkdownHandler::toHtml(md1);
        QVERIFY(!html.contains("class=\"language-"));
        QString md2 = MarkdownHandler::fromHtml(html);
        QVERIFY(md2.contains("plain code"));
    }

    void testCodeBlockMultiLineLanguageRoundTrip() {
        // 多行代码块 + 语言：合并 <pre> 后语言仍保留
        QString md1 = "```js\nconst a = 1;\nconst b = 2;\n```";
        QString html = MarkdownHandler::toHtml(md1);
        QVERIFY2(html.contains("class=\"language-js\""),
                 qPrintable("多行代码块应保留 language-js，实际: " + html));
        QVERIFY(html.contains("const a = 1;"));
        QVERIFY(html.contains("const b = 2;"));
        QString md2 = MarkdownHandler::fromHtml(html);
        QVERIFY(md2.contains("```js"));
    }

    void testMermaidRoundTrip() {
        // Mermaid 图表往返：```mermaid 围栏 ↔ <pre><code class="language-mermaid">
        // （editor.js 的 Mermaid 节点 renderHTML/parseHTML 依赖此契约）
        QString md1 = "```mermaid\ngraph TD; A-->B\n```";
        QString html = MarkdownHandler::toHtml(md1);
        QVERIFY2(html.contains("class=\"language-mermaid\""),
                 qPrintable("toHtml 应输出 language-mermaid，实际: " + html));
        QVERIFY2(html.contains("<pre><code"),
                 qPrintable("toHtml 应输出 <pre><code>，实际: " + html));
        // 源码中的 > 在 HTML 中会被转义为 &gt;（正确行为）
        QVERIFY(html.contains("graph TD"));
        QVERIFY(html.contains("A--"));
        // 完整往返：MD → HTML → MD，源码应原样恢复（含 --> 箭头）
        QString md2 = MarkdownHandler::fromHtml(html);
        QVERIFY2(md2.contains("```mermaid"),
                 qPrintable("fromHtml 应还原 ```mermaid 围栏，实际: " + md2));
        QVERIFY2(md2.contains("A-->B"),
                 qPrintable("往返后源码 A-->B 应原样保留，实际: " + md2));
        // 反向：TipTap 产出的 HTML（含已转义源码）→ 还原 ```mermaid 围栏
        QString md3 = MarkdownHandler::fromHtml(
            "<pre><code class=\"language-mermaid\">sequenceDiagram\nAlice-&gt;&gt;Bob: Hi</code></pre>");
        QVERIFY2(md3.contains("```mermaid"),
                 qPrintable("fromHtml 应还原 ```mermaid 围栏，实际: " + md3));
        QVERIFY(md3.contains("sequenceDiagram"));
    }

    void testCodeBlockEntityRoundTrip() {
        // 代码块含 < > & 的往返：不应在 .md 中残留 HTML 实体
        QString md1 = "```cpp\nif (a < b && c > d) {}\n```";
        QString html = MarkdownHandler::toHtml(md1);
        QString md2 = MarkdownHandler::fromHtml(html);
        QVERIFY2(md2.contains("a < b && c > d"),
                 qPrintable("代码块中 < > & 应原样保留，实际: " + md2));
        QVERIFY(!md2.contains("&lt;"));
        QVERIFY(!md2.contains("&gt;"));
        QVERIFY(!md2.contains("&amp;"));
    }

    void testInlineCodeEntityRoundTrip() {
        // 行内代码含 < > & 的往返：TipTap 输出的 <code> 实体应还原
        QString md = MarkdownHandler::fromHtml(
            "<p>比较 <code>a &lt; b &amp;&amp; c &gt; d</code> 完成</p>");
        QVERIFY2(md.contains("`a < b && c > d`"),
                 qPrintable("行内代码实体应还原，实际: " + md));
        QVERIFY(!md.contains("&lt;"));
        QVERIFY(!md.contains("&amp;"));
    }

    void testSubscriptRoundTrip() {
        // 下标 Pandoc 语法 ~x~ 往返：MD → HTML → MD
        QString md1 = "H~2~O 是水\n";
        QString html = MarkdownHandler::toHtml(md1);
        QVERIFY2(html.contains("<sub>2</sub>"),
                 qPrintable("toHtml 应转换 ~2~ → <sub>2</sub>，实际: " + html));
        QString md2 = MarkdownHandler::fromHtml(html);
        QVERIFY2(md2.contains("~2~"),
                 qPrintable("fromHtml 应还原 ~2~，实际: " + md2));
    }

    void testSuperscriptRoundTrip() {
        // 上标 Pandoc 语法 ^x^ 往返：MD → HTML → MD
        QString md1 = "2^10^ = 1024\n";
        QString html = MarkdownHandler::toHtml(md1);
        QVERIFY2(html.contains("<sup>10</sup>"),
                 qPrintable("toHtml 应转换 ^10^ → <sup>10</sup>，实际: " + html));
        QString md2 = MarkdownHandler::fromHtml(html);
        QVERIFY2(md2.contains("^10^"),
                 qPrintable("fromHtml 应还原 ^10^，实际: " + md2));
    }

    void testStrikeNotSubscript() {
        // 删除线 ~~x~~ 不应被误判为下标 ~x~
        QString md1 = "~~删除线~~ 正文\n";
        QString html = MarkdownHandler::toHtml(md1);
        QVERIFY2(!html.contains("<sub>"),
                 qPrintable("删除线不应产生 <sub>，实际: " + html));
    }

    void testSubSupTipTapOutputRoundTrip() {
        // TipTap subscript/superscript 输出 <sub>/<sup> → fromHtml 还原 Pandoc 语法
        QString html = "<p>H<sub>2</sub>O 与 2<sup>10</sup></p>";
        QString md = MarkdownHandler::fromHtml(html);
        QVERIFY2(md.contains("~2~"), qPrintable("应还原下标 ~2~，实际: " + md));
        QVERIFY2(md.contains("^10^"), qPrintable("应还原上标 ^10^，实际: " + md));
    }

    // ===== 脚注（footnote）MD → HTML 契约 =====

    void testFootnoteRefToHtml() {
        QString md = "正文[^1]内容";
        QString html = MarkdownHandler::toHtml(md);
        QVERIFY2(html.contains("data-type=\"footnoteRef\""),
                 qPrintable("脚注引用未转为 sup，实际: " + html));
        QVERIFY2(html.contains("data-id=\"1\""),
                 qPrintable("data-id 缺失，实际: " + html));
    }

    void testFootnoteDefToHtml() {
        QString md = "正文[^1]\n\n[^1]: 这是定义";
        QString html = MarkdownHandler::toHtml(md);
        QVERIFY2(html.contains("data-type=\"footnoteContainer\""),
                 qPrintable("缺少脚注容器，实际: " + html));
        QVERIFY2(html.contains("data-type=\"footnoteDef\""),
                 qPrintable("缺少脚注定义，实际: " + html));
        QVERIFY2(html.contains("这是定义"),
                 qPrintable("定义内容丢失，实际: " + html));
    }

    void testFootnoteChineseId() {
        QString md = "正文[^注1]\n\n[^注1]: 中文脚注";
        QString html = MarkdownHandler::toHtml(md);
        QVERIFY2(html.contains("data-id=\"注1\""),
                 qPrintable("中文 id 未保留，实际: " + html));
        QVERIFY2(html.contains("中文脚注"),
                 qPrintable("中文定义丢失，实际: " + html));
    }

    void testFootnoteInCodeNotConverted() {
        // 行内代码内的 [^x] 不转换
        QString md1 = "代码 `[^x]` 内";
        QString html1 = MarkdownHandler::toHtml(md1);
        QVERIFY2(!html1.contains("data-type=\"footnoteRef\""),
                 qPrintable("行内代码内脚注被误转，实际: " + html1));
        // 代码块内的 [^x] 不转换
        QString md2 = "```\n[^x]\n```";
        QString html2 = MarkdownHandler::toHtml(md2);
        QVERIFY2(!html2.contains("data-type=\"footnoteRef\""),
                 qPrintable("代码块内脚注被误转，实际: " + html2));
    }

    void testFootnoteMultiRefOneDef() {
        // 同 id 多次引用，定义去重
        QString md = "前[^1]中[^1]后\n\n[^1]: 共享定义";
        QString html = MarkdownHandler::toHtml(md);
        // 引用出现两次
        int refCount = html.count("data-type=\"footnoteRef\"");
        QVERIFY2(refCount == 2,
                 qPrintable(QString("期望 2 个引用，实际 %1: ").arg(refCount) + html));
        // 定义只出现一次
        int defCount = html.count("data-type=\"footnoteDef\"");
        QVERIFY2(defCount == 1,
                 qPrintable(QString("定义应去重为 1，实际 %1: ").arg(defCount) + html));
    }

    void testFootnoteDanglingRef() {
        // 悬空引用（有 ref 无 def）不报错
        QString md = "正文[^1]无定义";
        QString html = MarkdownHandler::toHtml(md);
        QVERIFY2(html.contains("data-type=\"footnoteRef\""),
                 qPrintable("悬空引用应正常转为 sup，实际: " + html));
        QVERIFY2(!html.contains("data-type=\"footnoteContainer\""),
                 qPrintable("无定义不应出现容器，实际: " + html));
    }

    void testFootnoteDefSubscript() {
        // 定义内容内的下标 ~x~ 须正确还原为 <sub>（不残留 ZZSB 占位符）
        QString md = "正文[^1]\n\n[^1]: a~b~c";
        QString html = MarkdownHandler::toHtml(md);
        QVERIFY2(html.contains("<sub>b</sub>"),
                 qPrintable("定义内下标未还原，实际: " + html));
        QVERIFY2(!html.contains("ZZSB"),
                 qPrintable("残留 ZZSB 占位符，实际: " + html));
    }

    // ===== 脚注（footnote）HTML → MD 往返 =====

    void testFootnoteRefRoundTrip() {
        QString md = "正文[^1]内容";
        QString html = MarkdownHandler::toHtml(md);
        QString back = MarkdownHandler::fromHtml(html);
        QVERIFY2(back.contains("[^1]"),
                 qPrintable("引用往返丢失，实际: " + back));
    }

    void testFootnoteDefRoundTrip() {
        QString md = "正文[^1]\n\n[^1]: 这是定义";
        QString back = MarkdownHandler::fromHtml(MarkdownHandler::toHtml(md));
        QVERIFY2(back.contains("[^1]"),
                 qPrintable("引用往返丢失，实际: " + back));
        QVERIFY2(back.contains("[^1]: 这是定义"),
                 qPrintable("定义行往返丢失，实际: " + back));
    }

    void testFootnoteRichContentRoundTrip() {
        QString md = "见[^a]\n\n[^a]: **加粗** 与 [链](http://x)";
        QString back = MarkdownHandler::fromHtml(MarkdownHandler::toHtml(md));
        QVERIFY2(back.contains("[^a]:"),
                 qPrintable("自定义 id 定义丢失，实际: " + back));
        QVERIFY2(back.contains("**加粗**"),
                 qPrintable("定义内加粗丢失，实际: " + back));
        QVERIFY2(back.contains("[链](http://x)"),
                 qPrintable("定义内链接丢失，实际: " + back));
    }

    void testFootnoteMultiDefRoundTrip() {
        QString md = "一[^1]二[^2]\n\n[^1]: A\n[^2]: B";
        QString back = MarkdownHandler::fromHtml(MarkdownHandler::toHtml(md));
        QVERIFY2(back.contains("[^1]: A"),
                 qPrintable("定义1往返丢失，实际: " + back));
        QVERIFY2(back.contains("[^2]: B"),
                 qPrintable("定义2往返丢失，实际: " + back));
    }

    void testFootnoteDefSubscriptRoundTrip() {
        QString md = "正文[^1]\n\n[^1]: a~b~c";
        QString back = MarkdownHandler::fromHtml(MarkdownHandler::toHtml(md));
        QVERIFY2(back.contains("[^1]: a~b~c"),
                 qPrintable("定义内下标往返丢失，实际: " + back));
    }

    void testExtractTitle() {
        QString md = "# 我的文档标题\n\n内容。";
        QString title = MarkdownHandler::extractTitle(md);
        QCOMPARE(title, QString("我的文档标题"));
    }

    void testExtractTitleNoH1() {
        QCOMPARE(MarkdownHandler::extractTitle("## 二级标题\n\n内容"), QString());
    }

    // ===== 任务列表（GFM task list ↔ TipTap taskList/taskItem 契约）=====

    void testTaskListFromHtml() {
        // Bug1 保存方向：TipTap getHTML() 产出的任务列表 → GFM - [ ] / - [x]
        QString html =
            "<ul data-type=\"taskList\">"
            "<li data-type=\"taskItem\" data-checked=\"false\">"
            "<label><input type=\"checkbox\"><span></span></label>"
            "<div><p>任务A</p></div></li>"
            "<li data-type=\"taskItem\" data-checked=\"true\">"
            "<label><input type=\"checkbox\" checked=\"checked\"><span></span></label>"
            "<div><p>任务B，已完成</p></div></li>"
            "</ul>";
        QString md = MarkdownHandler::fromHtml(html);
        QVERIFY2(md.contains("- [ ] 任务A"),
                 qPrintable("未勾选任务应保存为 - [ ]，实际: " + md));
        QVERIFY2(md.contains("- [x] 任务B，已完成"),
                 qPrintable("已勾选任务应保存为 - [x]，实际: " + md));
    }

    void testTaskListToHtml() {
        // Bug1 加载方向：- [ ] / - [x] → TipTap taskList 契约 HTML
        // （TipTap parseHTML 只认 ul[data-type=taskList] / li[data-type=taskItem]）
        QString html = MarkdownHandler::toHtml("- [ ] 任务A\n- [x] 任务B");
        QVERIFY2(html.contains("<ul data-type=\"taskList\">"),
                 qPrintable("应输出 TipTap taskList 契约，实际: " + html));
        QVERIFY2(html.contains("data-type=\"taskItem\""),
                 qPrintable("应输出 taskItem，实际: " + html));
        QVERIFY2(html.contains("data-checked=\"false\""),
                 qPrintable("任务A 应为未勾选，实际: " + html));
        QVERIFY2(html.contains("data-checked=\"true\""),
                 qPrintable("任务B 应为已勾选，实际: " + html));
        // 不应残留 Qt 自家的 class=unchecked/checked 格式
        QVERIFY2(!html.contains("class=\"unchecked\""),
                 qPrintable("不应使用 Qt 自有任务列表格式，实际: " + html));
    }

    void testTaskListMdRoundTrip() {
        // MD → HTML → MD 全等往返
        QString md1 = "- [ ] 任务A\n- [x] 任务B";
        QString html = MarkdownHandler::toHtml(md1);
        QString md2 = MarkdownHandler::fromHtml(html);
        QCOMPARE(md2, md1);
    }

    void testTaskListHtmlRoundTrip() {
        // HTML → MD → HTML：契约结构保持
        QString html1 =
            "<ul data-type=\"taskList\">"
            "<li data-type=\"taskItem\" data-checked=\"false\">"
            "<label><input type=\"checkbox\"><span></span></label>"
            "<div><p>任务A</p></div></li>"
            "<li data-type=\"taskItem\" data-checked=\"true\">"
            "<label><input type=\"checkbox\" checked=\"checked\"><span></span></label>"
            "<div><p>任务B</p></div></li>"
            "</ul>";
        QString html2 = MarkdownHandler::toHtml(MarkdownHandler::fromHtml(html1));
        QVERIFY2(html2.contains("<ul data-type=\"taskList\">"),
                 qPrintable("往返后应保留 taskList 契约，实际: " + html2));
        QVERIFY(html2.contains("data-checked=\"false\""));
        QVERIFY(html2.contains("data-checked=\"true\""));
        QVERIFY(html2.contains("任务A"));
        QVERIFY(html2.contains("任务B"));
    }

    void testTaskListNestedRoundTrip() {
        // 嵌套任务列表：2 空格缩进 ↔ 嵌套 ul
        QString md1 = "- [ ] 父任务\n  - [x] 子任务";
        QString html = MarkdownHandler::toHtml(md1);
        QCOMPARE(html.count("data-type=\"taskList\""), 2);
        QString md2 = MarkdownHandler::fromHtml(html);
        QCOMPARE(md2, md1);
    }

    void testTaskListInlineCodeRoundTrip() {
        // 任务条目内的行内代码往返不丢失
        QString md1 = "- [ ] 使用 `code` 的任务";
        QString md2 = MarkdownHandler::fromHtml(MarkdownHandler::toHtml(md1));
        QCOMPARE(md2, md1);
    }

    // ===== 列表后段落隔离 =====

    void testListParagraphIsolation() {
        // Bug2：列表项后紧跟正文行（无空行），正文应隔离为独立段落，
        // 而非被吞入列表项；其后的列表也应独立
        QString html = MarkdownHandler::toHtml(
            "- 任务A\n- 任务C\n如何隔离？\n- 无序列表1\n- 无序列表2");
        QCOMPARE(html.count("<ul"), 2);
        QVERIFY2(QRegularExpression("<p[^>]*>如何隔离？</p>").match(html).hasMatch(),
                 qPrintable("段落应独立存在，实际: " + html));
        QVERIFY2(!html.contains("任务C 如何隔离？"),
                 qPrintable("段落不应被吞入列表项，实际: " + html));
    }

    void testTaskListParagraphIsolation() {
        // Bug2 任务列表版本：- [ ] 列表 + 段落 + 无序列表
        QString html = MarkdownHandler::toHtml(
            "- [ ] 任务A\n- [ ] 任务C\n如何隔离？\n- 无序列表1\n- 无序列表2");
        QCOMPARE(html.count("data-type=\"taskList\""), 1);
        QCOMPARE(html.count("<ul"), 2);
        QVERIFY2(QRegularExpression("<p[^>]*>如何隔离？</p>").match(html).hasMatch(),
                 qPrintable("段落应独立存在，实际: " + html));
        QVERIFY2(!html.contains("任务C 如何隔离？"),
                 qPrintable("段落不应被吞入任务列表项，实际: " + html));
    }

    void testOrderedListParagraphIsolation() {
        // 有序列表后紧跟正文同样隔离
        QString html = MarkdownHandler::toHtml("1. 第一项\n正文段落\n2. 第二项");
        QVERIFY2(QRegularExpression("<p[^>]*>正文段落</p>").match(html).hasMatch(),
                 qPrintable("段落应独立存在，实际: " + html));
        QVERIFY2(!html.contains("第一项 正文段落"),
                 qPrintable("段落不应被吞入有序列表项，实际: " + html));
    }

    void testListBlankLineSeparationUnchanged() {
        // 已有空行分隔的内容不受隔离规则影响
        QString html = MarkdownHandler::toHtml("- 任务A\n\n如何隔离？\n\n- 无序列表1");
        QCOMPARE(html.count("<ul"), 2);
        QVERIFY(QRegularExpression("<p[^>]*>如何隔离？</p>").match(html).hasMatch());
    }

    // ===== span style / <u> 混合格式往返测试 =====

    void testSpanStyleRoundTrip()
    {
        // <span style="color:..."> 应在 fromHtml → toHtml 往返后保持
        QString html = QStringLiteral(
            "<p>正常文字<span style=\"color: rgb(186, 55, 42);\">红色文字</span>结束</p>");
        QString md = MarkdownHandler::fromHtml(html);
        // MD 中应保留原始 <span style> HTML
        QVERIFY(md.contains("<span style=\"color: rgb(186, 55, 42);\">红色文字</span>"));
        // 不应出现 ZZHS 占位符泄漏
        QVERIFY(!md.contains("ZZHS"));
        // 往返
        QString html2 = MarkdownHandler::toHtml(md);
        QVERIFY(html2.contains("span style=\"color: rgb(186, 55, 42);\""));
        QVERIFY(html2.contains("红色文字"));
    }

    void testUnderlineRoundTrip()
    {
        // <u> 应在往返后保持
        QString html = QStringLiteral("<p>普通<u>下划线</u>文字</p>");
        QString md = MarkdownHandler::fromHtml(html);
        QVERIFY(md.contains("<u>下划线</u>"));
        QVERIFY(!md.contains("ZZUL"));
        QString html2 = MarkdownHandler::toHtml(md);
        QVERIFY(html2.contains("<u>下划线</u>"));
    }

    void testMixedFormatRoundTrip()
    {
        // 粗体转 MD + 颜色保持 HTML
        QString html = QStringLiteral(
            "<p><strong>粗体</strong> <span style=\"color: red;\">红色</span></p>");
        QString md = MarkdownHandler::fromHtml(html);
        // 粗体→ MD
        QVERIFY(md.contains("**粗体**"));
        // 颜色→ 原始 HTML
        QVERIFY(md.contains("<span style=\"color: red;\">红色</span>"));
        // 往返
        QString html2 = MarkdownHandler::toHtml(md);
        // 粗体在 toHtml 侧由 QTextDocument 输出为 font-weight 加粗 span
        // （Qt 不输出 <strong>/<b>，而是 <span style=" font-weight:6xx/7xx;">）
        QVERIFY2(QRegularExpression("font-weight:\\s*[67]00").match(html2).hasMatch(),
                 qPrintable("粗体应以 font-weight 加粗样式保留，实际: " + html2));
        QVERIFY(html2.contains("粗体"));
        QVERIFY(html2.contains("span style=\"color: red;\""));
    }

    // ===== C1 回归：span/u 内部 MD 语法（下标/上标/代码）往返不丢失 =====

    void testSpanInnerSubscript()
    {
        // <span style> 内部的下标 ~x~ 须还原为 <sub>，不残留 ZZ* 占位符
        QString html = MarkdownHandler::toHtml(
            QStringLiteral("<span style=\"color: rgb(186, 55, 42);\">H~2~O</span>"));
        QVERIFY2(html.contains("<sub>2</sub>"),
                 qPrintable("span 内下标未还原，实际: " + html));
        QVERIFY2(html.contains("span style=\"color: rgb(186, 55, 42);\""),
                 qPrintable("span 样式丢失，实际: " + html));
        QVERIFY2(!html.contains("ZZSB") && !html.contains("ZZHI"),
                 qPrintable("残留占位符，实际: " + html));
    }

    void testUnderlineInnerSubscript()
    {
        // <u> 内部的下标 ~x~ 须还原为 <sub>
        QString html = MarkdownHandler::toHtml(
            QStringLiteral("<u>H~2~O</u>"));
        QVERIFY2(html.contains("<sub>2</sub>"),
                 qPrintable("u 内下标未还原，实际: " + html));
        QVERIFY2(html.contains("<u>"),
                 qPrintable("u 标签丢失，实际: " + html));
        QVERIFY2(!html.contains("ZZSB") && !html.contains("ZZHI"),
                 qPrintable("残留占位符，实际: " + html));
    }

    void testSpanInnerSubscriptRoundTrip()
    {
        // <span>内带 <sub> 的 HTML 经 fromHtml→toHtml 往返后下标保留
        QString html = QStringLiteral(
            "<p>化学式<span style=\"color: rgb(186, 55, 42);\">H<sub>2</sub>O</span>结束</p>");
        QString md = MarkdownHandler::fromHtml(html);
        QVERIFY2(md.contains("<span style=\"color: rgb(186, 55, 42);\">H~2~O</span>"),
                 qPrintable("fromHtml 未产出下标 MD，实际: " + md));
        QString html2 = MarkdownHandler::toHtml(md);
        QVERIFY2(html2.contains("<sub>2</sub>"),
                 qPrintable("span 内下标往返丢失，实际: " + html2));
        QVERIFY2(!html2.contains("ZZSB") && !html2.contains("ZZHI"),
                 qPrintable("残留占位符，实际: " + html2));
    }

    void testSpanInnerFootnote()
    {
        // 脚注引用在 span 内也能正确还原为 footnoteRef sup（C1 提取/还原顺序修复）
        QString html = MarkdownHandler::toHtml(
            QStringLiteral("<span style=\"color:red;\">a[^1]b</span>\n\n[^1]: def"));
        QVERIFY2(html.contains("data-type=\"footnoteRef\""),
                 qPrintable("span 内脚注未还原，实际: " + html));
        QVERIFY2(!html.contains("ZZFR") && !html.contains("ZZHI"),
                 qPrintable("残留占位符，实际: " + html));
    }

    void testKbdRoundTrip()
    {
        // kbd 往返：toHtml 保护 → fromHtml 还原 → 二次往返无占位符残留
        QString md = QStringLiteral("按<kbd>Esc</kbd>键退出");
        QString html = MarkdownHandler::toHtml(md);
        QVERIFY2(html.contains("<kbd>Esc</kbd>"),
                 qPrintable("toHtml 未保留 <kbd>，实际: " + html));

        QString md2 = MarkdownHandler::fromHtml(
            QStringLiteral("<p>按<kbd>Esc</kbd>键退出</p>"));
        QVERIFY2(md2.contains("<kbd>Esc</kbd>"),
                 qPrintable("fromHtml 未保留 <kbd>，实际: " + md2));

        // 二次往返，无占位符残留
        QString html2 = MarkdownHandler::toHtml(md2);
        QVERIFY2(html2.contains("<kbd>Esc</kbd>"),
                 qPrintable("二次往返 kbd 丢失，实际: " + html2));
        QVERIFY2(!html2.contains("ZZKD") && !html2.contains("ZZHI"),
                 qPrintable("残留占位符，实际: " + html2));
    }

    void testSmallRoundTrip()
    {
        QString md = QStringLiteral("这是<small>小字注释</small>文字");
        QString html = MarkdownHandler::toHtml(md);
        QVERIFY2(html.contains("<small>小字注释</small>"),
                 qPrintable("toHtml 未保留 <small>，实际: " + html));

        QString md2 = MarkdownHandler::fromHtml(
            QStringLiteral("<p>这是<small>小字注释</small>文字</p>"));
        QVERIFY2(md2.contains("<small>小字注释</small>"),
                 qPrintable("fromHtml 未保留 <small>，实际: " + md2));

        QString html2 = MarkdownHandler::toHtml(md2);
        QVERIFY2(html2.contains("<small>小字注释</small>"),
                 qPrintable("二次往返 small 丢失，实际: " + html2));
        QVERIFY2(!html2.contains("ZZSM") && !html2.contains("ZZHI"),
                 qPrintable("残留占位符，实际: " + html2));
    }

    void testDivBlockRoundTrip()
    {
        // 无 data-type 的手写 <div> 应被保护往返
        QString md = QStringLiteral("<div>容器内容</div>");
        QString html = MarkdownHandler::toHtml(md);
        QVERIFY2(html.contains("<div>容器内容</div>"),
                 qPrintable("toHtml 未保留 <div>，实际: " + html));

        QString md2 = MarkdownHandler::fromHtml(
            QStringLiteral("<div>容器内容</div>"));
        QVERIFY2(md2.contains("<div>容器内容</div>"),
                 qPrintable("fromHtml 未保留 <div>，实际: " + md2));

        // 有 data-type 的 <div> 不被保护（属于脚注/math 扩展）
        QString md3 = QStringLiteral("<div data-type=\"x\">内部</div>");
        QString html3 = MarkdownHandler::toHtml(md3);
        QVERIFY2(!html3.contains("ZZDV"),
                 qPrintable("data-type div 不应进入 ZZDV 保护，实际: " + html3));

        // 二次往返无残留
        QString html4 = MarkdownHandler::toHtml(md2);
        QVERIFY2(!html4.contains("ZZDV"),
                 qPrintable("残留 ZZDV 占位符，实际: " + html4));
    }

    void testColorSpanRoundTrip()
    {
        // 源码手写 <span style="color:red"> 经 toHtml 保护往返后还原
        QString md = QStringLiteral("普通文本<span style=\"color:red\">红色</span>尾部");
        QString html = MarkdownHandler::toHtml(md);
        QVERIFY2(html.contains("<span style=\"color:red\">红色</span>"),
                 qPrintable("toHtml 未保留颜色 span，实际: " + html));

        // fromHtml（编辑器 getHTML → 保存）侧对称保护
        QString md2 = MarkdownHandler::fromHtml(
            QStringLiteral("<p>普通文本<span style=\"color:red\">红色</span>尾部</p>"));
        QVERIFY2(md2.contains("<span style=\"color:red\">红色</span>"),
                 qPrintable("fromHtml 未保留颜色 span，实际: " + md2));

        // 二次往返 toHtml→fromHtml→toHtml，无占位符残留
        QString html2 = MarkdownHandler::toHtml(md2);
        QVERIFY2(html2.contains("<span style=\"color:red\">红色</span>"),
                 qPrintable("二次往返颜色 span 丢失，实际: " + html2));
        QVERIFY2(!html2.contains("ZZHI") && !html2.contains("ZZHS"),
                 qPrintable("残留占位符，实际: " + html2));
    }

    void testColorSpanMultiStyleRoundTrip()
    {
        // 多属性 style（文字色 + 背景色）完整往返
        QString md = QStringLiteral("<span style=\"color:#ffffff;background-color:#e23b3b\">红底白字</span>");
        QString html = MarkdownHandler::toHtml(md);
        QVERIFY2(html.contains("color:#ffffff") && html.contains("background-color:#e23b3b"),
                 qPrintable("多属性 style 往返丢失，实际: " + html));
    }

    void testColorSpanWithBoldRoundTrip()
    {
        // 粗体包裹颜色 span（编辑器内 <strong><span style>…</span></strong> 的 .md 形态）
        // 注：Qt QTextDocument 把 ** 粗体渲染为 <span style=" font-weight:700;">（非 <strong>），
        // 见同文件 testMixedFormatRoundTrip 的同样约定；此处验证粗体包裹下颜色 span 保留
        QString md = QStringLiteral("**<span style=\"color:red\">粗红</span>**");
        QString html = MarkdownHandler::toHtml(md);
        QVERIFY2(QRegularExpression("font-weight:\\s*700").match(html).hasMatch()
                     && html.contains("<span style=\"color:red\">粗红</span>"),
                 qPrintable("粗体+颜色 span 往返异常，实际: " + html));
    }

    // ===== 标题行首列表标记不应被过度转义（md4c 转义修复）=====

    void testHeadingNumberDotNotEscaped()
    {
        // Bug: <h2>1. 这是标题2</h2> 切换源码后多出 "\"，变成 "## 1\. 这是标题2"
        // ATX 标题内联内容不触发块级列表语法，数字点转义是多余的
        QString md = MarkdownHandler::fromHtml(
            "<h2>1. 这是标题2,一般表示章节</h2>");
        QVERIFY2(md.contains("## 1. 这是标题2"),
                 qPrintable("标题行首数字点不应被转义，实际: " + md));
        QVERIFY2(!md.contains("1\\."),
                 qPrintable("不应残留 1\\. 转义，实际: " + md));
        // 往返一致：去转义后 toHtml 仍是标题、文本不变
        QString html2 = MarkdownHandler::toHtml(md);
        QVERIFY2(html2.contains("1. 这是标题2"),
                 qPrintable("往返后标题文本丢失，实际: " + html2));
    }

    void testHeadingListMarkersNotEscaped()
    {
        // 多位数字.、数字)、-、+ 开头的标题行首转义都应去除
        QVERIFY2(!MarkdownHandler::fromHtml("<h2>2024. 年度报告</h2>").contains("\\."),
                 qPrintable("多位数字点不应转义，实际: " +
                            MarkdownHandler::fromHtml("<h2>2024. 年度报告</h2>")));
        QVERIFY2(!MarkdownHandler::fromHtml("<h2>1) 括号标题</h2>").contains("\\)"),
                 qPrintable("数字括号不应转义，实际: " +
                            MarkdownHandler::fromHtml("<h2>1) 括号标题</h2>")));
        QVERIFY2(!MarkdownHandler::fromHtml("<h2>- 减号标题</h2>").contains("\\-"),
                 qPrintable("减号不应转义，实际: " +
                            MarkdownHandler::fromHtml("<h2>- 减号标题</h2>")));
        QVERIFY2(!MarkdownHandler::fromHtml("<h2>+ 加号标题</h2>").contains("\\+"),
                 qPrintable("加号不应转义，实际: " +
                            MarkdownHandler::fromHtml("<h2>+ 加号标题</h2>")));
    }

    void testHeadingAsteriskEscapePreserved()
    {
        // * 涉及强调语义（如 *A* 会变强调），其转义必须保留，不能误去除
        QString md = MarkdownHandler::fromHtml("<h2>*A* 包裹</h2>");
        QVERIFY2(md.contains("\\*"),
                 qPrintable("* 转义应保留以防强调误解析，实际: " + md));
    }

    void testBodyListEscapePreserved()
    {
        // 正文里的有序列表标记转义必须保留 —— 去掉会变成 <ol> 有序列表，破坏语义
        QString md = MarkdownHandler::fromHtml("<p>1. 这是正文</p>");
        QVERIFY2(md.contains("1\\."),
                 qPrintable("正文数字点转义必须保留，实际: " + md));
    }

    void testHeadingFontSpanStripped()
    {
        // QTextDocument 在标题中注入的 <span style="font-weight:700;font-size:...">
        // 是默认标题渲染而非用户内容，fromHtml 应剔除这些纯字体样式 span
        QString md = MarkdownHandler::fromHtml(
            QStringLiteral("<h1><span style=\"font-weight:700;font-size:xx-large\">标题1</span></h1>"));
        QVERIFY2(md == QStringLiteral("# 标题1"),
                 qPrintable("标题纯字体 span 应被剔除，实际: " + md));

        // h2 的 QTextDocument 字体 span 也应被剔除
        QString md2 = MarkdownHandler::fromHtml(
            QStringLiteral("<h2><span style=\"font-weight:700;font-size:x-large\">小节</span></h2>"));
        QVERIFY2(md2 == QStringLiteral("## 小节"),
                 qPrintable("h2 字体 span 应被剔除，实际: " + md2));

        // 标题内用户颜色 span 不受影响
        QString md3 = MarkdownHandler::fromHtml(
            QStringLiteral("<h1><span style=\"color:red\">红标题</span></h1>"));
        QVERIFY2(md3.contains("<span style=\"color:red\">红标题</span>"),
                 qPrintable("用户颜色 span 不应被剔除，实际: " + md3));

        // 混合：QTextDocument 标题 span + 用户内联样式应保留后者
        QString md4 = MarkdownHandler::fromHtml(
            QStringLiteral("<h2><span style=\"font-weight:700;font-size:x-large;color:#2563eb\">蓝色标题</span></h2>"));
        QVERIFY2(md4.contains("color:#2563eb"),
                 qPrintable("含颜色的标题 span 不应被剔除，实际: " + md4));
    }

    void testSubSupRoundTrip()
    {
        // toHtml：原始 <sub>/<sup> HTML 标签应被保护（不被 QTextDocument 丢弃）
        QString md = QStringLiteral("化学式H<sub>2</sub>O和指数x<sup>2</sup>测试");
        QString html = MarkdownHandler::toHtml(md);
        QVERIFY2(html.contains("<sub>2</sub>"),
                 qPrintable("toHtml 未保留 <sub>，实际: " + html));
        QVERIFY2(html.contains("<sup>2</sup>"),
                 qPrintable("toHtml 未保留 <sup>，实际: " + html));

        // fromHtml：<sub> → ~x~，<sup> → ^x^（Pandoc 语法）
        QString md2 = MarkdownHandler::fromHtml(
            QStringLiteral("<p>化学式H<sub>2</sub>O和指数x<sup>2</sup>测试</p>"));
        QVERIFY2(md2.contains("~2~"),
                 qPrintable("fromHtml 未将 <sub> 转为 ~x~，实际: " + md2));
        QVERIFY2(md2.contains("^2^"),
                 qPrintable("fromHtml 未将 <sup> 转为 ^x^，实际: " + md2));
        QVERIFY2(!md2.contains("<sub>") && !md2.contains("<sup>"),
                 qPrintable("fromHtml 残留原始 sub/sup 标签，实际: " + md2));

        // 二次往返：toHtml 将 ~x~/^x^ 转回 <sub>/<sup>
        QString html2 = MarkdownHandler::toHtml(md2);
        QVERIFY2(html2.contains("<sub>2</sub>"),
                 qPrintable("二次往返 sub 丢失，实际: " + html2));
        QVERIFY2(html2.contains("<sup>2</sup>"),
                 qPrintable("二次往返 sup 丢失，实际: " + html2));
        QVERIFY2(!html2.contains("ZZSB") && !html2.contains("ZZSP")
                 && !html2.contains("ZZHI"),
                 qPrintable("残留占位符，实际: " + html2));
    }

    void testMarkRoundTrip()
    {
        // toHtml：原始 <mark> HTML 标签应被保护（不被 QTextDocument 丢弃）
        QString md = QStringLiteral("这是<mark>高亮文字</mark>测试");
        QString html = MarkdownHandler::toHtml(md);
        QVERIFY2(html.contains("<mark>高亮文字</mark>"),
                 qPrintable("toHtml 未保留 <mark>，实际: " + html));

        // fromHtml：<mark> → ==text==（Markdown 高亮语法）
        QString md2 = MarkdownHandler::fromHtml(
            QStringLiteral("<p>这是<mark>高亮文字</mark>测试</p>"));
        QVERIFY2(md2.contains("==高亮文字=="),
                 qPrintable("fromHtml 未将 <mark> 转为 ==x==，实际: " + md2));
        QVERIFY2(!md2.contains("<mark>"),
                 qPrintable("fromHtml 残留原始 <mark> 标签，实际: " + md2));

        // 二次往返：toHtml 将 ==x== 转回 <mark>
        QString html2 = MarkdownHandler::toHtml(md2);
        QVERIFY2(html2.contains("<mark>高亮文字</mark>"),
                 qPrintable("二次往返 mark 丢失，实际: " + html2));
        QVERIFY2(!html2.contains("ZZMK") && !html2.contains("ZZHI"),
                 qPrintable("残留占位符，实际: " + html2));
    }
};

QTEST_MAIN(TestMarkdownHandler)
#include "test_markdownhandler.moc"
