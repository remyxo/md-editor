#include <QTest>
#include <QElapsedTimer>
#include <QRegularExpression>
#include <QFile>
#include "../src/core/MarkdownHandler.h"
#include "tiptap_serialize.h"   // simulateTipTapSerialization（TipTap 序列化近似）

// 构造含空格、长度远超 80 列的测试文本。QTextDocument::toMarkdown() 硬编码
// 80 列换行，只有含空格（词边界）的文本才会触发折行——纯中文无空格不会折。
static QString longSpacedText(int words)
{
    QString s;
    for (int i = 1; i <= words; ++i)
        s += QStringLiteral("单词%1 ").arg(i);
    return s;
}

// 模拟 TipTap 序列化的辅助见 tiptap_serialize.h（与 test_roundtrip_matrix 共用）

// ===== 占位符家族契约断言辅助（家族清单见 docs/placeholder-families.md §8）=====
// 泄漏断言：输出不得含任何 ZZ<前缀><n>ZZ 形态 token。本组用例输入均不含字面
// ZZ 文本，输出出现该形态即为本批家族占位符未还原（或误还原为字面）。
static const QRegularExpression contractTokenRe(QStringLiteral("ZZ[A-Z]+[0-9]+ZZ"));
static void assertNoPlaceholderToken(const QString &s, const char *ctx)
{
    QVERIFY2(!s.contains(contractTokenRe),
             qPrintable(QString("[%1] 占位符 token 泄漏: %2").arg(QLatin1String(ctx), s)));
}
// 哨兵断言：输出不得含 \x01 控制字符（CODE/EUT/EUI 三族暂存哨兵，见清单 §5）。
static void assertNoControlSentinel(const QString &s, const char *ctx)
{
    QVERIFY2(!s.contains(QChar(0x01)),
             qPrintable(QString("[%1] \\x01 哨兵泄漏: %2").arg(QLatin1String(ctx), s)));
}

// 端到端往返：MD → toHtml → [TipTap 序列化] → fromHtml
static QString e2eRoundTrip(const QString &md)
{
    return MarkdownHandler::fromHtml(
        simulateTipTapSerialization(MarkdownHandler::toHtml(md)));
}

// 提取 HTML 中全部 <pre>…</pre> 的内部文本（含属性形态 <pre …>），供
// 「某文本不得被吞进代码块」类断言使用。
static QStringList preContentsOf(const QString &html)
{
    QStringList out;
    qsizetype from = 0;
    const QString open = QStringLiteral("<pre");
    while (true) {
        const qsizetype s = html.indexOf(open, from);
        if (s < 0) break;
        const qsizetype gt = html.indexOf(QLatin1Char('>'), s);
        const qsizetype e = html.indexOf(QStringLiteral("</pre>"), gt);
        if (gt < 0 || e < 0) break;
        out << html.mid(gt + 1, e - gt - 1);
        from = e + 6;
    }
    return out;
}

class TestMarkdownHandler : public QObject {
    Q_OBJECT
private slots:
    void testDivBlockNotWrappedInParagraph() {
        // 独占一行的 <div>（块级）还原后不得被 <p> 包裹：
        // <p><div>…</div></p> 是非法嵌套，TipTap 解析时把块级 divBlock 提升出
        // <p>、空 <p> 壳留在原位，渲染为 div 前后各一条空行。
        QString md = "# 这是标题1,一般是文章的标题\n"
                     "\n"
                     "这是普通文本。\n"
                     "这是普通文本。\n"
                     "\n"
                     "<div align=\"right\">这是右对齐标题, 通过html标签实现</div>\n"
                     "\n"
                     "## 1. 这是标题2，一般表示章节";
        QString html = MarkdownHandler::toHtml(md);
        // div 必须原样保留（ZZDV 保护生效）
        QVERIFY2(html.contains("<div align=\"right\">这是右对齐标题, 通过html标签实现</div>"),
                 qPrintable("div 应原样保留，实际: " + html));
        // 不得出现 <p…><div 嵌套（含纯 <p> 形态）
        static const QRegularExpression pWrapDivRe(
            "<p\\b[^>]*>\\s*<div\\b");
        QVERIFY2(!pWrapDivRe.match(html).hasMatch(),
                 qPrintable("div 不应被 <p> 包裹，实际: " + html));
        // 完整往返：div 行精确还原
        QString back = MarkdownHandler::fromHtml(html);
        QVERIFY2(back.contains("<div align=\"right\">这是右对齐标题, 通过html标签实现</div>"),
                 qPrintable("往返应还原 div 行，实际: " + back));
        QVERIFY2(!back.contains("ZZDV"),
                 qPrintable("往返不应泄漏 ZZDV 占位符，实际: " + back));
    }

    void testStrikeSubscriptEdgeRoundTrip() {
        // 删除线内下标贴分隔符边缘时，~~…~~ 与 ~x~ 共用 ~ 字符产生歧义：
        // ~~H~2~~~ Qt setMarkdown 整体按字面量解析、~~~2~H~~ 甚至输出空段落
        // 内容全丢。修复：fromHtml 检测内层下标占位符贴删除线首/尾时保留字面
        // <s>…</s>，往返保真；中部下标仍走 ~~H~2~O~~ 纯 Markdown。
        struct Case { const char *html; bool wantHtml; };
        const Case cases[] = {
            // 下标贴边（末尾/开头/仅下标）→ 保留字面 HTML
            {"<p><s>H<sub>2</sub></s></p>", true},
            {"<p><s><sub>2</sub>H</s></p>", true},
            {"<p><s><sub>2</sub></s></p>", true},
            // 下标在删除线中部 → 仍转 Markdown（不误伤正常形式）
            {"<p><s>H<sub>2</sub>O</s></p>", false},
        };
        for (const Case &c : cases) {
            const QString md = MarkdownHandler::fromHtml(c.html).trimmed();
            if (c.wantHtml) {
                QVERIFY2(md.startsWith("<s>") && md.endsWith("</s>"),
                         qPrintable(QString("贴边下标应保留 <s> HTML，实际: %1").arg(md)));
            } else {
                QVERIFY2(md.startsWith("~~") && md.endsWith("~~"),
                         qPrintable(QString("中部下标应转 ~~…~~，实际: %1").arg(md)));
            }
            // 完整往返：MD → HTML 须还原出 <s> 与 <sub>，且不得残留歧义 ~~…~~ 字面量
            const QString back = MarkdownHandler::toHtml(md);
            QVERIFY2(back.contains("<s>") && back.contains("<sub>2</sub>"),
                     qPrintable(QString("往返应还原 <s>+<sub>，实际: %1").arg(back)));
            QVERIFY2(!back.contains("~~~"),
                     qPrintable(QString("往返不得残留歧义 ~~~，实际: %1").arg(back)));
        }
    }

    void testNestedStrikeBoldItalicMarkRoundTrip() {
        // 删除线 ⚡ 加粗/斜体/高亮 互嵌：~~ 与 **/*/== 无合法组合能表达嵌套，
        // Qt setMarkdown 会把 **粗~~删~~** 拆成两段、内层样式丢失。
        // 修复：toHtml 的 line-through span → <s> 时保留同 span 的加粗/斜体
        // （<span font-weight:700;line-through> → <strong><s>…</s></strong>）；
        // fromHtml 对 加粗/斜体 ⚡ 删除线 嵌套保留整层字面 HTML；
        // 高亮 ⚡ 删除线 走 ==<s>…</s>…==（toHtml ==→<mark> 正则可含 HTML 属性后还原）。
        struct Case {
            const char *html;         // TipTap 侧输入 HTML
            const char *mdContain;    // fromHtml 输出应包含的子串
            const char *htmlContain;  // toHtml 往返输出应包含的子串（语义保真）
            bool wantS;               // 删除线以 <s> 形态还原（否则为 line-through span）
        };
        const Case cases[] = {
            // 加粗>删除线
            {"<p><strong>粗<s>删</s></strong></p>",
             "<strong>粗<s>删</s></strong>", "<strong><s>删</s></strong>", true},
            // 删除线>加粗
            {"<p><s>删<strong>粗</strong></s></p>",
             "<s>删<strong>粗</strong></s>", "<strong><s>粗</s></strong>", true},
            // 斜体>删除线
            {"<p><em>斜<s>删</s></em></p>",
             "<em>斜<s>删</s></em>", "<em><s>删</s></em>", true},
            // 删除线>斜体
            {"<p><s>删<em>斜</em></s></p>",
             "<s>删<em>斜</em></s>", "<em><s>斜</s></em>", true},
            // 高亮>删除线（mark 备份内层 MD 等价标签换回 MD 语法：
            // ==~~删~~高== 重载后 mark 正则还原 <mark><s>删</s>高</mark>，4 轮稳定）
            {"<p><mark><s>删</s>高</mark></p>",
             "==~~删~~高==", "<mark><s>删</s>高</mark>", true},
            // 加粗 > line-through span（外部 HTML / 旧 toHtml 输出的删除线形态）
            {"<p><strong>粗<span style=\"text-decoration: line-through;\">删</span></strong></p>",
             "<span style=\"text-decoration: line-through;\">删</span>",
             "<strong>粗<span style=\"text-decoration: line-through;\">删</span></strong>", false},
        };
        for (const Case &c : cases) {
            const QString md = MarkdownHandler::fromHtml(c.html);
            QVERIFY2(md.contains(c.mdContain),
                     qPrintable(QString("fromHtml 应保留嵌套 %1，实际: %2")
                                .arg(c.mdContain, md)));
            const QString back = MarkdownHandler::toHtml(md);
            QVERIFY2(back.contains(c.htmlContain),
                     qPrintable(QString("toHtml 往返应还原 %1，实际: %2")
                                .arg(c.htmlContain, back)));
            // 语义保真：加粗/斜体/高亮 与 删除线 必须同时存在（不得丢内层样式）
            QVERIFY2(c.wantS ? back.contains("<s>")
                             : back.contains("line-through"),
                     qPrintable(QString("往返应保留删除线，实际: %1").arg(back)));
            QVERIFY2(!back.contains("ZZNS") && !back.contains("ZZST") && !back.contains("ZZMK"),
                     qPrintable(QString("往返不应泄漏占位符，实际: %1").arg(back)));
        }
    }

    void testNestedStrikeTipTapRoundTrip() {
        // TipTap 对相邻文本 run 序列化时，若后一 run 新增外层 mark，会闭合重开、
        // 把共享外层拆成兄弟元素（<s>X</s><strong><s>Y</s></strong> /
        // <s><mark>X</mark></s><mark>Y</mark>）。fromHtml 须按 ProseMirror 的
        // 逆操作合并回嵌套形态，使源码在 WYSIWYG 往返后保持原样（而非变成
        // ~~X~~<strong><s>Y</s></strong> / ~~==X==~~==Y== 之类的拆分形式）。
        struct Case { const char *tapHtml; const char *md; };
        const Case cases[] = {
            // 删除线外层、加粗内层（拆成兄弟）
            {"<p><s>删</s><strong><s>粗</s></strong></p>", "<s>删<strong>粗</strong></s>"},
            // 高亮外层、删除线内层（拆成兄弟；mark 备份换回 MD 语法 ~~）
            {"<p><s><mark>删</mark></s><mark>高</mark></p>", "==~~删~~高=="},
            // 反向次序（mark 备份换回 MD 语法 ~~）
            {"<p><mark>高</mark><s><mark>删</mark></s></p>", "==高~~删~~=="},
            // 三 run 链式：一次合并后暴露新的相邻可合并对
            {"<p><s>a</s><strong><s>b</s></strong><em><s>c</s></em></p>",
             "<s>a<strong>b</strong><em>c</em></s>"},
            // 已是规范形态（共享外层删除线内嵌高亮），不得误合并
            {"<p><s>H<mark>2</mark>O</s></p>", "~~H==2==O~~"},
        };
        for (const Case &c : cases) {
            const QString md = MarkdownHandler::fromHtml(c.tapHtml).trimmed();
            QVERIFY2(md == c.md,
                     qPrintable(QString("fromHtml 应合并还原 %1，实际: %2")
                                .arg(c.md, md)));
            // 完整往返（MD → HTML → 合并后源码）应稳定于同一源码
            const QString back = MarkdownHandler::fromHtml(
                MarkdownHandler::toHtml(md)).trimmed();
            QVERIFY2(back == c.md,
                     qPrintable(QString("WYSIWYG 往返应稳定于 %1，实际: %2")
                                .arg(c.md, back)));
        }
    }

    void testDeepNestedListRoundTrip() {
        // Qt toMarkdown 对 3 层以上嵌套列表的缩进不自洽（第三层 8 空格、第四层
        // 6 空格，且 8 空格无法被 setMarkdown 重解析），多次往返后深层子列表被
        // 拍平到父项文本中。修复：通用列表像任务列表一样自绘（子项缩进 = 父项
        // 内容列），保证深层嵌套往返稳定。
        struct Case { const char *md; };
        const Case cases[] = {
            // 无序>无序>有序（用户报告：二层嵌套有序列表被拍平）
            {"- 列表1\n- 列表2\n  - 嵌套列表2-1\n  - 嵌套列表2-2\n"
             "    1. 二层嵌套有序2-2-1\n    2. 二层嵌套有序2-2-2\n"},
            // 有序>有序>有序
            {"1. 一层\n  1. 二层\n    1. 三层\n    2. 三层2\n"},
            // 无序>有序>无序
            {"- 一层\n  1. 二层\n    1. 三层\n"},
            // 四层（无序>无序>有序>无序）
            {"- A\n  - B\n    1. C\n        - D\n"},
            // 任务列表深层嵌套（回归）
            {"- [ ] 任务1\n  - [ ] 任务2\n    - [ ] 任务3\n"},
        };
        for (const Case &c : cases) {
            const QString md1 = MarkdownHandler::fromHtml(
                MarkdownHandler::toHtml(c.md)).trimmed();
            const QString md2 = MarkdownHandler::fromHtml(
                MarkdownHandler::toHtml(md1)).trimmed();
            // 深层子列表不得被拍平到父项文本中
            QVERIFY2(!md1.contains("嵌套列表2-2 1.") && !md2.contains("嵌套列表2-2 1."),
                     qPrintable(QString("深层子列表不得拍平，往返1: %1\n往返2: %2")
                                .arg(md1, md2)));
            // 两次往返应稳定于同一缩进方案
            QVERIFY2(md1 == md2,
                     qPrintable(QString("深层嵌套列表应往返稳定，往返1:\n%1\n往返2:\n%2")
                                .arg(md1, md2)));
        }
    }

    void testNoFoldByteStableRoundTrip() {
        // 去折行回归（vendored QtMarkdownWriter 关闭 80 列折行，见
        // docs/vendor-qt-markdown-writer.md）：这些用例曾触发 QTextMarkdownWriter
        // 折行家族缺陷——强切吞字（稀疏空格二次窗口 i=j+1 吞断点字符，中文高发、
        // 数据不可恢复）、特定折行点下 strong 闭合与 CJK 间空格丢失、列表/引用
        // 续行拆块。折行关闭后首轮即字节保真。
        struct Case { const char *name; QString md; };
        const Case cases[] = {
            {"CJK稀疏空格强切", QString(30, QChar(0x6C49)) + " " + QString(100, QChar(0x5B57))},
            {"ASCII稀疏空格强切", QString(60, QLatin1Char('A')) + " " + QString(200, QLatin1Char('B'))},
            {"strong闭合空格", QStringLiteral("路径（如 **`==`**→… 与 **加粗** 混排。")},
            {"长列表项", "- " + QStringLiteral("item ").repeated(40) + "tail"},
            {"长引用块", "> " + QStringLiteral("quote ").repeated(30) + "tail"},
            {"管道字符不拆行", QStringLiteral("filler ").repeated(12) + "issue | pipe tail"},
        };
        for (const Case &c : cases) {
            const QString back = MarkdownHandler::fromHtml(
                MarkdownHandler::toHtml(c.md)).trimmed();
            QVERIFY2(back == c.md.trimmed(),
                     qPrintable(QString("去折行用例 %1 应首轮字节保真，实际:\n%2")
                                .arg(c.name, back)));
        }
    }

    void testLargeSoftBreakCountRestore() {
        // 回归：软换行占位符还原曾按占位符个数枚举正则交替项
        // （ZZBR0ZZ|ZZBR1ZZ|…），2 万+ 个软换行时 PCRE2 编译超限 → 正则非法 →
        // 还原空转 → Debug 构建 Q_ASSERT「占位符残留泄漏」abort（Release 则
        // ZZBR 字面泄漏进正文，破坏保真）。还原必须与占位符个数无关
        // （通用 token 正则 + 集合成员判断），3 万行连续正文 = 29999 个软换行
        QStringList lines;
        for (int i = 0; i < 30000; ++i)
            lines << QStringLiteral("第%1行正文内容").arg(i);
        QString html = MarkdownHandler::toHtml(lines.join('\n'));
        QVERIFY2(!html.contains(QStringLiteral("ZZBR")),
                 "大文档软换行占位符不得泄漏进输出");
        QCOMPARE(html.count(QStringLiteral("br data-brk")), 29999);
    }

    void testLargeSoftBreakUserLiteralPreserved() {
        // 用户字面 ZZBRnZZ 文本（collisionFreePlaceholder 生成时已跳号避让，
        // 序号 99999 不在生成集合）不得被通用 token 正则替换或吃掉两侧空格
        QStringList lines;
        for (int i = 0; i < 50; ++i)
            lines << QStringLiteral("line %1 text").arg(i);
        lines << QStringLiteral("前后空格 ZZBR99999ZZ 保留");
        QString html = MarkdownHandler::toHtml(lines.join('\n'));
        QVERIFY2(html.contains(QStringLiteral(" ZZBR99999ZZ ")),
                 qPrintable(QStringLiteral("用户字面 ZZBR 文本须原样保留（含两侧空格）:\n%1")
                                .arg(html.mid(html.indexOf(QStringLiteral("ZZBR99999")) - 20, 60))));
    }

    void testManyCodeBlocksRestore() {
        // 批量还原助手（batchRestorePlaceholders）同样不得按占位符个数枚举
        // 正则交替项：3000 个代码块的占位符枚举同样超 PCRE2 编译上限
        QStringList parts;
        for (int i = 0; i < 3000; ++i)
            parts << QStringLiteral("```cpp\nint x%1;\n```").arg(i);
        QString html = MarkdownHandler::toHtml(parts.join("\n\n"));
        QVERIFY2(!html.contains(QStringLiteral("ZZCB")),
                 "大量代码块占位符不得泄漏进输出");
        QCOMPARE(html.count(QStringLiteral("<pre")), 3000);
    }

    void testLargeSoftBreakRoundTrip() {
        // 500 行连续正文的完整往返（toHtml → fromHtml）字节保真：
        // fromHtml 侧还原共用同一批助手，大占位符数量下两方向都要正确
        QStringList lines;
        for (int i = 0; i < 500; ++i)
            lines << QStringLiteral("第%1行正文内容").arg(i);
        const QString md = lines.join('\n');
        const QString back = MarkdownHandler::fromHtml(
            MarkdownHandler::toHtml(md)).trimmed();
        QCOMPARE(back, md);
    }

    void testBasicConversion() {
        QString md = "# Hello\n\nWorld";
        QString html = MarkdownHandler::toHtml(md);
        QVERIFY(!html.isEmpty());
        QVERIFY(html.contains("Hello"));
        QVERIFY(html.contains("World"));
    }

