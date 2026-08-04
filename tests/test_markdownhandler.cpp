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
