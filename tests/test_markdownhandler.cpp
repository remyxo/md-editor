#include <QTest>
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

    void testExtractTitle() {
        QString md = "# 我的文档标题\n\n内容。";
        QString title = MarkdownHandler::extractTitle(md);
        QCOMPARE(title, QString("我的文档标题"));
    }

    void testExtractTitleNoH1() {
        QCOMPARE(MarkdownHandler::extractTitle("## 二级标题\n\n内容"), QString());
    }
};

QTEST_MAIN(TestMarkdownHandler)
#include "test_markdownhandler.moc"
