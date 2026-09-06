#include <QTest>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include "../src/core/PdfExporter.h"
#include "../src/core/HtmlExporter.h"

class TestPdfExporter : public QObject {
    Q_OBJECT
private slots:
    void testBuildRenderHtmlInlinesImages() {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        QFile f(dir.path() + "/a.png");
        QVERIFY(f.open(QIODevice::WriteOnly)); f.write("PNGDATA"); f.close();

        QString html = "<p><img src=\"a.png\" alt=\"x\"></p>";
        QString out = PdfExporter::buildRenderHtml(html, dir.path(), "", "");
        QVERIFY(out.contains("data:image/png;base64,"));
        QVERIFY(!out.contains("src=\"a.png\""));
    }

    void testBuildRenderHtmlKeepsAbsoluteImageAsIsWhenMissing() {
        // 读不到的文件保留原 src（不抛异常、不报错）
        QString html = "<p><img src=\"/nonexistent/zzz.jpg\"></p>";
        QString out = PdfExporter::buildRenderHtml(html, "/tmp", "", "");
        QVERIFY(out.contains("src=\"/nonexistent/zzz.jpg\""));
    }

    void testBuildRenderHtmlInjectsMermaid() {
        QString out = PdfExporter::buildRenderHtml("<p>x</p>", "/tmp", "", "");
        QVERIFY(out.contains("mermaid.min.js"));
        QVERIFY(out.contains("__renderAllMermaid"));
    }

    void testBuildRenderHtmlIncludesTaskListCss() {
        // 任务列表复选框与文字须同行：直接 li 用 flex 并排（复选框一行、文字一行）。
        // 必须用子代选择器 > 限定直接 li——嵌套在任务项内的通用列表 li（如 列表2-2
        // 下再嵌 <ol>）若也命中 flex 会被当成 flex 容器，其子元素 p 与嵌套 <ol>
        // 排成一行、序号消失（导出嵌套列表回归）。
        QString out = PdfExporter::buildRenderHtml("<p>x</p>", "/tmp", "", "");
        QVERIFY(out.contains("ul[data-type=\"taskList\"] > li{display:flex;align-items:flex-start;gap:8px;}"));
        QVERIFY(out.contains("ul[data-type=\"taskList\"] > li > div > p{margin:0;}"));
    }

    void testBuildRenderHtmlPrintCssSplittable() {
        // R2 内容丢失回归防线（两条路径共用 @media print 块）：
        // 1) pre 禁用 overflow-x:auto（滚动容器让分页器 breakToken 卡死→静默丢段），
        //    改 pre-wrap + anywhere：每个行盒都是合法断点，超一页代码块可逐行分页
        // 2) tr 禁止跨页拆分：行内断点触发分页器表格重建吞行
        // 3) pre 逐行重排为块级 .pl（__splitPreLines）：字符偏移断点在行间空白区
        //    几何不可靠会丢整行（断点位置随 mermaid 尺寸漂移，抽奖式复现），
        //    元素边界断点与段落同路径零丢失
        QString out = PdfExporter::buildRenderHtml("<p>x</p>", "/tmp", "", "");
        QVERIFY(out.contains("white-space:pre-wrap;overflow-wrap:anywhere;"));
        QVERIFY(!out.contains("overflow-x:auto"));
        // tr/th/td 三级齐上：paged.js 元素分支只对 td/th 的 avoid 走「整行带走」
        // 工作区（上游注释：take the whole row, not just the cell），tr 单加时
        // 行内断点仍穿透——R3 实测第 17/46 行整行静默丢失
        QVERIFY(out.contains("tr,th,td{break-inside:avoid;}"));
        QVERIFY(out.contains("pre .pl{display:block;break-inside:avoid;}"));
        QVERIFY(out.contains("__splitPreLines"));
        // 逐行重排仅 paged 路径调用（legacy 走原生分页，pre 由引擎处理）
        QVERIFY(out.indexOf("window.__splitPreLines();")
                > out.indexOf("if(!window.PagedPolyfill)"));
        // R3 表格断点修正：paged.js 只在「下一行 top 越界」时断页，行底压线贴合
        // 区底的骑跨行留在本页碎片里布局损坏（行盒被拉伸到整内容区高），打印
        // 重排亚像素差使其不绘制——onOverflow 把断点回退到骑跨行之前（8px 安全
        // 间隙）+ 行内断点整行化
        QVERIFY(out.contains("TableRowBreakFixer"));
        QVERIFY(out.contains("window.Paged.registerHandlers(TableRowBreakFixer);"));
        QVERIFY(out.indexOf("window.__fixTableBreaks();")
                > out.indexOf("if(!window.PagedPolyfill)"));
        // R3 内容指纹自检：分页前后文本比对，静默丢失即回退 fallback 原生打印
        // （保底零丢失优先于页眉页脚）；快照在 splitPreLines 之后、preview 之前
        QVERIFY(out.contains("__verifyContent"));
        QVERIFY(out.indexOf("window.__preText=document.body.textContent;")
                > out.lastIndexOf("window.__splitPreLines();"));
        QVERIFY(out.indexOf("window.__preText=document.body.textContent;")
                < out.indexOf("PagedPolyfill.preview()"));
        QVERIFY(out.indexOf("window.__verifyContent()")
                > out.indexOf("PagedPolyfill.preview()"));
    }

