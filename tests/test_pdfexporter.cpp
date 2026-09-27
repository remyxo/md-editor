#include <QTest>
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QJSEngine>
#include <QPageSize>
#include <QRegularExpression>
#include "../src/core/PdfExporter.h"
#include "../src/core/HtmlExporter.h"
#include "../src/core/ExportCommon.h"

class TestPdfExporter : public QObject {
    Q_OBJECT
private slots:
    // 1×1 真实 PNG 字节（inlineImages 内容级 MIME 校验后，伪字节夹具不再内联）
    static QByteArray tinyPng() {
        static const unsigned char png[] = {0x89,0x50,0x4e,0x47,0x0d,0x0a,0x1a,0x0a,0,0,0,0x0d,0x49,0x48,0x44,0x52,
                                     0,0,0,1,0,0,0,1,0x08,0x06,0,0,0,0x1f,0x15,0xc4,0x89,0,0,0,0x0a,0x49,0x44,0x41,0x54,
                                     0x78,0x9c,0x63,0,1,0,0,0,0x05,0,1,0x0d,0x0a,0x2d,0xb4,0,0,0,0,0x49,0x45,0x4e,0x44,0xae,0x42,0x60,0x82};
        return QByteArray(reinterpret_cast<const char*>(png), sizeof(png));
    }

    void testBuildRenderHtmlInlinesImages() {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        QFile f(dir.path() + "/a.png");
        QVERIFY(f.open(QIODevice::WriteOnly)); f.write(tinyPng()); f.close();

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
        // 按需注入（2026-09-15 性能取证）：无流程图的文档白付 3.5MB mermaid.min.js
        // 加载+解析 ~350ms、无公式白付 katex 272KB+CSS——库文件仅内容命中时注入；
        // 渲染 helper 恒定义（自带 if(window.mermaid) 守卫，库缺席整段跳过）
        const QString mmd = "<pre><code class=\"language-mermaid\">graph TD;A--&gt;B;"
                            "</code></pre>";
        QString out = PdfExporter::buildRenderHtml(mmd, "/tmp", "", "");
        QVERIFY(out.contains("mermaid.min.js"));
        QVERIFY(out.contains("__renderAllMermaid"));
        // 纯文本：两库都不注入，helper 仍在
        QString plain = PdfExporter::buildRenderHtml("<p>x</p>", "/tmp", "", "");
        QVERIFY(!plain.contains("mermaid.min.js"));
        QVERIFY(!plain.contains("katex.min.js"));
        QVERIFY(plain.contains("__renderAllMermaid"));
        // 有公式：katex 库与传入的 KaTeX CSS 都注入，mermaid 不注入
        QString math = PdfExporter::buildRenderHtml(
            "<p><span data-type=\"inline-math\" data-latex=\"x\"></span></p>",
            "/tmp", "KATEXCSSMARK", "");
        QVERIFY(math.contains("katex.min.js"));
        QVERIFY(math.contains("KATEXCSSMARK"));
        QVERIFY(!math.contains("mermaid.min.js"));
        // 无公式但调用方传了 KaTeX CSS：一并省略（base64 字体不内联）
        QString plainCss = PdfExporter::buildRenderHtml("<p>x</p>", "/tmp",
                                                        "KATEXCSSMARK", "");
        QVERIFY(!plainCss.contains("KATEXCSSMARK"));
    }