    void testToBodyHtmlByteEqualsLegacyLoadFileExtraction() {
        // 等价断言（W4-3 上收）：toBodyHtml 收编 EditorTab::loadFile 的内联 body
        // 提取，本测试复刻该旧内联逻辑作为对照组（旧逻辑复刻，防上收走样），
        // 断言两路输出逐字节一致（QByteArray 比较，零行为变化铁闸）。
        auto legacyLoadFileExtract = [](const QString &md, bool extractFootnotes) {
            QString fullHtml = MarkdownHandler::toHtml(md, extractFootnotes);
            // ↓ 逐字复刻自 EditorTab::loadFile worker（旧内联提取）
            int bodyStart = fullHtml.indexOf("<body");
            int bodyEnd = fullHtml.indexOf("</body>");
            if (bodyStart >= 0 && bodyEnd > bodyStart) {
                bodyStart = fullHtml.indexOf('>', bodyStart) + 1;
                return fullHtml.mid(bodyStart, bodyEnd - bodyStart).trimmed();
            }
            return fullHtml;
        };

        // 双输入：guide（真实全特性文档）+ bench 最小档（确定性混合内容），
        // 外加定向边界（空串/脚注/围栏/表格/嵌套引用——覆盖 trim 与回退分支）
        QStringList inputs;
        {
            QFile gf(GUIDE_MD_PATH);
            if (!gf.open(QIODevice::ReadOnly | QIODevice::Text))
                QSKIP("找不到 markdown-guide.md");
            inputs << QString::fromUtf8(gf.readAll());
        }
        {
            QFile bf(BENCH_MD_PATH);
            if (!bf.open(QIODevice::ReadOnly | QIODevice::Text))
                QSKIP("找不到 bench-500lines.md");
            inputs << QString::fromUtf8(bf.readAll());
        }
        inputs << QString()
               << QStringLiteral("# 单段\n\n正文一行。")
               << QStringLiteral("```cpp\nint x;\n```\n\n[^1]: 脚注定义\n\n正文[^1]。")
               << QStringLiteral("| A | B |\n|---|---|\n| 1 | 2 |")
               << QStringLiteral("> 嵌套引用\n>\n> > 内层");

        for (int i = 0; i < inputs.size(); ++i) {
            for (int fn = 0; fn <= 1; ++fn) {
                const bool extractFootnotes = (fn == 1);
                const QString viaNew =
                    MarkdownHandler::toBodyHtml(inputs[i], extractFootnotes);
                const QString viaLegacy =
                    legacyLoadFileExtract(inputs[i], extractFootnotes);
                QVERIFY2(viaNew.toUtf8() == viaLegacy.toUtf8(),
                         qPrintable(QStringLiteral("输入 #%1（extractFootnotes=%2）"
                                                   "toBodyHtml 与旧内联提取不一致")
                                        .arg(i)
                                        .arg(extractFootnotes)));
            }
        }

        // 冒烟：产物确为去壳后的 body 内容（不再含文档级包装）
        const QString body = MarkdownHandler::toBodyHtml("# Hello\n\nWorld");
        QVERIFY(!body.contains("<body"));
        QVERIFY(!body.contains("<!DOCTYPE"));
        QVERIFY(body.contains("Hello"));
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

    void testBlockquoteInlineFormatRoundTrip() {
        // 块引用内的行内格式：fromHtml 的引用步骤曾先于行内转换运行，
        // inner.remove(<[^>]*>) 把引用内所有标签剥成纯文本——WYSIWYG 里对
        // 引用内文字加粗，每次保存即丢语义（> **加粗** → > 加粗）。
        // 修复：引用抽取后移至行内占位保护之后，Qt 侧标签族手动转 MD 语法。
        QString md = "> **加粗** *斜体* ~~删除~~ `代码` ==高亮==\n";
        QString html = MarkdownHandler::toHtml(md);
        QString md2 = MarkdownHandler::fromHtml(html);
        QVERIFY2(md2.contains("**加粗**"), qPrintable(md2));
        QVERIFY2(md2.contains("*斜体*"), qPrintable(md2));
        QVERIFY2(md2.contains("~~删除~~"), qPrintable(md2));
        QVERIFY2(md2.contains("`代码`"), qPrintable(md2));
        QVERIFY2(md2.contains("==高亮=="), qPrintable(md2));
        // 引用前缀仍在前
        QVERIFY2(md2.trimmed().startsWith("> "), qPrintable(md2));
        // 4 轮收敛（1 轮归一后稳定）
        QString cur = md2;
        for (int i = 0; i < 3; ++i)
            cur = MarkdownHandler::fromHtml(MarkdownHandler::toHtml(cur));
        QCOMPARE(cur, md2);
    }

    void testTildeFenceRoundTrip() {
        // 顶层 ~~~ 波浪围栏：曾只认 ``` 反引号围栏，~~~ 行落入 BRMARKER 段落逻辑，
        // 行尾被注入 <br data-brk>，fromHtml 剥离后代码行无分隔拼接、语言串打碎
        QString md = "~~~js\nvar a = 1;\nvar b = 2;\n~~~\n";
        QString html = MarkdownHandler::toHtml(md);
        QVERIFY2(html.contains("language-js"), qPrintable(html));
        QVERIFY2(html.contains("var a = 1;"), qPrintable(html));
        QVERIFY2(html.contains("var b = 2;"), qPrintable(html));
        QString md2 = MarkdownHandler::fromHtml(html);
        QVERIFY2(md2.contains("var a = 1;\nvar b = 2;"), qPrintable(md2));
        // 收敛（首往返归一为反引号围栏是预期，之后须稳定）
        QString cur = md2;
        for (int i = 0; i < 3; ++i)
            cur = MarkdownHandler::fromHtml(MarkdownHandler::toHtml(cur));
        QCOMPARE(cur, md2);
    }

    void testIndentedFenceRoundTrip() {
        // ≤3 空格缩进的围栏（CommonMark 合法）：曾与 ~~~ 同病
        QString md = "  ```cpp\nint x = 0;\n  ```\n";
        QString html = MarkdownHandler::toHtml(md);
        QVERIFY2(html.contains("language-cpp"), qPrintable(html));
        QVERIFY2(html.contains("int x = 0;"), qPrintable(html));
        QString md2 = MarkdownHandler::fromHtml(html);
        QVERIFY2(md2.contains("int x = 0;"), qPrintable(md2));
        QString cur = md2;
        for (int i = 0; i < 3; ++i)
            cur = MarkdownHandler::fromHtml(MarkdownHandler::toHtml(cur));
        QCOMPARE(cur, md2);
    }

    void testFenceMutualExclusion() {
        // 围栏互斥：``` 围栏内的 ~~~ 行是内容（同字符才可闭合），反之亦然。
        // 曾无 ~~~ 状态机，``` 内的 ~~~ 会错误开启波浪围栏吞掉后续正文
        QString md = "```\n~~~\ninner\n```\nafter\n";
        QString html = MarkdownHandler::toHtml(md);
        QVERIFY2(html.contains("~~~"), qPrintable(html));
        QVERIFY2(html.contains("inner"), qPrintable(html));
        QVERIFY2(html.contains("after"), qPrintable(html));
        QString md2 = MarkdownHandler::fromHtml(html);
        QVERIFY2(md2.contains("~~~"), qPrintable(md2));
        QVERIFY2(md2.contains("after"), qPrintable(md2));
    }

    void testTildeFenceNotClosedByBackticks() {
        // ~~~ 围栏内的 ``` 行是内容，直到同字符 ~~~ 才闭合
        QString md = "~~~\ncode ``` line\n~~~\ntail\n";
        QString html = MarkdownHandler::toHtml(md);
        QVERIFY2(html.contains("code ``` line"), qPrintable(html));
        QVERIFY2(html.contains("tail"), qPrintable(html));
    }

    void testMultiDigitOrderedListParagraphBoundary() {
        // isParagraphLine 曾只查 trimmed[1]=='.'：10. 及以上编号被判为正文行，
        // 段落后插入软换行标记（与单位编号行为分叉）。修复后多位与单位编号
        // 的「段落+列表」往返形态一致
        QString a = MarkdownHandler::fromHtml(
            MarkdownHandler::toHtml("段落行\n1. item\n"));
        // 归一化编号数字后比较结构（编号字面必然不同）
        auto norm = [](QString s) { s.remove(QRegularExpression("\\d+")); return s; };
        QString b = MarkdownHandler::fromHtml(
            MarkdownHandler::toHtml("段落行\n10. item\n"));
        QCOMPARE(norm(b), norm(a));
        QString c = MarkdownHandler::fromHtml(
            MarkdownHandler::toHtml("段落行\n999999999. item\n"));
        QCOMPARE(norm(c), norm(a));
        // 两位编号带右括号同样是列表
        QString d = MarkdownHandler::fromHtml(
            MarkdownHandler::toHtml("段落行\n10) item\n"));
        QCOMPARE(norm(d), norm(a));
        QVERIFY(MarkdownHandler::fromHtml(
            MarkdownHandler::toHtml("版本 1.5 发布\n")).contains("1.5"));
    }

    void testTableNormalizationSkipsCodeFences() {
        // fromHtml 步骤 6/6b 表格规范化曾无围栏感知：代码围栏内的 |a|b| / |-|-|
        // 被改写为 GFM 风格（违反代码块字节不可变），伪分隔行还会抢占真表格的
        // 对齐/合并行对位
        QString md = "```markdown\n|a|b|\n|-|-|\n|1|2|\n```\n\n|左|右|\n|---|---|\n|x|y|\n";
        QString html = MarkdownHandler::toHtml(md);
        QString md2 = MarkdownHandler::fromHtml(html);
        // 围栏内字节不变
        QVERIFY2(md2.contains("|a|b|"), qPrintable(md2));
        QVERIFY2(md2.contains("|-|-|"), qPrintable(md2));
        // 真表格单元格保留
        QVERIFY2(md2.contains("x") && md2.contains("y"), qPrintable(md2));
    }

    void testNestedSpanRoundTripBalanced() {
        // 嵌套 <span style>：两处提取曾用非贪婪 (.*?)</span>——匹配停在第一个
        // 闭合处，备份不平衡（<span a>外<span b>内</span>），尾部「尾</span>」
        // 残留为字面文本，结构逐轮退化。修复后平衡扫描整段进备份
        QString md = "<span style=\"color:red\">外<span style=\"background-color:yellow\">内</span>尾</span>\n";
        QString html = MarkdownHandler::toHtml(md);
        QString md2 = MarkdownHandler::fromHtml(html);
        QVERIFY2(md2.count("background-color:yellow") >= 1, qPrintable(md2));
        QVERIFY2(md2.count("</span>") == md.count("</span>"), qPrintable(md2));
        QVERIFY2(md2.count("<span") == md.count("<span"), qPrintable(md2));
        QVERIFY2(md2.contains("外") && md2.contains("内") && md2.contains("尾"), qPrintable(md2));
        // 收敛
        QString cur = md2;
        for (int i = 0; i < 3; ++i)
            cur = MarkdownHandler::fromHtml(MarkdownHandler::toHtml(cur));
        QCOMPARE(cur, md2);
    }

    void testBlockMathAfterMultipleBlankLines() {
        // 数学块前 ≥2 空行：ZZBL 把多余空行还原为 <p></p>，命中「清除紧邻 math div
        // 的空 <p>」清理正则——曾因正则仅 2 个捕获组却引用 \3（越界组号被 Qt 当
        // 字面量输出），div 开标签被替换成字面 "\3"，数学块损坏 + 垃圾文本注入。
        QString md = "a\n\n\n$$\nx\n$$\n";
        QString html = MarkdownHandler::toHtml(md);
        QVERIFY2(!html.contains("\\3"), qPrintable(html));
        QVERIFY(html.contains("data-type=\"block-math\""));
        QVERIFY(html.contains("data-latex=\"x\""));
        // 往返：数学块语义保留
        QString md2 = MarkdownHandler::fromHtml(html);
        QVERIFY2(md2.contains("$$"), qPrintable(md2));
        QVERIFY(md2.contains("x"));
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

    void testTableColumnAlignToHtml() {
        // GFM 列对齐 → toHtml：对齐应提升到 <td style="text-align:...">。
        // Qt toHtml 只在单元格内 <p align="..."> 上携带对齐，TipTap v3
        // TableCell 仅解析 td 自身的 style/align 属性，不提升则在加载时丢失。
        QString md = "| A | B | C |\n| --- | :-: | ---: |\n| 1 | 2 | 3 |\n";
        QString html = MarkdownHandler::toHtml(md);
        QVERIFY2(html.contains("text-align:center"),
                 qPrintable("居中列应提升为 td style，实际:\n" + html));
        QVERIFY2(html.contains("text-align:right"),
                 qPrintable("右对齐列应提升为 td style，实际:\n" + html));
        // 默认左对齐列不应被注入 style
        QVERIFY(!html.contains("text-align:left"));
    }

    void testTableColumnAlignFromHtml() {
        // TipTap getHTML 形式（td style）→ fromHtml：分隔行应还原对齐标记。
        // Qt toMarkdown 从不输出 :---: / ---:（恒 -），必须在 setHtml 前
        // 提取、步骤 6 注回（版本无关，不依赖 Qt 写出行为）。
        QString tipTapHtml =
            "<table><tbody><tr><th>A</th><th>B</th></tr>"
            "<tr><td style=\"text-align: center\">1</td>"
            "<td style=\"text-align: right\">2</td></tr></tbody></table>";
        QString md = MarkdownHandler::fromHtml(tipTapHtml);
        QVERIFY2(md.contains("| :---: |"),
                 qPrintable("居中列应还原 :-:，实际:\n" + md));
        QVERIFY2(md.contains("| ---: |"),
                 qPrintable("右对齐列应还原 ---:，实际:\n" + md));
    }

    void testTableCellParagraphAlignNoCorruption() {
        // 单元格内 <p style="text-align">（工具栏旧形式/手写 HTML）不应被
        // ZZA 对齐机制包成 <div align> 占位符破坏表格结构
        QString html =
            "<table><tbody><tr><th>A</th></tr>"
            "<tr><td><p style=\"text-align: center;\">x</p></td></tr></tbody></table>";
        QString md = MarkdownHandler::fromHtml(html);
        QVERIFY2(!md.contains("<div align"),
                 qPrintable("单元格内不应嵌入 <div align>，实际:\n" + md));
        QVERIFY2(md.contains("| x |"),
                 qPrintable("表格结构与内容应完好，实际:\n" + md));
    }

    void testTableColumnAlignFullCppRoundTrip() {
        // C++ 层全链路：toHtml 输出（已含 td style）直接喂 fromHtml，
        // 分隔行对齐标记应原样往返（不经过 TipTap 的下限验证）
        QString md1 = "| A | B |\n| :-: | ---: |\n| 1 | 2 |\n";
        QString html = MarkdownHandler::toHtml(md1);
        QString md2 = MarkdownHandler::fromHtml(html);
        QVERIFY2(md2.contains("| :---: |") && md2.contains("| ---: |"),
                 qPrintable("全链路对齐标记应保留，实际:\n" + md2));
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

    void testExplicitAlignedMathRoundTrip() {
        // 用户显式书写 \begin{aligned} 环境（含对齐列 &）应逐字保留，
        // 不得被 toHtml 双重包裹、也不得被 fromHtml 剥离为纯多行
        QString md1 = "$$\n\\begin{aligned}\na &= b \\\\\nc &= d\n\\end{aligned}\n$$\n";
        QString html = MarkdownHandler::toHtml(md1);
        // 不应双重包裹（仅一次 \begin{aligned}）
        QVERIFY2(html.count("\\begin{aligned}") == 1,
                 qPrintable("显式 aligned 不应双重包裹，实际: " + html));
        QString md2 = MarkdownHandler::fromHtml(html);
        QVERIFY2(md2.contains("\\begin{aligned}"), qPrintable("应保留 \\begin{aligned}，实际:\n" + md2));
        QVERIFY2(md2.contains("\\end{aligned}"), qPrintable("应保留 \\end{aligned}，实际:\n" + md2));
        QVERIFY2(md2.contains("a &= b"), qPrintable("应保留对齐列 &，实际:\n" + md2));
        QVERIFY2(md2.contains("c &= d"), qPrintable("应保留第二行，实际:\n" + md2));
    }

    void testExplicitMatrixMathRoundTrip() {
        // 用户显式书写 \begin{matrix} 环境应逐字保留（含 & 与 \\ 行分隔）
        QString md1 = "$$\n\\begin{matrix}\na & b \\\\\nc & d\n\\end{matrix}\n$$\n";
        QString html = MarkdownHandler::toHtml(md1);
        QString md2 = MarkdownHandler::fromHtml(html);
        QVERIFY2(md2.contains("\\begin{matrix}"), qPrintable("应保留 \\begin{matrix}，实际:\n" + md2));
        QVERIFY2(md2.contains("\\end{matrix}"), qPrintable("应保留 \\end{matrix}，实际:\n" + md2));
        QVERIFY2(md2.contains("a & b"), qPrintable("应保留 & 分隔，实际:\n" + md2));
        QVERIFY2(md2.contains("c & d"), qPrintable("应保留第二行，实际:\n" + md2));
    }

    void testExplicitCasesMathRoundTrip() {
        // \begin{cases} 环境（无 &）也应逐字保留，不得被当作自动包裹的 aligned 剥离
        QString md1 = "$$\n\\begin{cases}\nx \\\\\ny\n\\end{cases}\n$$\n";
        QString html = MarkdownHandler::toHtml(md1);
        QString md2 = MarkdownHandler::fromHtml(html);
        QVERIFY2(md2.contains("\\begin{cases}"), qPrintable("应保留 \\begin{cases}，实际:\n" + md2));
        QVERIFY2(md2.contains("\\end{cases}"), qPrintable("应保留 \\end{cases}，实际:\n" + md2));
        QVERIFY2(md2.contains("x") && md2.contains("y"),
                 qPrintable("应保留各行，实际:\n" + md2));
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

    void testUnclosedBlockMathLiteralRoundTrip() {
        // 回归：未闭合 $$ 块（开启后至文档结束无闭合定界符）。行扫描在开启行
        // 即把占位符推入 mathBlockPhs，而备份只在闭合 $$ 时入 mathBlocks——EOF
        // 未闭合使两列表错配一条，batchRestorePlaceholders 取 backups[i] 越界
        // 直接 abort。修复：EOF 收尾把占位符弹出并还原原文——解析器惯例
        // （未闭合定界符 = 字面量），按字面文本透传，不得静默丢弃用户内容。
        // 字面 needle：断言输出必须保留的原文片段（仅有开符的用例为 "$$"）
        const struct { const char *name; const char *md; const char *needle; bool hasMath; } cases[] = {
            // 最小复现（源码模式输入 $$ 后退出即触发）
            {"minimal", "正文\n\n$$\nE=mc^2\n", "E=mc^2", false},
            // 文档以未闭合 $$ 结尾且无尾换行
            {"no-trailing-newline", "$$\nE=mc^2", "E=mc^2", false},
            // 闭合块在前 + 未闭合块在后（确认前块不受收尾影响）
            {"closed-then-unclosed", "$$\na = b\n$$\n\n正文\n\n$$\nE=mc^2\n", "E=mc^2", true},
            // 仅有开符、无内容行
            {"opener-only", "$$\n", "$$", false},
        };
        for (const auto &c : cases) {
            const QString md = QString::fromUtf8(c.md);
            // 修复前：首条 toHtml 即越界 abort
            QString html = MarkdownHandler::toHtml(md);
            QVERIFY2(html.contains("$$"),
                     qPrintable(QString("[%1] HTML 应保留字面 $$，实际: %2")
                                    .arg(c.name, html)));
            QVERIFY2(html.contains(QString::fromUtf8(c.needle)),
                     qPrintable(QString("[%1] HTML 应保留原文 [%2]，实际: %3")
                                    .arg(c.name, c.needle, html)));
            QVERIFY2(!html.contains("ZZMB"),
                     qPrintable(QString("[%1] 占位符不得泄漏，实际: %2")
                                    .arg(c.name, html)));
            // 未闭合块不得渲染成公式块（闭合块对照在 hasMath 用例断言）
            if (!c.hasMath)
                QVERIFY2(!html.contains("block-math"),
                         qPrintable(QString("[%1] 未闭合 $$ 不应产出公式块，实际: %2")
                                        .arg(c.name, html)));
            // 往返保真：字面内容保留 + 收敛后 round2 ↔ round3 字节等价。
            // 字面 ^ 在正文中由序列化器转义为 \^（防上标误读的无损表示），
            // 比较 needle 前先还原转义形态
            QString md2 = MarkdownHandler::fromHtml(html);
            QString md2Unescaped = md2;
            md2Unescaped.replace(QStringLiteral("\\^"), QStringLiteral("^"));
            QVERIFY2(md2Unescaped.contains(QString::fromUtf8(c.needle)),
                     qPrintable(QString("[%1] 往返应保留原文 [%2]，实际: %3")
                                    .arg(c.name, c.needle, md2)));
            QString md3 = MarkdownHandler::fromHtml(MarkdownHandler::toHtml(md2));
            QVERIFY2(md3 == md2,
                     qPrintable(QString("[%1] 收敛后应字节稳定，round2=[%2] round3=[%3]")
                                    .arg(c.name, md2, md3)));
        }
        // 对照：闭合 $$ 既有行为不回归（渲染为公式块、内容入 data-latex）
        {
            QString html = MarkdownHandler::toHtml("$$\na = b\n$$\n");
            QVERIFY2(html.contains("block-math"),
                     qPrintable(QString("闭合 $$ 应渲染公式块，实际: %1").arg(html)));
            QVERIFY2(html.contains("a = b"),
                     qPrintable(QString("闭合 $$ 应保留内容，实际: %1").arg(html)));
        }
    }

    void testUnclosedCodeFenceNoCrash() {
        // 同模式审计：未闭合 ``` 围栏——codeBlockPhs 开启即入、codeBlocks 闭合
        // 才入，EOF 未闭合同样错配越界 abort（与未闭合 $$ 同根因）。修复：EOF
        // 按闭合补备份。CommonMark 惯例：围栏未闭合时内容到文档结束均为代码块，
        // 仍渲染为代码块（不按字面透传）；fromHtml 补写闭合围栏属归一化，收敛后
        // 字节稳定。
        const struct { const char *name; const char *md; const char *needle; } cases[] = {
            {"fence-with-lang", "```cpp\nint x = 1;\n", "int x = 1;"},
            {"fence-bare", "```\nplain line\n", "plain line"},
            {"fence-then-paragraph", "正文\n\n```js\nlet a = 2;\n", "let a = 2;"},
        };
        for (const auto &c : cases) {
            const QString md = QString::fromUtf8(c.md);
            QString html = MarkdownHandler::toHtml(md);   // 修复前 abort
            QVERIFY2(html.contains(QString::fromUtf8(c.needle)),
                     qPrintable(QString("[%1] HTML 应保留内容 [%2]，实际: %3")
                                    .arg(c.name, c.needle, html)));
            QVERIFY2(html.contains("<pre"),
                     qPrintable(QString("[%1] 未闭合围栏应渲染为代码块，实际: %2")
                                    .arg(c.name, html)));
            QVERIFY2(!html.contains("ZZCB"),
                     qPrintable(QString("[%1] 占位符不得泄漏，实际: %2")
                                    .arg(c.name, html)));
            QString md2 = MarkdownHandler::fromHtml(html);
            QVERIFY2(md2.contains(QString::fromUtf8(c.needle)),
                     qPrintable(QString("[%1] 往返应保留内容 [%2]，实际: %3")
                                    .arg(c.name, c.needle, md2)));
            QString md3 = MarkdownHandler::fromHtml(MarkdownHandler::toHtml(md2));
            QVERIFY2(md3 == md2,
                     qPrintable(QString("[%1] 收敛后应字节稳定，round2=[%2] round3=[%3]")
                                    .arg(c.name, md2, md3)));
        }
    }

    void testUnclosedBlockquoteCodeFenceNoDataLoss() {
        // 同模式审计：未闭合块引用内代码围栏 > ```——占位符与备份均在闭合时
        // 生成，两列表不错配、不崩溃，但 EOF 未闭合时内容被静默丢弃（整块
        // 无处还原，往返成空段落），违反往返保真红线。修复：EOF 补闭合，
        // 已收集内容按代码块还原。
        // 注：阶段 4（审查 P2-8）后围栏内容行统一带 > 前缀（严格引用形态，
        // 旧「内容与闭合围栏不带 > 前缀」的 lazy 形态正是围栏逃出引用的根源），
        // 未闭合路径与闭合路径输出形态一致。
        QString md = "> ```cpp\n> int x = 1;\n";
        QString html = MarkdownHandler::toHtml(md);
        QVERIFY2(html.contains("int x = 1;"),
                 qPrintable(QString("未闭合 > ``` 应保留内容，实际: %1").arg(html)));
        QVERIFY2(html.contains("<pre"),
                 qPrintable(QString("未闭合 > ``` 应渲染为代码块，实际: %1").arg(html)));
        QVERIFY2(!html.contains("ZZBQ"),
                 qPrintable(QString("占位符不得泄漏，实际: %1").arg(html)));
        QString md2 = MarkdownHandler::fromHtml(html);
        QVERIFY2(md2.contains("int x = 1;"),
                 qPrintable(QString("往返应保留内容，实际: %1").arg(md2)));
        // 锚定闭合路径的输出形态（闭合 > ``` 围栏往返与之逐字节一致）
        QVERIFY2(md2 == QStringLiteral("> ```cpp\n> int x = 1;\n> ```"),
                 qPrintable(QString("往返形态应与闭合路径一致，实际: %1").arg(md2)));
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

    void testStrikeRoundTrip() {
        // 删除线 ~~x~~ 往返：MD → HTML（<s>）→ MD，不得退化为字面 <span style>
        QString md1 = "~~删除线~~ 正文\n";
        QString html = MarkdownHandler::toHtml(md1);
        QVERIFY2(html.contains("<s>"), qPrintable("toHtml 应输出 <s>，实际: " + html));
        QVERIFY2(!html.contains("line-through"),
                 qPrintable("toHtml 不应残留 line-through span，实际: " + html));
        QString md2 = MarkdownHandler::fromHtml(html);
        QVERIFY2(md2.contains("~~删除线~~"),
                 qPrintable("fromHtml 应还原 ~~删除线~~，实际: " + md2));
    }

    void testStrikeSubscriptRoundTrip() {
        // 删除线内含下标 ~~H~2~O~~：往返后逐字保留（删除线 + 下标双语义不丢失）
        QString md1 = "~~H~2~O~~\n";
        QString html = MarkdownHandler::toHtml(md1);
        QVERIFY2(html.contains("<s>"), qPrintable("应输出 <s>，实际: " + html));
        QVERIFY2(html.contains("<sub>2</sub>"),
                 qPrintable("删除线内下标应转 <sub>2</sub>，实际: " + html));
        QString md2 = MarkdownHandler::fromHtml(html);
        QVERIFY2(md2.contains("~~H~2~O~~"),
                 qPrintable("应逐字还原 ~~H~2~O~~，实际: " + md2));
    }

    void testStrikeTipTapOutputRoundTrip() {
        // TipTap Strike mark 输出 <s>/<del> → fromHtml 还原 ~~...~~
        QString md = MarkdownHandler::fromHtml("<p><s>甲</s> <del>乙</del></p>");
        QVERIFY2(md.contains("~~甲~~"), qPrintable("应还原 <s> → ~~甲~~，实际: " + md));
        QVERIFY2(md.contains("~~乙~~"), qPrintable("应还原 <del> → ~~乙~~，实际: " + md));
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

    void testFootnoteDefBlockParagraphsToHtml() {
        // 空行 + 缩进 = 定义内新段落
        QString md = "正文[^1]。\n\n[^1]: 第一段\n\n    第二段\n";
        QString html = MarkdownHandler::toHtml(md);
        QVERIFY(html.contains("data-type=\"footnoteDef\""));
        QVERIFY(html.contains("footnote-content"));
        // 两个段落都在定义内（块级），且不再残留在正文：
        // 正文前缀（容器开标签之前）不含定义内容，容器区两段都在且顺序保留
        QVERIFY(html.contains("第一段"));
        QVERIFY(html.contains("第二段"));
        int contPos = html.indexOf("data-type=\"footnoteContainer\"");
        QVERIFY(contPos > 0);
        QVERIFY(!html.left(contPos).contains("第一段"));
        QVERIFY(!html.left(contPos).contains("第二段"));
        QVERIFY(html.mid(contPos).contains("第一段"));
        QVERIFY(html.mid(contPos).contains("第二段"));
        // 段落分隔恰一个空行：不触发嵌套管线 K≥2 空行守卫（否则定义内多出
        // 一个 <p></p> 空段落；空定义的 <p></p> 最小内容契约在
        // testFootnoteDefEmptyToHtml，与此处互不影响）
        QVERIFY(!html.contains("<p></p>"));
    }

    void testFootnoteDefBlockListToHtml() {
        QString md = "[^1]: 段落\n    - 甲\n    - 乙\n";
        QString html = MarkdownHandler::toHtml(md);
        QVERIFY(html.contains("<li"));
        QVERIFY(html.contains("甲"));
        // 列表在定义内：正文流不残留列表文本（processed 里定义行被移除）
        QVERIFY(!html.left(html.indexOf("data-type=\"footnoteContainer\"")).contains("- 甲"));
    }

    void testFootnoteDefLazyContinuationStaysInBody() {
        // 非缩进续行不归属定义（与 MVP 行为一致），保留在正文
        QString md = "[^1]: 定义内容\n紧随正文\n";
        QString html = MarkdownHandler::toHtml(md);
        QVERIFY(html.contains("紧随正文"));
        // 紧随正文须出现在定义区之外（正文区）
        QVERIFY(html.indexOf("紧随正文") < html.indexOf("footnoteDef"));
    }

    void testFootnoteDefEmptyToHtml() {
        QString md = "正文[^1]。\n\n[^1]:\n";
        QString html = MarkdownHandler::toHtml(md);
        QVERIFY(html.contains("data-type=\"footnoteDef\""));
        // 空定义给一个空段落（block+ schema 最小内容）
        QVERIFY(html.contains("<p></p>"));
    }

    void testFootnoteDefNestedPipelineMath() {
        // 定义内容经嵌套主管线：行内公式占位符机制生效（非裸 setMarkdown）
        QString md = "[^1]: 公式 $a+b$ 注\n";
        QString html = MarkdownHandler::toHtml(md);
        QVERIFY(html.contains("data-latex"));
    }

    void testFootnoteDefNestedDefLiteral() {
        // 嵌套定义行按字面文本（不二次提取）
        QString md = "[^1]: 外层\n    [^2]: 内层\n";
        QString html = MarkdownHandler::toHtml(md);
        QVERIFY(!html.contains("data-id=\"2\""));
        QVERIFY(html.count("data-type=\"footnoteDef\"") == 1);
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

    void testFootnoteDefBlockRoundTrip() {
        QString md = "正文[^1]。\n\n[^1]: 第一段\n\n    第二段\n";
        QString html = MarkdownHandler::toHtml(md);
        QString back = MarkdownHandler::fromHtml(html);
        QVERIFY2(back.contains("[^1]: 第一段"), qPrintable(back));
        QVERIFY2(back.contains("    第二段"), qPrintable(back));   // 续行缩进
        // 二次往返稳定
        QString back2 = MarkdownHandler::fromHtml(MarkdownHandler::toHtml(back));
        QCOMPARE(back2, back);
    }

    void testFootnoteDefBlockListRoundTrip() {
        QString md = "[^1]: 段落\n    - 甲\n    - 乙\n";
        QString back = MarkdownHandler::fromHtml(MarkdownHandler::toHtml(md));
        QVERIFY2(back.contains("    - 甲"), qPrintable(back));
        QVERIFY2(back.contains("    - 乙"), qPrintable(back));
        QString back2 = MarkdownHandler::fromHtml(MarkdownHandler::toHtml(back));
        QCOMPARE(back2, back);
    }

    void testFootnoteDefCodeBlockRoundTrip() {
        QString md = "[^1]: 说明\n        code line\n";
        // 8 空格 = 去一层后 4 空格缩进代码块
        QString back = MarkdownHandler::fromHtml(MarkdownHandler::toHtml(md));
        QVERIFY2(back.contains("code line"), qPrintable(back));
        QString back2 = MarkdownHandler::fromHtml(MarkdownHandler::toHtml(back));
        QCOMPARE(back2, back);
    }

    void testFootnoteDefEmptyRoundTrip() {
        QString md = "正文[^1]。\n\n[^1]:\n";
        QString back = MarkdownHandler::fromHtml(MarkdownHandler::toHtml(md));
        QVERIFY2(back.contains("[^1]:"), qPrintable(back));
        QString back2 = MarkdownHandler::fromHtml(MarkdownHandler::toHtml(back));
        QCOMPARE(back2, back);
    }

    void testFootnoteDefTaskListInDefRoundTrip() {
        // 定义内任务列表：嵌套 fromHtml 主管线的 taskList 占位保护对称生效
        QString md = "[^1]: 待办\n    - [ ] 事项\n";
        QString html = MarkdownHandler::toHtml(md);
        QVERIFY(html.contains("taskList"));
        QString back = MarkdownHandler::fromHtml(html);
        QVERIFY2(back.contains("- [ ] 事项"), qPrintable(back));
    }

    void testFootnoteDefSpanContentBackwardCompat() {
        // 旧内联形态（span.footnote-content，编辑器同会话旧 HTML/单测构造）仍可解析
        QString html = "<div data-type=\"footnoteContainer\">"
                       "<div data-type=\"footnoteDef\" data-id=\"1\">"
                       "<span class=\"footnote-marker\">1</span>"
                       "<span class=\"footnote-content\">旧内容</span></div></div>";
        QString back = MarkdownHandler::fromHtml(html);
        QVERIFY2(back.contains("[^1]: 旧内容"), qPrintable(back));
    }

    // ===== R1：脚注定义内表格往返（用户缺陷：保存重开表格丢失 + 末尾 pipe 乱码） =====
    // 根因双缺陷叠加：
    //   A）fromHtml 末段「表格规范化/合并行折叠」以 trimmed 基准整行重写，剥掉
    //      定义续行的 4 空格缩进 → 重开（toHtml）时定义在首个非缩进行处断裂，
    //      表格内容逃逸成正文 pipe 字面文本（用户看到的末尾乱码）；
    //   B）Qt toMarkdown 对空单元格列按列宽输出连字符，空列 0 宽 → 全空表格
    //      分隔行退化成 |||（与数据行无异）。toHtml 侧早有 ZZTC 空单元格占位
    //      防护，fromHtml 侧缺失（正文空表格往返同受影响，先于本批存在）。

    void testFootnoteDefEmptyTableRoundTrip() {
        // ① def 含全空表格（TipTap 真实形状：空单元格 = <td><p></p></td>，
        //    vitest 探针实测 insertTable 产物）：保存 MD 须带缩进续行 + 合法分隔行
        const QString html = QStringLiteral(
            "<p>正文<sup data-type=\"footnoteRef\" data-id=\"1\">1</sup></p>"
            "<div data-type=\"footnoteContainer\">"
            "<div data-type=\"footnoteDef\" data-id=\"1\">"
            "<span class=\"footnote-marker\">1</span>"
            "<div class=\"footnote-content\"><table><tbody>"
            "<tr><th colspan=\"1\" rowspan=\"1\"><p></p></th>"
            "<th colspan=\"1\" rowspan=\"1\"><p></p></th></tr>"
            "<tr><td colspan=\"1\" rowspan=\"1\"><p></p></td>"
            "<td colspan=\"1\" rowspan=\"1\"><p></p></td></tr>"
            "</tbody></table></div></div></div>");
        const QString md = MarkdownHandler::fromHtml(html);
        // 表头行接在定义行后（缩进续行 + 分隔行合法存在，空表格不再退化）
        QVERIFY2(md.contains("[^1]: |  |  |"), qPrintable(md));
        QVERIFY2(md.contains("\n    | --- | --- |"), qPrintable(md));
        QVERIFY2(md.contains("\n    |  |  |"), qPrintable(md));
        // 重开侧：表格恢复在定义区内（不逃逸正文）
        const QString html2 = MarkdownHandler::toHtml(md);
        const int defPos = html2.indexOf("data-type=\"footnoteDef\"");
        QVERIFY2(defPos > 0, qPrintable(html2));
        QVERIFY2(html2.mid(defPos).contains("<table"), qPrintable(html2));
        // 二次往返字节等价
        QCOMPARE(MarkdownHandler::fromHtml(html2), md);
    }

    void testFootnoteDefContentTableRoundTrip() {
        // ② def 含有内容表格：表格行重写保留缩进（根因 A），往返字节等价
        const QString html = QStringLiteral(
            "<p>正文<sup data-type=\"footnoteRef\" data-id=\"2\">2</sup></p>"
            "<div data-type=\"footnoteContainer\">"
            "<div data-type=\"footnoteDef\" data-id=\"2\">"
            "<span class=\"footnote-marker\">2</span>"
            "<div class=\"footnote-content\"><table><tbody>"
            "<tr><th colspan=\"1\" rowspan=\"1\"><p>表头0</p></th>"
            "<th colspan=\"1\" rowspan=\"1\"><p>表头1</p></th></tr>"
            "<tr><td colspan=\"1\" rowspan=\"1\"><p>数据0</p></td>"
            "<td colspan=\"1\" rowspan=\"1\"><p>数据1</p></td></tr>"
            "</tbody></table></div></div></div>");
        const QString md = MarkdownHandler::fromHtml(html);
        QVERIFY2(md.contains("[^2]: | 表头0 | 表头1 |"), qPrintable(md));
        QVERIFY2(md.contains("\n    | --- | --- |"), qPrintable(md));
        QVERIFY2(md.contains("\n    | 数据0 | 数据1 |"), qPrintable(md));
        // 二次往返字节等价
        const QString md2 = MarkdownHandler::fromHtml(MarkdownHandler::toHtml(md));
        QCOMPARE(md2, md);
    }

    void testBodyEmptyTableSeparatorPreserved() {
        // ③ 正文空表格：fromHtml 分隔行不退化（根因 B，先于本批存在）
        const QString html = QStringLiteral(
            "<p>正文</p><table><tbody>"
            "<tr><th colspan=\"1\" rowspan=\"1\"><p></p></th>"
            "<th colspan=\"1\" rowspan=\"1\"><p></p></th></tr>"
            "<tr><td colspan=\"1\" rowspan=\"1\"><p></p></td>"
            "<td colspan=\"1\" rowspan=\"1\"><p></p></td></tr>"
            "</tbody></table>");
        const QString md = MarkdownHandler::fromHtml(html);
        QVERIFY2(md.contains("| --- | --- |"), qPrintable(md));
        // 分隔行合法 → 重开恢复表格
        QVERIFY2(MarkdownHandler::toHtml(md).contains("<table"), qPrintable(md));
        QCOMPARE(MarkdownHandler::fromHtml(MarkdownHandler::toHtml(md)), md);
    }

    void testFootnoteDefTableNotStealBodyAligns() {
        // ④ 定义内表格不得抢占正文表格的列对齐：tableAligns 按文档序只对应正文
        //    表格（定义内容在嵌套管线已自行处理），外层分隔行对齐消费须以
        //    「第 0 列（无前导空白）行才参与」判别。定义块在前 + 正文居中表格
        //    （同列数 2）在后，无判别时定义分隔行会先消费走居中对齐。
        const QString html = QStringLiteral(
            "<div data-type=\"footnoteContainer\">"
            "<div data-type=\"footnoteDef\" data-id=\"3\">"
            "<span class=\"footnote-marker\">3</span>"
            "<div class=\"footnote-content\"><table><tbody>"
            "<tr><th colspan=\"1\" rowspan=\"1\"><p>定头0</p></th>"
            "<th colspan=\"1\" rowspan=\"1\"><p>定头1</p></th></tr>"
            "<tr><td colspan=\"1\" rowspan=\"1\"><p>定值0</p></td>"
            "<td colspan=\"1\" rowspan=\"1\"><p>定值1</p></td></tr>"
            "</tbody></table></div></div></div>"
            "<p>正文</p><table><tbody>"
            "<tr><th colspan=\"1\" rowspan=\"1\" style=\"text-align: center;\">"
            "<p>正头0</p></th>"
            "<th colspan=\"1\" rowspan=\"1\" style=\"text-align: center;\">"
            "<p>正头1</p></th></tr>"
            "<tr><td colspan=\"1\" rowspan=\"1\" style=\"text-align: center;\">"
            "<p>正值0</p></td>"
            "<td colspan=\"1\" rowspan=\"1\" style=\"text-align: center;\">"
            "<p>正值1</p></td></tr>"
            "</tbody></table>");
        const QString md = MarkdownHandler::fromHtml(html);
        // 正文表格的对齐保留在其自己的分隔行上（表头行紧邻判别）
        QVERIFY2(md.contains("正头1 |\n| :---: | :---: |"), qPrintable(md));
        // 定义内分隔行不带对齐标记（无对齐信息，不得抢注）
        QVERIFY2(!md.contains("    | :---:"), qPrintable(md));
        // 二次往返字节等价：toHtml 会把脚注容器规范化到文末（构造时容器在
        // 前属非规范首态），首轮即收敛为规范形态，此后逐字节稳定
        const QString md2 = MarkdownHandler::fromHtml(MarkdownHandler::toHtml(md));
        QCOMPARE(MarkdownHandler::fromHtml(MarkdownHandler::toHtml(md2)), md2);
        // 收敛后对齐与缩进契约仍成立
        QVERIFY2(md2.contains("正头1 |\n| :---: | :---: |"), qPrintable(md2));
        QVERIFY2(md2.contains("\n    | --- | --- |"), qPrintable(md2));
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

    void testTaskItemNestedGeneralListRoundTrip() {
        // 任务条目内嵌通用列表（TipTap taskItem nested:true 的 content 为
        // 'paragraph block*'，可容纳 <ol>/<ul>）。修复前：1. 行被拍平成条目
        // 尾部文本并转义为 "1\."，且被空行与前一任务割裂（用户报告）。
        struct Case { const char *md; bool expectOl; };
        const Case cases[] = {
            // 用户报告：任务项尾随普通有序列表（缩进 4 空格）
            {"- [ ] 任务1\n- [x] 任务2\n  - [ ] 任务2-1\n  - [x] 任务2-2\n"
             "    1. 已完成XXX工作，合格率100%\n"
             "    2. 已完成YYY工作，成功率99.9%，符合预期\n", true},
            // 任务项内嵌无序列表
            {"- [x] 顶层\n  - 子项A\n  - 子项B\n", false},
            // 任务项内嵌通用 ul，其 li 再嵌通用 ol（用户报告：往返不失真但
            // 渲染把三级列表首项拼到二级项同行——CSS 修复见 editor.css）
            {"- [ ] 任务1\n- [x] 任务2\n  - 列表2-1\n  - 列表2-2\n"
             "    1. 已完成XXX工作，合格率100%\n"
             "    2. 已完成YYY工作，成功率99.9%，符合预期\n", true},
            // 嵌套任务列表中某条目内嵌有序列表（更深的通用列表）
            {"- [x] 父\n  - [x] 子\n    1. 子项1\n    2. 子项2\n", true},
            // 通用列表后再接同级任务条目（嵌套与兄弟混排）
            {"- [x] A\n  - [x] A-1\n    1. sub1\n  - [x] A-2\n", true},
        };
        for (const Case &c : cases) {
            const QString html1 = MarkdownHandler::toHtml(c.md);
            const QString md1 = MarkdownHandler::fromHtml(html1).trimmed();
            const QString md2 = MarkdownHandler::fromHtml(
                MarkdownHandler::toHtml(md1)).trimmed();
            // 关键：通用列表不得被拍平/转义进任务条目文本
            QVERIFY2(!md1.contains("1\\.") && !md1.contains("sub1 2."),
                     qPrintable(QString("任务项内通用列表不得拍平，往返1:\n%1\n\n往返2:\n%2")
                                .arg(md1, md2)));
            QVERIFY2(!md2.contains("1\\.") && !md2.contains("sub1 2."),
                     qPrintable(QString("任务项内通用列表不得拍平，往返1:\n%1\n\n往返2:\n%2")
                                .arg(md1, md2)));
            // 两次往返应稳定
            QVERIFY2(md1 == md2,
                     qPrintable(QString("任务项内通用列表应往返稳定，往返1:\n%1\n\n往返2:\n%2")
                                .arg(md1, md2)));
            // toHtml 应产出嵌套通用列表于任务条目 div 内
            // （taskList 渲染为 <ul data-type=...>，通用列表为 <ul><li> / <ol><li>）
            const bool hasNested = c.expectOl
                ? html1.contains("<ol><li>")
                : html1.contains("<ul><li>");
            QVERIFY2(hasNested,
                     qPrintable("任务项内应产出嵌套通用列表，实际: " + html1));
        }
    }

    void testGeneralListNestedTaskListRoundTrip() {
        // 通用列表内嵌任务列表（用户报告）：修复前任务行被提升到顶层、
        // 且通用列表下多一个空行。期望：任务列表保持嵌套、无额外空行。
        const QString md1 =
            "1. 有序列表1\n2. 有序列表2\n  - [ ] 任务2-1\n  - [x] 任务2-2\n"
            "    - 已完成XXX工作，合格率100%\n"
            "    - 已完成YYY工作，成功率99.9%，符合预期\n";
        const QString html1 = MarkdownHandler::toHtml(md1);
        // toHtml 应把任务列表嵌进通用列表条目的 <li> 内
        QVERIFY2(html1.contains("有序列表2</p><ul data-type=\"taskList\">"),
                 qPrintable("任务列表应嵌套在通用列表条目内，实际: " + html1));
        const QString md2 = MarkdownHandler::fromHtml(html1).trimmed();
        QCOMPARE(md2, md1.trimmed());
        const QString md3 = MarkdownHandler::fromHtml(
            MarkdownHandler::toHtml(md2)).trimmed();
        QCOMPARE(md3, md2);   // 多次往返稳定
    }

    void testNestedListRoundTrip() {
        // 普通嵌套列表（非任务列表）：2 空格缩进往返逐字一致（CommonMark 规范缩进）。
        // 4 空格等非规范缩进会被归一化为 2 空格（语义结构保留，属预期行为）。
        const QString md1 = QStringLiteral("- 一级\n  - 二级\n    - 三级");
        const QString md2 = MarkdownHandler::fromHtml(MarkdownHandler::toHtml(md1));
        QCOMPARE(md2, md1);

        const QString md3 = QStringLiteral("1. 一\n   1. 子项");
        const QString md4 = MarkdownHandler::fromHtml(MarkdownHandler::toHtml(md3));
        // 有序列表项 Qt 输出 "1.  一"（序号后两空格对齐），故仅断言语义保留
        QVERIFY2(md4.contains("一") && md4.contains("子项"),
                 qPrintable("嵌套有序列表语义应保留，实际: " + md4));
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
        // 语言信息串均保留，块引用结构完整。阶段 4（审查 P2-8）后内容行统一
        // 带 > 前缀（严格引用形态；旧 lazy 形态不带前缀，正是围栏重开即逃出
        // 引用的根源）。文本段行尾的一个空格是 Qt 序列化痕迹，第二轮往返
        // 自愈（已在 /tmp 探针验证 round2 == round3）。
        struct { const char *in; const char *expect; } cases[] = {
            {"> 前\n> ```\n> int x;\n> ```\n> 后\n", "> 前 \n>\n> ```\n> int x;\n> ```\n>\n> 后"},
            {"> ```cpp\n> int x;\n> ```\n", "> ```cpp\n> int x;\n> ```"},
            {"> 前\n> ```\n> line1\n> line2\n> ```\n> 后\n", "> 前 \n>\n> ```\n> line1\n> line2\n> ```\n>\n> 后"},
            {"> ```\n> int x;\n> ```\n", "> ```\n> int x;\n> ```"},
            // 代码块后空行分隔的第二个块引用/正文：空行必须保留，不得合并进同一块引用
            {"> 前\n> ```\n> code\n> ```\n\n> 后\n", "> 前 \n>\n> ```\n> code\n> ```\n\n> 后"},
            {"> ```\n> code\n> ```\n\n正文\n", "> ```\n> code\n> ```\n\n正文"},
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

    void testBlockquoteNestingRoundTrip()
    {
        // 阶段 4（审查 P2-8）：引用内列表 / 二级引用 / 引用内 hr / 引用内围栏
        // 的结构保真。Qt setMarkdown 会把列表/hr 弹出引用、二级引用展平成双倍
        // 缩进段落（信息在 Qt 层已丢）；修复后 toHtml 预抽取 + 递归转换、
        // fromHtml 递归还原 > 前缀，全部字节往返，且第二轮稳定（不动点）。
        // 真实 TipTap 链路（jsdom）已另行验证 <blockquote> 内含 ul/嵌套 bq/
        // pre/hr 均可解析并保持结构（/tmp 探针，2026-09-13）。
        struct { const char *in; const char *expect; } cases[] = {
            {"> > a", "> > a"},                                   // 二级引用
            {"> - x\n> - y", "> - x\n> - y"},                     // 引用内无序列表
            {"> a\n>\n> ---", "> a\n>\n> ---"},                   // 引用内 hr
            {"> ```\n> fenced\n> ```", "> ```\n> fenced\n> ```"}, // 引用内围栏
            {"> 1. one\n> 2. two", "> 1. one\n> 2. two"},         // 引用内有序列表
        };
        for (const auto &c : cases) {
            const QString md1 = QString::fromUtf8(c.in);
            const QString html = MarkdownHandler::toHtml(md1);
            int bsi = html.indexOf("<body");
            int bei = html.indexOf("</body>");
            bsi = html.indexOf('>', bsi) + 1;
            const QString body = html.mid(bsi, bei - bsi).trimmed();
            const QString md2 = MarkdownHandler::fromHtml(body);
            QVERIFY2(md2 == QString::fromUtf8(c.expect),
                     qPrintable(QString("块引用嵌套应字节往返，输入[%1] 实际[%2]")
                                    .arg(c.in, md2)));
            const QString md3 = MarkdownHandler::fromHtml(MarkdownHandler::toHtml(md2));
            QVERIFY2(md3 == md2,
                     qPrintable(QString("块引用嵌套应为不动点，输入[%1] md2[%2] md3[%3]")
                                    .arg(c.in, md2, md3)));
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
        // 正文\n===（无空行）是 setext 一级标题，规范化为 ATX（# 标题），且无
        // 幽灵 <br data-brk>（历史 bug：曾被误判为正文行插入幽灵软换行）。
        // 正文\n--- 的语义 2026-08-29 反转：按「编辑区语义优先」处理为水平线
        //（用户写 --- 意图几乎总是分隔线；setext 二级标题可用 ## 书写），
        // 对应用例移入 testHrAfterParagraphRoundTrip。
        struct { const char *in; const char *expect; } cases[] = {
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

    void testHrAfterParagraphRoundTrip()
    {
        // 正文\n---（无空行）：CommonMark 视为 setext 二级标题下划线，本编辑器
        // 按「编辑区语义优先」处理为水平线——占位前补空行隔断，往返语义保真
        //（分隔线不丢，代价是格式规范化为空行分隔）。--- 与 <hr> 标签等价
        //（帮助文档约定），<hr> 同走水平线路径；不占位会被 escapeUnclosedHtmlTags
        // 当未闭合容器转义为字面文本（往返变 \<hr\>）。占位符是文本段落，
        // 前后紧跟正文都会被并为同段（<hr/> 与正文同 <p>，非法嵌套），两侧隔断。
        struct { const char *in; const char *expect; } cases[] = {
            {"3\n---\n4", "3\n\n---\n\n4"},
            {"标题\n---\n", "标题\n\n---"},
            {"x\n\n---\ny", "x\n\n---\n\ny"},
            {"<hr>", "---"},
            {"<hr/>", "---"},
            {"<HR>", "---"},          // 大小写不敏感
            {"a\n\n<hr>\n\nb", "a\n\n---\n\nb"},
            {"a\n<hr>\nb", "a\n\n---\n\nb"},
        };
        for (const auto &c : cases) {
            QString md1 = QString::fromUtf8(c.in);
            QString html = MarkdownHandler::toHtml(md1);
            QVERIFY2(!html.contains("\\<hr\\>"),
                     qPrintable(QString("<hr> 不应被转义为文本，输入[%1] HTML[%2]")
                                    .arg(c.in, html)));
            QString md2 = MarkdownHandler::fromHtml(html);
            QVERIFY2(md2 == QString::fromUtf8(c.expect),
                     qPrintable(QString("水平线语义往返，输入[%1] 实际[%2]")
                                    .arg(c.in, md2)));
        }
        // <hr/> 与正文同 <p> 的非法嵌套不得出现（hr 必须独立成块）
        static const QRegularExpression hrInP1("<p\\b[^>]*>[^<]*<hr\\b");
        static const QRegularExpression hrInP2("<hr\\b[^>]*>[^<]*</p>");
        for (const char *in : {"3\n---\n4", "x\n\n---\ny", "a\n<hr>\nb"}) {
            QString h = MarkdownHandler::toHtml(QString::fromUtf8(in));
            QVERIFY2(!hrInP1.match(h).hasMatch() && !hrInP2.match(h).hasMatch(),
                     qPrintable(QString("<hr/> 不得嵌于 <p> 内，输入[%1] HTML[%2]")
                                    .arg(QString::fromUtf8(in), h)));
        }
        // 行内混排 <hr>（text <hr> more）不属水平线行，维持现状：按字面文本转义
        QString inlineMd2 = MarkdownHandler::fromHtml(
            MarkdownHandler::toHtml("text <hr> more"));
        QVERIFY2(inlineMd2.contains("\\<hr\\>"),
                 qPrintable(QString("行内 <hr> 应保持文本转义，实际[%1]").arg(inlineMd2)));
    }

    void testTableSingleCellMergeRoundTrip()
    {
        // 合并单元格规则（问题4）：
        // ① 单格行（| 复杂嵌套 |）→ 占满整行（<td colspan="4">），WYSIWYG 合并；往返保真为 | x |。
        // ② 多格行（| 复杂嵌套 | | | |）→ 不合并（4 个独立单元格，首格内容其余空）；往返保真为 4 格。
        // ③ 短行（| x | 乙 |）→ 不合并（补满 4 格，尾两格空）。
        struct { const char *in; const char *htmlNeedle; const char *htmlAbsent; const char *mdRoundTripNeedle; } cases[] = {
            // ① 单格行合并
            {"| 列A | 列B | 列C | 列D |\n| --- | --- | --- | --- |\n| 复杂嵌套 |",
             "colspan=\"4\"", nullptr, "| 复杂嵌套 |"},
            // ② 多格行（首格内容 + 3 空）不合并
            {"| 列A | 列B | 列C | 列D |\n| --- | --- | --- | --- |\n| 复杂嵌套 | | | |",
             nullptr, "colspan=\"4\"", "| 复杂嵌套 |  |  |  |"},
            // ③ 短行（2 格）不合并，补满 4 格
            {"| 列A | 列B | 列C | 列D |\n| --- | --- | --- | --- |\n| 复杂嵌套 | 乙 |",
             nullptr, "colspan=\"3\"", "| 复杂嵌套 | 乙 |  |  |"},
        };
        for (const auto &c : cases) {
            QString md1 = QString::fromUtf8(c.in);
            QString html = MarkdownHandler::toHtml(md1);
            if (c.htmlNeedle)
                QVERIFY2(html.contains(c.htmlNeedle),
                         qPrintable(QString("toHtml 应含 [%1]，实际:\n%2").arg(c.htmlNeedle, html)));
            if (c.htmlAbsent)
                QVERIFY2(!html.contains(c.htmlAbsent),
                         qPrintable(QString("toHtml 不应含 [%1]（不得合并），实际:\n%2").arg(c.htmlAbsent, html)));
            QString md2 = MarkdownHandler::fromHtml(html);
            QVERIFY2(md2.contains(c.mdRoundTripNeedle),
                     qPrintable(QString("往返应含 [%1]，输入[%2] 实际[%3]")
                                    .arg(c.mdRoundTripNeedle, c.in, md2)));
        }
        // ① 整表精确往返（合并行保持单格行）
        QString md = QStringLiteral(
            "| 列A | 列B | 列C | 列D |\n| --- | --- | --- | --- |\n| 复杂嵌套 |");
        QString roundTrip = MarkdownHandler::fromHtml(MarkdownHandler::toHtml(md));
        QCOMPARE(roundTrip, md);
    }

    void testTableMergeStrayPipeLine()
    {
        // 回归：正文中的孤立 | x | 行（无分隔行，非表格）不得被 6b 误当成表格 run，
        // 否则后续真表格的 tableIdx 错位、合并行无法折叠回单格。
        // 用 4 列表格（| x | 单格行 → colspan=4，可折叠回单格；2 列是 Qt 怪癖不测）。
        const QString md1 = QStringLiteral(
            "| 列A | 列B | 列C | 列D |\n| --- | --- | --- | --- |\n| 复杂嵌套 |\n\n"
            "| 这是段落里的竖线行 |\n\n"
            "| 列E | 列F | 列G | 列H |\n| --- | --- | --- | --- |\n| 复杂嵌套 |");
        const QString md2 = MarkdownHandler::fromHtml(MarkdownHandler::toHtml(md1));
        // 两个表格的单格行都应保持单格（合并往返保真）
        QCOMPARE(md2.count("| 复杂嵌套 |"), 2);
        QVERIFY2(!md2.contains("| 复杂嵌套 |  |"),
                 qPrintable("合并行应折叠回单格，实际:\n" + md2));
        // 孤立的竖线行保留
        QVERIFY2(md2.contains("| 这是段落里的竖线行 |"),
                 qPrintable("孤立竖线行应保留，实际:\n" + md2));
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

    void testIndentedListFenceClaimed()
    {
        // 坑 4（读写不对称）：fromHtml 对列表项内代码块按「列表内容缩进」输出
        // 围栏（≥4sp 时超出 isTopLevelFenceLine 的 ≤3 认领域）。修复前 toHtml
        // 不认领该围栏，其闭合围栏（col-0）被误判为新块开栏，吞掉区间内全部
        // 内容（整节尾部成无语言 <pre> 字面量——源码模式导出 Consolas 量化）。
        // 修复后：扫描器按列表内容缩进基线认领缩进围栏，配对收集为 ZZCB。
        const struct { const char *name; QString md; const char *lang; } cases[] = {
            // 二级无序项内代码块（内容列 4 → 围栏 4sp；闭合 col-0 懒形态）
            {"L2无序4sp", QStringLiteral(
                 "- 一级\n  - 二级\n    ```cpp\nint x;\n```\n  - 二级续"),
             "cpp"},
            // 三级有序项内代码块（内容列 8 → 围栏 8sp，w2-alpha 实测形态）
            {"L3有序8sp", QStringLiteral(
                 "1. 一级\n   - 二级\n     1. 三级\n        ```cpp\nint x;\n```\n"
                 "        - 四级\n"),
             "cpp"},
            // 两位数有序项（"10. " 内容列 4 → 围栏 4sp）
            {"两位数有序4sp", QStringLiteral(
                 "9. 九\n10. 十\n    ```js\nint x;\n```\n11. 十一"),
             "js"},
        };
        for (const auto &c : cases) {
            const QString html = MarkdownHandler::toHtml(c.md);
            const QString langClass =
                QStringLiteral("class=\"language-") + c.lang + QLatin1String("\"");
            QVERIFY2(html.contains(langClass),
                     qPrintable(QString("%1：应认领缩进围栏并保留语言，HTML=[%2]")
                                    .arg(c.name, html)));
            QVERIFY2(html.contains(QLatin1String("int x;")) || html.contains(QLatin1String("x\n")),
                     qPrintable(QString("%1：代码内容应保留，HTML=[%2]").arg(c.name, html)));
            // 围栏后的列表续行不得被吞进代码块（修复前的爆发形态）
            const bool tailInList = html.contains(QStringLiteral("四级"))
                                    || html.contains(QStringLiteral("二级续"))
                                    || html.contains(QStringLiteral("十一"));
            QVERIFY2(tailInList,
                     qPrintable(QString("%1：围栏后列表行应存活于 HTML，HTML=[%2]")
                                    .arg(c.name, html)));
            for (const QString &pre : preContentsOf(html)) {
                QVERIFY2(!pre.contains(QStringLiteral("二级续"))
                             && !pre.contains(QStringLiteral("四级"))
                             && !pre.contains(QStringLiteral("十一")),
                         qPrintable(QString("%1：列表行被吞进代码块，pre=[%2]")
                                        .arg(c.name, pre)));
            }
            // 往返内容存活 + 无占位符泄漏（字节级两趟收敛不在本断言：粘连 <pre>
            // 的列表项需经真实 TipTap DOM 重构（pre 提升为项内块级 codeBlock）
            // 才能序列化回行首缩进围栏，sim 只近似属性转义不建 DOM——收敛性由
            // commonmark/gfm 语料棘轮与 headless 探针（真实链路）把关）
            const QString r1 = e2eRoundTrip(c.md);
            QVERIFY2(!r1.contains(QLatin1String("ZZCB")),
                     qPrintable(QString("%1：占位符泄漏，MD=[%2]").arg(c.name, r1)));
            QVERIFY2(r1.contains(QLatin1String("int x;")),
                     qPrintable(QString("%1：往返代码内容应保留，MD=[%2]").arg(c.name, r1)));
            QVERIFY2(r1.contains(QStringLiteral("二级续"))
                         || r1.contains(QStringLiteral("四级"))
                         || r1.contains(QStringLiteral("十一")),
                     qPrintable(QString("%1：往返列表续行应保留，MD=[%2]").arg(c.name, r1)));
        }
    }

    void testIndentedFencePairingMatrix()
    {
        // 缩进围栏配对组合穷举表（w2-alpha 第五节骨架）：
        //   开栏缩进 I（4/6/8）× 闭栏缩进 C（0/I/中间档，C ≤ I 合法闭合）
        //   × 围栏内容嵌缩进行（col-0 / 与开栏同缩进 / 更深缩进 / 列表标记形）
        //   × info string 仅开栏可有（带尾文本的围栏行不是闭合，是内容）
        const struct { const char *name; QString md; QString content; } cases[] = {
            {"开8闭0", QStringLiteral(
                 "- a\n  - b\n    - c\n        ```cpp\n代码一\n        - 看似列表\n```\n后文"),
             QStringLiteral("代码一")},
            {"开8闭4", QStringLiteral(
                 "- a\n  - b\n    - c\n        ```cpp\n代码二\n    ```\n后文"),
             QStringLiteral("代码二")},
            {"开8闭8", QStringLiteral(
                 "- a\n  - b\n    - c\n        ```cpp\n代码三\n        ```\n后文"),
             QStringLiteral("代码三")},
            {"开6闭0", QStringLiteral(
                 "1. a\n   1. b\n      ```js\nx\n```\n后文"),
             QStringLiteral("x")},
            {"开4闭4", QStringLiteral(
                 "- a\n  - b\n    ```cpp\n    缩进内容\n    ```\n后文"),
             QStringLiteral("缩进内容")},
            {"开4闭0", QStringLiteral(
                 "- a\n  - b\n    ```cpp\n内容\n```\n  - c"),
             QStringLiteral("内容")},
            {"闭栏带尾文本是内容", QStringLiteral(
                 "- a\n  - b\n    ```cpp\n行一\n    ```尾文本\n    ```\n后文"),
             QStringLiteral("行一")},
        };
        for (const auto &c : cases) {
            const QString html = MarkdownHandler::toHtml(c.md);
            QVERIFY2(html.contains(QLatin1String("language-")),
                     qPrintable(QString("%1：缩进围栏应被认领，HTML=[%2]").arg(c.name, html)));
            QVERIFY2(html.contains(c.content),
                     qPrintable(QString("%1：代码内容应保留，HTML=[%2]").arg(c.name, html)));
            const QStringList pres = preContentsOf(html);
            // 带尾文本的伪闭合行是内容；真闭合行本身不进内容
            if (QString::fromUtf8(c.name) == QStringLiteral("闭栏带尾文本是内容")) {
                QVERIFY2(pres.join(QChar::fromLatin1('\n')).contains(QStringLiteral("```尾文本")),
                         qPrintable(QString("%1：伪闭合应作为内容保留，HTML=[%2]").arg(c.name, html)));
            } else {
                for (const QString &pre : pres)
                    QVERIFY2(!pre.trimmed().isEmpty() && !pre.contains(QStringLiteral("后文"))
                                 && !pre.contains(QLatin1String("- c")),
                             qPrintable(QString("%1：闭合后文本被吞进代码块，pre=[%2]")
                                            .arg(c.name, pre)));
            }
            QVERIFY2(html.contains(QStringLiteral("后文")) || html.contains(QLatin1String(">c<")),
                     qPrintable(QString("%1：闭合围栏后的正文应存活，HTML=[%2]").arg(c.name, html)));
            // 往返内容存活 + 无占位符泄漏（字节收敛见 testIndentedListFenceClaimed 注）
            const QString r1 = e2eRoundTrip(c.md);
            QVERIFY2(!r1.contains(QLatin1String("ZZ")),
                     qPrintable(QString("%1：占位符泄漏，MD=[%2]").arg(c.name, r1)));
            QVERIFY2(r1.contains(c.content),
                     qPrintable(QString("%1：往返代码内容应保留，MD=[%2]").arg(c.name, r1)));
        }
    }

    void testIndentedFenceOutsideListNotClaimed()
    {
        // 负例：缩进 ≥4 且无列表上下文（或列表已被顶层段落终结）的围栏行不得被
        // 认领——顶层 ≥4sp 缩进是缩进代码块（CommonMark ex.134/109），认领即
        // 回归。期望行为与修复前一致：不产生带 language 的代码块、内容不丢。
        //（两趟收敛不在本测试范围：此族属既有已知 diverge，见 expected-diverge
        //  134/138 的围栏漂移登记——本测试守护的是「不得新认领」。）
        const QStringList cases = {
            QStringLiteral("    ```\n    aaa\n    ```"),
            QStringLiteral("段落文本\n\n        ```js\nx\n        ```"),
            QStringLiteral("- 项\n\n无列表续行的顶层段落\n\n        ```js\nx\n        ```"),
        };
        for (const QString &md : cases) {
            const QString html = MarkdownHandler::toHtml(md);
            QVERIFY2(!html.contains(QLatin1String("language-")),
                     qPrintable(QString("无列表上下文的深缩进围栏不得被认领，MD=[%1] HTML=[%2]")
                                    .arg(md, html)));
            QVERIFY2(html.contains(QLatin1String("aaa")) || html.contains(QLatin1Char('x')),
                     qPrintable(QString("内容不得丢失，HTML=[%1]").arg(html)));
        }
    }

    void testSourceModeIndentedFenceNoSectionSwallow()
    {
        // w2-alpha 实测锚（min-repro gen1 形态）：列表项内代码块的围栏已被
        // fromHtml 归位到列表内容缩进（8sp / 3sp），内容与闭合保持 col-0。
        // 修复前 toHtml(gen1)：8sp 围栏不认领 → col-0 闭合被当新块开栏 →
        // 四级~六级嵌套列表 + 2./3. 一级项 + 第二个代码块全部吞进一个无语言
        // <pre>（16 行字面量）。修复后应与原始顶格形态同构：两个带 language
        // 的代码块、嵌套列表存活、5. 续排项存活。
        const QString gen1 = QStringLiteral(
            "1. 一级有序项：编辑器内核\n"
            "   - 二级无序项：TipTap 初始化链路\n"
            "     1. 三级有序项：扩展加载与 schema 注册\n"
            "        ```cpp\n"
            "// 这段代码块属于上面的列表项，缩进随列表层级\n"
            "int main() { return 0; }\n"
            "```\n"
            "        - 四级无序项：自定义节点\n"
            "          1. 五级有序项：NodeView 实现\n"
            "             - 六级无序项：paint() 渲染\n"
            "2. 一级有序项：行内元素混合\n"
            "3. 一级有序项：默认路径与应用启动\n"
            "4. 一级有序项：列表项内含代码块\n"
            "   ```cpp\n"
            "// 第二个代码块\n"
            "int main() \n"
            "{\n"
            "    return 0;\n"
            "}\n"
            "```\n"
            "5. 一级有序项：列表项结尾\n");
        const QString html = MarkdownHandler::toHtml(gen1);
        QCOMPARE(html.count(QLatin1String("language-cpp")), 2);
        for (const QString &pre : preContentsOf(html)) {
            QVERIFY2(!pre.contains(QStringLiteral("四级")) && !pre.contains(QStringLiteral("五级"))
                         && !pre.contains(QStringLiteral("六级")) && !pre.contains(QStringLiteral("一级有序项")),
                     qPrintable(QString("节尾被吞进代码块，pre=[%1]").arg(pre)));
        }
        QVERIFY2(html.contains(QStringLiteral("四级")) && html.contains(QStringLiteral("五级"))
                     && html.contains(QStringLiteral("六级")),
                 qPrintable(QString("嵌套列表行应存活于代码块之外，HTML=[%1]").arg(html)));
        QVERIFY2(html.contains(QLatin1String("int main() { return 0; }")),
                 qPrintable(QString("第一个代码块内容应保留，HTML=[%1]").arg(html)));
        // 读写对称锚：gen1（缩进围栏）与原始顶格形态的 toHtml 输出必须逐字节
        // 一致——同一编辑器文档的两种合法 MD 源形态，中间表示唯一。修复前
        // gen1 的输出含吞节 <pre>（原始形态无），即不对称的机器可判证据。
        QString orig = gen1;
        orig.replace(QStringLiteral("\n        ```cpp"), QStringLiteral("\n```cpp"))
            .replace(QStringLiteral("\n   ```cpp"), QStringLiteral("\n```cpp"));
        QCOMPARE(html, MarkdownHandler::toHtml(orig));
        // 往返内容存活 + 无占位符泄漏（字节收敛需真实 TipTap DOM 重构，由
        // headless 探针与语料棘轮把关——见 testIndentedListFenceClaimed 注）
        const QString r1 = e2eRoundTrip(gen1);
        QVERIFY2(!r1.contains(QLatin1String("ZZ")),
                 qPrintable(QString("占位符泄漏，MD=[%1]").arg(r1)));
        QVERIFY2(r1.contains(QLatin1String("int main() { return 0; }"))
                     && r1.contains(QLatin1String("```cpp")),
                 qPrintable(QString("往返代码块应保留，MD=[%1]").arg(r1)));
        QVERIFY2(r1.contains(QStringLiteral("四级")) && r1.contains(QStringLiteral("六级")),
                 qPrintable(QString("往返嵌套列表应保留，MD=[%1]").arg(r1)));
    }

    void testCloseFenceFollowedTextNotMerged()
    {
        // 发现 13（四变体实验 a/b/c/d）：Qt setMarkdown 对「``` 闭合行的下一行
        // 直接是文字」的输入把代码块与该文字并进同一段落，占位符还原后产出
        // <p style="margin…"><pre>…</pre> 文字</p> 畸形；TipTap 解析时 codeBlock
        // 被提升出段落、留下空段落/前导空格段落链（用户目视「同一份文件里
        // 代码块与前后内容间距不一致」的根因）。围栏后是空行（变体 b）则占位符
        // 独立成段、后处理剥 <p> 包裹，输出干净顶层 <pre>。
        // 修复：toHtml 在配对闭合围栏后紧跟「文本形态行」（会懒延续占位符段落
        // 的行——纯文本/缩进文本/占位符化行如相邻围栏、hr、公式）时于中间流
        // 该围栏后补一个空行；结构性行（列表/标题/块引用）本就打断段落，不补
        // （补了会把 tight 列表翻 loose，坑 4 不动点链路发散，见实现注释）。
        // 段首并段畸形探测器：<p …> 开头直接跟 <pre>（变体 a/d 的形态）
        static const QRegularExpression pPreMergeRe(
            QStringLiteral("<p[^>]*>\\s*<pre"));
        auto ownParagraph = [](const QString &text) {
            return QRegularExpression(
                QStringLiteral("<p[^>]*>") + QRegularExpression::escape(text)
                    + QStringLiteral("</p>"));
        };

        // 变体 a：闭合围栏后紧跟文字 → <pre> 与文字分块（干净顶层 <pre> +
        // 独立段落）。修复前：<p …><pre>…</pre> 紧跟文字</p> 并段畸形。
        const QString mdA = QStringLiteral(
            "前文段落\n\n```cpp\nint x;\n```\n紧跟文字\n");
        const QString htmlA = MarkdownHandler::toHtml(mdA);
        QVERIFY2(!pPreMergeRe.match(htmlA).hasMatch(),
                 qPrintable(QString("变体 a：<pre> 被并进段落，HTML=[%1]").arg(htmlA)));
        QVERIFY2(ownParagraph(QStringLiteral("紧跟文字")).match(htmlA).hasMatch(),
                 qPrintable(QString("变体 a：闭合后文字应独立成段，HTML=[%1]").arg(htmlA)));
        QVERIFY2(htmlA.contains(QLatin1String("language-cpp"))
                     && htmlA.contains(QLatin1String("int x;")),
                 qPrintable(QString("变体 a：代码内容与语言应保留，HTML=[%1]").arg(htmlA)));

        // 变体 b（对照）：闭合围栏后空行 → 行为不变（本就干净，仍干净）
        const QString mdB = QStringLiteral(
            "前文段落\n\n```cpp\nint x;\n```\n\n空行后文字\n");
        const QString htmlB = MarkdownHandler::toHtml(mdB);
        QVERIFY2(!pPreMergeRe.match(htmlB).hasMatch(),
                 qPrintable(QString("变体 b：对照形态应保持干净，HTML=[%1]").arg(htmlB)));
        QVERIFY2(ownParagraph(QStringLiteral("空行后文字")).match(htmlB).hasMatch(),
                 qPrintable(QString("变体 b：空行后文字应独立成段，HTML=[%1]").arg(htmlB)));

        // 变体 c：围栏前不空行 + 闭合后紧跟文字 → 「吞后续块」症状同治
        //（后续块文字不再被并进含 <pre> 的段落）。围栏前正文与 <pre> 的同段
        // 粘连保留：<pre> 在段尾，TipTap 提升不产生空段落畸形（对照变体 a/d
        // 的段首 <pre> 形态），且补空行会引入 before 侧新行为——本测试只锚
        // 「后续块不被吞」。
        const QString mdC = QStringLiteral(
            "前文段落\n```cpp\nint x;\n```\n后续块文字\n");
        const QString htmlC = MarkdownHandler::toHtml(mdC);
        QVERIFY2(ownParagraph(QStringLiteral("后续块文字")).match(htmlC).hasMatch(),
                 qPrintable(QString("变体 c：后续块文字不应被吞进含 <pre> 的段落，HTML=[%1]")
                                .arg(htmlC)));
        QVERIFY2(htmlC.contains(QLatin1String("language-cpp")),
                 qPrintable(QString("变体 c：代码块应保留，HTML=[%1]").arg(htmlC)));

        // 变体 d：纯段落同触发（无前文、无语言）
        const QString mdD = QStringLiteral("```\nplain\n```\n直接文字\n");
        const QString htmlD = MarkdownHandler::toHtml(mdD);
        QVERIFY2(!pPreMergeRe.match(htmlD).hasMatch(),
                 qPrintable(QString("变体 d：<pre> 被并进段落，HTML=[%1]").arg(htmlD)));
        QVERIFY2(ownParagraph(QStringLiteral("直接文字")).match(htmlD).hasMatch(),
                 qPrintable(QString("变体 d：闭合后文字应独立成段，HTML=[%1]").arg(htmlD)));

        // 相邻代码块（闭合围栏后紧跟开栏）：同属文本形态延续，同治——修复前
        // 两个 <pre> 并进同一段落（<p …><pre>cpp</pre> <pre>python</pre></p>）
        const QString mdAdj = QStringLiteral("```cpp\na\n```\n```python\nc\n```\n");
        const QString htmlAdj = MarkdownHandler::toHtml(mdAdj);
        QVERIFY2(!pPreMergeRe.match(htmlAdj).hasMatch(),
                 qPrintable(QString("相邻代码块不应并进段落，HTML=[%1]").arg(htmlAdj)));
        QVERIFY2(htmlAdj.contains(QLatin1String("language-cpp"))
                     && htmlAdj.contains(QLatin1String("language-python")),
                 qPrintable(QString("相邻代码块语言应各自保留，HTML=[%1]").arg(htmlAdj)));

        // 结构性后续行（本就打断段落、修复前后零变化——列表场景的守卫锚）：
        // 列表行不补空行（补了 tight→loose 翻转，坑 4 家族往返发散）
        const QString mdList = QStringLiteral(
            "- 项\n\n```cpp\nint x;\n```\n- 下一项\n");
        const QString htmlList = MarkdownHandler::toHtml(mdList);
        QVERIFY2(!pPreMergeRe.match(htmlList).hasMatch(),
                 qPrintable(QString("列表行后应保持独立成块，HTML=[%1]").arg(htmlList)));
        QVERIFY2(htmlList.contains(QLatin1String("<li"))
                     && htmlList.contains(QStringLiteral("下一项")),
                 qPrintable(QString("列表后续行应存活，HTML=[%1]").arg(htmlList)));
        // 标题行同理（零变化守卫）
        const QString mdHead = QStringLiteral("```cpp\nint x;\n```\n# 标题\n");
        const QString htmlHead = MarkdownHandler::toHtml(mdHead);
        QVERIFY2(!pPreMergeRe.match(htmlHead).hasMatch()
                     && htmlHead.contains(QLatin1String("<h1")),
                 qPrintable(QString("标题行后应保持独立成块，HTML=[%1]").arg(htmlHead)));

        // 往返：并段畸形消除后首趟即归一（空行由 fromHtml 产出，两趟收敛）
        for (const QString &md : { mdA, mdD }) {
            const QString r1 = e2eRoundTrip(md);
            QVERIFY2(!r1.contains(QLatin1String("ZZ")),
                     qPrintable(QString("占位符泄漏，MD=[%1]").arg(r1)));
            QVERIFY2(r1.contains(QLatin1String("```")),
                     qPrintable(QString("往返代码块围栏应保留，MD=[%1]").arg(r1)));
        }
    }

    void testCloseFenceInListContinuationNotSplit()
    {
        // 评审 R1 锚（P3 族）：列表项内容缩进围栏 + 闭合后紧跟项内续文（真实
        // 语料形态：docs/superpowers/specs/2026-08-09-emoji-picker-design.md
        // :146）。占位符与续文同为列表项段落的懒延续，并段（<li>项一 <pre>…
        // </pre> 项一续文</li>）是该形态的既有行为（坑 4 读写对称设计），不是
        // 畸形——补空行会把续文拆成 <li> 间裸 <p>（非法嵌套）或把单个 <ul>
        // 拆成两个，往返丢列表归属。本测试锚定：不拆列表（单个 ul）、续文留在
        // 项内（与 <pre> 同 <li>）、往返文本存活。
        // 三变体：2sp 围栏+2sp 续文（P3）/ 2sp 围栏+col-0 续文（P3'）/ col-0
        // 围栏+col-0 续文（P3''，lazy continuation 形态）。
        const QStringList cases = {
            QStringLiteral("- 项一\n  ```cpp\nint a;\n  ```\n  项一续文\n- 项二\n"),
            QStringLiteral("- 项一\n  ```cpp\nint a;\n  ```\n项一续文\n- 项二\n"),
            QStringLiteral("- 项一\n```cpp\nint a;\n```\n项一续文\n- 项二\n"),
        };
        static const QRegularExpression liPreRe(
            QStringLiteral("</pre>\\s*项一续文\\s*</li>"));
        static const QRegularExpression barePRe(
            QStringLiteral("(</li>\\s*<p|<p[^>]*>\\s*项一续文)"));
        for (int k = 0; k < cases.size(); ++k) {
            const QString &md = cases[k];
            const QString html = MarkdownHandler::toHtml(md);
            QVERIFY2(html.count(QLatin1String("<ul")) == 1,
                     qPrintable(QString("P3 变体 %1：列表被拆分，HTML=[%2]")
                                    .arg(k).arg(html)));
            QVERIFY2(liPreRe.match(html).hasMatch(),
                     qPrintable(QString("P3 变体 %1：续文应与 <pre> 同 <li>（既有并段形态），"
                                        "HTML=[%2]").arg(k).arg(html)));
            QVERIFY2(!barePRe.match(html).hasMatch(),
                     qPrintable(QString("P3 变体 %1：<li> 间不得出现裸 <p>（非法嵌套），"
                                        "HTML=[%2]").arg(k).arg(html)));
            QVERIFY2(html.contains(QLatin1String("language-cpp"))
                         && html.contains(QLatin1String("int a;")),
                     qPrintable(QString("P3 变体 %1：代码块应保留，HTML=[%2]").arg(k).arg(html)));
            // 往返：续文与列表项文本存活、无占位符泄漏（真实链路收敛由 selftest
            // 与语料棘轮把关——sim 链对 li 内并段 <pre> 的 TipTap 重构建模受限）
            const QString r1 = e2eRoundTrip(md);
            QVERIFY2(!r1.contains(QLatin1String("ZZ")),
                     qPrintable(QString("P3 变体 %1：占位符泄漏，MD=[%2]").arg(k).arg(r1)));
            QVERIFY2(r1.contains(QStringLiteral("项一续文"))
                         && r1.contains(QStringLiteral("项二")),
                     qPrintable(QString("P3 变体 %1：续文与后续项应存活，MD=[%2]").arg(k).arg(r1)));
        }
    }

    void testSetextLineAfterCloseFenceNotHeading()
    {
        // 评审 R1 锚：闭合围栏后紧跟 === 行。setext 下划线不是「打断段落」而是
        // 「消费段落」——占位符行被吃成标题内容，还原后 <h1><pre>…</pre></h1>
        // 畸形，往返残留空 # 标题（"# \n\n```cpp…"）。=== 属文本形态延续，
        // 补空行后占位符独立成段（干净顶层 <pre>），=== 独立成段不再吃掉它。
        const QString md = QStringLiteral("```cpp\nint x;\n```\n===\n");
        const QString html = MarkdownHandler::toHtml(md);
        QVERIFY2(!QRegularExpression(QStringLiteral("<h[1-6][^>]*>\\s*<pre"))
                     .match(html).hasMatch(),
                 qPrintable(QString("<pre> 不得被并进标题，HTML=[%1]").arg(html)));
        QVERIFY2(!html.contains(QLatin1String("<h1")),
                 qPrintable(QString("=== 不得把占位符吃成 H1，HTML=[%1]").arg(html)));
        QVERIFY2(html.contains(QLatin1String("language-cpp")),
                 qPrintable(QString("代码块应保留，HTML=[%1]").arg(html)));
        const QString r1 = e2eRoundTrip(md);
        QVERIFY2(!r1.contains(QLatin1String("ZZ")),
                 qPrintable(QString("占位符泄漏，MD=[%1]").arg(r1)));
        QVERIFY2(r1.contains(QLatin1String("```")),
                 qPrintable(QString("往返代码块围栏应保留，MD=[%1]").arg(r1)));
        // === 往返存活锚（终审 Minor#4）：r1 中 === 须以原文或 Qt setext 保护
        // 转义形 \=\==（防行首 === 再被解析为 setext 下划线）存活——否则 ===
        // 被静默丢弃时上方断言仍绿，锚失效。
        QVERIFY2(r1.contains(QStringLiteral("==="))
                     || r1.contains(QStringLiteral("\\=\\==")),
                 qPrintable(QString("=== 往返应存活（原文或 \\=\\== 转义形），"
                                    "MD=[%1]").arg(r1)));
    }


    void testUnclosedHtmlTagNoSwallow()
    {
        // 未闭合的 HTML 容器标签（<script>/<hr>/<b> 等，全文无同名 </tag> 配对）会
        // 让 Qt setMarkdown 把其后全部内容当 HTML 块吞噬到文末（数据丢失）。
        // 修复：toHtml 中把未闭合标签转义为字面实体（<tag> → &lt;tag&gt;），
        // 受支持的标签（配对闭合、void <br>、自闭合 <img/>）保持原样。
        const QString md = QStringLiteral(
            "**问题**: `codeFenceLang` 拼接进属性。如 ``` ```<script> ```），则产生 XSS。\n"
            "**建议**: `codeFenceLang` 调用 `toHtmlEscaped()` 再拼接。\n");
        const QString html = MarkdownHandler::toHtml(md);
        // 关键 token 一个都不能丢（修复前整段被 Qt 吞掉）
        QVERIFY2(html.contains("XSS"),
                 qPrintable("后半段正文被吞噬，实际: " + html));
        QVERIFY(html.contains("建议"));
        QVERIFY(html.contains("toHtmlEscaped"));
        // <script> 应转义为实体（字面显示，不产生可执行标签）
        QVERIFY2(html.contains("&lt;script&gt;"),
                 qPrintable("<script> 应转义为实体，实际: " + html));
        QVERIFY(!html.contains("<script>"));
        // 往返内容保留，且往返结果稳定（MD1 == MD2，不逐次累积）
        const QString back1 = MarkdownHandler::fromHtml(html);
        QVERIFY2(back1.contains("XSS") && back1.contains("建议"),
                 qPrintable("往返丢失内容，实际: " + back1));
        const QString back2 = MarkdownHandler::fromHtml(MarkdownHandler::toHtml(back1));
        QVERIFY2(back1 == back2,
                 qPrintable("往返不稳定（内容逐次累积），MD1: " + back1 + " MD2: " + back2));
    }

    void testUnclosedHtmlTagKeepsSupportedTags()
    {
        // 未闭合标签转义不能误伤可正常往返的标签：配对闭合（<b>x</b>、<a href>…</a>）、
        // void <br>、自闭合 <img/>。
        const QString md = QStringLiteral(
            "a <b>x</b> c <a href=\"u\">y</a> <br> <img src=\"i\"/> <hr> TAIL\n");
        const QString html = MarkdownHandler::toHtml(md);
        // 配对 <b> 由 Qt 原生转 <strong>，<a>、<br> 保留；未闭合 <hr> 转义为实体
        QVERIFY2(html.contains("<strong>x</strong>"),
                 qPrintable("配对 <b> 应转 <strong>，实际: " + html));
        QVERIFY(html.contains("<a href=\"u\">y</a>"));
        QVERIFY(html.contains("<br>"));
        QVERIFY2(html.contains("&lt;hr&gt;"),
                 qPrintable("未闭合 <hr> 应转义，实际: " + html));
        QVERIFY(html.contains("TAIL"));
        // 往返后 TAIL 仍在（修复前被吞）
        const QString back = MarkdownHandler::fromHtml(html);
        QVERIFY2(back.contains("TAIL"),
                 qPrintable("往返后尾部丢失，实际: " + back));
    }

    void testLiteralBrmarkerPreserved()
    {
        // 源文档字面 "BRMARKER"（含行内代码内）不能被软换行后处理误替换为 <br>。
        // 修复前：后处理 html.replace("BRMARKER", "<br data-brk>") 把用户字面文本
        // 一并替换（如 audit 文档中的 `BRMARKER` 示例）。修复：改用带序号的
        // ZZBR<n>ZZ 占位符，与用户文本不冲突。
        const QString md = QStringLiteral(
            "a BRMARKER b\nc `BRMARKER` d\n");
        const QString html = MarkdownHandler::toHtml(md);
        QVERIFY2(html.contains("BRMARKER"),
                 qPrintable("字面 BRMARKER 应保留，实际: " + html));
        // 两行正文间的软换行仍应工作
        QVERIFY2(html.contains("<br data-brk>"),
                 qPrintable("软换行应正常，实际: " + html));
        const QString back = MarkdownHandler::fromHtml(html);
        QVERIFY(back.contains("BRMARKER"));
    }

    void testCodeBlockPlaceholderCollisionWithUserText()
    {
        // 真实代码块的占位符 ZZCB0ZZ 与用户字面文本 ZZCB0ZZ 冲突：还原时不得把
        // 用户字面文本误替换成代码块 HTML（往返数据丢失）。修复：占位符序号
        // 避开源文档/已产出内容中的同串（collisionFreePlaceholder）。
        const QString md = QStringLiteral(
            "```cpp\nint x;\n```\n\n**问题**：字面 `ZZCB0ZZ` 文本。TAIL\n");
        const QString html = MarkdownHandler::toHtml(md);
        QVERIFY2(html.contains("ZZCB0ZZ"),
                 qPrintable("用户字面 ZZCB0ZZ 应保留，实际: " + html));
        QVERIFY(html.contains("int x"));
        const QString back = MarkdownHandler::fromHtml(html);
        QVERIFY2(back.contains("ZZCB0ZZ") && back.contains("int x") && back.contains("TAIL"),
                 qPrintable("往返应保留全部内容，实际: " + back));
    }

    void testIndentedParaBetweenListItemsRoundTrip()
    {
        // 两列表项之间的缩进段落：Qt setMarkdown 把带前导空格的段落当列表项
        // lazy continuation 吸收进 <ol>（<ol> 内非法 <p>），fromHtml 的 toMarkdown
        // 丢弃该段（往返数据丢失）。修复：跨空行紧跟列表的缩进段落剥离前导空格
        // 成为独立段落；紧邻列表项无空行的缩进行（真正的续行）保留缩进。
        const QString md = QStringLiteral(
            "1. item one\n\n   建议建立完整的测试套件。\n\n2. item two\n");
        const QString back = MarkdownHandler::fromHtml(MarkdownHandler::toHtml(md));
        QVERIFY2(back.contains("建议建立完整的测试套件"),
                 qPrintable("列表间段落应往返保留，实际: " + back));
        // lazy continuation（无空行）仍是列表项内容
        const QString md2 = QStringLiteral("- item one\n   continuation text\n");
        const QString back2 = MarkdownHandler::fromHtml(MarkdownHandler::toHtml(md2));
        QVERIFY2(back2.contains("continuation"),
                 qPrintable("lazy continuation 应保留为列表项内容，实际: " + back2));
    }

    void testListBlankLineBetweenItemsRoundTrip()
    {
        // 列表项之间的空行：Qt setMarkdown 把「1. a\n\n2. b」折叠成同一 <ol> 内
        // 相邻 <li>，空行信息丢失（往返后列表项变相邻）。修复：toHtml 在列表项间
        // 空行处插入 HTML 注释占位强制拆分列表（Qt 产出独立 <ol>/<ul>，有序列表
        // 自动带 start 编号），fromHtml 的 toMarkdown 还原为列表项间的空行。
        // 仅顶层列表项（缩进 0）触发；嵌套项间空行受 Qt toMarkdown 合并限制不处理。
        const QString md = QStringLiteral(
            "1. item one\n\n2. item two\n\n3. item three\n");
        const QString back = MarkdownHandler::fromHtml(MarkdownHandler::toHtml(md));
        QVERIFY2(back.contains("1. item one\n\n2. item two\n\n3. item three"),
                 qPrintable("列表项间空行应往返保留，实际: " + back));
        // 无序列表
        const QString md2 = QStringLiteral("- a\n\n- b\n");
        const QString back2 = MarkdownHandler::fromHtml(MarkdownHandler::toHtml(md2));
        QVERIFY2(back2.contains("- a\n\n- b"),
                 qPrintable("无序列表项间空行应保留，实际: " + back2));
        // 相邻列表项（无空行）不应新增空行
        const QString md3 = QStringLiteral("1. a\n2. b\n3. c\n");
        const QString back3 = MarkdownHandler::fromHtml(MarkdownHandler::toHtml(md3));
        QVERIFY2(back3.contains("1. a\n2. b\n3. c"),
                 qPrintable("无空行列表不应新增空行，实际: " + back3));
    }

    void testListBlankLineAfterSubListRoundTrip()
    {
        // 子列表后空行再接下一顶层项（审计文档第五节常见结构）：
        // 空行在「子列表最后一项」与「下一顶层列表项」之间，需拆分为两个顶层列表。
        const QString md = QStringLiteral(
            "2. item two\n   - sub one\n   - sub two\n\n3. item three\n");
        const QString back = MarkdownHandler::fromHtml(MarkdownHandler::toHtml(md));
        QVERIFY2(back.contains("sub two\n\n3. item three"),
                 qPrintable("子列表后空行应保留，实际: " + back));
    }

    void testParagraphMultipleBlankLinesRoundTrip()
    {
        // 段落之间的多个空行：Qt setMarkdown 把「a\n\n\nb」（2 空行）折叠成单个
        // 段落分隔，空行信息丢失。且纯空行不可移植——标准 md 渲染器（GFM/Joplin
        // 之外的多数）把多空行折叠成 1。修复：空行用字面 &nbsp; 独占一行表示
        // （Joplin/MarkText 等跨编辑器把 &nbsp; 行渲染为空行）。toHtml 把连续
        // 空行中除段落分隔外的部分替换为 ZZBL 占位符行，setMarkdown 后还原为
        // <p></p>（空段落）；fromHtml 识别空段落，占位符行还原为 &nbsp; 行。
        // N 空行 → 段落分隔 + (N-1) 个 &nbsp; 行，往返稳定。
        const QString md2 = QStringLiteral("a\n\n\nb\n");
        const QString back2 = MarkdownHandler::fromHtml(MarkdownHandler::toHtml(md2));
        QVERIFY2(back2.contains("a\n\n&nbsp;\n\nb"),
                 qPrintable("段落间 2 空行应转为 1 个 &nbsp; 行，实际: " + back2));
        const QString md3 = QStringLiteral("a\n\n\n\nb\n");
        const QString back3 = MarkdownHandler::fromHtml(MarkdownHandler::toHtml(md3));
        QVERIFY2(back3.contains("a\n\n&nbsp;\n\n&nbsp;\n\nb"),
                 qPrintable("段落间 3 空行应转为 2 个 &nbsp; 行，实际: " + back3));
        // 单个空行（段落分隔）应保持纯空行（不转 &nbsp;）
        const QString md1 = QStringLiteral("a\n\nb\n");
        const QString back1 = MarkdownHandler::fromHtml(MarkdownHandler::toHtml(md1));
        QVERIFY2(back1.contains("a\n\nb"),
                 qPrintable("段落间 1 空行应保持，实际: " + back1));
        // 相邻段落（软换行，无空行）不应新增空行
        const QString md0 = QStringLiteral("a\nb\n");
        const QString back0 = MarkdownHandler::fromHtml(MarkdownHandler::toHtml(md0));
        QVERIFY2(back0.contains("a\nb"),
                 qPrintable("软换行不应新增空行，实际: " + back0));
    }

    void testEmptyParagraphFromHtmlRoundTrip()
    {
        // WYSIWYG 空行 = 空段落（TipTap getHTML 输出 <p></p>；toHtml 的空行保护
        // 输出 <p></p>；源码字面 &nbsp; 行经 setMarkdown 成 NBSP 段落）。
        // Qt setHtml 丢弃空段落、把 &nbsp; 实体转成 NBSP 字符（不可移植），故
        // fromHtml 在 setHtml 前识别空段落替换为占位符段落，toMarkdown 后
        // 占位符行还原为字面 &nbsp; 行。M 个空段落 → M 个 &nbsp; 行。
        const QString h1 = QStringLiteral(
            "<p>a</p><p></p><p>b</p>");   // 1 空段落
        const QString md1 = MarkdownHandler::fromHtml(h1);
        QVERIFY2(md1.contains("a\n\n&nbsp;\n\nb"),
                 qPrintable("单空段落应还原为 1 个 &nbsp; 行，实际: " + md1));
        const QString h2 = QStringLiteral(
            "<p>a</p><p></p><p></p><p>b</p>");   // 2 空段落
        const QString md2 = MarkdownHandler::fromHtml(h2);
        QVERIFY2(md2.contains("a\n\n&nbsp;\n\n&nbsp;\n\nb"),
                 qPrintable("双空段落应还原为 2 个 &nbsp; 行，实际: " + md2));
        // <p><br></p>（用户手写 <br> 单独成段等场景）同样识别为空段落
        const QString h3 = QStringLiteral(
            "<p>a</p><p><br></p><p>b</p>");
        const QString md3 = MarkdownHandler::fromHtml(h3);
        QVERIFY2(md3.contains("a\n\n&nbsp;\n\nb"),
                 qPrintable("<p><br></p> 空段落应还原为 1 个 &nbsp; 行，实际: " + md3));
        // <p>&nbsp;</p>（源码 &nbsp; 行经 toHtml+TipTap 的产物）同样识别
        const QString h4 = QStringLiteral(
            "<p>a</p><p>&nbsp;</p><p>b</p>");
        const QString md4 = MarkdownHandler::fromHtml(h4);
        QVERIFY2(md4.contains("a\n\n&nbsp;\n\nb"),
                 qPrintable("<p>&nbsp;</p> 空段落应还原为 &nbsp; 行，实际: " + md4));
    }

    void testInlineMultipleNbspRoundTrip()
    {
        // 行内连续空格：Qt toMarkdown 折叠普通连续空格（Line1   text → Line1
        // text），往返丢空格。WYSIWYG 连续空格以 NBSP 保存（TipTap 序列化为
        // &nbsp; 实体），fromHtml 需保护连续 2+ NBSP → 字面 &nbsp; 实体，
        // 跨编辑器可移植。单空格无需转义（往返安全）。
        const QString h2 = QStringLiteral("<p>Line1&nbsp;&nbsp;text</p>");
        const QString md2 = MarkdownHandler::fromHtml(h2);
        QVERIFY2(md2.contains("Line1&nbsp;&nbsp;text"),
                 qPrintable("行内 2 NBSP 应还原为 2 个 &nbsp; 实体，实际: " + md2));
        const QString h3 = QStringLiteral("<p>Line1&nbsp;&nbsp;&nbsp;text</p>");
        const QString md3 = MarkdownHandler::fromHtml(h3);
        QVERIFY2(md3.contains("Line1&nbsp;&nbsp;&nbsp;text"),
                 qPrintable("行内 3 NBSP 应还原为 3 个 &nbsp; 实体，实际: " + md3));
        // 全链路：源码 &nbsp; 实体 → 往返保持
        const QString mdSrc = QStringLiteral("Line1&nbsp;&nbsp;&nbsp;text\n");
        const QString back = MarkdownHandler::fromHtml(
            MarkdownHandler::toHtml(mdSrc));
        QVERIFY2(back.contains("Line1&nbsp;&nbsp;&nbsp;text"),
                 qPrintable("行内 &nbsp; 实体全链路往返应保持，实际: " + back));
        // 单空格不转义
        const QString h1 = QStringLiteral("<p>Line1&nbsp;text</p>");
        const QString md1 = MarkdownHandler::fromHtml(h1);
        QVERIFY2(!md1.contains("\\&nbsp;"),
                 qPrintable("单 NBSP 不应被转义为 \\&nbsp;，实际: " + md1));
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

    void testTableEmptyCellRoundTrip()
    {
        // 空单元格 ||（无空格）往返：Qt setMarkdown 会把 || 误解析、空单元格
        // 重定位到行尾。toHtml 应在 setMarkdown 前规范化 || → |  |。
        const QString md1 = QStringLiteral(
            "| a | b | c |\n"
            "| --- | --- | --- |\n"
            "| 1 || 3 |\n");
        const QString md2 = MarkdownHandler::fromHtml(MarkdownHandler::toHtml(md1));
        // 空单元格应保留在中间列，不得重定位为尾列（| 1 | 3 |  | 是损坏形态）
        QVERIFY2(md2.contains("| 1 |  | 3 |"),
                 qPrintable("|| 空单元格应规范化保留为中间空列，实际:\n" + md2));
        QVERIFY2(!md2.contains("| 1 | 3 |  |"),
                 qPrintable("空单元格不得重定位到行尾，实际:\n" + md2));
    }

    void testTableEmptyCellCodeSpanNotSplit()
    {
        // 行内代码内的 || 不应被当作空单元格分隔（反引号感知规范化）
        const QString md1 = QStringLiteral(
            "| a | b |\n"
            "| --- | --- |\n"
            "| `x||y` | 2 |\n");
        const QString md2 = MarkdownHandler::fromHtml(MarkdownHandler::toHtml(md1));
        QVERIFY2(md2.contains("`x||y`"),
                 qPrintable("行内代码内的 || 应原样保留，实际:\n" + md2));
    }

    void testSingleLineBlockMathRoundTrip()
    {
        // 单行块级公式 $$...$$（开闭定界符同一行，如 $$E=mc^{2}$$）应识别为
        // block-math，往返不泄漏 ZZMB 占位符、不丢 LaTeX 内容。
        // （保存时 fromHtml 规范化为多行 $$\n...\n$$，语义等价）
        const QString md1 = QStringLiteral("$$E=mc^{2}$$");
        const QString html = MarkdownHandler::toHtml(md1);
        QVERIFY2(html.contains("block-math"),
                 qPrintable("单行 $$...$$ 应生成 block-math，实际:\n" + html));
        QVERIFY2(html.contains("data-latex=\"E=mc^{2}\""),
                 qPrintable("LaTeX 内容应保留，实际:\n" + html));
        QVERIFY2(!html.contains("ZZMB"),
                 qPrintable("占位符 ZZMB 不应泄漏为字面文本，实际:\n" + html));

        const QString md2 = MarkdownHandler::fromHtml(html);
        QVERIFY2(md2.contains("$$"),
                 qPrintable("往返应含 $$ 定界符，实际:\n" + md2));
        QVERIFY2(md2.contains("E=mc^{2}"),
                 qPrintable("往返应保留 LaTeX 内容，实际:\n" + md2));
    }

    void testTableBoundaryGfm()
    {
        // GFM Tables 扩展：表格在「首个空行或其他块级结构」处结束。紧跟表格行、
        // 不含竖线的纯文本行被惰性延续为数据行（文本进第一列、其余列空）；标题/
        // 代码块/水平线等块级结构应打断表格。
        const QString tbl = QStringLiteral("| a | b |\n|---|---|\n| 1 | 2 |\n");

        // 纯文本行 → 吸收为数据行（带 | 前缀），不被分离成段落
        {
            const QString md2 = MarkdownHandler::fromHtml(
                MarkdownHandler::toHtml(tbl + QStringLiteral("紧跟的正文段落")));
            QVERIFY2(md2.contains("| 紧跟的正文段落"),
                     qPrintable("纯文本行应按 GFM 吸收为数据行，实际:\n" + md2));
            QVERIFY2(!md2.contains("\n\n紧跟的正文段落"),
                     qPrintable("纯文本行不应被分离成独立段落，实际:\n" + md2));
        }
        // 标题 → 打断表格
        {
            const QString md2 = MarkdownHandler::fromHtml(
                MarkdownHandler::toHtml(tbl + QStringLiteral("# 标题")));
            QVERIFY2(md2.contains("\n\n# 标题"),
                     qPrintable("标题应打断表格，实际:\n" + md2));
        }
        // 代码块 → 打断表格
        {
            const QString md2 = MarkdownHandler::fromHtml(
                MarkdownHandler::toHtml(tbl + QStringLiteral("```\nx\n```")));
            QVERIFY2(md2.contains("```\nx\n```"),
                     qPrintable("代码块应打断表格，实际:\n" + md2));
            QVERIFY2(!md2.contains("| ```"),
                     qPrintable("代码块不应被吞入表格，实际:\n" + md2));
        }
        // 水平线 → 打断表格
        {
            const QString md2 = MarkdownHandler::fromHtml(
                MarkdownHandler::toHtml(tbl + QStringLiteral("---")));
            QVERIFY2(md2.contains("\n\n---"),
                     qPrintable("水平线应打断表格，实际:\n" + md2));
        }
        // 纯文本 + 标题：纯文本吸收为数据行、标题仍打断（inTableRegion 跨惰性延续保持）
        {
            const QString md2 = MarkdownHandler::fromHtml(
                MarkdownHandler::toHtml(tbl + QStringLiteral("正文\n# 标题")));
            QVERIFY2(md2.contains("| 正文"),
                     qPrintable("惰性延续的纯文本应吸收为数据行，实际:\n" + md2));
            QVERIFY2(md2.contains("\n\n# 标题"),
                     qPrintable("惰性延续后标题仍应打断表格，实际:\n" + md2));
        }
    }

    void testBlockquoteParagraphSeparatorRoundTrip()
    {
        // 块引用内多段落（「> 空行」分隔）往返：两段不应被合并成一段。
        // 曾把裸 ">" 当正文行插入 <br data-brk>，「> a\n>\n> b」两段粘成一段。
        const QString md1 = QStringLiteral("> 第一段\n>\n> 第二段");
        const QString md2 = MarkdownHandler::fromHtml(MarkdownHandler::toHtml(md1));
        QVERIFY2(md2.contains("> 第一段\n\n> 第二段"),
                 qPrintable("两段引用之间应保留空行分隔，实际:\n" + md2));

        // 「> + 空格」形式同理
        const QString md3 = MarkdownHandler::fromHtml(
            MarkdownHandler::toHtml(QStringLiteral("> 第一段\n> \n> 第二段")));
        QVERIFY2(md3.contains("> 第一段\n\n> 第二段"),
                 qPrintable(">+空格 空行形式也应保留分隔，实际:\n" + md3));

        // 软换行（同一段落内两行）仍应保留为同一段，不被拆成两段
        const QString md4 = MarkdownHandler::fromHtml(
            MarkdownHandler::toHtml(QStringLiteral("> 第一行\n> 第二行")));
        QVERIFY2(md4.contains("> 第一行\n> 第二行"),
                 qPrintable("块引用内软换行不应被拆成两段，实际:\n" + md4));
    }

    void testTableEmptyCellNoPlaceholderLeak()
    {
        // 表格空单元格往返不得泄漏 ZZBL 占位符。
        // TipTap 把空 <td></td> 序列化为 <td><p></p></td>（单元格必须含段落
        // 节点）。fromHtml 的空段落保护若作用于表格容器内，占位符会进入
        // | ... | 行，还原时整行不等于占位符 → 泄漏（用户见到 ZZBL2ZZ）。
        const QString tipTapTable =
            QStringLiteral("<table><tbody><tr>"
                           "<td colspan=\"1\" rowspan=\"1\"><p>斜粗体</p></td>"
                           "<td colspan=\"1\" rowspan=\"1\"><p></p></td>"
                           "</tr></tbody></table>");
        const QString md = MarkdownHandler::fromHtml(tipTapTable);
        QVERIFY2(!md.contains("ZZBL"),
                 qPrintable("空单元格不得泄漏 ZZBL 占位符，实际: " + md));
        QVERIFY2(md.contains("| 斜粗体 |"),
                 qPrintable("表格行应保留，实际: " + md));

        // 完整往返：表格空单元格经 toHtml → fromHtml 稳定，无占位符泄漏
        const QString round =
            MarkdownHandler::fromHtml(MarkdownHandler::toHtml(
                QStringLiteral("| 斜粗体 | |\n|---|---|\n| a | |")));
        QVERIFY2(!round.contains("ZZBL"),
                 qPrintable("往返不得泄漏 ZZBL 占位符，实际:\n" + round));
        QVERIFY2(round.contains("| 斜粗体 |"),
                 qPrintable("往返应保留表格，实际:\n" + round));

        // 表格前后空行（字面 &nbsp; 行）仍应还原为 &nbsp;，不受表格内跳过影响
        const QString around =
            MarkdownHandler::fromHtml(
                MarkdownHandler::toHtml(QStringLiteral(
                    "前置\n\n&nbsp;\n\n| a | b |\n|---|---|\n| c | d |\n\n&nbsp;\n\n后置")));
        QVERIFY2(around.count(QStringLiteral("\n&nbsp;\n")) == 2,
                 qPrintable("表格前后空行应保留为 &nbsp;，实际:\n" + around));
    }

    void testLiteralPlaceholderTextCoexistsRoundTrip()
    {
        // 用户文档字面包含占位符形态文本（ZZSB0ZZ），与同前缀真实元素
        // （下标 ~2~）共存时，往返不得互相污染：占位符生成必须跳过源文中
        // 已存在的串，否则还原阶段 html.replace(生成的占位符, …) 会把用户
        // 字面文本一并误替换（往返数据损坏）。
        const QString md = QStringLiteral(
            "水的化学式是 H~2~O。\n"
            "\n"
            "字面占位符文本 ZZSB0ZZ 是文档示例。");
        const QString html = MarkdownHandler::toHtml(md);
        // 用户字面 ZZSB0ZZ 必须原样存活于 HTML（不被当下标占位符误替换）
        QVERIFY2(html.contains("ZZSB0ZZ"),
                 qPrintable("用户字面 ZZSB0ZZ 应原样保留，实际: " + html));
        // 真实下标只应还原出一处 <sub>2</sub>（字面文本不得被误转）
        QCOMPARE(html.count("<sub>2</sub>"), 1);
        // 完整往返：逐字还原
        const QString back = MarkdownHandler::fromHtml(html);
        QVERIFY2(back.contains("字面占位符文本 ZZSB0ZZ 是文档示例"),
                 qPrintable("往返应原样保留字面占位符文本，实际: " + back));
        QCOMPARE(back.trimmed(), md);
    }

    void testStrikeAndLiteralAsteriskNoPlaceholderCollision()
    {
        // ZZST 前缀双占用回归：fromHtml 中删除线 <s> 保护（计数 n）与步骤 D
        // 字面孤立 * 保护（计数 nSt）各自从 0 独立编号，同一文档两者共存时
        // 生成相同占位符 ZZST0ZZ 承载两份不同备份，LIFO 还原把全部替换为
        // 后入者 → 删除线内容丢失。碰撞检测应使后生成者跳号。
        const QString html = QStringLiteral(
            "<p><s>已删除</s> 以及 2*3=6</p>");
        const QString md = MarkdownHandler::fromHtml(html);
        QVERIFY2(md.contains("~~已删除~~"),
                 qPrintable("删除线应还原为 ~~已删除~~，实际: " + md));
        QVERIFY2(md.contains("2\\*3=6"),
                 qPrintable("字面 * 应还原为 \\*，实际: " + md));
        // 完整往返：<s> 与字面 * 均保真
        const QString back = MarkdownHandler::toHtml(md);
        QVERIFY2(back.contains("<s>已删除</s>"),
                 qPrintable("往返应还原 <s>，实际: " + back));
        QVERIFY2(back.contains("2*3=6") || back.contains("2\\*3=6"),
                 qPrintable("往返应保留字面 *，实际: " + back));
    }

    void testNbspLineIsBlankBlock()
    {
        // 源码字面 &nbsp; 行（用户显式空段）经 toHtml 应为独立空段落，而非被
        // Qt setMarkdown 当成同一段内软换行（&nbsp; 前后无空行分隔时，如
        // "Line 1\n&nbsp;\nLine 2" 曾产出 <p>Line 1<br data-brk>&nbsp;<br data-brk>Line 2</p>，
        // 往返后空行前带空格/多空行漂移）。
        const QString md1 = QStringLiteral("Line 1\n&nbsp;\nLine 2");
        const QString html1 = MarkdownHandler::toHtml(md1);
        QVERIFY2(html1.contains("<p></p>"),
                 qPrintable("&nbsp; 行应转独立空段落，实际: " + html1));
        QVERIFY2(!html1.contains("Line 1<br"),
                 qPrintable("Line 1 段尾不应有软换行污染，实际: " + html1));
        // 往返稳定：字面 &nbsp; 行保留（规范化前后空行分隔）
        const QString back1 = MarkdownHandler::fromHtml(html1);
        QVERIFY2(back1.contains("\n\n&nbsp;\n\n"),
                 qPrintable("往返应得 &nbsp; 空行，实际:\n" + back1));

        // &nbsp; 行两侧已有空行分隔 → 仍为 1 个空段（不得折叠成 2 个）
        const QString html2 = MarkdownHandler::toHtml(
            QStringLiteral("a\n\n&nbsp;\n\nb"));
        const int emptyP2 = html2.count(QStringLiteral("<p></p>"));
        QVERIFY2(emptyP2 == 1,
                 qPrintable("两侧空行的 &nbsp; 应仍 1 空段，实际 "
                            + QString::number(emptyP2) + " 个: " + html2));
    }

    void testEmphMarkerSpaceBeforeFullwidthPunctHealed()
    {
        // 旧版反换行在强调标记边界折行时误补空格，产生 "**审查结论** ：" 形态损坏。
        // 该空格是段落内唯一折行点，会触发 QTextMarkdownWriter 的 i=j+1 吞字 bug
        // （续行恰满 80 列时吞掉断点处的 。/token 字符）。fromHtml 定向归一化
        // 「加粗/斜体闭合 + 空格 + 全角标点」为无空格，使往返自愈且保句号。
        const QString md = QStringLiteral(
            "**审查结论** ：该项目具有很高的工程质量，往返保真机制设计优秀，实现完善。"
            "主要优化空间在于性能优化、边缘情况覆盖和代码可维护性改进。"
            "建议优先处理性能问题和增加更多边界测试用例。\n"
            "**审查人**：AI Code Reviewer");
        const QString back = MarkdownHandler::fromHtml(MarkdownHandler::toHtml(md));
        // 空格自愈
        QVERIFY2(back.contains("**审查结论**："),
                 qPrintable("损坏空格应自愈，实际:\n" + back));
        QVERIFY2(!back.contains("**审查结论** ："),
                 qPrintable("损坏空格不应残留，实际:\n" + back));
        // 句号保留（i=j+1 吞字 bug 回归守卫）
        QVERIFY2(back.contains("边界测试用例。"),
                 qPrintable("句号不得被吞，实际:\n" + back));
        // 无占位符泄漏
        QVERIFY2(!back.contains("ZZ"),
                 qPrintable("不应有占位符泄漏，实际:\n" + back));
        // 多轮往返稳定
        const QString back2 = MarkdownHandler::fromHtml(MarkdownHandler::toHtml(back));
        QCOMPARE(back2, back);
    }

    void testEmphMarkerSpaceKeepsLegitSpaces()
    {
        // 定向归一化仅限「加粗/斜体闭合 + 空格 + 全角标点」。加粗后接合法
        // ASCII 空格（**Bold** text）或普通 CJK 空格（**加粗** 中文）是合法
        // 排版，不得被修复误伤。
        const QString md1 = QStringLiteral("**Bold** text with spaces");
        QCOMPARE(MarkdownHandler::fromHtml(MarkdownHandler::toHtml(md1)), md1);
        const QString md2 = QStringLiteral("**加粗** 中文内容带空格");
        QCOMPARE(MarkdownHandler::fromHtml(MarkdownHandler::toHtml(md2)), md2);
        const QString md3 = QStringLiteral("**审查结论**：无空格直连");
        QCOMPARE(MarkdownHandler::fromHtml(MarkdownHandler::toHtml(md3)), md3);
    }

    void testLongBoldCjkRoundTripNoPeriodLoss()
    {
        // 长加粗段落 + 后续行：此前 toMarkdown 在 `**审查结论**` 标记边界折行后，
        // 续行恰满 80 列时吞掉「用例。」的句号（i=j+1 bug），且旧内容形态下还会
        // 拦腰拆占位符 token 泄漏 "ZZBK ZZ"。修复后两者均不发生。
        const QString md = QStringLiteral(
            "**审查结论** ：很高的工程质量，往返保真机制设计优秀，实现完善。"
            "主要优化空间在于性能优化、边缘情况覆盖和代码可维护性改进。"
            "建议优先处理性能问题和增加更多边界测试用例。\n"
            "**审查人**：AI Code Reviewer");
        const QString back = MarkdownHandler::fromHtml(MarkdownHandler::toHtml(md));
        QVERIFY2(back.contains("边界测试用例。"),
                 qPrintable("句号不得被吞，实际:\n" + back));
        QVERIFY2(!back.contains("ZZBK"),
                 qPrintable("不得泄漏 ZZBK 占位符，实际:\n" + back));
        QVERIFY2(back.contains("**审查人**：AI Code Reviewer"),
                 qPrintable("后续行加粗不得被拆，实际:\n" + back));
        // 斜体同规（</em> + 空格 + 全角标点同样自愈）
        const QString mdEm = QStringLiteral("*斜体结论* ：短文本");
        const QString backEm = MarkdownHandler::fromHtml(MarkdownHandler::toHtml(mdEm));
        QVERIFY2(backEm.contains("*斜体结论*："),
                 qPrintable("斜体后全角标点空格应自愈，实际:\n" + backEm));
    }

    void testLongInlineCodeRunNoTokenSplit()
    {
        // 密集相邻行内代码（占位符长串，如文档里列出全部占位符前缀）：
        // QTextMarkdownWriter 80 列折行在长行无空格断点时，会拦腰拆占位符 token
        // 并吞掉断点字符（i=j+1 吞字 bug）。此前 `ZZCD13ZZ` 被拆成 `ZZCD 3ZZ`
        // （中间数字丢失、空格插入）。修复：行内元素占位符追加尾随空格，给出
        // 合法折行点，还原时随 token 一起消费。
        const QString md = QStringLiteral(
            "`collisionFreePlaceholder` 目前仅 6 处使用（代码块 `CB`、fromHtml 部分 `CB`、"
            "`A`、`BL` 等）。其余占位符（`ZZHR`/`ZZBQ`/`ZZMK`/`ZZFR`/`ZZSB`/`ZZSP`/"
            "`ZZCD`/`ZZIC`/`ZZTL`/`ZZFD`/`ZZNS`/`ZZST`/`ZZHS`/`ZZUL`/`ZZKD`/`ZZSM`/"
            "`ZZDV`/`ZZEQ`/`ZZLT`/`ZZGT`/`ZZLM` 等）仍以固定模式生成。");
        const QString back = MarkdownHandler::fromHtml(MarkdownHandler::toHtml(md));
        // 全部占位符前缀代码段逐字节保留
        QCOMPARE(back, md);
        QVERIFY2(!back.contains("ZZCD "),
                 qPrintable("不得出现 token 拆分残留，实际:\n" + back));
        // 多轮稳定
        QCOMPARE(MarkdownHandler::fromHtml(MarkdownHandler::toHtml(back)), back);
    }

    void testEmphWrappedInlineCodeRoundTrip()
    {
        // md4c 不把 `*`/`**` 直接包裹 code span 解析为强调（加粗丢弃、斜体在
        // code 处分段重编码）。toHtml 侧「强调包裹代码」保护绕开：直接包裹与
        // 跨句包裹均还原为统一 <strong>/<em> 包 <code> HTML，fromHtml 正确还原。
        // 含 `==`（mark 标记字面量）内容的 code 不得被当作 mark 语法——占位符保护
        // 后内容在 <code> 内，还原时原样回引号。
        const struct { const char *name; const char *md; } cases[] = {
            // 直接包裹（code 紧贴强调符）
            {"direct-bold", "文字 **`code`** 尾"},
            {"direct-em", "*`code`* 单独"},
            // code 内容含 mark 标记字面量（== 往返曾塌陷成 `code` 丢强调）
            {"em-mark-eq", "*`==`*"},
            {"strong-mark-eq", "**`==`**"},
            // 跨句包裹（强调区间内嵌 code span）
            {"span-em", "*本报告为只读审查产物，含 `src/core/FileManager.cpp` 文本*"},
            {"span-strong", "**加粗文本含 `toHtmlEscaped()` 与 `unescapeHtmlEntities` 结尾**"},
            {"span-em-multi", "*`a` 与 `b` 文本*"},
            // 混排：普通强调 + code + 强调代码
            {"mixed", "强调 `code` 与 **`粗体`** 混排"},
        };
        for (const auto &c : cases) {
            const QString md = QString::fromUtf8(c.md);
            const QString back = MarkdownHandler::fromHtml(MarkdownHandler::toHtml(md));
            QCOMPARE(back, md);
            // 多轮稳定
            QCOMPARE(MarkdownHandler::fromHtml(MarkdownHandler::toHtml(back)), back);
        }
    }

    void testEscapedEmphDelimiterNoBackslashAccumulation()
    {
        // 转义字面量 `\*\*\`\<mark\>`\*\*`（= 字面 **`<mark>`**）：此前 emphCode
        // 保护正则不校验强调符是否被反斜杠转义，把 `\*` 当真实强调符保护，还原出
        // 夹带反斜杠的 <em>/<code>，反复往返反斜杠指数累积（\*\* → \*\\* → …）。
        // 修复：开/闭强调符两侧查转义（前有奇数个 \ 即字面量），跳过保护交 md4c。
        const QString md = QStringLiteral("\\*\\*\\`\\<mark\\>\\`\\*\\*");
        QString results[4];
        results[0] = md;
        for (int round = 1; round < 4; ++round)
            results[round] = MarkdownHandler::fromHtml(MarkdownHandler::toHtml(results[round - 1]));
        // 收敛：R3 == R2（不再逐轮漂移）。首轮可能有规范级转义归一（孤单个
        // 反引号不需转义，\` 与 ` 语义等价），但不得有反斜杠累积增长。
        QCOMPARE(results[3], results[2]);
        QVERIFY2(!results[3].contains("\\\\"),
                 qPrintable("反斜杠不得累积，实际:\n" + results[3]));
    }

    void testEmphCodeSpaceBoundedRoundTrip()
    {
        // `** `code`**`（** 后空格）：md4c 按 CommonMark 边界规则判为字面
        // （** 后空格非强调符）→ 转义 \*\*；但 WYSIWYG TipTap 输入规则生成真实
        // 加粗，两条路径不一致。2026-08-28 决策：emphCode 保护去掉空格边界跳过，
        // 空格包裹的强调+代码也归一为强调（空格保留在区间内），往返逐字节一致。
        const struct { const char *name; const char *md; } cases[] = {
            {"space-after-open", "** `code`**"},
            {"space-before-close", "**`code` **"},
            {"space-both", "** `code` **"},
            {"space-entity", "** `&lt;mark&gt;`**"},
            {"space-em", "* `code`*"},
            {"space-mixed", "文字 ** `粗` 体**"},
        };
        for (const auto &c : cases) {
            const QString md = QString::fromUtf8(c.md);
            const QString back = MarkdownHandler::fromHtml(MarkdownHandler::toHtml(md));
            QCOMPARE(back, md);
            // 多轮稳定
            QCOMPARE(MarkdownHandler::fromHtml(MarkdownHandler::toHtml(back)), back);
        }
    }

    void testStrikeMarkSubSupWrappedCodeRoundTrip()
    {
        // 与 `*`/`**` 同源缺陷的其它包裹符环绕行内代码：
        // - `~~`code`~~`：md4c 直接丢弃删除线（只留 <code>），~~X `code` Y~~
        //   在 code 处分段重编码（code 丢删除线）。toHtml 侧删除线包裹代码
        //   保护还原为 <s>…<code>…</code>…</s>，往返闭环。
        // - `==`code`==` / `^`code`^`：fromHtml 侧行内代码保护必须先于 mark/sup
        //   转换执行，否则 <code> 未占位化、mark/sup 抽取后反引号丢失
        //   （==`code`== 曾塌陷成 ==code==、^`code`^ 曾塌成 ^code^）。
        // - `~`code`~`：extractSubscripts 曾把代码占位符 \x01CODE 原样存进
        //   下标备份，还原时 <sub>\x01CODE0\x01</sub> 令牌泄漏；修复后还原为
        //   <sub><code>…</code></sub>（code 内容含 < 时正确转义，往返不漂移）。
        const struct { const char *name; const char *md; } cases[] = {
            // 删除线直接包裹 code
            {"strike-direct", "~~`code`~~"},
            {"strike-mixed", "~~X `code` Y~~"},
            {"strike-multi", "文字~~`a`~~尾 ~~`b`~~"},
            // 链式删除线（code 间文本删除线，~~X~~ 无 code span，不得被保护偷配）
            {"strike-chain", "`c1`~~X~~`c2`"},
            // mark / sup / sub 包裹 code
            {"mark", "==`code`=="},
            {"sup", "^`code`^"},
            {"sub", "~`code`~"},
            // sub 内 code 含 HTML 特殊字符（< 转义后不得漂移累积 &amp;）
            {"sub-html-char", "~`a<b`~"},
            // 嵌套与混排
            // bold 内嵌 strike 包 code：md4c 链式不解析，~~ 字面残留在 strong
            // 文本中，HTML 层与转义字面 \~~ 同型（TipTap parse→serialize 抹平
            // 任何区分标记），fromHtml 无法区分来源、统一输出转义形式（转义
            // 是唯一无二义规范：本引擎与 cmark-gfm 均显示字面 ~~；裸形式在本
            // 引擎字面、GitHub 上却是删除线，与编辑器所见不一致）。项目内渲染
            // 语义不变，首轮归一后收敛——期望值在下方循环特判。
            {"bold-wrap-strike", "**bold~~`code`~~**"},
            {"strike-wrap-bold-lit", "~~`code`**bold**~~"},
            // 相邻删除线区间（~~ 相邻边界，往返须稳定）。md4c 把第二对 ~~ 的
            // 前半吃作第一区间的闭合符，X~~ 为字面残余，与转义 \~~ 同型
            // （HTML 有损消歧）→ 归一为转义形式；孤立 ~~ 在本引擎与 cmark-gfm
            // 下渲染本就一致，语义不变，round2+ 收敛（下方循环特判）
            {"strike-adjacent", "~~`code` 与 ~~X~~ 并列"},
        };
        for (const auto &c : cases) {
            const QString md = QString::fromUtf8(c.md);
            const QString back = MarkdownHandler::fromHtml(MarkdownHandler::toHtml(md));
            if (QString::fromUtf8(c.name) == QLatin1String("bold-wrap-strike")) {
                // HTML 有损消歧：~~ 字面残留与转义 \~~ 同型，归一为转义形式
                QCOMPARE(back, QStringLiteral("**bold\\~~`code`\\~~**"));
            } else if (QString::fromUtf8(c.name) == QLatin1String("strike-adjacent")) {
                // 同上：X~~ 字面残余归一为 X\~~
                QCOMPARE(back, QStringLiteral("~~`code` 与 ~~X\\~~ 并列"));
            } else {
                QCOMPARE(back, md);
            }
            // 多轮稳定
            QCOMPARE(MarkdownHandler::fromHtml(MarkdownHandler::toHtml(back)), back);
        }
    }

    void testInlineCodeSpaceAfterBareHtml()
    {
        // 裸 HTML（TipTap getHTML 路径，无 Qt pre-wrap style）：占位符尾随空格
        // 与 code 后的原空格合并，QTextDocument 折叠双空格后 restore 会吞掉
        // 唯一空格（`code` Y → `code`Y）。此路径 toHtml→fromHtml 全 HTML
        // 单测测不出（pre-wrap 保留双空格）。修复：仅当 </code> 后紧跟
        // 「非空白、非标签」字符时追加尾随空格，紧跟空白用裸占位符。
        const struct { const char *name; const char *html; const char *want; } cases[] = {
            // code 后接 ASCII 空格（最易触发）
            {"ascii-space", "<p>X <code>code</code> Y</p>", "X `code` Y"},
            // code 后接全角标点（需尾随空格防折行拆 token）
            {"fullwidth-punct", "<p>X <code>code</code>。Y</p>", "X `code`。Y"},
            // 相邻 code + 后接空格（不得在 code 间插空格）
            {"adjacent-codes", "<p>X <code>a</code><code>b</code> Y</p>", "X `a``b` Y"},
            // 段尾 code
            {"para-end", "<p>X <code>code</code></p>", "X `code`"},
        };
        for (const auto &c : cases) {
            const QString back = MarkdownHandler::fromHtml(QString::fromUtf8(c.html));
            QCOMPARE(back, QString::fromUtf8(c.want));
        }
    }

    void testInlineCodeAroundEmphMarkers()
    {
        // 行内代码紧邻强调标记（`*`code`*` / `**`code`**` 形态）：
        // md4c 不把 `*`/`**` 包裹 code span 解析为强调，往返后字面星号转义。
        // toHtml 强调包裹代码保护（见 testEmphWrappedInlineCodeRoundTrip）使该形态
        // 逐字节往返；此处同时锁定普通代码与强调代码混排不互相污染。
        const QString md = QStringLiteral(
            "*斜体 `code` 测试* 与 `plain` 相邻");
        const QString back = MarkdownHandler::fromHtml(MarkdownHandler::toHtml(md));
        QCOMPARE(back, md);
        // 多轮稳定
        QCOMPARE(MarkdownHandler::fromHtml(MarkdownHandler::toHtml(back)), back);
    }

    void testEscapedExtDelimRoundTrip()
    {
        // 转义字面扩展定界符（\^ \~ \~~）往返保真。md4c 会消费反斜杠转义
        //（\^ → 字面 ^），使转义与未转义形态在 HTML 层不可区分：
        // - \^：toHtml 后处理 ^x^→<sup> 把转义字面量重新解释为上标（语义丢失）
        // - \~：extractSubscripts 不查前导 \，把转义 ~ 当下标定界符（反斜杠
        //   残留累积 \\~，HTML 泄漏 \<sub>）
        // - \~~：md4c 解析成 ~+code+<s>~</s> 混合形态（二轮泄漏 &lt;s&gt;）
        // 修复：行级提取 \~~/\~/\^ → ZZED 占位（代码段先暂存；反斜杠 run 奇偶
        // 校验防 \\~ 误配），定界符以字面文本穿过 setMarkdown 与全部后处理，
        // 末尾统一还原；fromHtml D 步对字面 ^/~/~~ 输出转义形式（与既有
        // * / == 保护同范式），mark 步备份内容同样转义，往返逐字节一致。
        const struct { const char *name; const char *md; } cases[] = {
            // 转义 ^（上标定界符）包 code / 纯文本 / 单个
            {"esc-caret-code", "\\^`code`\\^"},
            {"esc-caret-text", "\\^text\\^"},
            {"esc-caret-single", "5\\^2 单个"},
            // 转义 ~（下标定界符）
            {"esc-tilde-code", "\\~`code`\\~"},
            {"esc-tilde-text", "\\~text\\~"},
            {"esc-tilde-single", "约 \\~5 个"},
            // 转义 ~~（删除线定界符）
            {"esc-strike-code", "\\~~`code`\\~~"},
            {"esc-strike-text", "\\~~text\\~~"},
            // 混排（三种定界符同段）
            {"mixed", "文字 \\^a\\^ 与 \\~b\\~ 与 \\~~c\\~~ 混排"},
            // 代码段内的 \~/\^ 是字面文本（code span 原样保留，不参与转义保护）
            {"code-literal-esc", "`\\~x~` 与 `\\^y^`"},
            // 转义定界符在 mark 区间内（==\^x\^==：mark 备份内容不得裸输出 ^）
            {"mark-content-esc", "==\\^x\\^=="},
            // 转义定界符在其它包裹区间内（对应各备份构造点）：
            // - strong：不占位、经 setHtml→toMarkdown，字面量由 D 步转义
            {"strong-content-esc", "**\\^x\\^**"},
            {"em-content-esc", "*\\~x\\~*"},
            // - strike：ZZST 备份内容（~~…~~）不得裸输出 ^
            {"strike-content-esc", "~~\\^x\\^~~"},
            // - sub：ZZSB 备份内容（~…~）不得裸输出 ~（重载时 [^~] 提前配对）
            {"sub-content-esc", "~a\\~b~"},
            // - sup：ZZSP 备份内容（^…^）不得裸输出 ^/~（重载时提前闭合/被拆裂）
            {"sup-content-esc", "^a\\^b^"},
            // 注：^a\~b~c^（转义+裸混写）允许首轮归一——HTML 层 <sup>a~b~c</sup>
            // 已无法区分哪个 ~ 原本转义（有损中间表示），只能统一输出转义形式
            {"sup-content-tilde", "^a\\~b~c^"},
            // 双反斜杠 + 定界符：\\^ = 字面 \ + 裸 ^，语义等价归一为 \\+\^，
            // 不得把第二个 \ 当转义符误配（反斜杠奇偶校验）
            {"double-bs-caret", "a\\\\^b"},
        };
        for (const auto &c : cases) {
            const QString md = QString::fromUtf8(c.md);
            QString results[3];
            results[0] = md;
            for (int round = 1; round < 3; ++round)
                results[round] = MarkdownHandler::fromHtml(
                    MarkdownHandler::toHtml(results[round - 1]));
            // 多数字节往返；HTML 有损消歧的形态（双反斜杠、转义+裸混写）允许
            // 首轮归一（语义等价），但必须收敛且不得累积
            const bool allowNormalize =
                QString::fromUtf8(c.name) == QLatin1String("double-bs-caret")
                || QString::fromUtf8(c.name) == QLatin1String("sup-content-tilde");
            if (allowNormalize) {
                QCOMPARE(results[2], results[1]);
                QVERIFY2(!results[2].contains("\\\\\\\\"),
                         qPrintable("反斜杠不得累积，实际:\n" + results[2]));
            } else {
                QCOMPARE(results[1], md);
            }
            QCOMPARE(results[2], results[1]);
        }
        // 对照：未转义形态（真实语法）不受转义保护影响
        const struct { const char *name; const char *md; } plain[] = {
            {"plain-sup", "^`code`^"},
            {"plain-sub", "~`code`~"},
            {"plain-strike", "~~`code`~~"},
            {"plain-mark", "==`code`=="},
        };
        for (const auto &c : plain) {
            const QString md = QString::fromUtf8(c.md);
            const QString back = MarkdownHandler::fromHtml(MarkdownHandler::toHtml(md));
            QCOMPARE(back, md);
            QCOMPARE(MarkdownHandler::fromHtml(MarkdownHandler::toHtml(back)), back);
        }
    }

    void testOrphanStrongSpaceTailRoundTrip()
    {
        // 后跟空白的孤立字面 ** 往返保真（用户报告：链式上下文中
        // **`code`** 后跟「空格+中文」，往返后 ** 被转义为 \*\*）。
        // 根因：md4c 的 flanking 规则使「后跟空白」的 ** 无法作开启符，
        // 链式配对失败后残留为 HTML 文本层字面；D 步原一律转义为 \*\*。
        // 但该形态裸输出同样稳定（重载时 md4c 同样拒绝配对→同样残留），
        // 且裸形式在本引擎/CommonMark/GitHub 渲染一致（对比 ~~：裸形式
        // GitHub 是删除线，故必须归一为 \~~；** 后空白则两态渲染等价，
        // 归一为裸形式即保真方向）。D 步改为「后跟 ASCII 空白或块尾
        // （</p>、<br 等）的字面 ** 还原裸形式」；仍走占位符——裸 **
        // 留在文本层会被 Qt toMarkdown 版本相关地转义（6.8 转义/6.4 不
        // 转义），占位符保证输出确定性。全角空格（U+3000）不是 CommonMark
        // 空白，md4c 视其后为可开启，不在豁免之列（保持转义）。
        // 注：链式上下文中 md4c 对直接包裹 code 的 ** 对会整对丢弃
        // （HTML 层无痕）——2026-08-29 起由 toHtml 后处理 wrapDiscardedEmphCode
        // 补包 <strong>（见 testChainDiscardedEmphCodeRoundTrip）；此处仅
        // 锁定「残留字面 ** 不被转义」与多轮收敛。
        const QString userLine = QString::fromUtf8(
            "`unescapeHtmlEntities`**（约 **`:306-314`**）按「先 **`&lt;/&gt;`**、"
            "最后 **`&amp;`**」顺序解码，**`toHtml`**侧代码块用 **`toHtmlEscaped()`**、"
            "fromHtml 侧用 **`unescapeHtmlEntities`** 还原，**`if`**...**`&`**... "
            "手动转序列（约 **`:2404`**）也成对。逻辑上构成了闭环，但\\*\\*是否在"
            "所有路径上「转义恰好一次」\\*\\*未被多重往返测试锁定，个别路径"
            "（如 **`==`**→**`&lt;mark&gt;`** 与实体转义之间的顺序、**`\\x01`** "
            "与占位符与实体处理的交叉）存在累积转义 / 漏转义的理论风险。**");
        {
            const QString back = MarkdownHandler::fromHtml(
                MarkdownHandler::toHtml(userLine));
            // mk 处与行尾的 ** 字节保真（不转义为 \*\*）
            QVERIFY2(back.contains(QStringLiteral("**`&lt;mark&gt;`** 与实体转义")),
                     qPrintable("mk 处 ** 应保真，实际:\n" + back));
            QVERIFY2(back.endsWith(QStringLiteral("风险。**")),
                     qPrintable("行尾 ** 应保真，实际:\n" + back));
            // 转义来源（后跟非空白）不受影响，仍保真 \*\*
            QVERIFY(back.contains(QStringLiteral("但\\*\\*是否")));
            // 多轮收敛
            const QString back2 = MarkdownHandler::fromHtml(
                MarkdownHandler::toHtml(back));
            QCOMPARE(back2, back);
        }
        // 直接包裹形态的字面保真
        const struct { const char *name; const char *md; const char *expect; } cases[] = {
            // 孤立 ** 后跟空白：裸形式保真
            {"orphan-space-tail", "x** y 与 z", "x** y 与 z"},
            // 段尾（</p> 块边界，等效行尾）
            {"orphan-block-tail", "风险。**", "风险。**"},
            // 转义来源、后跟空白：归一为裸形式（渲染等价，与 \~~ 归一方向
            // 相反的理由见函数头注释），一轮归一后收敛
            {"escape-space-normalize", "但\\*\\* 是否转义", "但** 是否转义"},
        };
        for (const auto &c : cases) {
            const QString md = QString::fromUtf8(c.md);
            const QString back = MarkdownHandler::fromHtml(MarkdownHandler::toHtml(md));
            QCOMPARE(back, QString::fromUtf8(c.expect));
            // 多轮收敛
            QCOMPARE(MarkdownHandler::fromHtml(MarkdownHandler::toHtml(back)), back);
        }
        // 回归：转义来源后跟非空白，仍转义保真（不被豁免误伤）
        {
            const QString md = QStringLiteral("但\\*\\*是否转义");
            const QString back = MarkdownHandler::fromHtml(MarkdownHandler::toHtml(md));
            QCOMPARE(back, md);
        }
        // 回归：全角空格不是 CommonMark 空白，** 后跟全角空格仍转义（保守）
        {
            const QString md = QString::fromUtf8("x**　y");
            const QString back = MarkdownHandler::fromHtml(MarkdownHandler::toHtml(md));
            QCOMPARE(back, QString::fromUtf8("x\\*\\*　y"));
        }
        // 已修复（2026-08-29）：链式上下文中 md4c 对直接包裹 code 的 ** 对整对
        // 丢弃，由 toHtml 后处理 wrapDiscardedEmphCode 补包——此原「已知限制」
        // 用例升级为保真断言（详见 testChainDiscardedEmphCodeRoundTrip）
        {
            const QString md = QStringLiteral("前 **`a`** 中 **`b`** 后");
            const QString back = MarkdownHandler::fromHtml(MarkdownHandler::toHtml(md));
            QCOMPARE(back, md);
            QCOMPARE(MarkdownHandler::fromHtml(MarkdownHandler::toHtml(back)), back);
        }
    }

    void testChainDiscardedEmphCodeRoundTrip()
    {
        // 链式级联中 md4c 对「直接包裹 code 的 **/* 对」整对丢弃（HTML 无 strong、
        // 无字面 * 残留，信息在 setMarkdown 层已丢，fromHtml 无法恢复）。修复：
        // toHtml 后处理 wrapDiscardedEmphCode——MD 侧（processed）候选 = code 两侧
        // 紧贴等长 run 的 **（strong）/*（em，run>=3 不判）；html 侧验证对应 mono
        // span 两侧既非强调边界（</span>/<span，征用形态）也无字面 *（字面形态），
        // 双条件交集才补包 <strong>/<em>。数量/内容核对失败 → 整体放弃（fail-safe）。

        // ① 用户报告原句（整对丢弃 + 征用 + 字面 + 扁平化 strong 混合并存）：整行逐字节保真
        {
            const QString md = QString::fromUtf8(
                "`unescapeHtmlEntities`**（约 **`:306-314`**）按「先 **`&lt;/&gt;`**、"
                "最后 **`&amp;`**」顺序解码，**`toHtml`**侧代码块用 **`toHtmlEscaped()`**、"
                "fromHtml 侧用 **`unescapeHtmlEntities`** 还原，**`if`**...**`&`**... "
                "手动转序列（约 **`:2404`**）也成对。逻辑上构成了闭环，但\\*\\*是否在"
                "所有路径上「转义恰好一次」\\*\\*未被多重往返测试锁定，个别路径"
                "（如 **`==`**→**`&lt;mark&gt;`** 与实体转义之间的顺序、**`\\x01`** "
                "与占位符与实体处理的交叉）存在累积转义 / 漏转义的理论风险。**");
            const QString html = MarkdownHandler::toHtml(md);
            // 整对丢弃的铁证修复：\x01 的 code 在 html 中被补包为 strong+code
            QVERIFY2(html.contains(QStringLiteral("<strong><code>\\x01</code></strong>")),
                     qPrintable("\\x01 应被补包 <strong><code>，实际:\n" + html));
            const QString back = MarkdownHandler::fromHtml(html);
            QCOMPARE(back, md);
            QCOMPARE(MarkdownHandler::fromHtml(MarkdownHandler::toHtml(back)), back);
        }
        // ② 链中最小复现：b 与 \x01 的 ** 对补救；孤立 **（后跟标点）归一 \*\* 后收敛
        {
            const QString md = QStringLiteral("`a`**（约 **`b`**）也 、 **`\\x01`** 与 Y");
            const QString back = MarkdownHandler::fromHtml(MarkdownHandler::toHtml(md));
            QVERIFY2(back.contains(QStringLiteral("**`b`**")),
                     qPrintable("链中 b 的 ** 对应恢复，实际:\n" + back));
            QVERIFY2(back.contains(QStringLiteral("**`\\x01`**")),
                     qPrintable("链中 \\x01 的 ** 对应恢复，实际:\n" + back));
            QVERIFY2(back.contains(QStringLiteral("`a`\\*\\*（约")),
                     qPrintable("孤立 **（后跟标点）归一转义形态，实际:\n" + back));
            const QString back2 = MarkdownHandler::fromHtml(MarkdownHandler::toHtml(back));
            QCOMPARE(back2, back);
        }
        // ③ em 链版本：单星整对丢弃同样补救
        {
            const QString md = QStringLiteral("`a`*（约 *`b`*）也 、 *`\\x01`* 与 Y");
            const QString back = MarkdownHandler::fromHtml(MarkdownHandler::toHtml(md));
            QVERIFY2(back.contains(QStringLiteral("*`b`*")),
                     qPrintable("链中 b 的 * 对应恢复，实际:\n" + back));
            QVERIFY2(back.contains(QStringLiteral("*`\\x01`*")),
                     qPrintable("链中 \\x01 的 * 对应恢复，实际:\n" + back));
            QCOMPARE(MarkdownHandler::fromHtml(MarkdownHandler::toHtml(back)), back);
        }
        // ④ 征用哨兵：纯文本 strong 链夹心 code（html 两侧为 </strong>/<strong>
        //    边界）——md4c 已正确配对，不得误包导致双重强调
        {
            const QString md = QStringLiteral("`c0`**约，**`c1`**按，**`c2`**别，**`c3`**看，**`c4`");
            const QString back = MarkdownHandler::fromHtml(MarkdownHandler::toHtml(md));
            QCOMPARE(back, md);
            QCOMPARE(MarkdownHandler::fromHtml(MarkdownHandler::toHtml(back)), back);
        }
        // ⑤ 字面哨兵：closer 无 opener 的 ** 残留字面（html 有字面 *），不补包
        {
            const QString md = QString::fromUtf8("→**`mk`** 与");
            const QString back = MarkdownHandler::fromHtml(MarkdownHandler::toHtml(md));
            QCOMPARE(back, md);
        }
        // ⑥ 无标记 code：MD 侧无候选，零干预
        {
            const QString md = QString::fromUtf8("手写 unescapeHtmlEntities 与 `plain` 混排");
            const QString back = MarkdownHandler::fromHtml(MarkdownHandler::toHtml(md));
            QCOMPARE(back, md);
        }
        // ⑦ 表格共存：表格（占位符）内 code 不计入核对，链部分仍获补救
        {
            const QString md = QStringLiteral("`a`**（约 **`b`**）也 、 **`\\x01`** 与 Y\n\n"
                                              "| 列A | 列B |\n|----|----|\n| `x` | **y** |");
            const QString back = MarkdownHandler::fromHtml(MarkdownHandler::toHtml(md));
            QVERIFY2(back.contains(QStringLiteral("**`\\x01`**")),
                     qPrintable("表格共存时链中 \\x01 仍应补救，实际:\n" + back));
            QVERIFY(back.contains(QStringLiteral("| `x` |")));
            QCOMPARE(MarkdownHandler::fromHtml(MarkdownHandler::toHtml(back)), back);
        }
        // ⑧ fail-safe：双反引号 run 使 MD/html span 数量核对失败 → 整体放弃补救
        //    （回到修复前行为），不产生错误输出且多轮收敛
        {
            const QString md = QStringLiteral("`a`**（约 **``x`y``**）也 、 **`\\x01`** 与 Y");
            const QString back = MarkdownHandler::fromHtml(MarkdownHandler::toHtml(md));
            QVERIFY(!back.contains(QStringLiteral("**`\\x01`**")));
            QCOMPARE(MarkdownHandler::fromHtml(MarkdownHandler::toHtml(back)), back);
        }
        // ⑨ 无空格直接包裹（md4c 同样整对丢弃，html 两侧皆裸）：补包保真
        {
            const QString md = QStringLiteral("text**`c`**text");
            const QString back = MarkdownHandler::fromHtml(MarkdownHandler::toHtml(md));
            QCOMPARE(back, md);
        }
        // ⑩ run>=3（***）不判：嵌套形态保持现状（不误包 <strong><em> 组合）
        {
            const QString md = QStringLiteral("`a`***（约 ***`b`***）也 、 ***`\\x01`*** 与 Y");
            const QString back = MarkdownHandler::fromHtml(MarkdownHandler::toHtml(md));
            QCOMPARE(MarkdownHandler::fromHtml(MarkdownHandler::toHtml(back)), back);
        }
        // ⑪ 真实链路 seam：TipTap getHTML 把相邻 strong 合并为单个 strong 内嵌
        //     code（ProseMirror 同 mark 相邻节点合并，vitest 已锁定 JS 侧形态）。
        //     fromHtml 对该形态输出「大 strong 含 code」——与扁平化三段视觉等价
        //     （code 在 strong 内本就不加粗渲染），多轮收敛；补包形态本身逐字节还原。
        {
            const QString merged = QStringLiteral("<p><strong>约，<code>c1</code>按，</strong></p>");
            const QString md = MarkdownHandler::fromHtml(merged);
            QCOMPARE(md, QStringLiteral("**约，`c1`按，**"));
            QCOMPARE(MarkdownHandler::fromHtml(MarkdownHandler::toHtml(md)), md);
            QCOMPARE(MarkdownHandler::fromHtml(
                         QStringLiteral("<p>、<strong><code>\\x01</code></strong> 与</p>")),
                     QStringLiteral("、**`\\x01`** 与"));
        }
    }

    void testMassInlineCodeRoundTrip()
    {
        // 海量行内代码（→ 海量 ZZ 占位符）不得崩溃：fromHtml 逆序还原曾把
        // 全部占位符拼成一个巨型多选正则（ZZCD0ZZ|ZZCD1ZZ|…），约 2800+ 个
        // 占位符时 PCRE2 编译超限 → invalid QRegularExpression → doMatch
        // qFatal abort（进程直接崩溃）。大技术文档（数千行内代码）真实可触发。
        // 修复：通用模式 ZZ[A-Z]+[0-9]+ZZ 单遍匹配 + 哈希查表；未命中的
        // 用户字面 ZZ 形态文本保留原文（碰撞检测保证真占位符不与用户文本
        // 同串，通用模式多匹配到的必然是用户字面，须原样保留）。
        QString md;
        md.reserve(5000 * 40);
        for (int i = 0; i < 5000; ++i) {
            md += QStringLiteral("段%1 含 `code%1` 行内代码。").arg(i);
            if (i + 1 < 5000)
                md += QStringLiteral("\n\n");
        }
        // 崩溃发生在 fromHtml（toHtml 正常完成）；进程 abort 即测试失败
        const QString back = MarkdownHandler::fromHtml(MarkdownHandler::toHtml(md));
        QCOMPARE(back, md);
        // 防误删回归：用户字面 ZZ 形态文本不被通用模式匹配吞掉
        const QString tricky = QStringLiteral("字面 ZZCD99999ZZ 与 `真code` 混排");
        QCOMPARE(MarkdownHandler::fromHtml(MarkdownHandler::toHtml(tricky)), tricky);
    }

    void testLargeDocumentRoundTripBenchmark()
    {
        // 大文档（约 10k 行）往返计时基准：此前项目无任何性能基准，
        // 一切性能讨论无数据支撑。本测试建立基线并设宽松上限防大幅退化。
        // 文档混合标题/段落/行内格式/列表/代码块，覆盖主要占位符管线。
#ifndef QT_NO_DEBUG
        // Debug 构建的 QString/正则开销数倍于 Release，计时结果无意义且会误触上限
        QSKIP("计时基准仅 Release 构建运行");
#endif
        const int N = 900;  // 每轮约 11 行 → 约 10k 行
        QString md;
        md.reserve(N * 400);
        for (int i = 0; i < N; ++i) {
            md += QStringLiteral("## 章节 %1\n").arg(i);
            md += QStringLiteral("\n");
            md += QStringLiteral("这是第 %1 段正文，含 **加粗**、*斜体* 与 "
                                 "`行内代码`，以及足够长度用于触发 80 列折行"
                                 "的词汇序列 甲 乙 丙 丁 戊 己 庚 辛。\n").arg(i);
            md += QStringLiteral("\n");
            md += QStringLiteral("- 列表项 %1-A\n- 列表项 %1-B\n- 列表项 %1-C\n").arg(i);
            md += QStringLiteral("\n");
            md += QStringLiteral("```cpp\nint f%1() { return %1; }\n```\n").arg(i);
            md += QStringLiteral("\n");
        }

        QElapsedTimer timer;
        timer.start();
        const QString html = MarkdownHandler::toHtml(md);
        const qint64 toHtmlMs = timer.elapsed();
        const QString back = MarkdownHandler::fromHtml(html);
        const qint64 totalMs = timer.elapsed();
        qDebug() << "基准: ~10k 行文档 toHtml" << toHtmlMs
                 << "ms, fromHtml" << (totalMs - toHtmlMs)
                 << "ms, 往返合计" << totalMs << "ms";

        // 正确性守卫：标题与行内代码数量不得在往返中丢失
        QCOMPARE(back.count(QStringLiteral("## 章节")), N);
        QCOMPARE(back.count(QStringLiteral("`行内代码`")), N);
        // 宽松上限防大幅退化（数量级回归才失败）。
        // 基线（2026-08-27，本地 Release，Qt 6.8.2，Debian 13）：
        //   修复前 toHtml ≈ 11.3s，fromHtml ≈ 17s，往返合计 ≈ 28.4s
        //   （根因：逐次 match(s, offset)/replace 的 O(K·n) 模式 + 每行现构造正则）
        //   修复后 toHtml ≈ 253ms，fromHtml ≈ 254ms，往返合计 ≈ 507ms
        // 上限取修复后基线 ~10 倍余量：正常 CI 差异远低于此，而任何 O(K·n)
        // 模式回归（回到秒级）都会被捕获。
        QVERIFY2(totalMs < 10000,
                 qPrintable(QStringLiteral("10k 行往返超时: %1 ms").arg(totalMs)));
    }

    void testFromHtmlHotpathBenchmark()
    {
        // 审查 P1-6 热点用例的防回归基准（2026-09-13 阶段 2 修复）。
        // 修复前（while(true){ re.match(s,offset); s.replace(...) } 的 O(K·n)
        // 模式，18 处 + isInsideGeneralList/span 平衡扫描）：
        //   3000 <span style> → fromHtml 9.3s（span 提取独占 5.2s）
        //   4000 行表格 → fromHtml 16.3s（同一 <table> 被完整扫描两遍）
        //   4000 行内公式 → fromHtml 3.7s
        // 修复后（globalMatch 单遍重建 + span 栈配对预计算 + 表格单趟合并）：
        //   76ms / 276ms / 64ms（表格已低于纯 Qt setHtml+toMarkdown 的 341ms
        //   基线，后处理成本不再是瓶颈）。
        // 上限取修复后基线 5-10 倍余量：正常 CI 差异远低于此，任何 O(K·n)
        // 回归（回到秒级）都会被捕获。
#ifndef QT_NO_DEBUG
        // Debug 构建的 QString/正则开销数倍于 Release，计时结果无意义且会误触上限
        QSKIP("计时基准仅 Release 构建运行");
#endif
        QElapsedTimer timer;

        // 用例 1：3000 个 <span style>（step N 的 span 提取）
        QString spanHtml;
        spanHtml.reserve(3000 * 90);
        for (int i = 0; i < 3000; ++i)
            spanHtml += QStringLiteral(
                "<p>前文 %1 <span style=\"color:red\">红字 %1</span> 尾文</p>\n").arg(i);
        timer.start();
        const QString spanMd = MarkdownHandler::fromHtml(spanHtml);
        const qint64 spanMs = timer.elapsed();
        QCOMPARE(spanMd.count(QStringLiteral("<span style=")), 3000);
        QVERIFY2(spanMs < 1000,
                 qPrintable(QStringLiteral("3000-span fromHtml 超时: %1 ms").arg(spanMs)));

        // 用例 2：4000 行表格（对齐提取 + 空单元格防护 + 合并行提取）
        QString tableHtml = QStringLiteral("<table><tr><th>H1</th><th>H2</th></tr>");
        tableHtml.reserve(4000 * 40);
        for (int i = 0; i < 4000; ++i)
            tableHtml += QStringLiteral("<tr><td>a%1</td><td>b%1</td></tr>").arg(i);
        tableHtml += QStringLiteral("</table>");
        timer.start();
        const QString tableMd = MarkdownHandler::fromHtml(tableHtml);
        const qint64 tableMs = timer.elapsed();
        QCOMPARE(tableMd.count(QStringLiteral("| a")), 4000);
        QVERIFY(tableMd.contains(QStringLiteral("| a0 | b0 |")));
        QVERIFY(tableMd.contains(QStringLiteral("| a3999 | b3999 |")));
        QVERIFY2(tableMs < 2000,
                 qPrintable(QStringLiteral("4000-row-table fromHtml 超时: %1 ms").arg(tableMs)));

        // 用例 3：4000 个行内公式（data-latex 提取 + 实体解码）
        QString mathHtml;
        mathHtml.reserve(4000 * 90);
        for (int i = 0; i < 4000; ++i)
            mathHtml += QStringLiteral(
                "<p>前 %1 <span data-type=\"inline-math\" data-latex=\"x_%1\"></span> 后</p>\n").arg(i);
        timer.start();
        const QString mathMd = MarkdownHandler::fromHtml(mathHtml);
        const qint64 mathMs = timer.elapsed();
        QCOMPARE(mathMd.count(QStringLiteral("$x_")), 4000);
        QVERIFY2(mathMs < 800,
                 qPrintable(QStringLiteral("4000-inline-math fromHtml 超时: %1 ms").arg(mathMs)));

        qDebug() << "热点基准: span" << spanMs << "ms, table" << tableMs
                 << "ms, math" << mathMs << "ms";
    }

    void testInlineCodeTagNotDoubleEscaped()
    {
        // 行内代码内的字面标签（`<hr>`）是 code span 文本，不是 HTML 标签：
        // escapeUnclosedHtmlTags 不得转义——md4c 按 CommonMark 不解码 code 内
        // 实体，转义产物 `&lt;hr&gt;` 直通显示为字面文本。帮助文档语法指南
        // 受此影响：「分割线：`<hr>` 与 `---` 等价」显示成了 `&lt;hr&gt;`。
        // 断言双重转义标志（&amp;lt;）不出现；code 内容以单次转义形态存在。
        const QString md = QStringLiteral(
            "分割线：`<hr>` 与 `---` 等价，`<sub>`/`<sup>`/`<mark>` 同理。");
        const QString html = MarkdownHandler::toHtml(md);
        QVERIFY2(!html.contains("&amp;lt;") && !html.contains("&amp;gt;"),
                 qPrintable(QString("行内代码内标签不应双重转义，HTML[%1]").arg(html)));
        QVERIFY2(html.contains("&lt;hr&gt;") && html.contains("&lt;sub&gt;"),
                 qPrintable(QString("code 内容应单次转义存在，HTML[%1]").arg(html)));
        // 往返保真（code span 内容原样回）
        QCOMPARE(MarkdownHandler::fromHtml(html), md);
    }

    // ===== 阶段 1 往返保真修复 =====

    void testEmptyAltImageRoundTrip()
    {
        // `![](path)`（空 alt）曾整条被删：Qt setMarkdown 对空 alt 图片不产出
        // <img>，打开即看不见图、保存后原语法被写成 &nbsp;。修复为「临时补
        // 占位 alt 交给 setMarkdown，产出后清空 alt」。
        const QStringList cases = {
            QStringLiteral("![](a.png)"),
            QStringLiteral("![](images/a.png)"),
            QStringLiteral("![](a.png \"t\")"),
            QStringLiteral("正文 ![](a.png) 正文"),
            QStringLiteral("> ![](a.png)"),
        };
        for (const QString &md : cases) {
            const QString html = MarkdownHandler::toHtml(md);
            QVERIFY2(html.contains(QLatin1String("<img")),
                     qPrintable(QString("图片不应被丢弃，MD=[%1] HTML=[%2]").arg(md, html)));
            // 占位 alt 不得泄漏到编辑器内容
            QVERIFY2(!html.contains(QLatin1String("ZZIA")),
                     qPrintable(QString("alt 占位符泄漏，HTML=[%1]").arg(html)));
            QCOMPARE(e2eRoundTrip(md), md);
        }
        // 多张相邻图片：块间空行被折叠是既有归一化（非空 alt 对照同样如此），
        // 此处只守「两张都在、无实体残留、收敛」——旧实现是整段变 &nbsp;
        {
            const QString md = QStringLiteral("![](a.png)\n\n![](b.png)");
            const QString e2e = e2eRoundTrip(md);
            QVERIFY2(e2e.contains(QLatin1String("![](a.png)"))
                         && e2e.contains(QLatin1String("![](b.png)")),
                     qPrintable(QString("两张图片都应保留，往返=[%1]").arg(e2e)));
            QCOMPARE(e2eRoundTrip(e2e), e2e);
            // 对照：非空 alt 的相邻两图同样折叠块间空行（既有行为，非本次引入）
            QCOMPARE(e2eRoundTrip(QStringLiteral("![x](a.png)\n\n![y](b.png)")),
                     QStringLiteral("![x](a.png)![y](b.png)"));
        }
        // 对照：非空 alt 既有行为不回归
        QCOMPARE(e2eRoundTrip(QStringLiteral("![x](a.png)")), QStringLiteral("![x](a.png)"));
        // 行内代码内的字面 ![](x) 不是图片，不得改写
        QCOMPARE(e2eRoundTrip(QStringLiteral("写法：`![](a.png)`")),
                 QStringLiteral("写法：`![](a.png)`"));
    }

    void testHeadingEmphasisRoundTrip()
    {
        // 标题内强调曾静默丢失：Qt toMarkdown 不输出 <hN> 内的强调标记
        //（`# a **c**` → `# a c`），且 toHtml 兜底正则会把多 span 标题吞成
        // 悬空 </span> 的畸形 HTML。
        const QStringList cases = {
            QStringLiteral("# **c**"),
            QStringLiteral("# a **c**"),
            QStringLiteral("# **c** b"),
            QStringLiteral("# **a** x **b**"),
            QStringLiteral("# 标题**粗**"),
            QStringLiteral("# 标**粗**题"),
            QStringLiteral("## a **c**"),
            QStringLiteral("# a *c*"),
            QStringLiteral("# a **b** *c* d"),
            QStringLiteral("### a *i* b **b** 尾"),
        };
        for (const QString &md : cases) {
            const QString html = MarkdownHandler::toHtml(md);
            // 畸形 HTML 回归：旧兜底正则在多内联段标题上产出 `a </span><span …>`
            QVERIFY2(!html.contains(QLatin1String("</span><span")),
                     qPrintable(QString("标题 HTML 不应含悬空 </span>，HTML=[%1]").arg(html)));
            QCOMPARE(e2eRoundTrip(md), md);
        }
    }

    void testMathAttributeEscapingRoundTrip()
    {
        // data-latex 属性转义（toHtml）+ 实体解码（fromHtml）：
        //   - latex 含 " 曾提前终结属性，公式被截断（$$\text{"hi"}$$ → 只剩 \text{）
        //   - latex 含 & / < 曾逐轮膨胀（&amp; → &amp;amp;，对齐环境 & 是列分隔符）
        // expect 为既有归一化的目标形态（与本次修复无关，一并钉住防漂移）：
        //   - 单行块级公式 $$…$$ 规范化为多行形态（blockMathLatexToMarkdown 恒多行）
        //   - `\&` 在 toHtml 侧（经 setMarkdown）本就归一化为 `&`
        const struct { const char *md; const char *expect; } cases[] = {
            {"$\\text{\"hi\"}$",                "$\\text{\"hi\"}$"},
            {"$$\\text{\"hi\"}$$",              "$$\n\\text{\"hi\"}\n$$"},
            {"$a < b$",                         "$a < b$"},
            {"$a \\& b$",                       "$a & b$"},
            {"$$\n\\begin{aligned}\na &= b \\\\\nc &= d\n\\end{aligned}\n$$",
             "$$\n\\begin{aligned}\na &= b \\\\\nc &= d\n\\end{aligned}\n$$"},
            {"$$\n\\begin{matrix}\na & b \\\\\nc & d\n\\end{matrix}\n$$",
             "$$\n\\begin{matrix}\na & b \\\\\nc & d\n\\end{matrix}\n$$"},
        };
        for (const auto &c : cases) {
            const QString md = QString::fromUtf8(c.md);
            const QString e2e = e2eRoundTrip(md);
            QVERIFY2(!e2e.contains(QLatin1String("&amp;"))
                         && !e2e.contains(QLatin1String("&lt;"))
                         && !e2e.contains(QLatin1String("&gt;"))
                         && !e2e.contains(QLatin1String("&quot;")),
                     qPrintable(QString("公式不应残留 HTML 实体，MD=[%1] 往返=[%2]").arg(md, e2e)));
            QCOMPARE(e2e, QString::fromUtf8(c.expect));
            // 收敛：再走一轮不得继续变化（旧实现的 &amp; 会逐轮膨胀）
            QCOMPARE(e2eRoundTrip(e2e), e2e);
        }
        // 转义本身：属性值应带实体而非裸符
        const QString html = MarkdownHandler::toHtml(
            QStringLiteral("$$\n\\begin{aligned}\na &= b\n\\end{aligned}\n$$"));
        QVERIFY2(html.contains(QLatin1String("&amp;=")),
                 qPrintable(QString("data-latex 应做属性转义，HTML=[%1]").arg(html)));
    }

    void testListImageRoundTrip()
    {
        // 列表项内的图片曾保存即丢：编辑器块级 Image 落在 li 内时产
        // <li><p>前</p><img><p>后</p></li>，fromHtml 的列表序列化
        //（taskItemInlineText）把 <img> 随所有标签一并剥离。
        // 另：块级图片不能做 li 的首子元素——ProseMirror 无处安放会把整张图
        // **弹出列表**（真实链路实测 <li><img></li> → <li><p></p></li></ul><img>），
        // 故 toHtml 侧补空 <p> 挡在图片前（TipTap 稳定形态），fromHtml 侧把
        // 图片转回 ![alt](src) 并合并为单行（续行会被 Qt 解析成列表外段落）。
        const QStringList cases = {
            QStringLiteral("- ![x](a.png)"),
            QStringLiteral("- ![](a.png)"),
            QStringLiteral("- 前 ![x](a.png) 后"),
            QStringLiteral("- [ ] ![x](a.png)"),
            QStringLiteral("- a\n  - ![x](b.png)"),
            QStringLiteral("1. ![x](a.png)"),
            QStringLiteral("- 普通项\n- ![x](c.png)\n- 尾项"),
        };
        for (const QString &md : cases) {
            const QString html = MarkdownHandler::toHtml(md);
            QVERIFY2(html.contains(QLatin1String("<img")),
                     qPrintable(QString("列表项内图片不应被丢弃，MD=[%1] HTML=[%2]").arg(md, html)));
            // 图片作首子元素时须先补空 <p>（否则 TipTap 把图弹出列表）
            QVERIFY2(!html.contains(QLatin1String("<li><img")),
                     qPrintable(QString("首子元素图片前应补 <p></p>，HTML=[%1]").arg(html)));
            QCOMPARE(MarkdownHandler::fromHtml(html), md);
        }
        // 常规列表（无图）不因该步引入多余空段落
        const QString plain = MarkdownHandler::toHtml(QStringLiteral("- a\n- b"));
        QVERIFY2(!plain.contains(QLatin1String("<p></p><img"))
                     && plain.contains(QLatin1String("<li")),
                 qPrintable(QString("无图列表不应被改动，HTML=[%1]").arg(plain)));
        QCOMPARE(MarkdownHandler::fromHtml(plain), QStringLiteral("- a\n- b"));
    }

    void testLegacyEntityEscapedHeals()
    {
        // 历史损坏文件的修复：6.8.2 及以前 fromHtml 不解属性实体，把 aligned 的
        // 对齐列 `&` 以字面 `&amp;` 写进了 .md。载入时若不解码，toHtml 会把
        // `&amp;` 再转义成 `&amp;amp;`，编辑器只解一层 → 公式显示成字面
        // 「x&amp;= a + b」，且成为稳定不动点、永不自愈（用户实测）。
        // 期望：载入即显示正确的 `&`，下次保存回写干净的 `&`（一次性修复）。
        const QString legacy = QStringLiteral(
            "$$\n\\begin{aligned}\nx &amp;= a + b \\\\\ny &amp;= c + d\n\\end{aligned}\n$$");
        const QString html = MarkdownHandler::toHtml(legacy);
        // 属性值恰好一层转义：DOM 解码后即干净的 &
        QVERIFY2(html.contains(QLatin1String("x &amp;= a + b")),
                 qPrintable(QString("载入应解一层实体，HTML=[%1]").arg(html)));
        QVERIFY2(!html.contains(QLatin1String("&amp;amp;")),
                 qPrintable(QString("不得二次转义，HTML=[%1]").arg(html)));
        // 保存回写干净实体（文件自愈），且此后收敛
        const QString healed = QStringLiteral(
            "$$\n\\begin{aligned}\nx &= a + b \\\\\ny &= c + d\n\\end{aligned}\n$$");
        QCOMPARE(MarkdownHandler::fromHtml(html), healed);
        QCOMPARE(e2eRoundTrip(healed), healed);
        // 干净源不受影响（解码对无实体文本是恒等变换）
        QCOMPARE(MarkdownHandler::toHtml(healed), html);

        // 同类历史损坏：围栏信息串 / 脚注 id（旧版写坏 → 载入解码 → 保存自愈）
        const struct { const char *legacy; const char *healed; } others[] = {
            {"```c&amp;x\ncode\n```",          "```c&x\ncode\n```"},
            {"```c&quot;x\ncode\n```",         "```c\"x\ncode\n```"},
            {"文字[^a&amp;b]\n\n[^a&amp;b]: 定义", "文字[^a&b]\n\n[^a&b]: 定义"},
            {"$a &amp; b$",                     "$a & b$"},
        };
        for (const auto &c : others) {
            const QString load = MarkdownHandler::toHtml(QString::fromUtf8(c.legacy));
            QVERIFY2(!load.contains(QLatin1String("&amp;amp;")),
                     qPrintable(QString("载入不得二次转义，HTML=[%1]").arg(load)));
            QCOMPARE(MarkdownHandler::fromHtml(load), QString::fromUtf8(c.healed));
            QCOMPARE(e2eRoundTrip(QString::fromUtf8(c.healed)), QString::fromUtf8(c.healed));
        }
    }

    void testFootnoteIdAndCodeLangEscaping()
    {
        // 脚注 id / 围栏信息串同属「拼进 HTML 属性」的位置，缺转义时 id 被截断
        //（[^a"b] 往返成 [^a]）、语言丢失。
        QCOMPARE(e2eRoundTrip(QStringLiteral("文字[^a\"b]\n\n[^a\"b]: 定义")),
                 QStringLiteral("文字[^a\"b]\n\n[^a\"b]: 定义"));
        QCOMPARE(e2eRoundTrip(QStringLiteral("```c\"x\ncode\n```")),
                 QStringLiteral("```c\"x\ncode\n```"));
    }

    // ==================== 占位符家族契约断言 ====================
    // 依据 docs/placeholder-families.md（§2 toHtml 19 家族 / §3 fromHtml 37 家族
    // / §5 哨兵与标记）。断言形式按家族性质分形（见清单 §8 设计原则）：
    // 内容备份型断言还原产物；边界标记型断言结构语义；转义保护型以负向断言为主；
    // 收敛容忍型不断言字节级形态。所有用例输入不含字面 ZZ 形态文本，故
    // 「token 零泄漏」成立即本家族占位符铸造→还原闭环成立。

    void testContractToHtmlZZCB_BQ()
    {
        // ZZCB（:2404→:3160）：围栏代码块 → <pre><code>，语言入 class
        const QString cb = MarkdownHandler::toHtml(QStringLiteral("```js\n代码乙\n```"));
        QVERIFY2(cb.contains(QStringLiteral("<pre")) && cb.contains(QStringLiteral("代码乙"))
                 && cb.contains(QStringLiteral("language-js")),
                 qPrintable(QString("ZZCB 应产出代码块，HTML=[%1]").arg(cb)));
        assertNoPlaceholderToken(cb, "ZZCB");
        // ZZBQ（toHtml :2493→:3165）：引用内围栏 → <blockquote> 内 <pre>。
        // 注：自 8b55a63 起该铸造路径不可达——含围栏引用经 protectComplexQuoteBlocks
        // (ZZDV) 整段预抽取（清单 §2 注 ①）；本断言作为行为回归保留，不锁定 BQ 家族链
        const QString bq = MarkdownHandler::toHtml(QStringLiteral("> ```\n> 代码甲\n> ```"));
        QVERIFY2(bq.contains(QStringLiteral("<blockquote>")) && bq.contains(QStringLiteral("<pre"))
                 && bq.contains(QStringLiteral("代码甲")),
                 qPrintable(QString("ZZBQ 应产出引用内代码块，HTML=[%1]").arg(bq)));
        assertNoPlaceholderToken(bq, "ZZBQ");
        // ZZBQ（fromHtml :5027 侧）：引用往返，`> ` 前缀化还原
        const QString bqBack = MarkdownHandler::fromHtml(
            MarkdownHandler::toHtml(QStringLiteral("> 引用甲")));
        QVERIFY2(bqBack.contains(QStringLiteral("> 引用甲")),
                 qPrintable(QString("ZZBQ(fromHtml) 应还原引用，实际=[%1]").arg(bqBack)));
        assertNoPlaceholderToken(bqBack, "ZZBQ-fromHtml");
    }

    void testContractToHtmlZZBQSEP()
    {
        // ZZBQSEP（:2501→:3359-3367）：边界标记型——无备份内容，其「还原」是
        // <p>token</p> 整段移除，语义效果 = 打破 Qt 对相邻引用/段落的合并。
        // 注：自 8b55a63 起该铸造路径不可达（引用经 ZZDV 整段预抽取，清单 §2
        // 注 ①）；断言经 DV 路径成立，作为行为回归保留，不锁定 BQSEP 家族链。
        // 断言的是结构：后续段落必须在 </blockquote> 之外，且 token 不残留。
        const QString html = MarkdownHandler::toHtml(
            QStringLiteral("> ```\n> 码\n> ```\n\n后续段落"));
        QVERIFY2(html.contains(QStringLiteral("</blockquote>")),
                 qPrintable(QString("应存在引用闭合，HTML=[%1]").arg(html)));
        const int close = html.indexOf(QStringLiteral("</blockquote>"));
        const int after = html.indexOf(QStringLiteral("后续段落"));
        QVERIFY2(after > close,
                 qPrintable(QString("后续段落应在引用之外：close=%1 after=%2 HTML=[%3]")
                                .arg(close).arg(after).arg(html)));
        assertNoPlaceholderToken(html, "ZZBQSEP");
    }

    void testContractToHtmlZZHR_LB()
    {
        // ZZHR（:2777→:3170）：水平线占位防 setext 误解析 → <hr/>
        const QString hr = MarkdownHandler::toHtml(QStringLiteral("前段\n\n---\n\n后段"));
        QVERIFY2(hr.contains(QStringLiteral("<hr/>")),
                 qPrintable(QString("ZZHR 应产出 <hr/>，HTML=[%1]").arg(hr)));
        assertNoPlaceholderToken(hr, "ZZHR");
        // ZZHR（fromHtml :4398 侧）：<hr> → ---
        const QString hrBack = MarkdownHandler::fromHtml(
            QStringLiteral("<p>甲</p>\n<hr>\n<p>乙</p>"));
        QVERIFY2(hrBack.contains(QStringLiteral("---")) && hrBack.contains(QStringLiteral("甲")),
                 qPrintable(QString("ZZHR(fromHtml) 应还原 ---，实际=[%1]").arg(hrBack)));
        assertNoPlaceholderToken(hrBack, "ZZHR-fromHtml");
        // ZZBL（toHtml :2609→:3182）：连续空行 → 空段落占位（折叠语义）
        const QString bl = MarkdownHandler::toHtml(QStringLiteral("甲\n\n\n\n乙"));
        QVERIFY2(bl.contains(QStringLiteral("<p></p>")),
                 qPrintable(QString("ZZBL 应产出空段落，HTML=[%1]").arg(bl)));
        assertNoPlaceholderToken(bl, "ZZBL");
        // ZZLB（:2630，HTML 注释标记，不走 collisionFreePlaceholder）：
        // 列表项间空行 → 注释被 Qt setMarkdown 丢弃 → 列表拆分
        const QString lb = MarkdownHandler::toHtml(QStringLiteral("- 甲项\n\n- 乙项"));
        QVERIFY2(lb.count(QStringLiteral("<ul")) >= 2,
                 qPrintable(QString("ZZLB 应拆分为两个列表，HTML=[%1]").arg(lb)));
        assertNoPlaceholderToken(lb, "ZZLB");
    }

    void testContractToHtmlZZBR_BK()
    {
        // ZZBR（toHtml :2761→:3210）：段落内软换行 → <br data-brk>
        const QString br = MarkdownHandler::toHtml(QStringLiteral("行甲\n行乙"));
        QVERIFY2(br.contains(QStringLiteral("data-brk")),
                 qPrintable(QString("ZZBR 应产出 data-brk，HTML=[%1]").arg(br)));
        assertNoPlaceholderToken(br, "ZZBR");
        // ZZBK（fromHtml :5342，副线容器）：列表项内软换行按行上下文还原缩进。
        // 断言收敛容忍形态：两行内容存活 + 换行结构存在（列表续行不并段）
        const QString bk = MarkdownHandler::fromHtml(
            MarkdownHandler::toHtml(QStringLiteral("- 项甲\n  项乙续行")));
        QVERIFY2(bk.contains(QStringLiteral("项甲")) && bk.contains(QStringLiteral("项乙续行"))
                 && bk.contains(QLatin1Char('\n')),
                 qPrintable(QString("ZZBK 应保真列表软换行，实际=[%1]").arg(bk)));
        assertNoPlaceholderToken(bk, "ZZBK");
    }

    void testContractToHtmlZZTC()
    {
        // ZZTC 双向：toHtml（:380/:389→:3868）空单元格防 Qt 合并 → <td></td>；
        // fromHtml（:5153）表格整体占位 → 规范化直接产出。空单元格须在数据行
        // （还原正则只匹配 <td>）
        const QString html = MarkdownHandler::toHtml(
            QStringLiteral("| 甲 | 乙 |\n| --- | --- |\n| 1 |  |"));
        QVERIFY2(html.contains(QStringLiteral("<td></td>")),
                 qPrintable(QString("ZZTC 空单元格应还原为 <td></td>，HTML=[%1]").arg(html)));
        const QString back = MarkdownHandler::fromHtml(html);
        QVERIFY2(back.contains(QStringLiteral("甲")) && back.count(QLatin1Char('|')) >= 6,
                 qPrintable(QString("ZZTC(fromHtml) 应还原表格管道，实际=[%1]").arg(back)));
        assertNoPlaceholderToken(back, "ZZTC");
    }

    void testContractToHtmlZZDV_A()
    {
        // ZZDV（toHtml :594→:3497）：手写 div 块级容器原样保留（不得被 <p> 包裹）
        const QString dv = MarkdownHandler::toHtml(
            QStringLiteral("<div align=\"right\">右对齐文本</div>"));
        QVERIFY2(dv.contains(QStringLiteral("<div align=\"right\">"))
                 && !dv.contains(QStringLiteral("<p><div")),
                 qPrintable(QString("ZZDV 应保真 div 且不被 <p> 包裹，HTML=[%1]").arg(dv)));
        assertNoPlaceholderToken(dv, "ZZDV");
        // ZZDV（fromHtml :5074 侧）：div 往返保真（ZZA 协同）
        const QString dvBack = MarkdownHandler::fromHtml(dv);
        QVERIFY2(dvBack.contains(QStringLiteral("<div align=\"right\">"))
                 && dvBack.contains(QStringLiteral("右对齐文本")),
                 qPrintable(QString("ZZDV(fromHtml) 应还原 div，实际=[%1]").arg(dvBack)));
        assertNoPlaceholderToken(dvBack, "ZZDV-fromHtml");
        // 复杂引用（protectComplexQuoteBlocks :2157，递归 toHtml 产物整块占位）
        const QString cq = MarkdownHandler::toHtml(QStringLiteral("> - 甲项\n> - 乙项"));
        QVERIFY2(cq.contains(QStringLiteral("<blockquote>")) && cq.contains(QStringLiteral("<li")),
                 qPrintable(QString("复杂引用应整体保真，HTML=[%1]").arg(cq)));
        assertNoPlaceholderToken(cq, "ZZDV-complexquote");
        // ZZA（fromHtml :5231，alignRe :5212-5215）：text-align 段落 → <div align>。
        // 输出形态经 mdconv 实测（2026-10-05）：整段 div 包装、无 <p> 残留
        const QString za = MarkdownHandler::fromHtml(
            QStringLiteral("<p style=\"text-align:center\">居中文字</p>"));
        QVERIFY2(za.contains(QStringLiteral("<div align=\"center\">"))
                 && za.contains(QStringLiteral("居中文字</div>")),
                 qPrintable(QString("ZZA 应还原 text-align 为 div align，实际=[%1]").arg(za)));
        assertNoPlaceholderToken(za, "ZZA");
    }

    void testContractToHtmlZZIA_IB()
    {
        // ZZIA（:631→:3140）：空 alt 图片占位（EUI 暂存路径 :623 先行）
        const QString ia = MarkdownHandler::toHtml(QStringLiteral("![](图.png)"));
        QVERIFY2(ia.contains(QStringLiteral("alt=\"\"")) && ia.contains(QStringLiteral("图.png")),
                 qPrintable(QString("ZZIA 应保真空 alt 图片，HTML=[%1]").arg(ia)));
        assertNoControlSentinel(ia, "EUI");
        // ZZIB（fromHtml :672 铸→:6050 还原）：alt 位 token → ![](src)
        const QString ib = MarkdownHandler::fromHtml(ia);
        QVERIFY2(ib.contains(QStringLiteral("![](图.png)")),
                 qPrintable(QString("ZZIB 应还原 ![](src)，实际=[%1]").arg(ib)));
        assertNoPlaceholderToken(ib, "ZZIB");
        assertNoControlSentinel(ib, "ZZIB");
    }

    void testContractToHtmlZZSB_SP()
    {
        // ZZSB（toHtml :905→:3818）：下标 ~x~ → <sub>（不得残留波浪定界符）
        const QString sb = MarkdownHandler::toHtml(QStringLiteral("水H~2~O示例"));
        QVERIFY2(sb.contains(QStringLiteral("<sub>2</sub>"))
                 && !sb.contains(QStringLiteral("~2~")),
                 qPrintable(QString("ZZSB 应产出 <sub>，HTML=[%1]").arg(sb)));
        assertNoPlaceholderToken(sb, "ZZSB");
        // ZZSB（fromHtml :4576 侧）：<sub> → ~x~
        const QString sbBack = MarkdownHandler::fromHtml(
            MarkdownHandler::toHtml(QStringLiteral("水H<sub>2</sub>O示例")));
        QVERIFY2(sbBack.contains(QStringLiteral("~2~")),
                 qPrintable(QString("ZZSB(fromHtml) 应还原 ~x~，实际=[%1]").arg(sbBack)));
        assertNoPlaceholderToken(sbBack, "ZZSB-fromHtml");
        // ZZSP（fromHtml :4596）：<sup>（非脚注）→ ^x^（触发须字面 sup 标签）
        const QString sp = MarkdownHandler::fromHtml(
            MarkdownHandler::toHtml(QStringLiteral("敏<sup>甲</sup>感")));
        QVERIFY2(sp.contains(QStringLiteral("^甲^")),
                 qPrintable(QString("ZZSP 应还原 ^x^，实际=[%1]").arg(sp)));
        assertNoPlaceholderToken(sp, "ZZSP");
    }

    void testContractToHtmlZZES_ED()
    {
        // ZZES（:930→:3475）：转义 == 不得转成 <mark>
        const QString es = MarkdownHandler::toHtml(QStringLiteral("\\=\\=字面等\\=\\="));
        QVERIFY2(es.contains(QStringLiteral("==字面等==")) && !es.contains(QStringLiteral("<mark")),
                 qPrintable(QString("ZZES 应保字面 ==，HTML=[%1]").arg(es)));
        assertNoPlaceholderToken(es, "ZZES");
        // ZZED（:980→:3889，toHtml 最后一步）：转义扩展定界符保字面
        const struct { const char *in; const char *lit; const char *tag; const char *name; } cases[] = {
            {"\\~字面波\\~",     "~字面波~",     "<sub", "ZZED(~)"},
            {"\\~\\~字面双波\\~\\~", "~~字面双波~~", "<s>",  "ZZED(~~)"},
            {"\\^字面幂\\^",     "^字面幂^",     "<sup", "ZZED(^)"},
        };
        for (const auto &c : cases) {
            const QString html = MarkdownHandler::toHtml(QString::fromUtf8(c.in));
            QVERIFY2(html.contains(QString::fromUtf8(c.lit))
                     && !html.contains(QString::fromUtf8(c.tag)),
                     qPrintable(QString("%1 应保字面 %2，HTML=[%3]")
                                    .arg(c.name, QString::fromUtf8(c.lit), html)));
            assertNoPlaceholderToken(html, c.name);
        }
    }

    void testContractToHtmlZZHI()
    {
        // ZZHI（:1050-:1141→:3734-:3739，多趟还原 ≤8）：8 类行内 HTML 标签逐一
        // 「铸造→还原」：标签原样存活 + 零泄漏 + 无哨兵残留（各路径 code 暂存）
        const QString tags[] = {
            QStringLiteral("<span style=\"color:red\">红</span>"),
            QStringLiteral("<u>下划</u>"),
            QStringLiteral("<kbd>按键</kbd>"),
            QStringLiteral("<small>小字</small>"),
            QStringLiteral("<mark>标记</mark>"),
            QStringLiteral("<sub>下标</sub>"),
            QStringLiteral("<sup>上标</sup>"),
            QStringLiteral("<br>"),
        };
        for (const QString &tag : tags) {
            const QString html = MarkdownHandler::toHtml(QStringLiteral("甲") + tag
                                                         + QStringLiteral("乙"));
            QVERIFY2(html.contains(tag),
                     qPrintable(QString("ZZHI 应还原标签 %1，HTML=[%2]").arg(tag, html)));
            assertNoPlaceholderToken(html, "ZZHI");
            assertNoControlSentinel(html, "ZZHI");
        }
        // \x01CODE 哨兵（:888 等多路径共用暂存表）：code 与行内标签共存还原
        const QString mixed = MarkdownHandler::toHtml(
            QStringLiteral("甲`码`乙<u>下划</u>丙"));
        QVERIFY2(mixed.contains(QStringLiteral("<code>码</code>"))
                 && mixed.contains(QStringLiteral("<u>下划</u>")),
                 qPrintable(QString("code 暂存应与行内标签共存，HTML=[%1]").arg(mixed)));
        assertNoPlaceholderToken(mixed, "CODE-sentinel");
        assertNoControlSentinel(mixed, "CODE-sentinel");
    }

    void testContractToHtmlZZFR_FD()
    {
        // ZZFR（:1184→:3741）：脚注引用 → <sup data-type="footnoteRef">
        const QString html = MarkdownHandler::toHtml(
            QStringLiteral("正文[^9]注\n\n[^9]: 契约定义"));
        QVERIFY2(html.contains(QStringLiteral("data-type=\"footnoteRef\""))
                 && html.contains(QStringLiteral("data-type=\"footnoteDef\"")),
                 qPrintable(QString("ZZFR 应产出脚注引用/定义，HTML=[%1]").arg(html)));
        assertNoPlaceholderToken(html, "ZZFR");
        // ZZFR（fromHtml :4556 侧）：脚注整链往返
        const QString fr = MarkdownHandler::fromHtml(html);
        QVERIFY2(fr.contains(QStringLiteral("[^9]")) && fr.contains(QStringLiteral("[^9]: 契约定义")),
                 qPrintable(QString("ZZFR(fromHtml) 应还原脚注，实际=[%1]").arg(fr)));
        assertNoPlaceholderToken(fr, "ZZFR-fromHtml");
        // ZZFD（fromHtml :4721，expandPlaceholdersInPlace 递归 :4708）：定义容器独立铸造
        const QString fd = MarkdownHandler::fromHtml(QStringLiteral(
            "<div data-type=\"footnoteContainer\"><div data-type=\"footnoteDef\" data-id=\"7\">"
            "<span class=\"footnote-marker\">7</span>"
            "<span class=\"footnote-content\">嵌套定义</span></div></div>"));
        QVERIFY2(fd.contains(QStringLiteral("[^7]: 嵌套定义")),
                 qPrintable(QString("ZZFD 应还原定义，实际=[%1]").arg(fd)));
        assertNoPlaceholderToken(fd, "ZZFD");
    }

    void testContractToHtmlZZMB_BM_IM()
    {
        // ZZMB（:2322/:2335→:3821）：块级公式（含 EOF 兜底）→ block-math div
        const QString mb = MarkdownHandler::toHtml(QStringLiteral("$$E=mc^{2}$$"));
        QVERIFY2(mb.contains(QStringLiteral("block-math"))
                 && mb.contains(QStringLiteral("E=mc^{2}")),
                 qPrintable(QString("ZZMB 应产出块级公式，HTML=[%1]").arg(mb)));
        assertNoPlaceholderToken(mb, "ZZMB");
        // ZZBM（fromHtml :4287）：block-math div → $$ 围栏
        const QString bm = MarkdownHandler::fromHtml(mb);
        QVERIFY2(bm.contains(QStringLiteral("$$")) && bm.contains(QStringLiteral("E=mc^{2}")),
                 qPrintable(QString("ZZBM 应还原 $$，实际=[%1]").arg(bm)));
        assertNoPlaceholderToken(bm, "ZZBM");
        // ZZIM（fromHtml :4301）：inline-math 空 span + data-latex → $latex$
        const QString im = MarkdownHandler::fromHtml(QStringLiteral(
            "<p>式前<span data-type=\"inline-math\" data-latex=\"a+b\"></span>式后</p>"));
        QVERIFY2(im.contains(QStringLiteral("$a+b$")) && im.contains(QStringLiteral("式前")),
                 qPrintable(QString("ZZIM 应还原 $…$，实际=[%1]").arg(im)));
        assertNoPlaceholderToken(im, "ZZIM");
    }

    void testContractToHtmlZZEC_EM_SC()
    {
        // ZZEC/ZZEM（protectEmphCode :3053，prefix 参数两态）/ZZSC（:3106）：
        // 强调包裹行内代码三族——断言强调标记与 <code> 同时存活（多趟还原）
        const struct { const char *in; const char *wrap; const char *name; } cases[] = {
            {"**甲 `码` 乙**",  "<strong>", "ZZEC"},
            {"*甲 `码`*",      "<em>",     "ZZEM"},
            {"~~甲 `码`~~",     "<s>",      "ZZSC"},
        };
        for (const auto &c : cases) {
            const QString html = MarkdownHandler::toHtml(QString::fromUtf8(c.in));
            QVERIFY2(html.contains(QString::fromUtf8(c.wrap))
                     && html.contains(QStringLiteral("<code>码</code>")),
                     qPrintable(QString("%1 应保真强调包裹代码，HTML=[%2]")
                                    .arg(c.name, html)));
            assertNoPlaceholderToken(html, c.name);
        }
    }

    void testContractToHtmlZZTL()
    {
        // ZZTL 双向：toHtml（:2667/:2691→:3223）任务列表 → taskList ul；
        // fromHtml（:4632）→ - [x]
        const QString html = MarkdownHandler::toHtml(QStringLiteral("- [ ] 甲任\n- [x] 乙任"));
        QVERIFY2(html.contains(QStringLiteral("<ul data-type=\"taskList\">"))
                 && html.contains(QStringLiteral("checked")),
                 qPrintable(QString("ZZTL 应产出任务列表，HTML=[%1]").arg(html)));
        assertNoPlaceholderToken(html, "ZZTL");
        const QString back = MarkdownHandler::fromHtml(html);
        QVERIFY2(back.contains(QStringLiteral("- [ ] 甲任"))
                 && back.contains(QStringLiteral("- [x] 乙任")),
                 qPrintable(QString("ZZTL(fromHtml) 应还原勾选态，实际=[%1]").arg(back)));
        assertNoPlaceholderToken(back, "ZZTL-fromHtml");
    }

    void testContractFromHtmlZZMK()
    {
        // ZZMK（:4526）：<mark> → ==x==（md 语法，非 HTML——断言 md 形态）
        const QString md = MarkdownHandler::fromHtml(QStringLiteral("<p>前<mark>高亮</mark>后</p>"));
        QVERIFY2(md.contains(QStringLiteral("==高亮==")) && md.contains(QStringLiteral("前"))
                 && md.contains(QStringLiteral("后")),
                 qPrintable(QString("ZZMK 应还原 ==高亮==，实际=[%1]").arg(md)));
        assertNoPlaceholderToken(md, "ZZMK");
    }

    void testContractFromHtmlZZCD_IC()
    {
        // ZZCD（:4489）：<code> 防丢弃 → `x`。铸造点在 toHtml 输出进入 fromHtml
        // 时触发，故经 toHtml 链路（含 __CODE__ 瞬态 token，见清单 §5）
        const QString cd = MarkdownHandler::fromHtml(
            MarkdownHandler::toHtml(QStringLiteral("用 `码` 内联")));
        QVERIFY2(cd.contains(QStringLiteral("`码`")),
                 qPrintable(QString("ZZCD 应还原行内代码，实际=[%1]").arg(cd)));
        assertNoPlaceholderToken(cd, "ZZCD");
        // ZZIC（:4507）：mono span（font-family 匹配 monoSpanFamilyPattern）→ `x`
        const QString ic = MarkdownHandler::fromHtml(
            QStringLiteral("<p>前<span style=\" font-family:'Courier New';\">等宽</span>后</p>"));
        QVERIFY2(ic.contains(QStringLiteral("`等宽`")) && ic.contains(QStringLiteral("前"))
                 && ic.contains(QStringLiteral("后")),
                 qPrintable(QString("ZZIC 应还原 mono span，实际=[%1]").arg(ic)));
        assertNoPlaceholderToken(ic, "ZZIC");
    }

    void testContractFromHtmlZZST_NS()
    {
        // ZZST 义一（:4863/:4878）：<s>/<del> 非互嵌 → ~~x~~
        const QString st = MarkdownHandler::fromHtml(QStringLiteral("<p><s>删除线</s></p>"));
        QVERIFY2(st.contains(QStringLiteral("~~删除线~~")),
                 qPrintable(QString("ZZST(义一) 应还原 ~~x~~，实际=[%1]").arg(st)));
        assertNoPlaceholderToken(st, "ZZST");
        // ZZNS（:4783/:4799）：删除线⚡强调互嵌无合法 MD 组合 → 字面 HTML 兜底。
        // 收敛容忍型：断言双方语义存活 + 零泄漏，不断言字节级形态
        const QString ns = MarkdownHandler::fromHtml(QStringLiteral("<p><s>删<em>斜</em></s></p>"));
        QVERIFY2(ns.contains(QStringLiteral("删")) && ns.contains(QStringLiteral("斜")),
                 qPrintable(QString("ZZNS 应保真互嵌双方内容，实际=[%1]").arg(ns)));
        assertNoPlaceholderToken(ns, "ZZNS");
    }

    void testContractFromHtmlZZHE()
    {
        // ZZHE（:4840）：标题内强调——Qt toMarkdown 丢弃 <hN> 内强调标记，
        // 处理器先抽取备份再还原（字节级不配对尾形态由 testHeadingEmphasisRoundTrip
        // 与矩阵用例所有，此处断言用户可见契约：<h1> 内 <strong> 存活）
        const QString he = MarkdownHandler::fromHtml(
            MarkdownHandler::toHtml(QStringLiteral("# **粗体标题**")));
        QVERIFY2(he.contains(QStringLiteral("#")) && he.contains(QStringLiteral("**粗体标题**")),
                 qPrintable(QString("ZZHE 应还原标题内强调（md 形态），实际=[%1]").arg(he)));
        assertNoPlaceholderToken(he, "ZZHE");
    }

    void testContractFromHtmlZZUL_KD_SM_HS()
    {
        // ZZUL/ZZKD/ZZSM（:4944/:4956/:4968）：Qt toMarkdown 丢弃 u/kbd/small，
        // 备份还原后标签存活即参与证据
        const struct { const char *html; const char *expect; const char *name; } cases[] = {
            {"<p>甲<u>下划</u>乙</p>",     "<u>下划</u>",     "ZZUL"},
            {"<p>甲<kbd>按键</kbd>乙</p>", "<kbd>按键</kbd>", "ZZKD"},
            {"<p>甲<small>小字</small>乙</p>", "<small>小字</small>", "ZZSM"},
        };
        for (const auto &c : cases) {
            const QString md = MarkdownHandler::fromHtml(QString::fromUtf8(c.html));
            QVERIFY2(md.contains(QString::fromUtf8(c.expect)),
                     qPrintable(QString("%1 应保真标签，实际=[%2]")
                                    .arg(c.name, md)));
            assertNoPlaceholderToken(md, c.name);
        }
        // ZZHS（:4927）：通用 style span 经 span 栈配对提取，原样保留
        const QString hs = MarkdownHandler::fromHtml(MarkdownHandler::toHtml(
            QStringLiteral("甲<span style=\"color:red\">红字</span>乙")));
        QVERIFY2(hs.contains(QStringLiteral("color:red")) && hs.contains(QStringLiteral("红字")),
                 qPrintable(QString("ZZHS 应保真 style span，实际=[%1]").arg(hs)));
        assertNoPlaceholderToken(hs, "ZZHS");
    }

    void testContractFromHtmlZZGL_NB()
    {
        // ZZGL（:5387）：通用列表自绘缩进——嵌套结构往返保真
        const QString gl = MarkdownHandler::fromHtml(
            MarkdownHandler::toHtml(QStringLiteral("- 甲项\n  - 乙项")));
        QVERIFY2(gl.contains(QStringLiteral("甲项")) && gl.contains(QStringLiteral("乙项"))
                 && gl.count(QStringLiteral("- ")) >= 2,
                 qPrintable(QString("ZZGL 应保真嵌套列表，实际=[%1]").arg(gl)));
        assertNoPlaceholderToken(gl, "ZZGL");
        // ZZNB（:5312）：连续 &nbsp;（实体或 U+00A0 均 ≥2 个）整段备份，
        // 还原产物为字面 &nbsp; 串
        const QString nb = MarkdownHandler::fromHtml(
            QStringLiteral("<p>甲&nbsp;&nbsp;&nbsp;乙</p>"));
        QVERIFY2(nb.contains(QStringLiteral("甲&nbsp;")) && nb.contains(QStringLiteral("乙")),
                 qPrintable(QString("ZZNB 应保真连续 &nbsp;，实际=[%1]").arg(nb)));
        assertNoPlaceholderToken(nb, "ZZNB");
    }

    void testContractFromHtmlZZBL()
    {
        // ZZBL 双向闭环：toHtml 空行标记 → <p></p>（:2609/:3182）；
        // fromHtml 空段落 → &nbsp; 独占行（:5289 铸 / :5700 整行还原）
        const QString html = MarkdownHandler::toHtml(QStringLiteral("甲\n\n\n\n乙"));
        QVERIFY2(html.contains(QStringLiteral("<p></p>")),
                 qPrintable(QString("ZZBL(toHtml) 应产出空段落，HTML=[%1]").arg(html)));
        const QString back = MarkdownHandler::fromHtml(html);
        QVERIFY2(back.contains(QStringLiteral("&nbsp;")),
                 qPrintable(QString("ZZBL(fromHtml) 应还原 &nbsp; 空行，实际=[%1]").arg(back)));
        assertNoPlaceholderToken(back, "ZZBL");
    }

    void testContractFromHtmlZZBR()
    {
        // ZZBR 义（fromHtml :5326）：手写 <br>（无 data-brk）字面备份，与软换行
        // data-brk 形态严格区分（三层分离契约，见 br-and-softbreak-roundtrip.md）
        const QString br = MarkdownHandler::fromHtml(
            MarkdownHandler::toHtml(QStringLiteral("甲<br>乙")));
        QVERIFY2(br.contains(QStringLiteral("<br>")) && !br.contains(QStringLiteral("data-brk")),
                 qPrintable(QString("ZZBR 应还原字面 <br> 且不带 data-brk，实际=[%1]").arg(br)));
        assertNoPlaceholderToken(br, "ZZBR");
    }

    void testContractFromHtmlLiteralStateMachine()
    {
        // 字面符号状态机 D 九家族聚合（:5462-:5615）：正文中的字面 MD 语法字符
        // 须以占位符+转义备份穿越 Qt 序列化。断言：字面符号存活（含转义形态，
        // contains 为子串匹配天然覆盖）+ token 零泄漏；字节级精确归一方向由
        // 转义保护专测与矩阵用例所有，此处不重复
        const struct { const char *html; const char *expect; const char *name; } cases[] = {
            {"<p>甲 ** 乙</p>",  "**", "ZZDS"},
            {"<p>甲 * 乙</p>",   "*",  "ZZST(义二)"},
            {"<p>甲 == 乙</p>",  "==", "ZZEQ"},
            {"<p>甲 ~~ 乙</p>",  "~~", "ZZTT"},
            {"<p>甲 ~ 乙</p>",   "~",  "ZZTD"},
            {"<p>甲 ^ 乙</p>",   "^",  "ZZCR"},
            {"<p>甲 &lt; 乙</p>", "<", "ZZLT"},
            {"<p>甲 &gt; 乙</p>", ">", "ZZGT"},
        };
        for (const auto &c : cases) {
            const QString md = MarkdownHandler::fromHtml(QString::fromUtf8(c.html));
            // 剥离转义反斜杠后须含符号本体——兼容「裸形式保留」与「转义形式
            // （如 == → \=\=，防误渲染为 mark）」两种归一方向
            QString stripped = md;
            stripped.remove(QLatin1Char('\\'));
            QVERIFY2(stripped.contains(QString::fromUtf8(c.expect)),
                     qPrintable(QString("%1 应保真字面符号，实际=[%2]")
                                    .arg(c.name, md)));
            assertNoPlaceholderToken(md, c.name);
        }
        // ZZLM（:5615）：段落行首列表标记转义，防误判为列表
        const QString lm = MarkdownHandler::fromHtml(QStringLiteral("<p>- 不是列表项</p>"));
        QVERIFY2(lm.contains(QStringLiteral("\\-")) && lm.contains(QStringLiteral("不是列表项")),
                 qPrintable(QString("ZZLM 应转义行首列表标记，实际=[%1]").arg(lm)));
        assertNoPlaceholderToken(lm, "ZZLM");
        const QString lmOrdered = MarkdownHandler::fromHtml(QStringLiteral("<p>1. 不是列表</p>"));
        QVERIFY2(lmOrdered.contains(QStringLiteral("1\\.")) && lmOrdered.contains(QStringLiteral("不是列表")),
                 qPrintable(QString("ZZLM 应转义行首有序标记，实际=[%1]").arg(lmOrdered)));
        assertNoPlaceholderToken(lmOrdered, "ZZLM-ordered");
        // ZZDS 裸形态既有定论：`**` 后随空格保持裸形式（参与证据，e2e 精确断言）
        QCOMPARE(MarkdownHandler::fromHtml(MarkdownHandler::toHtml(QStringLiteral("x** y 与 z"))),
                 QStringLiteral("x** y 与 z"));
    }

    void testContractSentinels()
    {
        // \x01EUT（escapeUnclosedHtmlTags :782，code 先暂存 :803）：未闭合 HTML
        // 标签转义。无 EUT 时 Qt 会把 <script> 后全文吞噬——「后文存活」即参与证据
        const QString eut = MarkdownHandler::toHtml(QStringLiteral("正文 <script> 未闭合标签"));
        QVERIFY2(eut.contains(QStringLiteral("&lt;script")) && eut.contains(QStringLiteral("未闭合标签")),
                 qPrintable(QString("EUT 应转义未闭合标签且不吞噬后文，HTML=[%1]").arg(eut)));
        assertNoControlSentinel(eut, "EUT");
        assertNoPlaceholderToken(eut, "EUT");
        // \x01CODE 下标路径（:888 下标铸造前暂存 code，还原后双语义共存）；
        // u 路径见 testContractToHtmlZZHI 的共存断言
        const QString code = MarkdownHandler::toHtml(QStringLiteral("甲`码`乙H~2~O"));
        QVERIFY2(code.contains(QStringLiteral("<code>码</code>"))
                 && code.contains(QStringLiteral("<sub>2</sub>")),
                 qPrintable(QString("CODE 哨兵应随下标路径正确还原，HTML=[%1]").arg(code)));
        assertNoControlSentinel(code, "CODE");
        assertNoPlaceholderToken(code, "CODE");
        // \x01EUI 断言见 testContractToHtmlZZIA_IB（空 alt 图片路径）
    }

};

QTEST_MAIN(TestMarkdownHandler)
#include "test_markdownhandler.moc"
