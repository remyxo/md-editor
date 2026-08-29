#include <QTest>
#include <QTextDocument>
#include <QTextCursor>
#include <QTextCharFormat>
#include <QTextImageFormat>
#include <QTextList>
#include <QTextStream>
#include "../src/core/qt/QtMarkdownWriter.h"

// vendored QtMarkdownWriter 与 QTextDocument::toMarkdown() 的等价性测试
// （行为保持验证：同一 QTextDocument 双侧序列化须逐字节一致）。
// 背景、改动清单与合规说明见 docs/vendor-qt-markdown-writer.md。
// 该测试同时是上游同步的回归防线：Qt 升级后若等价性破坏，在此暴露差异。
class TestQtMarkdownWriter : public QObject {
    Q_OBJECT

private:
    // 控制字符可视化，便于失败时对比
    static QString show(const QString &s)
    {
        QString r;
        for (const QChar &c : s) {
            if (c == '\n') r += "\\n";
            else if (c == '\r') r += "\\r";
            else if (c == u'\x2028') r += "\\u2028";
            else if (c == '\t') r += "\\t";
            else r += c;
        }
        return r;
    }

    static QString vendoredSerialize(const QTextDocument &doc)
    {
        QString md;
        QTextStream ts(&md);
        QtMarkdownWriter w(ts, QTextDocument::MarkdownDialectGitHub);
        w.writeAll(&doc);
        return md;
    }

    // 语料经 setMarkdown 构建 → 双侧序列化逐字节对比
    void checkEquiv(const QString &md)
    {
        QTextDocument doc;
        doc.setMarkdown(md);
        QCOMPARE(vendoredSerialize(doc), doc.toMarkdown());
    }

private slots:
    // 折行全路径：多重折行、CJK 稀疏空格（含强切吞字路径）、超长无空格、
    // 行首特殊字符、列表/引用内折行（缩进/前缀参与列位）
    void testEquivalenceFoldPaths() {
        checkEquiv("");
        checkEquiv("hello world");
        checkEquiv(QStringList({"word ", "lorem ", "ipsum ", "dolor ", "sit ", "amet "})
                       .join(QString()).repeated(6));
        checkEquiv(QString(30, QChar(0x6C49)) + " " + QString(100, QChar(0x5B57)));
        checkEquiv(QString("x0123456789").repeated(21));
        checkEquiv(QString(60, 'A') + " " + QString(200, 'B'));
        checkEquiv(QStringLiteral("filler ").repeated(12)
                   + "issue #5 bolts - like 2. items | pipes");
    }

    void testEquivalenceBlocks() {
        checkEquiv("# h1\n## h2\n### h3\n#### h4\n##### h5\n###### h6");
        checkEquiv("#" + QString(120, QLatin1Char('H')) + " tail");
        checkEquiv("- a\n- b\n  - c\n    - d");
        checkEquiv("5. five\n6. six");
        checkEquiv("1) one\n2) two");
        checkEquiv("- [x] done\n- [ ] todo");
        checkEquiv("- a\n\n- b\n");
        checkEquiv("- " + QStringLiteral("item ").repeated(30) + " tail");
        checkEquiv("- first second third");
        checkEquiv("- first\n\n  continuation paragraph\n");
        checkEquiv("> a\n>> b\n> > c");
        checkEquiv("> " + QStringLiteral("quote ").repeated(20) + " tail");
        checkEquiv("```cpp\nint x;\n```\n");
        checkEquiv("~~~\ncode\n~~~\n");
        checkEquiv("    indented code");
        checkEquiv("```\na\n\nb\n```");
        checkEquiv("> ```\n> quoted code\n> ```");
        checkEquiv("| a | b |\n|---|---|\n| 1 | 2 |");
        checkEquiv("| a | b |\n|---|---|\n| 1 | " + QString(100, QLatin1Char('c')) + " |");
        checkEquiv("a\n\n---\n\nb");
        checkEquiv("line1  \nline2\\\nline3");
    }

    void testEquivalenceInline() {
        checkEquiv("**bold** *it* ~~s~~ _u_ `code` **b *i* b**");
        checkEquiv("``a`b`` and `code`");
        checkEquiv("[t](url) [t2](url \"title with \\\"q\\\"\") <https://auto.link>");
        checkEquiv("![alt](img.png) ![alt2](img.png \"img title\")");
        checkEquiv("text \\* not em, <b>html</b>, a<b, 5<6, & amp");
    }