    void testBuildRenderHtmlStripsStyleCloseFromCustomCss() {
        // 自注入面：customCss 携带 </style> 会提前终结注入的样式块、向产物
        // 注入任意标记。与 HtmlExporter 对称剥离（大小写不敏感、容忍空白）。
        // 剥离后剩余的 <script> 等文本是 <style> rawtext 内的惰性内容——
        // 样式块只被我们追加的 </style> 终结，注入无法逃出样式上下文。
        QString out = PdfExporter::buildRenderHtml(
            "<p>x</p>", "/tmp", "",
            "body{color:red}</style><script>alert(1)</script>");
        QVERIFY(out.contains("body{color:red"));
        // 攻击串锚定（非裸 "</style><script>"——按需注入后模板自身
        // </style> 后可合法紧跟 <script>，裸串断言误报）
        QVERIFY(!out.contains("</style><script>alert"));
        // 剥离后注入块内不再有第二个 </style>（只保留我们追加的收尾）
        QString out2 = PdfExporter::buildRenderHtml(
            "<p>x</p>", "/tmp", "", "a{b:c}</  STYLE  >");
        QVERIFY(!out2.contains("</  STYLE  >"));
        QVERIFY(out2.count("</style>") <= out2.count("<style>"));
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

    void testFootnotePerPageFloatWiring() {
        // 脚注随页浮动（Word 形态）：paged 路径注入 float:footnote 指令 +
        // 脚注区分隔线样式 + call 自动编号抑制 + __floatFootnotes 重排（须先于
        // 指纹快照——def 移位改变文本顺序，快照基线取移动后文档）；legacy
        // 路径无这些注入。基础版式（marker 浮动 + 首子元素去顶距）两路径都有
        // ——footnote-content 是块级 div，缺规则时行内 marker 后跟块级元素拆
        // 匿名块、编号独占一行（PDF 模板不加载 editor.css 的教训）
        PdfPageSpec spec;
        spec.headerEnabled = true;   // 任一开关激活 paged 分页
        const QString paged = PdfExporter::buildRenderHtml(
            "<p>正文<sup data-type=\"footnoteRef\" data-id=\"1\">1</sup></p>",
            "/tmp", "", "", spec);
        QVERIFY(paged.contains("div[data-type=\"footnoteDef\"].pdf-fn{float:footnote;}"));
        QVERIFY(paged.contains(".pagedjs_footnote_area>.pagedjs_footnote_content"));
        QVERIFY(paged.contains("[data-footnote-call]::after{content:none!important;}"));
        // 溢出拆分碎片的 text-align-last:justify 抑制：上游 Footnotes.
        // handleAlignment 对碎片无条件 justify（正文版同款方法有 text-align:
        // justify 前置），短行尾行被撑满全宽逐字拉开（多段脚注跨页拆分实测）
        QVERIFY(paged.contains(
            ".pagedjs_footnote_area [data-align-last-split-element='justify']"
            "{text-align-last:auto;}"));
        // 页脚 counter(pages) 修正须在 preview 之后（页元素已定型）、指纹校验
        // 之前：clonePage 不更新 this.total → 渲染收尾把 --pagedjs-page-count
        // 写成不含脚注续页的旧值（「第3页/共2页」实测）
        const qsizetype cp = paged.indexOf("'--pagedjs-page-count'");
        QVERIFY(cp >= 0);
        QVERIFY(cp > paged.indexOf("PagedPolyfill.preview()"));
        QVERIFY(cp < paged.indexOf("window.__verifyContent();"));
        QVERIFY(paged.indexOf("__floatFootnotes();") >= 0);
        // 浮动重排在指纹快照之后（正文指纹取剔除 def 的克隆，def 走字符
        // 多重集 __preFns——重排/跨页拆分不误伤）、preview 之前
        QVERIFY(paged.indexOf("__preText=") < paged.indexOf("__floatFootnotes();"));
        QVERIFY(paged.indexOf("__floatFootnotes();")
                < paged.indexOf("PagedPolyfill.preview()"));
        QVERIFY(paged.contains(".footnote-marker{float:left"));
        QVERIFY(paged.contains(".footnote-content>*:first-child{margin-top:0;}"));

        const QString legacy = PdfExporter::buildRenderHtml("<p>x</p>", "/tmp", "", "");
        QVERIFY(!legacy.contains("float:footnote"));      // 浮动指令仅 paged 注入
        // .pagedjs_footnote_area 裸串不能作判据——__verifyContent 脚本文本两路径
        // 共有（legacy 分支提前 return 不执行），锁 CSS 规则形态（含 > 子选择器）
        QVERIFY(!legacy.contains(".pagedjs_footnote_area>.pagedjs_footnote_content"));
        QVERIFY(!legacy.contains("[data-footnote-call]"));
        // 脚本文本两条路径都在 HTML 里（legacy 分支提前 return 不执行），
        // 执行守卫 = 调用点位于 PagedPolyfill 存在性检查之后（splitPreLines 同款）
        QVERIFY(legacy.indexOf("__floatFootnotes();")
                > legacy.indexOf("if(!window.PagedPolyfill)"));
        QVERIFY(legacy.contains(".footnote-marker{float:left"));
    }

    void testBuildRenderHtmlNativeHf() {
        // 原生分页+覆盖层页眉页脚（2026-09-19 提速；2026-09-21 版式重做对齐
        // paged.js）：打印区覆盖整张纸，页边距改由流内结构模拟——thead 带高
        // = 用户上边距（带内标题墨迹垫到纸顶 ~7.4mm，与 @top-center 同落点）、
        // 逐页强制分页（__paginateNativeHf）模拟下边距、内容格 padding 模拟
        // 左右边距；页脚覆盖层落在纸底带（字形底距纸底 ~8mm，与 @bottom-right
        // 同带）。不注入 paged.js，__preparePrint 在无 PagedPolyfill 时按
        // __NATIVE_HF 置 nativehf 态
        PdfPageSpec s = specWithHeader();
        s.nativeHf = true;
        s.marginsMm = QMarginsF(18, 18, 18, 18);
        QString out = PdfExporter::buildRenderHtml("<p>x</p>", "/tmp", "", "", s);
        QVERIFY(!out.contains("paged.polyfill.js"));
        QVERIFY(out.contains("window.__NATIVE_HF=true;"));
        QVERIFY(out.contains("<table class=\"hf\"><thead>"));        // 页眉重复带
        QVERIFY(out.contains("我的文档"));                             // 标题
        QVERIFY(out.contains("__stampPageNumbers=function(n,h1,hc,totalPx){"));   // 页码注入器
        // 页眉带：高 = 用户上边距（此处 18mm = 12mm 带高 + 3mm 底部净空），
        // 文字底对齐压向正文顶线（Word 式贴页边距；居中悬在页边距带中央的
        // 旧式让墨迹离纸边仅 ~8mm，打印机不可打印边距 4-6mm 时太薄）
        QVERIFY(out.contains("padding:0 0 3.00mm 0;height:15.00mm"));
        QVERIFY(out.contains("vertical-align:bottom"));
        // 内容格：左右 padding 模拟页边距，宽度由渲染视图尺寸决定（不再钉宽）
        QVERIFY(out.contains("#hfcell{font-size:11pt;padding:0 18.00mm 0 18.00mm;}"));
        // 字号基准：包装单元格是 <td>，会吃到基础 CSS 的 th,td 字号（历史 10pt
        // 曾把单元格内所有 em 相对值整体带小 10%，实测标题差一整档；2026-09-24
        // 起 th,td 与正文统一为 11pt，钉回保留为防御——th,td 字号再变时 em 基准
        // 不随动）。见 docs/export-visual-spec.md
        QVERIFY(!out.contains("width:65"));               // 无钉宽（auto 布局里也钉不住）
        // 包装表 fixed 布局：列宽不受内容影响，截住 min-content 撑爆
        QVERIFY(out.contains("table.hf{table-layout:fixed;}"));
        // 页脚位形：打印区覆盖整纸，页码盒整体落在片段内的纸底带（不再越段
        // 末）。**位置由下边距推导**：行盒顶 = 正文底线（纸底 − 下边距）+ 净空
        // 3mm（kHfHugGapMm，与 paged 路径 @bottom-right 的
        // vertical-align:top + padding-top 同式同值）——不再写死"距纸底 8mm"；
        // 18mm 下边距 ⇒ 偏移 15mm，墨迹落在距纸底 ~10.9mm（可打印余量充足）。
        // 右缘退右页边距与正文右边界对齐
        QVERIFY(out.contains("sp.style.position='absolute';"));
        QVERIFY(out.contains("sp.style.padding='0 0 0 6px';"));
        QVERIFY(out.contains("sp.style.top='calc(100% - 15.00mm)';"));
        QVERIFY(out.contains("sp.style.right='18.00mm';"));
        QVERIFY(out.contains("sp.textContent='第 '+k+' 页 / 共 '+n+' 页';"));
        QVERIFY(out.contains("if(totalPx>0)host.style.height=totalPx+'px';"));   // 宿主总高
        // 页高分段：每片恰一页高（新版式页眉带已移出片段内位移，首片不再扣 thead）
        QVERIFY(out.contains("d.style.height=(k===1?h1:hc)+'px';"));
        // 轻量分页：切页器 + 页内容高预算 + 单块超页高计数（C++ 据此提前回退）
        QVERIFY(out.contains("window.__paginateNativeHf=function(Hpx,MbPx){"));
        QVERIFY(out.contains("window.__hfPageContentPx="));
        QVERIFY(out.contains("window.__hfPageMarginBottomPx="));
        // 安全断点：li 上任意层级可断（探针实测），li 内部的非 li 元素不可断
        // （同探针实测会失效、内容错位）；不可断单元按自然页格同步 pageTop
        QVERIFY(out.contains("if(t==='LI'){desc(el,el,true);return;}"));
        QVERIFY(out.contains("var natCap=Hpx+MbPx;"));
        QVERIFY(out.contains("window.__hfPagTall=tall;"));
        // 断点经 breakAt 落位：li 上直接打，li 内部走就地拆 li（拆分手术当前
        // 暂缓 → 判为不可断，故 li 内部单元不会进断点分支）
        QVERIFY(out.contains("breakAt(u[0]);"));
        // 包装表必须被列宽归一跳过：它的 querySelectorAll 覆盖全文档，会给
        // 每个单元格写下"本表格列宽"（100%）污染内层测量
        QVERIFY(out.contains("if(table.classList.contains('hf'))return;"));
        // 屏幕视口禁滚动条（测量期临时压制）：屏幕布局必须 = 打印布局
        QVERIFY(out.contains("document.documentElement.style.overflow='hidden';"));
        QVERIFY(out.contains("document.documentElement.style.overflow='';"));
        // 基础 CSS 媒体包装：nativeHf 不包（屏幕布局 = 打印布局的前提）
        QVERIFY(!out.contains("@media print{"));
        QVERIFY(out.contains(
            "window.__printState=window.__NATIVE_HF?'nativehf':'legacy';"));
        QVERIFY(!out.contains("float:footnote"));                  // paged 专属不注入
        QVERIFY(out.contains("html,body{margin:0;padding:0;}"));   // 覆盖层坐标系归零
        QVERIFY(!out.contains("padding-bottom:22px"));             // 旧底垫 hack 已撤
        // 标题 HTML 转义（用户可命名任意字符）
        PdfPageSpec t = specWithHeader();
        t.nativeHf = true; t.headerTitle = QStringLiteral("a<b&\"c");
        QString esc = PdfExporter::buildRenderHtml("<p>x</p>", "/tmp", "", "", t);
        QVERIFY(esc.contains("a&lt;b&amp;&quot;c"));
        // 仅页脚（无页眉）：包装表仍在（新版式下正文顶 = 带高，页眉开关不得
        // 改变正文版式），thead 留空带；页码注入器照旧
        PdfPageSpec f = specWithHeader();
        f.nativeHf = true; f.headerEnabled = false;
        QString fo = PdfExporter::buildRenderHtml("<p>x</p>", "/tmp", "", "", f);
        QVERIFY(fo.contains("<table class=\"hf\"><thead><tr><td></td>"));
        QVERIFY(fo.contains("__stampPageNumbers=function(n,h1,hc,totalPx){"));
        // 仅页眉（无页脚）：table 在、无页码注入器（免数页数单遍直出）
        PdfPageSpec h = specWithHeader();
        h.nativeHf = true; h.footerEnabled = false;
        QString ho = PdfExporter::buildRenderHtml("<p>x</p>", "/tmp", "", "", h);
        QVERIFY(ho.contains("<table class=\"hf\"><thead>"));
        QVERIFY(!ho.contains("__stampPageNumbers"));
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
        // 4) 连续无空格长串（长 URL/色块串）Must 能断行：只给 th,td 加的话，
        //    nativeHf 因正文包在包装表单元格里而全篇继承、paged 路径不继承
        //    ——同一文档两路折行行为分叉（实测 2.3 节超长列表项在 paged 路径
        //    墨迹直达纸边被页盒裁掉，nativeHf 正常折行）。提到文档级后两路
        //    一致；anywhere 而非 break-word：前者同时收敛 min-content
        QVERIFY(out.contains("overflow-wrap:anywhere;}"));
        // tr/th/td 三级齐上：paged.js 元素分支只对 td/th 的 avoid 走「整行带走」
        // 工作区（上游注释：take the whole row, not just the cell），tr 单加时
        // 行内断点仍穿透——R3 实测第 17/46 行整行静默丢失
        QVERIFY(out.contains("tr,th,td{break-inside:avoid;}"));
        QVERIFY(out.contains("pre .pl{display:block;break-inside:avoid;}"));
        // 5) 跨页代码块的底色落在**行块**上（逐行重排后 JS 清掉 pre 自身底色）：
        //    pre 跨页时其背景会被 Chromium 画到片段容器末端——快路径可打印区覆盖
        //    整张纸，那片底色一直画到纸底压过页脚（实测灰底 242→296.6mm，用户
        //    截图）。行块 break-inside:avoid 永不跨页 ⇒ 背景不越界
        QVERIFY(out.contains("pre .pl{background:#f4f5f7;padding:0 14px;}"));
        QVERIFY(out.contains("pre .pl:first-child{padding-top:10px;border-radius:4px 4px 0 0;}"));
        QVERIFY(out.contains("pre .pl:last-child{padding-bottom:10px;border-radius:0 0 4px 4px;}"));
        //    拆行器实现已迁至共享 web 资源 pdf-split-prelines.js（vitest 覆盖其
        //    行为，含底色清零），渲染页只承载 script 标签接线
        QVERIFY(out.contains("<script src=\"pdf-split-prelines.js\"></script>"));
        // qrc 打包存在性锁（Task3-M1）：上面的接线断言只锁定模板字符串，与
        // app.qrc 是否真的打包该文件脱钩——qrc 条目误删时接线断言双绿、渲染页
        // 运行时 404 静默降级（__splitPreLines 未定义即不拆行），重开 paged 断页
        // 丢行旧 bug。此处从 qrc 实读，条目缺失即编译期外最早失败点
        QVERIFY(!ExportCommon::readResource(
                     QStringLiteral(":/web/pdf-split-prelines.js")).isEmpty());
        // editor 侧同构接线（highlightExportHtml 依赖）：export-highlight.js
        // 的 qrc 打包同锁，防同款 404 静默降级（高亮步整体失效）
        QVERIFY(!ExportCommon::readResource(
                     QStringLiteral(":/web/export-highlight.js")).isEmpty());
        // <code> 的底色/内边距同样清掉：逐行重排后它的块级子元素会切开行内盒，
        // 首尾空碎片各带一份底色内边距，在代码块上下画出细灰条
        QVERIFY(out.contains("pre code{background:none;padding:0;border-radius:0;}"));
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
        // （快照形态为 IIFE：剔除脚注 def 的 body 克隆文本——def 随页浮动后文本
        // 顺序必然改变，def 文本走独立字符多重集指纹 __preFns）
        QVERIFY(out.contains("__verifyContent"));
        QVERIFY(out.indexOf("window.__preText=(function(){")
                > out.lastIndexOf("window.__splitPreLines();"));
        QVERIFY(out.indexOf("window.__preText=(function(){")
                < out.indexOf("PagedPolyfill.preview()"));
        QVERIFY(out.indexOf("window.__verifyContent()")
                > out.indexOf("PagedPolyfill.preview()"));
    }

    void testBuildRenderHtmlTableColumnFixer() {
        // 超宽表格（列 min-content 之和 > 页宽）两症状的根治前置：
        // ① th,td 可任意断行：长 URL 等无断行点 token 不再撑大列 min-content
        //    （auto 布局表格永不小于 min-content → 右溢页宽被 sheet 静默右裁）。
        //    规则在 @media print 之外：paged.js 屏幕分页测量与打印折行必须一致，
        //    否则测量行高失真（打印侧折行更高 → 行溢出页底）
        // ② __fixTableColumns：首行列几何 → 百分比固定列（重写 colgroup + 每行
        //    单元格内联百分比宽 + table-layout:fixed + 清 min-width）。paged.js
        //    跨页碎片是 cloneNode(false) 浅克隆，丢 colgroup 子元素——列几何必须
        //    自描述在行单元格上，换页前后碎片列才能对齐。调用点在指纹快照之前
        //    （与 __splitPreLines 同一前置区，重排不改 textContent）
        QString out = PdfExporter::buildRenderHtml("<p>x</p>", "/tmp", "", "", specWithHeader());
        QVERIFY2(out.contains("th,td{overflow-wrap:anywhere;}"), qPrintable(out));
        // paged 路径**不包 @media print**：分页前的列宽归一/预分页切块都在屏幕
        // 媒体做，包着（等 paged.js preview() 里才解包）就量在「无基础 CSS」的
        // 裸布局上（实测行高 30px vs 成品 52px，切块塞进 1.7 倍行数）。与
        // nativeHf 同款：屏幕布局 = 打印布局。legacy 路径另测（仍包着）
        QVERIFY2(!out.contains("@media print{"), qPrintable(out));
        QVERIFY2(out.contains("window.__fixTableColumns=function()"), qPrintable(out));
        // 兼容层 cfg 测量基准随单元格字号（11pt = 单元格与正文统一，
        // 2026-09-24 定案）——测量与打印渲染必须同字号，列宽分配才不失真
        QVERIFY2(out.contains("fontSize:'11pt'"), qPrintable(out));
        // slack:2 边界余量（2026-09-26）：floor 恰=min-content 的边界列（已完成
        // =60px）经百分比回算少 0.02px 即折行——PDF/DOCX 与 HTML 导出同补 2px
        QVERIFY(out.contains("slack:2"));
        QVERIFY2(out.contains("table.style.tableLayout='fixed'"), qPrintable(out));
        // 列宽分配基准：页面内容宽（px）注入为 JS 全局（@page 宽−左右边距，
        // 横向换向取短边），不能拿屏幕视口宽度当可用宽
        QVERIFY2(out.contains("window.__pageContentWidthPx="), qPrintable(out));
        // 表格预分页（paged 路径）：跨页长表在分页前按行切块，避免 paged.js 的
        // 续表碎片在部分几何下把行留在页内、撑成整页高而不显示（实测：含脚注
        // 文档 1.1 表第 11 行、1.2 表首数据行丢失）。续块从页顶开始
        // （break-before:page），故**只需保证单块装得下一页**，与全文档页格无关
        // （旧实现按 y/Hpx 累加页格，而真实页高随脚注有无变化，步长差逐页累积，
        // 长表所在页越切越短——用户实测第 5/7 页下方空出好几行）
        QVERIFY2(out.contains("window.__prepaginateTables=function(A){"), qPrintable(out));
        QVERIFY(out.contains("table.pdf-tbl-split{break-before:page;}"));
        QVERIFY(out.contains("table.pdf-tbl-cont{break-before:page;margin-top:0;}"));
        QVERIFY(out.contains("window.__pagedContentPx="));
        // 单页可用高 = 版心高；**该页含脚注引用**再扣**实测**脚注区高 __pagedFnH
        // （测量遍量得，C++ 注回；不硬编码、不按脚注定义体高估；按页判定，不给
        // 整篇一刀切预留——无脚注的页白留一个脚注区高＝每页少一行）
        QVERIFY(out.contains("var F=window.__pagedFnH||0;"));
        QVERIFY(out.contains("var capAt=function(top,shift){"));
        QVERIFY(out.contains("if(ry>=top&&ry<top+A)return A-F;"));
        // 页游标：首块的起始页余量 / 续块各自页顶
        QVERIFY(out.contains("var pageInfo=function(y,shift){"));
        // 切块前先缓存各表顶与行高（切块会改 DOM：续表 margin-top:0、删行、插表）
        QVERIFY(out.contains("var list=[];"));
        QVERIFY(out.contains("list.push({t:t,all:all,y0:"));
        // 位移累加：切块把内容推到新页，后续内容整体下移——未切块的表尾在页格
        // 坐标下是 y+总高（y 已含前序位移），用 y0 会重复计入前序位移
        QVERIFY(out.contains("push+=Math.max(0,(pageTop+used)-(y+totalH));"));
        // 溢出是亚像素级的（实测 1.1 表首块 710px + 真实起点 248 超内容盒 956
        // 仅 2px 即触发 paged.js 切表→错位碎片丢行）⇒ 余量 ≥20px，且本页余量
        // 连首行都放不下时整表挪页（pdf-tbl-head）
        QVERIFY(out.contains("var slack=Math.max(A*0.005,20);"));
        QVERIFY(out.contains("table.pdf-tbl-head{break-before:page;}"));
        QVERIFY(out.contains("t.classList.add('pdf-tbl-head')"));
        QVERIFY(out.contains("sup[data-type=\\\"footnoteRef\\\"]"));
        // 脚注浮动必须**先于**预分页切块：def 变引用旁 inline-block 会把其后内容
        // 整体下推（实测 1.1 表顶 222 → 248），而首块余量依赖表顶位置
        QVERIFY(out.indexOf("window.__floatFootnotes();")
                < out.indexOf("window.__prepaginateTables(window.__pagedContentPx>0"));
        // 调用时机：列宽归一之后（行高才是终值）、paged.js 分页之前
        QVERIFY(out.contains("window.__prepaginateTables(window.__pagedContentPx>0?window.__pagedContentPx:window.__pagedBoxH)"));
        QVERIFY(out.indexOf("window.__prepaginateTables(window.__pagedContentPx>0")
                > out.indexOf("window.__fixTableColumns();"));
        QVERIFY(out.indexOf("window.__prepaginateTables(window.__pagedContentPx>0")
                < out.indexOf("PagedPolyfill.preview()"));
        // 滚动条压制罩住**全部分页前测量**（列宽归一 + 预分页切块），进分页前
        // 才清除：滚动条吃掉视口宽，屏幕布局会比 paged 页内容盒窄
        QVERIFY(out.indexOf("document.documentElement.style.overflow='hidden'")
                < out.indexOf("window.__prepaginateTables(window.__pagedContentPx>0"));
        QVERIFY(out.indexOf("window.__prepaginateTables(window.__pagedContentPx>0")
                < out.indexOf("document.documentElement.style.overflow=''"));
        // 测量遍：排两页（脚注区内容到第 2 页才定型）即中断（MEASURE_DONE），
        // 页数/注入由 C++ 侧两次加载完成
        QVERIFY(out.contains("if(window.__pagedPageN<2)return;"));
        QVERIFY(out.contains("throw new Error('MEASURE_DONE');"));
        QVERIFY(out.contains("window.__printState='measured';"));
        QVERIFY(out.contains("if(window.__printState==='measured')return 'measured';"));
        // 两趟 intrinsic 测量（min-content 作保底宽、max-content 作期望宽）：
        // 直接量取屏幕 auto 布局会把短列（编号/状态）压到单字宽——anywhere 使
        // min-content 塌缩为单字符，富余宽度按 max-content 比例尽归长列
        QVERIFY2(out.contains("table.style.width='min-content'"), qPrintable(out));
        QVERIFY2(out.contains("table.style.width='max-content'"), qPrintable(out));
        QVERIFY(out.indexOf("window.__fixTableColumns();")
                < out.indexOf("window.__preText=(function(){"));
    }

    void testBuildRenderHtmlInlineScriptsSyntaxValid() {
        // 内联 <script> 的 JS 语法有效性：preparePrint 是几十段拼接的字符串字面量，
        // 子串断言测不出语法错误——一处语法错即整块解析失败，__preparePrint 不存在、
        // __printState 永不置位 → C++ 侧 30s 超时回退原生分页（产物丢页眉页脚），
        // 且无任何报错指向真因。用 QJSEngine 包一层【不被调用】的函数体做纯语法
        // 检查：解析覆盖全串，顶层语句（window.* 赋值等）不执行，无 DOM 依赖
        QString out = PdfExporter::buildRenderHtml("<p>x</p>", "/tmp", "", "", specWithHeader());
        QRegularExpression re("<script>(.*?)</script>",
                              QRegularExpression::DotMatchesEverythingOption);
        auto it = re.globalMatch(out);
        int n = 0;
        while (it.hasNext()) {
            QString js = it.next().captured(1);
            // QJSEngine 要求先有 QCoreApplication（本测试族是 QTEST_APPLESS_MAIN）；
            // 函数级静态只构造一次，非 GUI 应用无平台插件依赖
            static int argc = 1;
            static char arg0[] = "test";
            static char *argv[] = {arg0, nullptr};
            static QCoreApplication app(argc, argv);
            QJSEngine engine;
            QJSValue r = engine.evaluate("(function(){\n" + js + "\n})");
            QVERIFY2(!r.isError(),
                     qPrintable(QStringLiteral("内联脚本%1语法错误: %2").arg(n).arg(r.toString())));
            ++n;
        }
        QVERIFY(n >= 3);   // rAF 垫片 + PagedConfig + preparePrint + KaTeX 引导
    }

    void testBuildRenderHtmlVerifyContentCoversImages() {
        // #33 img 维度指纹：图片无文本，textContent 指纹对图片丢失/裁切/重复铺排
        // 全部空转（#30 红基线实证：超高 img 溢出时 state=paged 不回退，被
        // .pagedjs_sheet overflow:hidden 静默裁切）——须独立比对 src 多重集。
        // 字符串形状锁（渲染行为由 E2E probe 直调 __verifyContent 验证，此处
        // 锁关键标识存在性，让「img 维度被整体删除」必转红）：
        // 1) 快照：__preImgs 与 __preText 同点（splitPreLines 之后、preview 之前
        //    ——splitPreLines 不动 img，同点即对称），从 document.body 取 img
        // 2) 比对：__verifyContent 函数体内对页面内容盒+脚注盒 querySelectorAll
        //    ('img') 取 post 侧多重集，不等返回 '图片指纹不一致'
        // 3) 文本指纹 '内容指纹不一致' 原样仍在（img 维度是叠加不是替换）
        QString out = PdfExporter::buildRenderHtml("<p>x</p>", "/tmp", "", "", specWithHeader());
        // 快照侧：标识存在 + 时机与 __preText 对称（splitPreLines 后、preview 前）
        QVERIFY(out.contains("window.__preText=(function(){"));
        QVERIFY(out.contains("window.__preImgs="));
        QVERIFY(out.contains("document.body.querySelectorAll('img')"));
        QVERIFY(out.indexOf("window.__preImgs=")
                > out.lastIndexOf("window.__splitPreLines();"));
        QVERIFY(out.indexOf("window.__preImgs=")
                < out.indexOf("PagedPolyfill.preview()"));
        // 比对侧：代码必须落在 __verifyContent 函数体内（仅快照侧出现 __preImgs
        // 不构成比对——切块到 __preparePrint 定义前，防标识在别处空过）
        const qsizetype v = out.indexOf("window.__verifyContent=function(){");
        QVERIFY(v >= 0);
        const qsizetype vend = out.indexOf("window.__preparePrint=function(){", v);
        QVERIFY(vend > v);
        const QString vc = out.mid(v, vend - v);
        QVERIFY(vc.contains("__preImgs"));
        QVERIFY(vc.contains("querySelectorAll('img')"));
        QVERIFY(vc.contains("'图片指纹不一致 '"));
        // 文本指纹原样仍在，且先于 img 维度（先文本后图片，丢失文本优先报文本）
        QVERIFY(vc.contains("'内容指纹不一致 '"));
        QVERIFY(vc.indexOf("'内容指纹不一致 '") < vc.indexOf("'图片指纹不一致 '"));
    }

    void testBuildRenderHtmlPagedInjectsPageRules() {
        PdfPageSpec s = specWithHeader();
        QString out = PdfExporter::buildRenderHtml("<p>x</p>", "/tmp", "", "", s);
        QVERIFY(out.contains("@top-center"));
        QVERIFY(out.contains("我的文档"));
        QVERIFY(out.contains("@bottom-right"));
        QVERIFY(out.contains("counter(page)"));
        // 页眉页脚贴页边距（与 nativeHf 同式）：paged.js 把页边距盒里的
        // vertical-align 翻译成 align-items（bottom→flex-end / top→flex-start），
        // 即"贴向正文一侧"；padding 留与正文的净空（kHfHugGapMm = 3mm）
        QVERIFY(out.contains("vertical-align:bottom;padding-bottom:3.00mm;"));
        QVERIFY(out.contains("vertical-align:top;padding-top:3.00mm;"));
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
        // paged 屏幕流与打印页内容盒同宽（2026-09-26 修复）：漏清 UA 默认 8px
        // 边距时屏幕流窄 16px，临界折行多折一行→预分页切块虚胖→断页留白
        QVERIFY(out.contains("html,body{margin:0;padding:0;}"));
        // TipTap 单元格内容包 <p>：清零格内段边距，行高与 DOCX 摊平单段一致
        //（2026-09-26 修复：p{margin-bottom:.3em} 每格加 3.3pt→行高 23.6→26.9pt）
        QVERIFY(out.contains("th,td p{margin:0;}"));
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
                == QStringLiteral(".mermaid-diagram svg{display:block;max-height:255.0mm!important;}"));
        // 横向 A4：内容盒高 210-36-2=172mm
        s.orientation = QPageLayout::Landscape;
        out = PdfExporter::buildRenderHtml("<p>x</p>", "/tmp", "", "", s);
        QVERIFY(out.contains(".mermaid-diagram svg{display:block;max-height:168.0mm!important;}"));
        // 上限规则须在屏幕媒体生效（paged.js 在屏幕媒体上分页测量）：paged 路径
        // 整份基础 CSS 都不包 @media print（否则分页前测量在裸布局上做）
        QVERIFY(out.contains(".mermaid-diagram svg{"));
        QVERIFY(!out.contains("@media print{"));
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
            "img:not([width]):not([style]){max-height:255.0mm!important;"
            "width:auto!important;}")));
        // 横向 A4：内容盒高 210-36-2=172mm
        s.orientation = QPageLayout::Landscape;
        out = PdfExporter::buildRenderHtml("<p>x</p>", "/tmp", "", "", s);
        QVERIFY(out.contains(QStringLiteral(
            "img:not([width]):not([style]){max-height:168.0mm!important;"
            "width:auto!important;}")));
        // 两条规则均须在屏幕媒体生效——paged.js 在屏幕媒体上分页测量：
        // max-width 只在打印媒体生效时，超宽图屏幕布局无宽度约束 → 横向溢出
        // .pagedjs_sheet（overflow:hidden）→ 静默右裁（指纹通过不回退，比回退
        // 更隐蔽）；max-height 同理须在分页测量前生效。paged 路径整份基础 CSS
        // 不包 @media print ⇒ 两条规则无条件生效
        QVERIFY(out.contains("img{max-width:100%;}"));
        QVERIFY(!out.contains("@media print{"));
        QVERIFY(out.contains("img:not([width])"));
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
        // paged 态：img{max-width:100%;} 出现两处——屏幕媒体分页测量副本
        // （pageCss 注入）+ 模板基础 CSS 副本（paged 路径不包 @media print，
        // 无条件生效；两处同值，重复无害）
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
        // nativeHf 版式同理取微量值：打印区须覆盖整张纸（页眉带占纸顶、
        // 页脚覆盖层落纸底带），页边距由流内结构模拟
        const QMarginsF n = PdfExporter::nativeHfPrintMargins();
        QVERIFY(n.left() > 0.0 && n.left() <= 1.0);
        QVERIFY(n.top() > 0.0 && n.top() <= 1.0);
        QVERIFY(n.right() > 0.0 && n.right() <= 1.0);
        QVERIFY(n.bottom() > 0.0 && n.bottom() <= 1.0);
    }

    void testBuildRenderHtmlTitleEscaped() {
        PdfPageSpec s = specWithHeader();
        s.headerTitle = QStringLiteral("a\"b\\c\nd");
        QString out = PdfExporter::buildRenderHtml("<p>x</p>", "/tmp", "", "", s);
        QVERIFY(out.contains("a\\\"b\\\\c d"));   // " → \"，\ → \\，换行 → 空格
    }

private:
    void testPageNumberTextMatchesVerifyPattern() {
        // 页码文案单一来源的漂移锁：文案（ExportCommon::kPageText*）与成品校验
        // 正则必须同源——只改文案不改正则，nativeHf 每份产物都会判「页码漂移」
        // → 静默回退 paged.js（慢但正确，且无报错指向真因）
        const QString text = ExportCommon::pageNumberText(3, 11);
        QCOMPARE(text, QStringLiteral("第 3 页 / 共 11 页"));
        QString normalized = text;
        normalized.remove(' ');
        const auto m = ExportCommon::pageNumberVerifyPattern().match(normalized);
        QVERIFY2(m.hasMatch(), qPrintable(normalized));
        QCOMPARE(m.captured(1), QStringLiteral("3"));
        QCOMPARE(m.captured(2), QStringLiteral("11"));
    }

    PdfPageSpec specWithHeader() {
        PdfPageSpec s;
        s.headerEnabled = true; s.footerEnabled = true;
        s.headerTitle = QStringLiteral("我的文档");
        return s;
    }
};

QTEST_APPLESS_MAIN(TestPdfExporter)
#include "test_pdfexporter.moc"