    void testBuildRenderHtmlPagedInjectsPageRules() {
        PdfPageSpec s = specWithHeader();
        QString out = PdfExporter::buildRenderHtml("<p>x</p>", "/tmp", "", "", s);
        QVERIFY(out.contains("@top-center"));
        QVERIFY(out.contains("我的文档"));
        QVERIFY(out.contains("@bottom-right"));
        QVERIFY(out.contains("counter(page)"));
        QVERIFY(out.contains("counter(pages)"));
        QVERIFY(out.contains("paged.polyfill.js"));
        QVERIFY(out.contains("PagedConfig={auto:false}"));
        QVERIFY(out.contains("window.__preparePrint"));
        // rAF 垫片仅 paged 路径注入（隐藏视图无合成帧，paged.js 队列会饿死），
        // 且必须在 polyfill 脚本之前
        QVERIFY(out.contains("window.requestAnimationFrame=function"));
        QVERIFY(out.indexOf("window.requestAnimationFrame=function")
                < out.indexOf("paged.polyfill.js"));
        // 状态机与 body 还原兜底（runJavaScript 不等 Promise，C++ 靠轮询）
        QVERIFY(out.contains("window.__printState"));
        QVERIFY(out.contains("window.__restoreBody"));
        // paged 激活时不得再有旧 @page{margin:0}（后位级联会覆盖新 margin）
        QVERIFY(!out.contains("@page{margin:0;}"));
    }

    void testBuildRenderHtmlLegacyPathUnchanged() {
        // 页眉页脚全关 → 不引入 paged.js，走原管线（@page{margin:0} 保留）
        QString out = PdfExporter::buildRenderHtml("<p>x</p>", "/tmp", "", "");
        QVERIFY(!out.contains("paged.polyfill.js"));
        QVERIFY(out.contains("@page{margin:0;}"));
        QVERIFY(out.contains("window.__preparePrint"));   // 打印前置入口两种路径都在
        QVERIFY(out.contains("window.__printState"));     // 完成态轮询两种路径都在
        QVERIFY(!out.contains("window.requestAnimationFrame=function"));   // 无 paged 无需垫片
    }

    void testBuildRenderHtmlHeaderOnly() {
        PdfPageSpec s = specWithHeader(); s.footerEnabled = false;
        QString out = PdfExporter::buildRenderHtml("<p>x</p>", "/tmp", "", "", s);
        QVERIFY(out.contains("@top-center"));
        QVERIFY(!out.contains("@bottom-right"));
        QVERIFY(out.contains("paged.polyfill.js"));
    }

    void testBuildRenderHtmlPaperOrientationMargins() {
        PdfPageSpec s = specWithHeader();
        s.paper = QPageSize::A5;
        s.orientation = QPageLayout::Landscape;
        s.marginsMm = QMarginsF(15, 20, 15, 22);   // 左上右下（QMarginsF 语义）
        QString out = PdfExporter::buildRenderHtml("<p>x</p>", "/tmp", "", "", s);
        QVERIFY(out.contains("size:A5 landscape"));
        // @page margin 顺序 = 上 右 下 左：20mm 15mm 22mm 15mm
        QVERIFY(out.contains("margin:20mm 15mm 22mm 15mm"));
    }

    void testBuildRenderHtmlTitleEscaped() {
        PdfPageSpec s = specWithHeader();
        s.headerTitle = QStringLiteral("a\"b\\c\nd");
        QString out = PdfExporter::buildRenderHtml("<p>x</p>", "/tmp", "", "", s);
        QVERIFY(out.contains("a\\\"b\\\\c d"));   // " → \"，\ → \\，换行 → 空格
    }

private:
    PdfPageSpec specWithHeader() {
        PdfPageSpec s;
        s.headerEnabled = true; s.footerEnabled = true;
        s.headerTitle = QStringLiteral("我的文档");
        return s;
    }
};

QTEST_APPLESS_MAIN(TestPdfExporter)
#include "test_pdfexporter.moc"
