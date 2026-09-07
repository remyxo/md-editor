#include <QTest>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QPageSize>
#include <QRegularExpression>
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
        // 横向发显式换向尺寸（宽前高后、宽>高），不用 landscape 关键字——vendored
        // paged.js 的 pageSizes 表键为 A0-A10/B4/B5（大写）与 letter/legal/
        // ledger（小写英寸系），而 Qt QPageSize::key() 返回 "Letter"/"Legal"
        // （首字母大写），查表无大小写归一即失配 → width/height 解析为空 →
        // 尺寸变量永不写入，DOM 页盒停留 8.5×11in 纵向默认值，横向打印版式
        // 与 DOM 几何分叉（内容被裁/页眉页脚错位）。显式 mm 尺寸走 Dimension
        // 解析直设 width/height，不查命名尺寸表
        QVERIFY(!out.contains("landscape"));
        const QSizeF mm = QPageSize(s.paper).size(QPageSize::Millimeter);
        QVERIFY(out.contains(QStringLiteral("size:%1mm %2mm")
                                 .arg(mm.height(), 0, 'f', 1)
                                 .arg(mm.width(), 0, 'f', 1)));
        QRegularExpression re("size:([0-9.]+)mm ([0-9.]+)mm");
        QRegularExpressionMatch m = re.match(out);
        QVERIFY(m.hasMatch());
        QVERIFY(m.captured(1).toDouble() > m.captured(2).toDouble());   // 宽>高
        // @page margin 顺序 = 上 右 下 左：20mm 15mm 22mm 15mm
        QVERIFY(out.contains("margin:20mm 15mm 22mm 15mm"));
    }

    void testBuildRenderHtmlPortraitExplicitDims() {
        // 纵向显式 mm 尺寸（宽前高后，宽<高）：vendored paged.js 的 pageSizes
        // 表键为 A0-A10/B4/B5（大写）与 letter/legal/ledger（小写英寸系），
        // Qt QPageSize::key() 返回 "Letter"/"Legal"（首字母大写）——查表无
        // 大小写归一即失配。A 系/B4/B5 纵向恰好同形幸免；Letter 纵向纯靠
        // DOM 默认 8.5×11in 巧合蒙对；Legal 纵向（8.5×14in）是真错——DOM
        // 页盒停 11in 高与打印版式分叉（内容被裁/页眉页脚错位）。两向统一
        // 显式 mm 走 Dimension 解析直设 width/height，命名尺寸表不再被依赖
        // （数值取 QPageSize 毫米、定点 1 位小数，与横向同源）
        PdfPageSpec s = specWithHeader();
        QString out = PdfExporter::buildRenderHtml("<p>x</p>", "/tmp", "", "", s);
        const QSizeF mm = QPageSize(s.paper).size(QPageSize::Millimeter);   // 默认 A4
        QVERIFY(out.contains(QStringLiteral("size:%1mm %2mm;")
                                 .arg(mm.width(), 0, 'f', 1)
                                 .arg(mm.height(), 0, 'f', 1)));
        QVERIFY(!out.contains("size:A4"));   // 命名尺寸不再出现
        QVERIFY(!out.contains("landscape"));
    }

    void testBuildRenderHtmlLegalPortraitExplicitDims() {
        // Legal 纵向显式尺寸（大小写失配主场景）：命名尺寸 "Legal" 查不到
        // pageSizes 表键 "legal"（小写）→ width/height 解析为空 → DOM 页盒
        // 停留默认 8.5×11in，与 8.5×14in 打印版式分叉。期望值由
        // QPageSize(QPageSize::Legal).size(Millimeter) 驱动拼接（勿硬编码
        // ——Qt 毫米换算精度跨版本可能有微差，Qt 6.8 实测 215.9×355.6mm）
        PdfPageSpec s = specWithHeader();
        s.paper = QPageSize::Legal;
        QString out = PdfExporter::buildRenderHtml("<p>x</p>", "/tmp", "", "", s);
        const QSizeF mm = QPageSize(QPageSize::Legal).size(QPageSize::Millimeter);
        QVERIFY(out.contains(QStringLiteral("size:%1mm %2mm;")
                                 .arg(mm.width(), 0, 'f', 1)
                                 .arg(mm.height(), 0, 'f', 1)));
        QVERIFY(!out.contains("size:Legal"));
        QVERIFY(!out.contains("landscape"));
    }

    void testBuildRenderHtmlPagedCapsMermaidSvg() {
        // mermaid SVG 页高容纳上限（横向丢页眉页脚的根因）：mermaid 渲染的 svg
        // 带 width="100%" + 内联 max-width，宽度自适应容器但高度无约束——高于
        // 内容区的图（横向内容高 174mm，常见时序图 ~195mm）不可分页拆分 →
        // 分页器 "Unable to layout item" → 内容重复铺排 → 指纹校验拦截回退
        // 原生分页（无页眉页脚）。只约束 max-height：宽度归 mermaid 自身机制
        // （width="100%" + 内联 max-width 永不超容器），加 width:auto 反而会把
        // 小图放大到内容区宽（R1 审查实测 300×150 被放大 2.17×）；高度超限时
        // viewBox 按缺省 meet 等比居中（盒宽不变）。2mm 安全间隙防压线亚像素
        // 溢出
        PdfPageSpec s = specWithHeader();
        s.marginsMm = QMarginsF(18, 18, 18, 18);
        QString out = PdfExporter::buildRenderHtml("<p>x</p>", "/tmp", "", "", s);
        QVERIFY(out.contains("holder.className='mermaid-diagram'"));
        // 纵向 A4：内容盒高 297-36-2=259mm；规则必须恰为 max-height 单约束
        // ——width/height:auto 或 max-width 都会破坏「小图不放大」（字符串层
        // 测不了渲染尺寸，锁规则形状是唯一护栏）
        const qsizetype i = out.indexOf(".mermaid-diagram svg{");
        QVERIFY(i >= 0);
        const qsizetype j = out.indexOf(QLatin1Char('}'), i);
        QVERIFY(out.mid(i, j - i + 1)
                == QStringLiteral(".mermaid-diagram svg{max-height:259.0mm!important;}"));
        // 横向 A4：内容盒高 210-36-2=172mm
        s.orientation = QPageLayout::Landscape;
        out = PdfExporter::buildRenderHtml("<p>x</p>", "/tmp", "", "", s);
        QVERIFY(out.contains(".mermaid-diagram svg{max-height:172.0mm!important;}"));
        // 上限规则须在 @media print 之外（paged.js 在屏幕媒体上分页测量）
        QVERIFY(out.indexOf(".mermaid-diagram svg{")
                < out.indexOf("@media print{"));
        // legacy 路径（页眉页脚全关）无 paged 分页，不注入容纳上限
        QString legacy = PdfExporter::buildRenderHtml("<p>x</p>", "/tmp", "", "");
        QVERIFY(!legacy.contains(".mermaid-diagram svg{"));
    }

    void testBuildRenderHtmlPagedCapsImages() {
        // 普通 <img> 页高容纳上限 + 屏幕媒体宽度约束（与上方 mermaid 上限同型）：
        // 高于内容区的图片是原子元素不可分页拆分 → 分页器 "Unable to layout item"
        // → 内容重复铺排 → 指纹校验拦截回退原生分页（无页眉页脚）；横向 A4 内容
        // 高仅 172mm，竖版截图/长图必超。形状调查结论（选择器依据）：导出 HTML
        // 一律来自 TipTap getHTML()（schema 只序列化 src/alt/title/width/height），
        // 常规插入/剪贴板粘贴/md4c 加载路径均不产 width 属性——:not([width])
        // 覆盖全部常规图片；显式 width 属性唯一来源是用户字面 HTML（TipTap
        // parseHTML 按 schema 吸收 width/height），刻意排除——width:auto!important
        // 会把用户显式缩小的缩略图放大回自然尺寸（R1 审查 300×150 被放大 2.17×
        // 的同型陷阱），显式尺寸超高时回退保底仍在。:not([style]) 同理：当前链路
        // ProseMirror 解析丢弃非 schema 的 style 属性（导出 HTML 无带 style 的
        // img），排除是零成本护栏——width:auto!important 会覆盖非 important 的
        // 内联 style 宽度（buildRenderHtml 为公开 API，防未来内联样式路径）
        PdfPageSpec s = specWithHeader();
        s.marginsMm = QMarginsF(18, 18, 18, 18);
        QString out = PdfExporter::buildRenderHtml("<p>x</p>", "/tmp", "", "", s);
        // 纵向 A4：内容盒高 297-36-2=259mm。形状锁：max-height 与 width:auto
        // 恰成一对——max-height 收缩高度时只有 width:auto 才按纵横比等比缩宽
        // （CSS 替换元素约束），字符串层测不了渲染尺寸，锁规则形状是唯一护栏
        QVERIFY(out.contains(QStringLiteral(
            "img:not([width]):not([style]){max-height:259.0mm!important;"
            "width:auto!important;}")));
        // 横向 A4：内容盒高 210-36-2=172mm
        s.orientation = QPageLayout::Landscape;
        out = PdfExporter::buildRenderHtml("<p>x</p>", "/tmp", "", "", s);
        QVERIFY(out.contains(QStringLiteral(
            "img:not([width]):not([style]){max-height:172.0mm!important;"
            "width:auto!important;}")));
        // 两条规则均须在 @media print 之外——paged.js 在屏幕媒体上分页测量：
        // max-width 只在打印媒体生效时，超宽图屏幕布局无宽度约束 → 横向溢出
        // .pagedjs_sheet（overflow:hidden）→ 静默右裁（指纹通过不回退，比回退
        // 更隐蔽）；max-height 同理须在分页测量前生效
        // 先锁存在再锁位置——indexOf 删规则返回 -1 会空过比较（-1 < N 恒真）
        const qsizetype m = out.indexOf("img{max-width:100%;}");
        QVERIFY(m >= 0);
        QVERIFY(m < out.indexOf("@media print{"));
        const qsizetype n = out.indexOf("img:not([width])");
        QVERIFY(n >= 0);
        QVERIFY(n < out.indexOf("@media print{"));
        // legacy 路径（页眉页脚全关）无 paged 分页，不注入两条容纳规则：img 宽度
        // 规则仅剩 @media print 内的一处（前缀计数区分屏幕副本与打印副本；打印
        // 副本的 %% 死值已在 #32 修正为有效单 %，形态由
        // testBuildRenderHtmlPrintCssPercentValid 锁定，此处只锁数量）
        QString legacy = PdfExporter::buildRenderHtml("<p>x</p>", "/tmp", "", "");
        QVERIFY(!legacy.contains("img:not([width])"));
        QCOMPARE(legacy.count("img{max-width:100"), 1);
        QVERIFY(legacy.lastIndexOf("img{max-width:100") > legacy.indexOf("@media print{"));
    }

    void testBuildRenderHtmlPrintCssPercentValid() {
        // @media print 模板里的百分比宽度必须是有效 CSS 值：QString::arg 只识别
        // %1..%99 占位符、没有 printf 式的 %% 转义语义——模板里写 100%% 会原样
        // 保留进最终 HTML（无效 CSS 值），table 的 width:100%% 与 img 的
        // max-width:100%% 两条规则从未生效过（死 CSS）。历史缺陷已修，此处双向
        // 锁定：全文无 %% 残留 + 两条规则复活后的完整形态
        // paged 态：img{max-width:100%;} 出现两处——@media print 之外的屏幕媒体
        // 分页测量副本（pageCss 注入）+ @media print 内的打印副本（模板）
        PdfPageSpec s = specWithHeader();
        QString out = PdfExporter::buildRenderHtml("<p>x</p>", "/tmp", "", "", s);
        QVERIFY(!out.contains("%%"));
        QVERIFY(out.contains(
            "table{border-collapse:collapse;width:100%;margin:0.5em 0;}"));
        QVERIFY(out.contains("img{max-width:100%;}"));
        QCOMPARE(out.count("img{max-width:100%;}"), 2);
        // legacy 态：img{max-width:100%;} 恰 1 处（仅 @media print 内的打印副本）
        QString legacy = PdfExporter::buildRenderHtml("<p>x</p>", "/tmp", "", "");
        QVERIFY(!legacy.contains("%%"));
        QVERIFY(legacy.contains(
            "table{border-collapse:collapse;width:100%;margin:0.5em 0;}"));
        QVERIFY(legacy.contains("img{max-width:100%;}"));
        QCOMPARE(legacy.count("img{max-width:100%;}"), 1);
    }

    void testPagedPrintMarginsTinyNonZero() {
        // paged 路径 Qt 打印边距（mm）形状锁：每边必须 > 0 且 <= 1.0——
        // ① Qt 6.8 printToPdf 对恰为 0 的边距套用 ~18mm（51pt）有效默认打印
        //    边距（CSS @page 边距 18→10mm 落纸内缩不变的判别实验证实与 CSS
        //    无关；5mm→内缩精确 14.17pt 证明非 0 即精确采用）
        // ② 有效边距压缩可打印区 → Chromium shrink-to-fit 把自然宽=纸宽的
        //    paged.js 文档等比缩 0.879(横)/0.829(纵)
        // ③ 缩放后横向 sheet 盒 561.4pt < 网格 595.3pt，.pagedjs_sheet 的
        //    overflow:hidden 裁掉底部 33.9pt → 页脚整段落裁切线外全丢；纵向
        //    不裁但全文 83% 缩放 + 页脚悬高于真实底边距行 ~80pt
        // 0.1mm（用户裁决值）避开零值默认、落纸偏差不可见。上限 1.0mm 防
        // 有人改成大边距引入与 @page 边距的双重边距
        const QMarginsF m = PdfExporter::pagedPrintMargins();
        QVERIFY(m.left() > 0.0 && m.left() <= 1.0);
        QVERIFY(m.top() > 0.0 && m.top() <= 1.0);
        QVERIFY(m.right() > 0.0 && m.right() <= 1.0);
        QVERIFY(m.bottom() > 0.0 && m.bottom() <= 1.0);
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