    // front matter：MarkdownDialectGitHub 特性位含 0x100000（kFrontMatterFeature
    // 常量替换路径），metaInformation 设置后两侧均应写出
    void testEquivalenceFrontMatter() {
        QTextDocument doc;
        doc.setMarkdown("body text");
        doc.setMetaInformation(QTextDocument::FrontMatter, "title: x\nauthor: y");
        QCOMPARE(vendoredSerialize(doc), doc.toMarkdown());
        QVERIFY(vendoredSerialize(doc).contains("---\ntitle: x"));
    }

    // 光标构造的富格式（setMarkdown 产不出的形态：等宽/字体属性切换/
    // 链接多行 title/图片 title 属性/折行落标记边界）
    void testEquivalenceCursorBuilt() {
        {
            QTextDocument doc;
            QTextCursor c(&doc);
            QTextCharFormat mono;
            QFont f("monospace");
            f.setStyleHint(QFont::TypeWriter);
            mono.setFont(f);
            mono.setFontFixedPitch(true);
            c.insertText("plain then ");
            c.insertText("fixed pitch fragment", mono);
            c.insertText(" and plain again");
            QCOMPARE(vendoredSerialize(doc), doc.toMarkdown());
        }
        {
            QTextDocument doc;
            QTextCursor c(&doc);
            QTextCharFormat bold;
            bold.setFontWeight(QFont::Bold);
            QTextCharFormat ital;
            ital.setFontItalic(true);
            QTextCharFormat und;
            und.setFontUnderline(true);
            QTextCharFormat strike;
            strike.setFontStrikeOut(true);
            c.insertText("mix ", bold);
            QTextCharFormat bi;
            bi.setFontWeight(QFont::Bold);
            bi.setFontItalic(true);
            c.insertText("bold-italic ", bi);
            c.insertText("under ", und);
            c.insertText("struck ", strike);
            c.insertText("normal");
            QCOMPARE(vendoredSerialize(doc), doc.toMarkdown());
        }
        {
            QTextDocument doc;
            QTextCursor c(&doc);
            QTextCharFormat link;
            link.setAnchor(true);
            link.setAnchorHref("https://example.com/path");
            link.setProperty(QTextFormat::TextToolTip,
                             "tip \"quoted\"\nsecond line\r\nthird");
            c.insertText("link text", link);
            c.insertText(" plain");
            QCOMPARE(vendoredSerialize(doc), doc.toMarkdown());
        }
        {
            QTextDocument doc;
            QTextCursor c(&doc);
            QTextImageFormat img;
            img.setName("pic.png");
            img.setProperty(QTextFormat::ImageAltText, "alt text");
            img.setProperty(QTextFormat::ImageTitle, "the title");
            c.insertImage(img);
            c.insertText(" after image");
            QCOMPARE(vendoredSerialize(doc), doc.toMarkdown());
        }
        {
            // 长段 + 中途格式切换，迫使折行落在标记边界（endingMarkers / wi<0 路径）
            QTextDocument doc;
            QTextCursor c(&doc);
            QTextCharFormat bold;
            bold.setFontWeight(QFont::Bold);
            c.insertText(QStringLiteral("lead ").repeated(12) + " ");
            c.insertText(QString(70, QChar(0x6F22)) + " ", bold);
            c.insertText(QStringLiteral("trail ").repeated(10));
            QCOMPARE(vendoredSerialize(doc), doc.toMarkdown());
        }
        {
            QTextDocument doc;
            doc.setMarkdown("para one para two");
            QCOMPARE(vendoredSerialize(doc), doc.toMarkdown());
        }
    }

    // 长度扫描：折行断点对 lead/tail 长度敏感，系统扫描覆盖
    // 空格断点 / 强切 / 标记边界三类路径（CJK / ASCII / 引用 × 12×11 组合）
    void testEquivalenceLengthSweep() {
        for (int lead = 20; lead <= 100; lead += 7) {
            for (int tail = 60; tail <= 200; tail += 13) {
                checkEquiv(QString(lead, QChar(0x6C49)) + " "
                           + QString(tail, QChar(0x5B57)));
                checkEquiv(QString(lead, QLatin1Char('a')) + " mid "
                           + QStringLiteral("wd ").repeated(tail / 3));
                checkEquiv("> " + QString(lead, QLatin1Char('q')) + " sp "
                           + QString(tail, QLatin1Char('r')));
            }
        }
    }
};

QTEST_MAIN(TestQtMarkdownWriter)
#include "test_qtmarkdownwriter.moc"
