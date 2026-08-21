#include <QTest>
#include <QRegularExpression>
#include "../src/core/MarkdownHandler.h"

// 构造含空格、长度远超 80 列的测试文本。QTextDocument::toMarkdown() 硬编码
// 80 列换行，只有含空格（词边界）的文本才会触发折行——纯中文无空格不会折。
static QString longSpacedText(int words)
{
    QString s;
    for (int i = 1; i <= words; ++i)
        s += QStringLiteral("单词%1 ").arg(i);
    return s;
}

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

    void testInlineCodeSyntaxLiteralPreserved() {
        // 行内代码内的功能符号是字面文本，不应被转换为下标/上标/高亮/标签：
        // `H~2~O` 显示 H~2~O（不是 H₂O），`x^2^` 显示 x^2^（不是 x²）
        QString md = "写法 `H~2~O` 与 `x^2^` 与 `==高亮==`\n";
        QString html = MarkdownHandler::toHtml(md);
        QVERIFY2(html.contains("<code>H~2~O</code>"),
                 qPrintable("`H~2~O` 应字面保留，实际: " + html));
        QVERIFY2(html.contains("<code>x^2^</code>"),
                 qPrintable("`x^2^` 应字面保留，实际: " + html));
        QVERIFY2(html.contains("<code>==高亮==</code>"),
                 qPrintable("`==高亮==` 应字面保留，实际: " + html));
        // 对照：行内代码外的写法正常转换（代码外的 H~2~O → <sub>2</sub>）
        QString md2 = "效果 H~2~O 与 x^2^ 与 ==高亮==\n";
        QString html2 = MarkdownHandler::toHtml(md2);
        QVERIFY2(html2.contains("<sub>2</sub>"),
                 qPrintable("代码外 ~2~ 应转下标，实际: " + html2));
        QVERIFY2(html2.contains("<sup>2</sup>"),
                 qPrintable("代码外 ^2^ 应转上标，实际: " + html2));
        QVERIFY2(html2.contains("<mark>高亮</mark>"),
                 qPrintable("代码外 ==高亮== 应转高亮，实际: " + html2));
    }

    void testInlineCodeHtmlTagsLiteralPreserved() {
        // 行内代码内的 HTML 标签是字面文本：`H<sub>2</sub>O` 显示为带标签的写法
        // （往返后应还原 `H<sub>2</sub>O` 原文，不被解析为真下标）
        QString md1 = "写法 `H<sub>2</sub>O` 与 `x<sup>2</sup>`\n";
        QString html = MarkdownHandler::toHtml(md1);
        QString code1, code2;
        int p = html.indexOf("<code>"); p = html.indexOf("<code>", p + 1); // 跳过第一个（若无）
        // 提取两个 code 的内容：允许 &lt; 转义形态或原文形态，但都必须是"字面展示"——
        // 即 sub/sup 标签在 <code> 内部作为文本（转义）或成对原文，而不是渲染元素。
        // 简化断言：code 内含 "2" 且不含未转义的 "<sub"（未被当作受保护 HTML 直通渲染）
        QRegularExpression codeRe("<code>(.*?)</code>");
        auto it = codeRe.globalMatch(html);
        int n = 0;
        while (it.hasNext()) {
            auto m = it.next();
            QString c = m.captured(1);
            if (c.contains("2")) { n++; QVERIFY2(!c.contains("<sub>"), "code 内不应有真 <sub>"); }
            if (c.contains("2")) QVERIFY(!c.contains("<sup>"));
        }
        QVERIFY(n >= 2);
        // 完整往返：`H<sub>2</sub>O` 原文恢复
        QString back = MarkdownHandler::fromHtml(html);
        QVERIFY2(back.contains("`H<sub>2</sub>O`"),
                 qPrintable("往返应还原 `H<sub>2</sub>O` 原文，实际: " + back));
        QVERIFY2(back.contains("`x<sup>2</sup>`"),
                 qPrintable("往返应还原 `x<sup>2</sup>` 原文，实际: " + back));
    }

    void testInlineCodeDivLiteralPreserved() {
        // 行内代码内的 <div> 是字面文本：`<div align="center">…</div>` 应显示为
        // 带标签的写法（HTML 转义形态），而不是被 ZZDV 当真标签保护后直通——
        // 原始 <div> 插进 <code> 会被浏览器当结构元素解析，字面标签"被过滤"。
        QString md = "<div align=\"center\">居中的段落</div> —— 写法：`<div align=\"center\">居中的段落</div>`\n";
        QString html = MarkdownHandler::toHtml(md);
        // 真 div（代码外）仍受保护、原样出现在 HTML 中
        QVERIFY2(html.contains("<div align=\\\"center\\\">居中的段落</div> —— 写法：") ||
                 html.contains("<div align=\"center\">居中的段落</div> —— 写法："),
                 qPrintable("代码外真 div 应保留，实际: " + html));
        // code 内应为 HTML 转义形态（&lt;div…），非原始标签
        QVERIFY2(html.contains("<code>&lt;div"),
                 qPrintable("code 内 div 应为转义字面文本，实际: " + html));
        QVERIFY2(!html.contains("<code><div"),
                 qPrintable("code 内不应有原始 <div> 标签，实际: " + html));
        // 完整往返：原文精确恢复
        QString back = MarkdownHandler::fromHtml(html);
        QVERIFY2(back.contains("`<div align=\\\"center\\\">居中的段落</div>`") ||
                 back.contains("`<div align=\"center\">居中的段落</div>`"),
                 qPrintable("往返应还原原文，实际: " + back));
    }

    void testNoPlaceholderLeakInGuide() {
        // 回归：ZZDV 守卫的行内代码正则若允许跨行配对（`[^`]+`），
        // 双反引号写法（`` `x` ``）会与后续行的反引号误配对，多行文本
        // 被吞成一个"代码段"并压成一行；随后 divRe 从裸 <div> 跨区捕获
        // 到远处 </div>，把沿途 \x01CODE 占位符吞进 divBackups，还原
        // 步骤扫不到 → \x01 泄漏进最终 HTML。全文渲染不得含 \x01。
        QFile f(GUIDE_MD_PATH);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
            QSKIP("找不到 markdown-guide.md");
        QString guide = QString::fromUtf8(f.readAll());
        QString html = MarkdownHandler::toHtml(guide);
        QVERIFY2(!html.contains(QChar(1)),
                 qPrintable("HTML 中泄漏 \\x01 占位符: " +
                            html.mid(html.indexOf(QChar(1)) - 40, 120)));
        // 最小复现集：代码围栏 + 双反引号表格行 + 含裸 <div> 的散文 +
        // 行内代码写法表格（曾泄漏 13 处）
        QString mini = "```markdown\n```\n"
                       "| `` `行内代码` `` | `行内代码` |\n"
                       "```markdown\n```\n"
                       "编辑器支持手写 HTML 标签，如 <div> 与 <span>。\n"
                       "| `H~2~O` | H~2~O |\n"
                       "| `<div align=\"center\">x</div>` | 居中 |\n";
        QString html2 = MarkdownHandler::toHtml(mini);
        QVERIFY2(!html2.contains(QChar(1)),
                 qPrintable("最小复现集泄漏 \\x01: " + html2));
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

    void testInlineCodeWithBacktickRoundTrip() {
        // 行内代码内容含反引号：定界符必须长于内容中最长连续反引号串，
        // 否则 fromHtml 固定单反引号序列化会提前闭合、往返错乱
        // （与代码块围栏同源问题，见 docs/code-fence-roundtrip.md 坑 2）。
        const QString html = MarkdownHandler::toHtml(QStringLiteral("`` `x` ``\n"));
        const QString back = MarkdownHandler::fromHtml(html);
        // 内容 `x`（含两侧反引号）必须完整保留为 `` `x` ``
        QVERIFY2(back.contains(QStringLiteral("`` `x` ``")),
                 qPrintable("往返应保留 `` `x` ``，实际: " + back));
    }

    void testInlineCodeDoubleBacktickRoundTrip() {
        // 表格内行内代码含反引号（markdown-guide.md「行内代码」演示行）：
        // `` `行内代码` `` 往返后必须保留双反引号定界与内容反引号，
        // 否则演示行退化为 ``行内代码``（内容反引号丢失）。
        const QString md = QStringLiteral(
            "| 语法 | 效果 |\n| --- | --- |\n| `` `行内代码` `` | `行内代码` |\n");
        const QString html = MarkdownHandler::toHtml(md);
        const QString back = MarkdownHandler::fromHtml(html);
        QVERIFY2(back.contains(QStringLiteral("`` `行内代码` ``")),
                 qPrintable("表格内 `` `行内代码` `` 往返应保留，实际: " + back));
    }

    void testInlineCodeRedundantDoubleBacktickNormalized() {
        // 内容无反引号却用双反引号定界（``行内代码``）：定界长度在 HTML 中
        // 天然丢失，往返后归一为最小定界符 `行内代码`（与代码块围栏坑 2 同理：
        // 内容无反引号时 1 根即正确，与用户手写 2 根语义等价）。
        const QString md = QStringLiteral(
            "| 语法 | 效果 |\n| --- | --- |\n|``行内代码``|`行内代码`|\n");
        const QString html = MarkdownHandler::toHtml(md);
        QVERIFY(!html.contains(QStringLiteral("data-bk")));
        const QString back = MarkdownHandler::fromHtml(html);
        QVERIFY2(back.contains(QStringLiteral("`行内代码`")),
                 qPrintable("往返应归一为单反引号，实际: " + back));
        QVERIFY(!back.contains(QStringLiteral("``行内代码``")));
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

        // 用户报告：任务列表含中文括号及多条目
        QString md3 = QStringLiteral("- [ ] 任务列表1\n- [x] 任务列表2（已完成）\n- [ ] 任务列表3");
        QString html3 = MarkdownHandler::toHtml(md3);
        QString md4 = MarkdownHandler::fromHtml(html3);
        QVERIFY2(!md4.contains("<div>"),
                 qPrintable("任务列表往返不应出现裸 <div>，md=[" + md4 + "]"));
        QVERIFY2(md4.contains("- [ ] 任务列表1"),
                 qPrintable("任务1 应保留，md=[" + md4 + "]"));
        QVERIFY2(md4.contains("- [x] 任务列表2"),
                 qPrintable("任务2 应保留，md=[" + md4 + "]"));
        QVERIFY2(md4.contains("- [ ] 任务列表3"),
                 qPrintable("任务3 应保留，md=[" + md4 + "]"));

        // 仅传入 body 内容（模拟 TipTap getHTML 输出）→ 不应退化
        {
            int bsi = html3.indexOf("<body");
            int bei = html3.indexOf("</body>");
            bsi = html3.indexOf('>', bsi) + 1;
            QString bodyOnly = html3.mid(bsi, bei - bsi).trimmed();
            QString mdBody = MarkdownHandler::fromHtml(bodyOnly);
            QVERIFY2(!mdBody.contains("<div>"),
                     qPrintable("body-only 往返不应出现裸 <div>，md=[" + mdBody + "]"));
            QVERIFY2(mdBody.contains("- [ ] 任务列表1"),
                     qPrintable("body-only: 任务1 应保留，md=[" + mdBody + "]"));
            QVERIFY2(mdBody.contains("- [x] 任务列表2"),
                     qPrintable("body-only: 任务2 应保留，md=[" + mdBody + "]"));
            QVERIFY2(mdBody.contains("- [ ] 任务列表3"),
                     qPrintable("body-only: 任务3 应保留，md=[" + mdBody + "]"));
        }

        // 模拟 TipTap getHTML 的带缩进/换行格式化输出
        {
            QString tipTapStyle =
                QStringLiteral("<ul data-type=\"taskList\">\n")
                + QStringLiteral("  <li data-type=\"taskItem\" data-checked=\"false\">\n")
                + QStringLiteral("    <label contenteditable=\"false\"><input type=\"checkbox\"><span></span></label>\n")
                + QStringLiteral("    <div>\n")
                + QStringLiteral("      <p>任务列表1</p>\n")
                + QStringLiteral("    </div>\n")
                + QStringLiteral("  </li>\n")
                + QStringLiteral("  <li data-type=\"taskItem\" data-checked=\"true\">\n")
                + QStringLiteral("    <label contenteditable=\"false\"><input type=\"checkbox\" checked><span></span></label>\n")
                + QStringLiteral("    <div>\n")
                + QStringLiteral("      <p>任务列表2（已完成）</p>\n")
                + QStringLiteral("    </div>\n")
                + QStringLiteral("  </li>\n")
                + QStringLiteral("  <li data-type=\"taskItem\" data-checked=\"false\">\n")
                + QStringLiteral("    <label contenteditable=\"false\"><input type=\"checkbox\"><span></span></label>\n")
                + QStringLiteral("    <div>\n")
                + QStringLiteral("      <p>任务列表3</p>\n")
                + QStringLiteral("    </div>\n")
                + QStringLiteral("  </li>\n")
                + QStringLiteral("</ul>");
            QString mdTipTap = MarkdownHandler::fromHtml(tipTapStyle);
            QVERIFY2(!mdTipTap.contains("<div>"),
                     qPrintable("TipTap 格式化输出不应出现裸 <div>，md=[" + mdTipTap + "]"));
            QVERIFY2(mdTipTap.contains("- [ ] 任务列表1"),
                     qPrintable("TipTap fmt: 任务1 应保留，md=[" + mdTipTap + "]"));
            QVERIFY2(mdTipTap.contains("- [x] 任务列表2"),
                     qPrintable("TipTap fmt: 任务2 应保留，md=[" + mdTipTap + "]"));
            QVERIFY2(mdTipTap.contains("- [ ] 任务列表3"),
                     qPrintable("TipTap fmt: 任务3 应保留，md=[" + mdTipTap + "]"));
        }

        // 模拟 divBlock 截获 taskItem 内 <div> 后的双重 <div> 退化 HTML
        // （未修复前：divBlock parseHTML 无 closest('li[data-type="taskItem"]') 排除，
        //  taskItem renderHTML 输出外层 <div>，divBlock renderHTML 再输出内层 <div>，
        //  导致 <div><div></div><p>text</p></div> 结构——<p> 是 block 不能被 inline-only
        //  divBlock 容纳，故被踢到外层与空 divBlock 并列）
        {
            QString degradedWithDoubleDiv =
                QStringLiteral("<ul data-type=\"taskList\">\n")
                + QStringLiteral("  <li data-type=\"taskItem\" data-checked=\"false\">\n")
                + QStringLiteral("    <label contenteditable=\"false\"><input type=\"checkbox\"><span></span></label>\n")
                + QStringLiteral("    <div>\n")
                + QStringLiteral("      <div></div>\n")
                + QStringLiteral("      <p>任务列表1</p>\n")
                + QStringLiteral("    </div>\n")
                + QStringLiteral("  </li>\n")
                + QStringLiteral("  <li data-type=\"taskItem\" data-checked=\"true\">\n")
                + QStringLiteral("    <label contenteditable=\"false\"><input type=\"checkbox\" checked><span></span></label>\n")
                + QStringLiteral("    <div>\n")
                + QStringLiteral("      <div></div>\n")
                + QStringLiteral("      <p>任务列表2（已完成）</p>\n")
                + QStringLiteral("    </div>\n")
                + QStringLiteral("  </li>\n")
                + QStringLiteral("  <li data-type=\"taskItem\" data-checked=\"false\">\n")
                + QStringLiteral("    <label contenteditable=\"false\"><input type=\"checkbox\"><span></span></label>\n")
                + QStringLiteral("    <div>\n")
                + QStringLiteral("      <div></div>\n")
                + QStringLiteral("      <p>任务列表3</p>\n")
                + QStringLiteral("    </div>\n")
                + QStringLiteral("  </li>\n")
                + QStringLiteral("</ul>");
            QString mdDegraded = MarkdownHandler::fromHtml(degradedWithDoubleDiv);
            QVERIFY2(!mdDegraded.contains("<div>"),
                     qPrintable("双重 div 退化不应出现裸 <div>，md=[" + mdDegraded + "]"));
            QVERIFY2(mdDegraded.contains("- [ ] 任务列表1"),
                     qPrintable("双重div: 任务1 应保留，md=[" + mdDegraded + "]"));
            QVERIFY2(mdDegraded.contains("- [x] 任务列表2"),
                     qPrintable("双重div: 任务2 应保留，md=[" + mdDegraded + "]"));
            QVERIFY2(mdDegraded.contains("- [ ] 任务列表3"),
                     qPrintable("双重div: 任务3 应保留，md=[" + mdDegraded + "]"));
        }
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
        // 粗体在 toHtml 侧由 QTextDocument 输出为 font-weight span，经通用
        // 转换步骤统一转为 <strong>（阻断 colorSpan 误捕获导致的往返污染）
        QVERIFY2(html2.contains("<strong>粗体</strong>"),
                 qPrintable("粗体应转为 <strong>，实际: " + html2));
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
        // 通用转换步骤将 QTextDocument 的 font-weight span 统一转为 <strong>，
        // 阻断 colorSpan 误捕获导致的往返污染
        QString md = QStringLiteral("**<span style=\"color:red\">粗红</span>**");
        QString html = MarkdownHandler::toHtml(md);
        // 提取 body，排除 <body> 标签自身的 font-weight:400 样式
        int bsi = html.indexOf("<body");
        int bei = html.indexOf("</body>");
        bsi = html.indexOf('>', bsi) + 1;
        QString body = html.mid(bsi, bei - bsi).trimmed();
        QVERIFY2(body.contains("<strong>")
                     && body.contains("<span style=\"color:red\">粗红</span>"),
                 qPrintable("粗体+颜色 span 应保留，body=[" + body + "]"));
        QVERIFY2(!body.contains("font-weight"),
                 qPrintable("body 内不应残留 font-weight span，body=[" + body + "]"));
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

    void testBrRoundTrip()
    {
        // toHtml：原始 <br> 硬换行标签应被保护（不被 QTextDocument 丢弃）
        QString md = QStringLiteral("第一行<br>第二行");
        QString html = MarkdownHandler::toHtml(md);
        QVERIFY2(html.contains("第一行") && html.contains("<br>") && html.contains("第二行"),
                 qPrintable("toHtml <br> 丢失，实际: " + html));
        QVERIFY2(!html.contains("ZZHI"),
                 qPrintable("toHtml 残留 ZZHI，实际: " + html));

        // fromHtml：编辑器中的 <br> 应保留为字面 <br> 标签（同 <kbd>/<small>）
        QString md2 = MarkdownHandler::fromHtml(
            QStringLiteral("<p>第一行<br>第二行</p>"));
        QVERIFY2(md2.contains("第一行") && md2.contains("第二行"),
                 qPrintable("fromHtml 内容丢失，实际: " + md2));
        QVERIFY2(md2.contains("<br>"),
                 qPrintable("fromHtml <br> 应保留为字面标签，实际: " + md2));
        QVERIFY2(!md2.contains("ZZBR") && !md2.contains("ZZHI"),
                 qPrintable("fromHtml 残留占位符，实际: " + md2));

        // 二次往返：fromHtml 产出的 <br> 经 toHtml 转回 HTML <br>
        QString html2 = MarkdownHandler::toHtml(md2);
        QVERIFY2(html2.contains("<br>"),
                 qPrintable("二次往返 br 丢失，实际: " + html2));

        // 带属性的 <br/> 自闭合形式也应受保护
        QString md3 = MarkdownHandler::toHtml(
            QStringLiteral("A<br/>B"));
        QVERIFY2(md3.contains("<br/>") && md3.contains("A") && md3.contains("B"),
                 qPrintable("toHtml <br/> 丢失，实际: " + md3));

        // 完整往返：toHtml → fromHtml 用实际 toHtml body 输出（模拟源码模式切换）
        QString actualHtml = MarkdownHandler::toHtml(
            QStringLiteral("第一行<br>第二行"));
        // 提取 body 内容（模拟 EditorTab::loadFile → setContent → getContent 链路）
        int bsi = actualHtml.indexOf("<body");
        int bei = actualHtml.indexOf("</body>");
        bsi = actualHtml.indexOf('>', bsi) + 1;
        QString body = actualHtml.mid(bsi, bei - bsi).trimmed();
        QString actualMd = MarkdownHandler::fromHtml(body);
        QVERIFY2(actualMd.contains("第一行") && actualMd.contains("第二行"),
                 qPrintable("完整往返丢失内容，body=[" + body + "] md=[" + actualMd + "]"));
        QVERIFY2(actualMd.contains("<br>"),
                 qPrintable("完整往返 <br> 应保留为字面标签，md=[" + actualMd + "]"));
    }

    void testBrSoloLine()
    {
        // 用户场景：多行 md 中 <br> 独占一行时，应保留为一个换行
        QString md = QStringLiteral("第一行。\n第二行。\n<br>\n第三行。\n第四行。");
        QString html = MarkdownHandler::toHtml(md);
        // body 提取（模拟 EditorTab::loadFile）
        int bsi = html.indexOf("<body");
        int bei = html.indexOf("</body>");
        bsi = html.indexOf('>', bsi) + 1;
        QString body = html.mid(bsi, bei - bsi).trimmed();
        qDebug() << "[testBrSolo] toHtml body:" << body;

        // 往返
        QString md2 = MarkdownHandler::fromHtml(body);
        qDebug() << "[testBrSolo] fromHtml:" << md2;

        // 所有文本应保留，<br> 应保留为字面标签
        QVERIFY2(md2.contains("第三行") && md2.contains("第四行"),
                 qPrintable("<br> 后内容丢失，md=[" + md2 + "]"));
        QVERIFY2(md2.contains("<br>"),
                 qPrintable("<br> 标签应在 MD 中保留，md=[" + md2 + "]"));
        QVERIFY2(!md2.contains("ZZBR") && !md2.contains("ZZHI"),
                 qPrintable("残留占位符，md=[" + md2 + "]"));
    }

    void testHeadingWithDiv()
    {
        // 用户场景：# 后跟 <div>——标题信息不应丢失
        QString md = QStringLiteral("# <div align=\"center\">这是标题1</div>");
        QString html = MarkdownHandler::toHtml(md);
        int bsi = html.indexOf("<body");
        int bei = html.indexOf("</body>");
        bsi = html.indexOf('>', bsi) + 1;
        QString body = html.mid(bsi, bei - bsi).trimmed();
        qDebug() << "[testHeadingDiv] toHtml body:" << body;

        QString md2 = MarkdownHandler::fromHtml(body);
        qDebug() << "[testHeadingDiv] fromHtml:" << md2;

        // toHtml 应将 <div align> 转为 heading text-align（TipTap TextAlign 渲染）
        QVERIFY2(body.contains("text-align:center") && body.contains("<h1") && body.contains("这是标题1"),
                 qPrintable("h1 应带 text-align:center，body=[" + body + "]"));
        // 标题标记 # 必须保留
        QVERIFY2(md2.contains("#"),
                 qPrintable("# 丢失，md=[" + md2 + "]"));
        // div 容器必须保留
        QVERIFY2(md2.contains("<div align=\"center\">"),
                 qPrintable("<div> 丢失，md=[" + md2 + "]"));

        // h2 / h3 同理
        QString mdH2 = QStringLiteral("## <div align=\"center\">章节标题</div>");
        QString outH2 = MarkdownHandler::fromHtml(
            MarkdownHandler::toHtml(mdH2));
        QVERIFY2(outH2.contains("##") && outH2.contains("<div align=\"center\">章节标题</div>"),
                 qPrintable("h2 往返失败，out=[" + outH2 + "]"));

        QString mdH3 = QStringLiteral("### <div align=\"left\">小节标题</div>");
        QString outH3 = MarkdownHandler::fromHtml(
            MarkdownHandler::toHtml(mdH3));
        QVERIFY2(outH3.contains("###") && outH3.contains("<div align=\"left\">小节标题</div>"),
                 qPrintable("h3 往返失败，out=[" + outH3 + "]"));

        // 多轮往返：模拟反复切换源码/WYSIWYG（5 轮）
        QString multi = QStringLiteral("# <div align=\"center\">标题</div>");
        for (int r = 0; r < 5; ++r) {
            QString body2 = MarkdownHandler::toHtml(multi);
            int bsi2 = body2.indexOf("<body");
            int bei2 = body2.indexOf("</body>");
            bsi2 = body2.indexOf('>', bsi2) + 1;
            QString bodyContent = body2.mid(bsi2, bei2 - bsi2).trimmed();
            multi = MarkdownHandler::fromHtml(bodyContent);
        }
        // 5 轮后应仍保留 # 和 div
        QVERIFY2(multi.contains("#") && multi.contains("<div align=\"center\">标题</div>"),
                 qPrintable("5 轮往返后退化，md=[" + multi + "]"));
        QVERIFY2(!multi.contains("font-size:xx-large"),
                 qPrintable("5 轮后残留 heading font span，md=[" + multi + "]"));
    }

    void testTextAlignFromHtml()
    {
        // fromHtml：段落 text-align 样式应转为 <div align="..."> 容器
        QString md = MarkdownHandler::fromHtml(
            QStringLiteral("<p style=\"text-align: center\">居中文字</p>"));
        QVERIFY2(md.contains("<div align=\"center\">居中文字</div>"),
                 qPrintable("text-align:center 未转为 div align，实际: " + md));

        // 左对齐
        QString md2 = MarkdownHandler::fromHtml(
            QStringLiteral("<p style=\"text-align: left\">左对齐</p>"));
        QVERIFY2(md2.contains("<div align=\"left\">左对齐</div>"),
                 qPrintable("text-align:left 未转为 div align，实际: " + md2));

        // 右对齐
        QString md3 = MarkdownHandler::fromHtml(
            QStringLiteral("<p style=\"text-align: right\">右对齐</p>"));
        QVERIFY2(md3.contains("<div align=\"right\">右对齐</div>"),
                 qPrintable("text-align:right 未转为 div align，实际: " + md3));
    }

    void testHeadingFontSpanFallback()
    {
        // 验证 toHtml 兜底剥离：正常标题（无 div）的 font span 被移除
        QString md = QStringLiteral("# 普通标题");
        QString html = MarkdownHandler::toHtml(md);
        int bsi = html.indexOf("<body");
        int bei = html.indexOf("</body>");
        bsi = html.indexOf('>', bsi) + 1;
        QString body = html.mid(bsi, bei - bsi).trimmed();

        // 不应包含 font-size:xx-large 或 font-weight:700（已被兜底剥离）
        QVERIFY2(!body.contains("font-size:xx-large"),
                 qPrintable("h1 残留 font-size span，body=[" + body + "]"));
        QVERIFY2(!body.contains("font-weight:700"),
                 qPrintable("h1 残留 font-weight span，body=[" + body + "]"));

        // h2 同理
        QString mdH2 = QStringLiteral("## h2 标题");
        QString h2Html = MarkdownHandler::toHtml(mdH2);
        int bsi2 = h2Html.indexOf("<body");
        int bei2 = h2Html.indexOf("</body>");
        bsi2 = h2Html.indexOf('>', bsi2) + 1;
        QString bodyH2 = h2Html.mid(bsi2, bei2 - bsi2).trimmed();
        QVERIFY2(!bodyH2.contains("font-size:x-large") && !bodyH2.contains("font-weight:700"),
                 qPrintable("h2 残留 font span，body=[" + bodyH2 + "]"));

        // h3 同理
        QString mdH3 = QStringLiteral("### h3 标题");
        QString h3Html = MarkdownHandler::toHtml(mdH3);
        int bsi3 = h3Html.indexOf("<body");
        int bei3 = h3Html.indexOf("</body>");
        bsi3 = h3Html.indexOf('>', bsi3) + 1;
        QString bodyH3 = h3Html.mid(bsi3, bei3 - bsi3).trimmed();
        QVERIFY2(!bodyH3.contains("font-size:large") && !bodyH3.contains("font-weight:700"),
                 qPrintable("h3 残留 font span，body=[" + bodyH3 + "]"));
    }

    void testDivHeadingSpanDefensivePrefixed()
    {
        // 模拟用户反复操作后的降解输出：
        // "# <div align=\"center\"><strong><span style=\"font-size: xx-large; font-weight: 700;\">标题</span></strong></div>"
        // 应被 fromHtml 防御步骤剥离 strong+span，保留 # 和 <div>
        QString degraded = QStringLiteral(
            "# <div align=\"center\"><strong><span style=\"font-size: xx-large; font-weight: 700;\">这是标题1</span></strong></div>");
        QString html = MarkdownHandler::toHtml(degraded);
        int bsi = html.indexOf("<body");
        int bei = html.indexOf("</body>");
        bsi = html.indexOf('>', bsi) + 1;
        QString body = html.mid(bsi, bei - bsi).trimmed();

        // 往返应稳定：font span 和 strong 被清除
        QString md2 = MarkdownHandler::fromHtml(body);
        QVERIFY2(md2.contains("#") && md2.contains("<div align=\"center\">这是标题1</div>"),
                 qPrintable("降解格式未恢复，md=[" + md2 + "]"));
        QVERIFY2(!md2.contains("font-size:xx-large"),
                 qPrintable("残留 font-size span，md=[" + md2 + "]"));
        QVERIFY2(!md2.contains("<strong>"),
                 qPrintable("残留 <strong>，md=[" + md2 + "]"));

        // 无 strong 版本
        QString degraded2 = QStringLiteral(
            "# <div align=\"left\"><span style=\"font-size: x-large; font-weight: 700;\">h2标题</span></div>");
        QString md3 = MarkdownHandler::fromHtml(degraded2);
        QVERIFY2(md3.contains("##") && md3.contains("<div align=\"left\">h2标题</div>"),
                 qPrintable("无strong降解恢复失败，md=[" + md3 + "]"));
    }

    void testDivHeadingSpanDefensiveBare()
    {
        // 模拟标题信息完全丢失的情况（无 # 前缀，纯 div+font-span）
        // fromHtml 应检测 font-size 并恢复标题前缀

        // h1 无 strong
        QString degraded = QStringLiteral(
            "<div align=\"center\"><span style=\"font-size: xx-large; font-weight: 700;\">居中标题</span></div>");
        QString md = MarkdownHandler::fromHtml(degraded);
        QVERIFY2(md.contains("# <div align=\"center\">居中标题</div>"),
                 qPrintable("bare h1 降解未恢复，md=[" + md + "]"));

        // h2 带 strong
        QString degraded2 = QStringLiteral(
            "<div align=\"left\"><strong><span style=\"font-weight: 700; font-size: x-large;\">左对齐h2</span></strong></div>");
        QString md2 = MarkdownHandler::fromHtml(degraded2);
        QVERIFY2(md2.contains("## <div align=\"left\">左对齐h2</div>"),
                 qPrintable("bare h2+strong 降解未恢复，md=[" + md2 + "]"));

        // h3 无 div 属性（纯 div）
        QString degraded3 = QStringLiteral(
            "<div><span style=\"font-size: large; font-weight: 700;\">h3标题</span></div>");
        QString md3 = MarkdownHandler::fromHtml(degraded3);
        QVERIFY2(md3.contains("### h3标题"),
                 qPrintable("bare h3 无div属性降解未恢复，md=[" + md3 + "]"));

        // h2 无 strong，属性顺序 font-weight 在前
        QString degraded4 = QStringLiteral(
            "<div align=\"right\"><span style=\"font-weight: 700; font-size: x-large;\">右对齐h2</span></div>");
        QString md4 = MarkdownHandler::fromHtml(degraded4);
        QVERIFY2(md4.contains("## <div align=\"right\">右对齐h2</div>"),
                 qPrintable("bare h2 属性反序未恢复，md=[" + md4 + "]"));
    }

    void testHeadingDivDegradation5Round()
    {
        // 模拟"反复居中、左对齐、切换正文、标题1、居中"后的降解状态
        // 验证 5 轮往返后不退化
        QString degraded = QStringLiteral(
            "# <div align=\"center\"><strong><span style=\"font-size: xx-large; font-weight: 700;\">这是标题1</span></strong></div>");
        for (int r = 0; r < 5; ++r) {
            QString html = MarkdownHandler::toHtml(degraded);
            int bsi = html.indexOf("<body");
            int bei = html.indexOf("</body>");
            bsi = html.indexOf('>', bsi) + 1;
            QString body = html.mid(bsi, bei - bsi).trimmed();
            degraded = MarkdownHandler::fromHtml(body);
        }
        // 5 轮后应干净
        QVERIFY2(degraded.contains("#") && degraded.contains("<div align=\"center\">这是标题1</div>"),
                 qPrintable("5 轮降解恢复失败，md=[" + degraded + "]"));
        QVERIFY2(!degraded.contains("font-size") && !degraded.contains("<strong>"),
                 qPrintable("5 轮后残留污染，md=[" + degraded + "]"));
    }

    void testTableHeaderFontSpanStripped()
    {
        // toHtml：MD 表格首行（表头）不应含 font-weight:700 span
        // QTextDocument 对表头输出 <td><p><span style="font-weight:700;"> 而非 <th>
        {
            QString md = QStringLiteral(
                "| 显示 | md语法 | html语法 |\n"
                "|------|--------|----------|\n"
                "| a    | b      | c        |\n");
            QString html = MarkdownHandler::toHtml(md);
            int bsi = html.indexOf("<body");
            int bei = html.indexOf("</body>");
            bsi = html.indexOf('>', bsi) + 1;
            QString body = html.mid(bsi, bei - bsi).trimmed();
            QVERIFY2(body.contains("<table"), qPrintable("应含 <table>，body=[" + body + "]"));
            QVERIFY2(body.contains("显示") && body.contains("md语法"),
                     qPrintable("表头文字应保留"));
            QVERIFY2(!body.contains("font-weight:700"),
                     qPrintable("表格内不应含 font-weight:700 span，body=[" + body + "]"));
            // 非表头行（数据行）文字也应保留
            QVERIFY2(body.contains("a</p>") || body.contains(">a<"),
                     qPrintable("数据行文字应保留，body=[" + body + "]"));
        }

        // fromHtml：含 font-weight span 的 <th> → 剥离 span，洁净 MD
        {
            QString html = QStringLiteral(
                "<table><thead><tr>"
                "<th><span style=\"font-weight:700;\">显示</span></th>"
                "<th><span style=\"font-weight:700;\">md语法</span></th>"
                "</tr></thead><tbody><tr>"
                "<td>a</td><td>b</td>"
                "</tr></tbody></table>");
            QString md = MarkdownHandler::fromHtml(html);
            QVERIFY2(md.contains("显示") && md.contains("md语法"),
                     qPrintable("表头文字应保留，md=[" + md + "]"));
            QVERIFY2(!md.contains("font-weight"),
                     qPrintable("不应残留 font-weight 样式，md=[" + md + "]"));
            QVERIFY2(!md.contains("span style"),
                     qPrintable("不应残留 span 标签，md=[" + md + "]"));
        }

        // fromHtml：含 <strong> 的 <th>（TipTap Bold mark 产物）→ 剥离 strong
        {
            QString html = QStringLiteral(
                "<table><thead><tr>"
                "<th><strong>重要</strong></th>"
                "<th><strong>标题</strong></th>"
                "</tr></thead><tbody><tr>"
                "<td>a</td><td>b</td>"
                "</tr></tbody></table>");
            QString md = MarkdownHandler::fromHtml(html);
            QVERIFY2(md.contains("重要") && md.contains("标题"),
                     qPrintable("表头文字应保留，md=[" + md + "]"));
            // 不应输出 **重要** 或 <strong> 等粗体语法/标签
            QVERIFY2(!md.contains("**重要**") && !md.contains("<strong>"),
                     qPrintable("不应含粗体标记/标签，md=[" + md + "]"));
        }

        // 完整往返：MD 表格 → toHtml → fromHtml，表头保持洁净（无多余 ** 或 span）
        {
            QString md1 = QStringLiteral(
                "| 列A | 列B |\n"
                "|-----|-----|\n"
                "| 1   | 2   |\n");
            QString html = MarkdownHandler::toHtml(md1);
            int bsi = html.indexOf("<body");
            int bei = html.indexOf("</body>");
            bsi = html.indexOf('>', bsi) + 1;
            QString body = html.mid(bsi, bei - bsi).trimmed();
            QString md2 = MarkdownHandler::fromHtml(body);
            QVERIFY2(md2.contains("列A") && md2.contains("列B"),
                     qPrintable("表头应保留，md=[" + md2 + "]"));
            QVERIFY2(!md2.contains("font-weight") && !md2.contains("<span"),
                     qPrintable("不应残留 span/font-weight，md=[" + md2 + "]"));
        }
    }

    void testFontSpanToSemanticRoundTrip()
    {
        // toHtml：**粗体**、*斜体*、***粗斜体*** 的 QTextDocument font-span
        // 应被通用步骤转为 <strong>/<em>，不残留 font-weight/font-style span
        {
            QString md = QStringLiteral("这是**粗体**和*斜体*以及***粗斜体***文字");
            QString html = MarkdownHandler::toHtml(md);
            int bsi = html.indexOf("<body");
            int bei = html.indexOf("</body>");
            bsi = html.indexOf('>', bsi) + 1;
            QString body = html.mid(bsi, bei - bsi).trimmed();

            // 粗体 → <strong>
            QVERIFY2(body.contains("<strong>粗体</strong>"),
                     qPrintable("**粗体** 应转为 <strong>，body=[" + body + "]"));
            // 斜体 → <em>
            QVERIFY2(body.contains("<em>斜体</em>"),
                     qPrintable("*斜体* 应转为 <em>，body=[" + body + "]"));
            // 粗斜体 → <strong><em>（次序不定）
            QVERIFY2(body.contains("粗斜体"),
                     qPrintable("***粗斜体*** 文本应保留，body=[" + body + "]"));
            QVERIFY2(body.contains("<strong>") && body.contains("<em>"),
                     qPrintable("***粗斜体*** 应含 <strong> 和 <em>，body=[" + body + "]"));
            // 不应残留 font-weight 或 font-style span
            QVERIFY2(!body.contains("font-weight:") && !body.contains("font-style:"),
                     qPrintable("不应残留 font-weight/font-style span，body=[" + body + "]"));
        }

        // 往返：粗体+颜色混排 → fromHtml 产出的 MD → toHtml 再次转换仍洁净
        {
            QString md1 = QStringLiteral("这是**粗体**和<span style=\"color:red\">红色</span>文字");
            QString html = MarkdownHandler::toHtml(md1);
            int bsi = html.indexOf("<body");
            int bei = html.indexOf("</body>");
            bsi = html.indexOf('>', bsi) + 1;
            QString body = html.mid(bsi, bei - bsi).trimmed();

            // 粗体 → <strong>，颜色 span 保留
            QVERIFY2(body.contains("<strong>粗体</strong>"),
                     qPrintable("粗体应转 <strong>，body=[" + body + "]"));
            QVERIFY2(body.contains("color:red"),
                     qPrintable("颜色 span 应保留，body=[" + body + "]"));
            QVERIFY2(!body.contains("font-weight:700"),
                     qPrintable("不应残留 font-weight，body=[" + body + "]"));

            // 往返：body → fromHtml → toHtml → 仍然洁净
            QString md2 = MarkdownHandler::fromHtml(body);
            QVERIFY2(md2.contains("**粗体**"),
                     qPrintable("粗体应往返为 **...**，md=[" + md2 + "]"));
            QVERIFY2(!md2.contains("font-weight") && !md2.contains("font-style"),
                     qPrintable("MD 不应残留 font-weight/style，md=[" + md2 + "]"));
        }
    }

    void testLiteralDoubleStarRoundTrip()
    {
        // 用户通过 \*\* 写入的字面星号，往返后不应丢失反斜杠转义
        // （QTextDocument::toMarkdown 不自动转义 **，需手动保护）

        // 场景 1：toHtml 正确处理 \*\* → 字面 **
        {
            // C++ 字符串中 \\*\\* 表示字面 \*\*（用户实际输入 markdown 的内容）
            // 注意：传递给 toHtml 的参数是 markdown 源码，其中 \* 表示转义星号
            QString md = QStringLiteral("这是\\*\\*黑体\\*\\*展示");
            QString html = MarkdownHandler::toHtml(md);
            int bsi = html.indexOf("<body");
            int bei = html.indexOf("</body>");
            bsi = html.indexOf('>', bsi) + 1;
            QString body = html.mid(bsi, bei - bsi).trimmed();
            // toHtml 应输出字面 **（在 HTML 中以纯文本形式存在）
            QVERIFY2(body.contains("**"),
                     qPrintable("字面 ** 应保留在 HTML 中，body=[" + body + "]"));
            // 不应渲染为粗体（QTextDocument 应识别转义）
            QVERIFY2(!body.contains("<strong>黑体</strong>"),
                     qPrintable("转义星号不应渲染为粗体，body=[" + body + "]"));
        }

        // 场景 2：HTML 中字面 ** → fromHtml 输出 \*\*
        {
            // 模拟 TipTap getHTML 中字面 ** 文本
            QString md = MarkdownHandler::fromHtml(
                QStringLiteral("<p>这是**黑体**展示</p>"));
            QVERIFY2(md.contains("\\*\\*黑体\\*\\*"),
                     qPrintable("字面 ** 应转为 \\*\\*，md=[" + md + "]"));
            QVERIFY2(!md.contains("**黑体**"),
                     qPrintable("不应残留未转义 **（会被误解释为粗体），md=[" + md + "]"));
        }

        // 场景 3：完整往返：\*\* → HTML → \*\*
        {
            QString md1 = QStringLiteral("教程：用\\*\\*粗体\\*\\*强调文字");
            QString html = MarkdownHandler::toHtml(md1);
            int bsi = html.indexOf("<body");
            int bei = html.indexOf("</body>");
            bsi = html.indexOf('>', bsi) + 1;
            QString body = html.mid(bsi, bei - bsi).trimmed();
            QString md2 = MarkdownHandler::fromHtml(body);
            // 往返后 ** 应保持转义
            QVERIFY2(md2.contains("\\*\\*") && md2.contains("粗体"),
                     qPrintable("往返应保留 \\*\\* 转义，md=[" + md2 + "]"));
        }
    }

    void testHighlightRoundTrip()
    {
        // ==text== 高亮语法 → toHtml → <mark>
        {
            QString md = QStringLiteral("这是==高亮==文字");
            QString html = MarkdownHandler::toHtml(md);
            QVERIFY2(html.contains("<mark>高亮</mark>"),
                     qPrintable("==高亮== 应转为 <mark>，实际: " + html));
        }

        // 转义 \=\= → 字面 ==，不触发高亮
        {
            QString md = QStringLiteral("显示\\=\\=这是高亮\\=\\=");
            QString html = MarkdownHandler::toHtml(md);
            int bsi = html.indexOf("<body");
            int bei = html.indexOf("</body>");
            bsi = html.indexOf('>', bsi) + 1;
            QString body = html.mid(bsi, bei - bsi).trimmed();
            // 转义等号应显示为字面 ==，不应转为 <mark>
            QVERIFY2(body.contains("=="),
                     qPrintable("字面 == 应保留，body=[" + body + "]"));
            QVERIFY2(!body.contains("<mark>"),
                     qPrintable("转义 == 不应转为 <mark>，body=[" + body + "]"));
        }

        // 内联代码内的 == 不应被转换
        {
            QString md = QStringLiteral("语法：`==这是高亮==`");
            QString html = MarkdownHandler::toHtml(md);
            int bsi = html.indexOf("<body");
            int bei = html.indexOf("</body>");
            bsi = html.indexOf('>', bsi) + 1;
            QString body = html.mid(bsi, bei - bsi).trimmed();
            // <code> 内应保留 ==，不应出现 <mark>
            QVERIFY2(body.contains("=="),
                     qPrintable("内联代码内的 == 应保留，body=[" + body + "]"));
            QVERIFY2(!body.contains("<mark>"),
                     qPrintable("内联代码内的 == 不应转为 <mark>，body=[" + body + "]"));
        }

        // 往返：MD ==text== → HTML → MD
        {
            QString md1 = QStringLiteral("这是==高亮==文字");
            QString html = MarkdownHandler::toHtml(md1);
            int bsi = html.indexOf("<body");
            int bei = html.indexOf("</body>");
            bsi = html.indexOf('>', bsi) + 1;
            QString body = html.mid(bsi, bei - bsi).trimmed();
            QString md2 = MarkdownHandler::fromHtml(body);
            QVERIFY2(md2.contains("==高亮=="),
                     qPrintable("往返应保留 ==text== 高亮语法，md=[" + md2 + "]"));
        }

        // 往返：转义 \=\= → HTML 字面 == → fromHtml 反转义回 \=\=
        {
            QString md1 = QStringLiteral("显示\\=\\=这是高亮\\=\\=");
            QString html = MarkdownHandler::toHtml(md1);
            int bsi = html.indexOf("<body");
            int bei = html.indexOf("</body>");
            bsi = html.indexOf('>', bsi) + 1;
            QString body = html.mid(bsi, bei - bsi).trimmed();
            // HTML 中应显为字面 ==
            QVERIFY2(body.contains("=="),
                     qPrintable("HTML 中应保留字面 ==，body=[" + body + "]"));
            QString md2 = MarkdownHandler::fromHtml(body);
            // 往返后应恢复转义
            QVERIFY2(md2.contains("\\=\\="),
                     qPrintable("往返应恢复 \\=\\= 转义，md=[" + md2 + "]"));
            QVERIFY2(!md2.contains("=="),
                     qPrintable("不应有未转义的 ==，md=[" + md2 + "]"));
        }
    }

    void testBlockquoteMultilineRoundTrip()
    {
        // 多行引用往返后不应合并为一行
        QString md1 = QStringLiteral("> 引用1\n> 引用-2\n> 最后引用行\n");
        QString html = MarkdownHandler::toHtml(md1);
        int bsi = html.indexOf("<body");
        int bei = html.indexOf("</body>");
        bsi = html.indexOf('>', bsi) + 1;
        QString body = html.mid(bsi, bei - bsi).trimmed();
        QString md2 = MarkdownHandler::fromHtml(body);
        QVERIFY2(md2.contains("> 引用1\n") || md2.contains("> 引用1\r"),
                 qPrintable("第一行引用应保留，md=[" + md2 + "]"));
        QVERIFY2(md2.contains("> 引用-2"),
                 qPrintable("第二行引用应保留，md=[" + md2 + "]"));
        QVERIFY2(md2.contains("> 最后引用行"),
                 qPrintable("最后行引用应保留，md=[" + md2 + "]"));
        // 不应合并为一行
        QVERIFY2(!md2.contains("引用1 引用-2 最后引用行"),
                 qPrintable("多行引用不应合并为一行，md=[" + md2 + "]"));
    }

    void testBlockquoteInterruptParagraphNoSoftBreak()
    {
        // 段落紧跟块引用（无空行）：块引用打断段落是块边界，非软换行，
        // 不应插入 <br data-brk>（否则往返多一个空行）。
        QString md1 = QStringLiteral("正文\n> 引用\n");
        QString html = MarkdownHandler::toHtml(md1);
        int bsi = html.indexOf("<body");
        int bei = html.indexOf("</body>");
        bsi = html.indexOf('>', bsi) + 1;
        QString body = html.mid(bsi, bei - bsi).trimmed();
        QVERIFY2(!body.contains("<br data-brk>"),
                 qPrintable("段落到块引用不应插入软换行，body=[" + body + "]"));
        QString md2 = MarkdownHandler::fromHtml(body);
        QVERIFY2(md2 == QStringLiteral("正文\n\n> 引用"),
                 qPrintable("段落到块引用应精确往返（无多余空行/软换行），实际: [" + md2 + "]"));
    }

    void testBlockquoteLazyContinuationKeepsSoftBreak()
    {
        // 反向：块引用行后紧跟普通行（lazy continuation）仍属同一段落，
        // 软换行应保留（规范化为显式 > 前缀，内容与换行不丢失）。
        QString md1 = QStringLiteral("> 引用\n正文\n");
        QString md2 = MarkdownHandler::fromHtml(MarkdownHandler::toHtml(md1));
        QVERIFY2(md2 == QStringLiteral("> 引用\n> 正文"),
                 qPrintable("块引用 lazy continuation 应保留软换行，实际: [" + md2 + "]"));
    }

    void testBlockquoteCodeFenceRoundTrip()
    {
        // 块引用内代码围栏：此前 BRMARKER 会破坏围栏解析，把 <pre> 拆成多段并
        // 注入 <br data-brk>，代码内容与语言信息串全部丢失。修复后代码内容、
        // 语言信息串均保留，块引用结构完整。代码内容行规范化为 lazy 形式
        // （不带 > 前缀，语义等价，Qt toMarkdown 的固有输出）。
        struct { const char *in; const char *expect; } cases[] = {
            {"> 前\n> ```\n> int x;\n> ```\n> 后\n", "> 前\n> ```\nint x;\n```\n> 后"},
            {"> ```cpp\n> int x;\n> ```\n", "> ```cpp\nint x;\n```"},
            {"> 前\n> ```\n> line1\n> line2\n> ```\n> 后\n", "> 前\n> ```\nline1\nline2\n```\n> 后"},
            {"> ```\n> int x;\n> ```\n", "> ```\nint x;\n```"},
            // 代码块后空行分隔的第二个块引用/正文：空行必须保留，不得合并进同一块引用
            {"> 前\n> ```\n> code\n> ```\n\n> 后\n", "> 前\n> ```\ncode\n```\n\n> 后"},
            {"> ```\n> code\n> ```\n\n正文\n", "> ```\ncode\n```\n\n正文"},
        };
        for (const auto &c : cases) {
            QString md1 = QString::fromUtf8(c.in);
            QString html = MarkdownHandler::toHtml(md1);
            int bsi = html.indexOf("<body");
            int bei = html.indexOf("</body>");
            bsi = html.indexOf('>', bsi) + 1;
            QString body = html.mid(bsi, bei - bsi).trimmed();
            // 代码块不应被注入软换行（BRMARKER 破坏围栏的典型症状）
            QVERIFY2(!body.contains("<br data-brk>"),
                     qPrintable(QString("块引用代码块不应含软换行，输入[%1] body[%2]")
                                    .arg(c.in, body)));
            // 代码块应整体包裹在 <blockquote> 内
            QVERIFY2(body.contains("<blockquote>") && body.contains("<pre"),
                     qPrintable(QString("块引用代码块应含 <blockquote><pre>，输入[%1] body[%2]")
                                    .arg(c.in, body)));
            QString md2 = MarkdownHandler::fromHtml(body);
            QVERIFY2(md2 == QString::fromUtf8(c.expect),
                     qPrintable(QString("块引用代码块应精确往返，输入[%1] 实际[%2]")
                                    .arg(c.in, md2)));
        }
    }

    void testHorizontalRuleRoundTrip()
    {
        // 连字符水平线 --- 往返：Qt setMarkdown 会把连续 --- 折叠为一个 <hr>，
        // 且 ---\n\n正文\n\n--- 会被灾难性误解析（正文与水平线一并丢失），
        // toHtml 需占位保护。以下各场景均应精确往返。
        struct { const char *in; const char *expect; } cases[] = {
            {"---\n", "---"},
            {"正文\n\n---\n", "正文\n\n---"},
            {"---\n\n正文\n", "---\n\n正文"},
            {"---\n\n---\n\n---\n", "---\n\n---\n\n---"},
            {"---\n\n正文\n\n---\n", "---\n\n正文\n\n---"},
        };
        for (const auto &c : cases) {
            QString md1 = QString::fromUtf8(c.in);
            QString md2 = MarkdownHandler::fromHtml(MarkdownHandler::toHtml(md1));
            QVERIFY2(md2 == QString::fromUtf8(c.expect),
                     qPrintable(QString("水平线往返应精确，输入[%1] 实际[%2]")
                                    .arg(c.in, md2)));
        }
    }

    void testSetextHeadingNotTreatedAsHr()
    {
        // 正文\n---（无空行）是 setext 二级标题，非水平线，不应被占位保护。
        // 正文\n=== 是 setext 一级标题，此前被误判为正文行，在标题与下划线之间
        // 插入幽灵 <br data-brk>。二者往返均应规范化为 ATX 形式，且无幽灵软换行。
        struct { const char *in; const char *expect; } cases[] = {
            {"标题\n---\n", "## 标题"},
            {"标题\n===\n", "# 标题"},
        };
        for (const auto &c : cases) {
            QString md1 = QString::fromUtf8(c.in);
            QString body = MarkdownHandler::toHtml(md1);
            int bsi = body.indexOf("<body");
            int bei = body.indexOf("</body>");
            bsi = body.indexOf('>', bsi) + 1;
            body = body.mid(bsi, bei - bsi).trimmed();
            QVERIFY2(!body.contains("<br data-brk>"),
                     qPrintable(QString("setext 标题不应含幽灵软换行，输入[%1] body[%2]")
                                    .arg(c.in, body)));
            QString md2 = MarkdownHandler::fromHtml(body);
            QVERIFY2(md2 == QString::fromUtf8(c.expect),
                     qPrintable(QString("setext 标题应规范化，输入[%1] 实际[%2]")
                                    .arg(c.in, md2)));
        }
    }

    void testTableSeparatorRoundTrip()
    {
        // Qt toMarkdown 把表格分隔行输出为单连字符（|-|-|），此前归一化正则
        // 只匹配 3+ 连字符，导致分隔行被误判为数据行、--- 缩成 -。
        struct { const char *in; const char *expect; } cases[] = {
            {"| a | b |\n| --- | --- |\n| 1 | 2 |\n", "| a | b |\n| --- | --- |\n| 1 | 2 |"},
        };
        for (const auto &c : cases) {
            QString md1 = QString::fromUtf8(c.in);
            QString md2 = MarkdownHandler::fromHtml(MarkdownHandler::toHtml(md1));
            QVERIFY2(md2 == QString::fromUtf8(c.expect),
                     qPrintable(QString("表格分隔行应精确往返（--- 不缩成 -），输入[%1] 实际[%2]")
                                    .arg(c.in, md2)));
        }
    }

    void testBlockPlaceholderNoTrailingSpace()
    {
        // 块级元素（块引用/代码块/块公式/任务列表）后跟段落：占位符作为裸文本
        // 节点时 setHtml 会把块间换行折成行尾空格，往返后残留。占位符统一包裹
        // <p> 后应消除该尾随空格。
        struct { const char *in; const char *expect; } cases[] = {
            {"> 引用\n\n正文\n", "> 引用\n\n正文"},
            {"```cpp\nint x;\n```\n\n正文\n", "```cpp\nint x;\n```\n\n正文"},
            {"$$\na=1\n$$\n\n正文\n", "$$\na=1\n$$\n\n正文"},
            {"- [ ] 任务\n\n正文\n", "- [ ] 任务\n\n正文"},
        };
        for (const auto &c : cases) {
            QString md1 = QString::fromUtf8(c.in);
            QString md2 = MarkdownHandler::fromHtml(MarkdownHandler::toHtml(md1));
            QVERIFY2(md2 == QString::fromUtf8(c.expect),
                     qPrintable(QString("块级元素后跟段落应精确往返（无行尾空格），输入[%1] 实际[%2]")
                                    .arg(c.in, md2)));
        }
    }

    void testBrMixedWithSoftBreaks()
    {
        // 软换行（BRMARKER）+ 硬换行（<br>）混排，往返后 <br> 不应丢失
        QString md1 = QStringLiteral("这是新行。 \n这是新行。 <br>\n这是新行。 \n这是新行。\n");
        QString html = MarkdownHandler::toHtml(md1);
        int bsi = html.indexOf("<body");
        int bei = html.indexOf("</body>");
        bsi = html.indexOf('>', bsi) + 1;
        QString body = html.mid(bsi, bei - bsi).trimmed();
        // toHtml 应正确区分 BRMARKER（<br data-brk>）和用户 <br>
        QVERIFY2(body.contains("<br data-brk>"),
                 qPrintable("软换行应有 data-brk，body=[" + body + "]"));
        QVERIFY2(body.contains("<br>"),
                 qPrintable("用户 <br> 应保留，body=[" + body + "]"));

        QString md2 = MarkdownHandler::fromHtml(body);
        // <br> 必须保留
        QVERIFY2(md2.contains("<br>"),
                 qPrintable("<br> 不应丢失，md=[" + md2 + "]"));
        // 所有文本应保留
        QVERIFY2(md2.contains("这是新行。"),
                 qPrintable("文本内容应保留，md=[" + md2 + "]"));
    }

    void testEscapedAngleBracketRoundTrip()
    {
        // 场景 1: \<mark\>text\</mark\> 往返后转义符不应丢失
        {
            QString md1 = QStringLiteral("\\<mark\\>这是文字\\</mark\\>\n");
            QString html = MarkdownHandler::toHtml(md1);
            int bsi = html.indexOf("<body");
            int bei = html.indexOf("</body>");
            bsi = html.indexOf('>', bsi) + 1;
            QString body = html.mid(bsi, bei - bsi).trimmed();

            // toHtml 后 \< → &lt; 实体
            QVERIFY2(body.contains("&lt;mark&gt;"),
                     qPrintable("\\<mark\\> 应转义为实体，body=[" + body + "]"));
            QVERIFY2(body.contains("&lt;/mark&gt;"),
                     qPrintable("\\</mark\\> 应转义为实体，body=[" + body + "]"));

            QString md2 = MarkdownHandler::fromHtml(body);
            // fromHtml 后应还原为 \< 和 \>
            QVERIFY2(md2.contains("\\<mark\\>"),
                     qPrintable("\\< 和 \\> 不应丢失，md=[" + md2 + "]"));
            QVERIFY2(md2.contains("\\</mark\\>"),
                     qPrintable("\\< 和 \\> 不应丢失（关闭标签），md=[" + md2 + "]"));
            // 不应出现裸 HTML 标签
            QVERIFY2(!md2.contains("<mark>"),
                     qPrintable("不应出现裸 <mark> 标签，md=[" + md2 + "]"));
        }

        // 场景 2: 完整往返 — 再次加载不污染
        {
            QString md1 = QStringLiteral("转义\\<span\\>文字\\</span\\>测试\n");
            QString html1 = MarkdownHandler::toHtml(md1);
            int bsi = html1.indexOf("<body");
            int bei = html1.indexOf("</body>");
            bsi = html1.indexOf('>', bsi) + 1;
            QString body1 = html1.mid(bsi, bei - bsi).trimmed();
            QString md2 = MarkdownHandler::fromHtml(body1);

            // 第二次往返
            QString html2 = MarkdownHandler::toHtml(md2 + "\n");
            bsi = html2.indexOf("<body");
            bei = html2.indexOf("</body>");
            bsi = html2.indexOf('>', bsi) + 1;
            QString body2 = html2.mid(bsi, bei - bsi).trimmed();
            QString md3 = MarkdownHandler::fromHtml(body2);

            // 不应退化为高亮/样式
            QVERIFY2(!md3.contains("=="),
                     qPrintable("二次往返不应出现高亮 ==，md=[" + md3 + "]"));
            QVERIFY2(md3.contains("\\<span\\>"),
                     qPrintable("二次往返转义不应丢失，md=[" + md3 + "]"));
        }

        // 场景 3: 普通文本中的字面 < 和 >（非 HTML 标签）
        {
            QString md1 = QStringLiteral("a\\<b 且 c\\>d\n");
            QString html = MarkdownHandler::toHtml(md1);
            int bsi = html.indexOf("<body");
            int bei = html.indexOf("</body>");
            bsi = html.indexOf('>', bsi) + 1;
            QString body = html.mid(bsi, bei - bsi).trimmed();
            QString md2 = MarkdownHandler::fromHtml(body);

            QVERIFY2(md2.contains("\\<b"),
                     qPrintable("\\< 不应丢失，md=[" + md2 + "]"));
            QVERIFY2(md2.contains("\\>d"),
                     qPrintable("\\> 不应丢失，md=[" + md2 + "]"));
        }
    }

    // ===== 块级图片分隔（TipTap 块级 Image 节点 → 独立块）=====

    void testBlockImageAfterHeading()
    {
        // Bug：TipTap Image 是块级原子节点（inline:false），getHTML() 输出裸 <img>
        // 顶层元素。fromHtml 若不处理，QTextDocument::setHtml 会把游离 <img> 并入
        // 前一个标题块，toMarkdown 产出 "# 标题![](...)"，块边界丢失。
        QString html = QStringLiteral("<h1>标题</h1><img src=\"x.jpg\">");
        QString md = MarkdownHandler::fromHtml(html);
        // 图片不应粘在标题同一行
        QVERIFY2(!md.contains("标题!["),
                 qPrintable("图片不应粘在标题后，md=[" + md + "]"));
        // 标题与图片间的空行应保留（往返保真：不删用户写入的块间空行）。
        // 该空行不影响 TipTap 渲染，且与"段落→图片需空行"保持一致。
        QVERIFY2(md.contains("# 标题\n\n!["),
                 qPrintable("标题与图片间应保留空行，md=[" + md + "]"));
    }

    void testBlockImageAfterParagraph()
    {
        // 普通文本回车后插入图片：图片应与段落分开
        QString html = QStringLiteral("<p>普通文本</p><img src=\"x.jpg\">");
        QString md = MarkdownHandler::fromHtml(html);
        QVERIFY2(!md.contains("普通文本!["),
                 qPrintable("图片不应粘在段落文本后，md=[" + md + "]"));
        int p = md.indexOf("普通文本");
        int img = md.indexOf("](x.jpg)");
        QVERIFY2(p >= 0 && img > p,
                 qPrintable("段落与图片均应存在，md=[" + md + "]"));
        QVERIFY2(md.mid(p, img - p).contains("\n\n"),
                 qPrintable("段落与图片间应有空行，md=[" + md + "]"));
    }

    void testBlockImageTipTapExact()
    {
        // 用户实际复现输入：标题 + 裸 <img> + 尾随空 <p></p>（光标行）
        QString html = QStringLiteral(
            "<h1>这是标题1</h1>"
            "<img src=\"/home/daijd/Desktop/464-640x480.jpg\">"
            "<p></p>");
        QString md = MarkdownHandler::fromHtml(html);
        QVERIFY2(!md.contains("这是标题1!["),
                 qPrintable("图片不应粘在标题后，md=[" + md + "]"));
        QVERIFY2(md.contains("# 这是标题1"),
                 qPrintable("标题应保留，md=[" + md + "]"));
        // 标题与图片间空行保留（往返保真，见 testBlockImageAfterHeading）
        QVERIFY2(md.contains("# 这是标题1\n\n!["),
                 qPrintable("标题与图片间应保留空行，md=[" + md + "]"));
    }

    void testBlockImageAtStart()
    {
        // 图片作为文档首块：应为独立块，不与后续文本合并
        QString html = QStringLiteral("<img src=\"x.jpg\"><p>正文</p>");
        QString md = MarkdownHandler::fromHtml(html);
        QVERIFY2(!md.contains("![") || md.indexOf("](x.jpg)") < md.indexOf("正文"),
                 qPrintable("图片应在正文之前为独立块，md=[" + md + "]"));
        int img = md.indexOf("](x.jpg)");
        int t = md.indexOf("正文");
        QVERIFY2(img >= 0 && t > img,
                 qPrintable("图片与正文均应存在，md=[" + md + "]"));
        QVERIFY2(md.mid(img, t - img).contains("\n\n"),
                 qPrintable("图片与正文间应有空行，md=[" + md + "]"));
    }

    void testInlineImageInParagraphUnchanged()
    {
        // 回归保护：已在 <p> 内的内联图片应留在段落内，不被提升为独立块
        QString html = QStringLiteral("<p>前文<img src=\"x.jpg\">后文</p>");
        QString md = MarkdownHandler::fromHtml(html);
        QVERIFY2(!md.contains("\n\n!["),
                 qPrintable("内联图片不应被拆成独立块，md=[" + md + "]"));
        QVERIFY2(md.contains("前文") && md.contains("后文"),
                 qPrintable("前后文应保留，md=[" + md + "]"));
    }

    // 加载方向：标题后的块级图片必须渲染为裸 <img>，不得包在 <p> 里。
    // TipTap 的 Image 节点为块级原子节点，若传入 <p><img></p>，TipTap 会把图片
    // 提升为块、却把空 <p> 壳留在原处 —— 渲染成标题与图片之间的空行。
    // （与 fromHtml 的步骤 E 互为对偶：保存方向 Qt 需 <p> 才认块，加载方向 TipTap 需裸 <img>）
    void testToHtmlUnwrapsBlockImageFromParagraph()
    {
        const QString md = QStringLiteral(
            "# 这是标题1\n![464-640x480.jpg](images/464-640x480.jpg)");
        const QString html = MarkdownHandler::toHtml(md);
        QVERIFY2(html.contains("<img"),
                 qPrintable("应含图片，html=[" + html + "]"));
        // 图片若包在 <p> 里，TipTap 提升后会留空 <p> 渲染成空行
        bool wrapped = QRegularExpression("<p[^>]*>\\s*<img\\b").match(html).hasMatch();
        QVERIFY2(!wrapped,
                 qPrintable("块级图片不应包在 <p> 里（会致渲染空行），html=[" + html + "]"));
    }

    void testToHtmlImageAfterParagraphIsBlock()
    {
        // 段落紧跟图片（无空行）：图片应为独立块级，不被 lazy-continuation 并入
        // 段落成内联图片（否则 TipTap 块级 Image 提升图片后留空段落，渲染成空行）。
        const QString html = MarkdownHandler::toHtml(
            QStringLiteral("插入的图片（相对路径）\n![image](580-640x480.jpg)"));
        // 图片应位于文本段结束之后（块级），而非内联在文本段内
        int textEnd = html.indexOf(QStringLiteral("插入的图片（相对路径）</p>"));
        int imgPos = html.indexOf("<img");
        QVERIFY2(textEnd >= 0 && imgPos > textEnd,
                 qPrintable("图片应在文本段之后为块级，html=[" + html + "]"));
        // 图片不应包在 <p> 里
        bool wrapped = QRegularExpression("<p[^>]*>\\s*<img\\b").match(html).hasMatch();
        QVERIFY2(!wrapped,
                 qPrintable("块级图片不应包在 <p> 里，html=[" + html + "]"));
    }

    void testNestedFenceCodeBlock()
    {
        // 嵌套围栏：```markdown 源码块内嵌 ```mermaid（展示原始 MD 源码）
        // 修复前：朴素围栏翻转会把内层 ``` 误判为闭合，导致源码块内容错乱、
        //         后续标题/正文被吞入代码块。
        const QString md = QStringLiteral(
            "````markdown\n```mermaid\ngraph TD\nA --> B\n```\n````\n\n## 标题\n\n正文\n");
        const QString html = MarkdownHandler::toHtml(md);
        // 外层应识别为一个 markdown 代码块，内层 ```mermaid 作为字面内容保留
        QVERIFY2(html.contains("class=\"language-markdown\""),
                 qPrintable("应输出 language-markdown 代码块，实际: " + html));
        QVERIFY2(html.contains("```mermaid"),
                 qPrintable("内层 ```mermaid 应作为字面内容保留，实际: " + html));
        QVERIFY(html.contains("graph TD"));
        // 后续标题不应被吞入代码块
        QVERIFY2(html.contains(">标题<"),
                 qPrintable("嵌套围栏后的标题应正常渲染，实际: " + html));
        QVERIFY(html.contains("正文"));
        // 往返：源码应原样恢复
        const QString back = MarkdownHandler::fromHtml(html);
        QVERIFY2(back.contains("```mermaid"),
                 qPrintable("往返后应保留 ```mermaid，实际: " + back));
        QVERIFY(back.contains("graph TD"));
    }

    void testNestedFenceRoundTrip()
    {
        // 嵌套围栏完整往返：````markdown 内含 ```cpp。fromHtml 还原时外层围栏
        // 必须长于内层（4 反引号 > 3 反引号），否则内外同长会提前闭合，
        // 再切回 WYSIWYG 即错乱（source mode 往返 bug）。
        const QString md = QStringLiteral(
            "````markdown\n```cpp\n#include <iostream>\nint main() {}\n```\n````\n");
        const QString html = MarkdownHandler::toHtml(md);
        const QString back = MarkdownHandler::fromHtml(html);
        QVERIFY2(back.contains("````markdown"),
                 qPrintable("外层围栏应还原为 4 反引号，实际: " + back));
        QVERIFY(back.contains("```cpp"));
        QVERIFY(back.contains("int main() {}"));
        // 往返后的 MD 再次 toHtml 仍应正确识别外层 markdown、内层 ```cpp 为字面内容
        const QString html2 = MarkdownHandler::toHtml(back);
        QVERIFY2(html2.contains("class=\"language-markdown\""),
                 qPrintable("往返后外层仍应是 markdown 代码块，实际: " + html2));
        QVERIFY2(html2.contains("```cpp"),
                 qPrintable("往返后内层 ```cpp 应作为字面内容保留，实际: " + html2));
    }

    void testCodeBlockAdjacentBlockMathRoundTrip()
    {
        // 源码块（含 $$ 块级公式演示）紧邻真实块级公式：
        // fromHtml 中 setHtml 把相邻块级占位符放进同一段落，toMarkdown
        // 输出为同一行空格分隔（"ZZCB0ZZ ZZBM0ZZ"），还原后闭合 ``` 与 $$
        // 粘连成 "```$$"，围栏看似丢失。修复：多行备份占位符独占一行。
        const QString md = QStringLiteral(
            "```markdown\n$$\nE = mc^2\n$$\n```\n\n$$\nE = mc^2\n$$\n");
        const QString html = MarkdownHandler::toHtml(md);
        const QString back = MarkdownHandler::fromHtml(html);
        QVERIFY2(!back.contains("```$$"),
                 qPrintable("闭合围栏不应与 $$ 粘连，实际: " + back));
        QVERIFY2(!back.contains("$$```"),
                 qPrintable("$$ 不应与围栏粘连，实际: " + back));
        QVERIFY2(back.contains("```\n\n$$\nE = mc^2\n$$"),
                 qPrintable("闭合围栏与块级公式应以空行分隔，实际: " + back));
    }

    void testAdjacentCodeBlocksNotMerged()
    {
        // 相邻两个代码块（仅空行分隔）不应被合并为一个，语言各自保留
        const QString md = QStringLiteral("```cpp\na\nb\n```\n\n```python\nc\nd\n```\n");
        const QString html = MarkdownHandler::toHtml(md);
        QVERIFY2(html.contains("class=\"language-cpp\""),
                 qPrintable("应含 language-cpp，实际: " + html));
        QVERIFY2(html.contains("class=\"language-python\""),
                 qPrintable("应含 language-python，实际: " + html));
        // 两个代码块应各自独立（cpp 内容不含 python 内容）
        int cppPos = html.indexOf("class=\"language-cpp\"");
        int pyPos = html.indexOf("class=\"language-python\"");
        QVERIFY2(cppPos >= 0 && pyPos >= 0 && cppPos != pyPos,
                 qPrintable("两个代码块应分别输出，实际: " + html));
    }

    // ===== 80 列自动换行回归：长段落/列表项/引用块不应被折行拆分 =====
    // 根因：QTextDocument::toMarkdown() 硬编码 ColumnLimit = 80（无公开 API 关闭），
    // 把含空格的长段落/列表项/引用块在词边界折成多行，破坏往返保真（列表项被拆成
    // 多块、段落被拆成多行）。修复：fromHtml 在 toMarkdown 之后做反换行后处理。

    void testLongParagraphNotWrapped()
    {
        const QString md1 = longSpacedText(30);   // 含空格、远超 80 列
        const QString md2 = MarkdownHandler::fromHtml(MarkdownHandler::toHtml(md1));
        QVERIFY2(!md2.contains('\n'),
                 qPrintable("长段落不应被折成多行，实际: " + md2));
        QVERIFY2(md2.trimmed() == md1.trimmed(),
                 qPrintable("长段落往返文本应一致，实际: " + md2));
    }

    void testLongOrderedListNotWrapped()
    {
        const QString md1 = QStringLiteral("1. ") + longSpacedText(30) + QStringLiteral("\n");
        const QString md2 = MarkdownHandler::fromHtml(MarkdownHandler::toHtml(md1));
        QVERIFY2(!md2.contains('\n'),
                 qPrintable("长有序列表项不应被折成多行，实际: " + md2));
        QVERIFY2(md2.contains("1. ") && md2.contains("单词30"),
                 qPrintable("有序列表项内容应保留，实际: " + md2));
    }

    void testLongUnorderedListNotWrapped()
    {
        const QString md1 = QStringLiteral("- ") + longSpacedText(30) + QStringLiteral("\n");
        const QString md2 = MarkdownHandler::fromHtml(MarkdownHandler::toHtml(md1));
        QVERIFY2(!md2.contains('\n'),
                 qPrintable("长无序列表项不应被折成多行，实际: " + md2));
        QVERIFY2(md2.contains("- ") && md2.contains("单词30"),
                 qPrintable("无序列表项内容应保留，实际: " + md2));
    }

    void testLongBlockquoteNotWrapped()
    {
        const QString md1 = QStringLiteral("> ") + longSpacedText(30) + QStringLiteral("\n");
        const QString md2 = MarkdownHandler::fromHtml(MarkdownHandler::toHtml(md1));
        QVERIFY2(!md2.contains('\n'),
                 qPrintable("长引用块不应被折成多行，实际: " + md2));
        QVERIFY2(md2.contains("> ") && md2.contains("单词30"),
                 qPrintable("引用块内容应保留，实际: " + md2));
    }

    void testLongListMultipleItemsNotSplit()
    {
        // 用户报告：3 条长有序列表项往返后被拆成 4 条。修复后仍为 3 条单行项。
        const QString md1 =
            QStringLiteral("1. ") + longSpacedText(20) + QStringLiteral("\n")
            + QStringLiteral("2. ") + longSpacedText(20) + QStringLiteral("\n")
            + QStringLiteral("3. ") + longSpacedText(20) + QStringLiteral("\n");
        const QString md2 = MarkdownHandler::fromHtml(MarkdownHandler::toHtml(md1));
        // 只应有 2 个换行（3 条列表项之间的两个分隔），无折行续行
        QVERIFY2(md2.count('\n') == 2,
                 qPrintable("3 条列表项应只有 2 个换行，实际: " + md2));
        QVERIFY2(md2.contains("1. ") && md2.contains("2. ") && md2.contains("3. "),
                 qPrintable("3 条列表项序号应保留，实际: " + md2));
    }

    void testSoftBreakInListRoundTrip()
    {
        // Shift+Enter 在列表项内产生的 <br data-brk> 应保留为项内续行（缩进）
        const QString html =
            QStringLiteral("<ol><li>第一行内容<br data-brk=\"true\">第二行内容</li>"
                           "<li>第三行内容</li></ol>");
        const QString md = MarkdownHandler::fromHtml(html);
        QVERIFY2(md.contains("第一行内容") && md.contains("第二行内容"),
                 qPrintable("软换行两侧内容应保留，实际: " + md));
        // 续行应以缩进保持在同一列表项内，且列表仍为 2 条
        QVERIFY2(md.contains("\n    "),
                 qPrintable("列表项内软换行应带 4 空格续行缩进，实际: " + md));
        QVERIFY2(!md.contains("ZZBK"),
                 qPrintable("不应残留 ZZBK 占位符，实际: " + md));
    }

    void testSoftBreakInParagraphRoundTrip()
    {
        // 段落内软换行（两行正文，toHtml 的 BRMARKER 路径）应保留为两行，不折行
        const QString md1 = QStringLiteral("第一行正文内容\n第二行正文内容\n");
        const QString md2 = MarkdownHandler::fromHtml(MarkdownHandler::toHtml(md1));
        QVERIFY2(md2.contains("第一行正文内容") && md2.contains("第二行正文内容"),
                 qPrintable("段落软换行两侧内容应保留，实际: " + md2));
        QVERIFY2(md2.count('\n') == 1,
                 qPrintable("两行段落应恰好一个软换行，实际: " + md2));
        QVERIFY2(!md2.contains("ZZBK"),
                 qPrintable("不应残留 ZZBK 占位符，实际: " + md2));
        // 软换行行尾不得残留空格（BRMARKER 前置隔离空格未在替换时删除的缺陷）
        QVERIFY2(md2 == QStringLiteral("第一行正文内容\n第二行正文内容"),
                 qPrintable("软换行往返应精确相等（无行尾空格），实际: [" + md2 + "]"));
    }

    void testSoftBreakNoTrailingSpaceEnglish()
    {
        // 英文词边界的软换行：往返后行尾同样不得残留空格
        const QString md1 = QStringLiteral("hello world\nnext line\n");
        const QString md2 = MarkdownHandler::fromHtml(MarkdownHandler::toHtml(md1));
        QVERIFY2(md2 == QStringLiteral("hello world\nnext line"),
                 qPrintable("英文软换行往返应精确相等（无行尾空格），实际: [" + md2 + "]"));
    }

    void testFromHtmlPlatformMonoSpanToInlineCode()
    {
        // QTextMarkdownImporter 给行内代码的字体是
        // QFontDatabase::systemFont(QFontDatabase::FixedFont)，其族名平台相关：
        //   Linux="monospace"、Windows="Courier New"、macOS="Menlo"。
        // QTextDocument::toHtml 据此导出 <span font-family:'...'>，fromHtml 的
        // monospace span 兜底正则若硬编码只认 'monospace'，Windows/macOS 上
        // 行内代码在加载时即丢失（切源码后反引号消失、内容被转义）。
        const QStringList platformFamilies = {
            QStringLiteral("Courier New"),   // Windows
            QStringLiteral("Menlo"),         // macOS
        };
        for (const QString &family : platformFamilies) {
            const QString html = QStringLiteral(
                "<p>before <span style=\" font-family:'%1';\">==code==</span> after</p>")
                .arg(family);
            const QString md = MarkdownHandler::fromHtml(html);
            QVERIFY2(md.contains("`==code==`"),
                     qPrintable(family + " span 应还原为行内代码，实际: " + md));
        }
    }

    void testTableInlineCodeRoundTrip()
    {
        // 用户实测输入：表格「语法|显示」对照表（行内代码在表格单元格内）
        const QString md1 = QStringLiteral(
            "| markdown语法 | 显示 |\n"
            "| --- | --- |\n"
            "| `==高亮==` | ==高亮== |\n"
            "| `**A**` | **A** |\n");
        const QString md2 = MarkdownHandler::fromHtml(MarkdownHandler::toHtml(md1));
        // 行内代码语义必须保留（code mark 往返）
        QVERIFY2(md2.contains("`==高亮==`"),
                 qPrintable("表格内行内代码应保留反引号，实际: " + md2));
        QVERIFY2(md2.contains("`**A**`"),
                 qPrintable("表格内行内代码应保留反引号，实际: " + md2));
        // 表格空格规范化不应因单元格含反引号而整行跳过：
        // 竖线两侧各恰好一个空格（GFM 风格 | cell |）
        static const QRegularExpression rowRe(
            QStringLiteral("^\\| (`==高亮==`|`\\*\\*A\\*\\*`) \\| (==高亮==|\\*\\*A\\*\\*) \\|$"),
            QRegularExpression::MultilineOption);
        QVERIFY2(rowRe.match(md2).hasMatch(),
                 qPrintable("表格行应为 | cell | 格式（竖线两侧各一空格），实际: " + md2));
    }

    void testTableDoubleBacktickCellSpacing()
    {
        // 内容本身含反引号的行内代码（`` `x` `` 形式）同样不应阻断表格规范化
        const QString md1 = QStringLiteral(
            "| 语法 | 显示 |\n"
            "| --- | --- |\n"
            "| `` `code` `` | `code` |\n");
        const QString md2 = MarkdownHandler::fromHtml(MarkdownHandler::toHtml(md1));
        static const QRegularExpression rowRe(
            QStringLiteral("^\\| `` `code` `` \\| `code` \\|$"),
            QRegularExpression::MultilineOption);
        QVERIFY2(rowRe.match(md2).hasMatch(),
                 qPrintable("双反引号单元格行应为 | cell | 格式，实际: " + md2));
    }
};

QTEST_MAIN(TestMarkdownHandler)
#include "test_markdownhandler.moc"
