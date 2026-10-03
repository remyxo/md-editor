#include <QTest>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QJSEngine>
#include <QJSValue>
#include <QRegularExpression>
#include "../src/core/HtmlExporter.h"
#include "../src/core/ExportCommon.h"

class TestHtmlExporter : public QObject {
    Q_OBJECT
private slots:
    void testMimeForExt() {   // mimeForExt 已迁 ExportCommon（三导出器共用）
        QCOMPARE(ExportCommon::mimeForExt("woff2"), QStringLiteral("font/woff2"));
        QCOMPARE(ExportCommon::mimeForExt("woff"), QStringLiteral("font/woff"));
        QCOMPARE(ExportCommon::mimeForExt("ttf"), QStringLiteral("font/ttf"));
        QCOMPARE(ExportCommon::mimeForExt("png"), QStringLiteral("image/png"));
        QCOMPARE(ExportCommon::mimeForExt("jpg"), QStringLiteral("image/jpeg"));
        QCOMPARE(ExportCommon::mimeForExt("jpeg"), QStringLiteral("image/jpeg"));
        QCOMPARE(ExportCommon::mimeForExt("svg"), QStringLiteral("image/svg+xml"));
    }

    void testBlockquoteLeftBorderIsBlueInBothThemes() {
        // 引用块左侧竖线统一为蓝色 #0078d4（与 PDF/DOCX 一致），亮暗主题均生效
        const QStringList themes{"light", "dark"};
        for (const QString &theme : themes) {
            QString css = HtmlExporter::bodyCss(theme);
            QVERIFY2(css.contains("blockquote{border-left:4px solid #0078d4"),
                     qPrintable(theme));
        }
    }

    void testInlineCssFontsReplacesUrlWithBase64() {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        QString fontsDir = dir.path() + "/fonts";
        QVERIFY(QDir().mkpath(fontsDir));
        QFile f(fontsDir + "/KaTeX_Main-Regular.woff2");
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("FONTDATA"); f.close();

        QString css = "src:url(fonts/KaTeX_Main-Regular.woff2) format('woff2');";
        QString out = HtmlExporter::inlineCssFonts(css, dir.path() + "/fonts");
        QVERIFY(out.contains("data:font/woff2;base64,"));
        QVERIFY(!out.contains("url(fonts/"));
    }

    // 1×1 真实 PNG 字节（内容级 MIME 校验后，伪字节夹具不再被内联）
    static QByteArray tinyPng() {
        static const unsigned char png[] = {0x89,0x50,0x4e,0x47,0x0d,0x0a,0x1a,0x0a,0,0,0,0x0d,0x49,0x48,0x44,0x52,
                                     0,0,0,1,0,0,0,1,0x08,0x06,0,0,0,0x1f,0x15,0xc4,0x89,0,0,0,0x0a,0x49,0x44,0x41,0x54,
                                     0x78,0x9c,0x63,0,1,0,0,0,0x05,0,1,0x0d,0x0a,0x2d,0xb4,0,0,0,0,0x49,0x45,0x4e,0x44,0xae,0x42,0x60,0x82};
        return QByteArray(reinterpret_cast<const char*>(png), sizeof(png));
    }

    void testInlineImagesReplacesRelativeSrcWithBase64() {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        QFile f(dir.path() + "/a.png");
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(tinyPng()); f.close();

        QString html = "<p><img src=\"a.png\" alt=\"x\"></p>";
        QString out = HtmlExporter::inlineImages(html, dir.path());
        QVERIFY(out.contains("data:image/png;base64,"));
        QVERIFY(!out.contains("src=\"a.png\""));
        QVERIFY(out.contains("alt=\"x\"")); // 其他属性保留
    }

    void testInlineImagesSkipsMissingFile() {
        QString html = "<p><img src=\"nope.png\"></p>";
        QString out = HtmlExporter::inlineImages(html, "/nonexistent");
        QVERIFY(out.contains("src=\"nope.png\"")); // 读不到时原样保留
    }

    void testInlineImagesHandlesSingleQuotedSrc() {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        QFile f(dir.path() + "/b.png");
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(tinyPng()); f.close();

        QString html = "<p><img src='b.png'></p>";
        QString out = HtmlExporter::inlineImages(html, dir.path());
        QVERIFY(out.contains("data:image/png;base64,"));
        QVERIFY(!out.contains("src='b.png'"));
    }

    void testBodyCssDiffersByTheme() {
        QVERIFY(HtmlExporter::bodyCss("light").contains("background:"));
        QVERIFY(HtmlExporter::bodyCss("light") != HtmlExporter::bodyCss("dark"));
    }

    void testBodyCssTaskListInlineLayout() {
        // 任务列表 checkbox 与文字需同行：直接 li 用 flex 布局（镜像 editor.css）。
        // 必须用子代选择器 > 限定直接 li——嵌套在任务项内的通用列表 li（如 列表2-2
        // 下再嵌 <ol>）若也命中 flex 会被当成 flex 容器，其子元素 p 与嵌套 <ol>
        // 排成一行、序号消失（导出嵌套列表回归）。
        QVERIFY(HtmlExporter::bodyCss("light").contains("ul[data-type=\"taskList\"] > li{display:flex;"));
        QVERIFY(HtmlExporter::bodyCss("dark").contains("ul[data-type=\"taskList\"] > li{display:flex;"));
        // 内容段落 margin 归零，避免顶部 .6em 把文字压到复选框下方（对齐）；
        // > div > p 只命中任务项内容直接段落，嵌套列表段落不受影响
        QVERIFY(HtmlExporter::bodyCss("light").contains("ul[data-type=\"taskList\"] > li > div > p{margin:0;}"));
        QVERIFY(HtmlExporter::bodyCss("dark").contains("ul[data-type=\"taskList\"] > li > div > p{margin:0;}"));
        // TipTap 单元格内容包 <p>：清零格内段边距，表格行高与 PDF/DOCX 对齐
        //（2026-09-26 修复：p{margin-bottom:.3em} 每格加 3.3pt→行高与 DOCX 分叉）
        QVERIFY(HtmlExporter::bodyCss("light").contains("th,td p{margin:0;}"));
        QVERIFY(HtmlExporter::bodyCss("dark").contains("th,td p{margin:0;}"));
    }

    void testBodyCssFootnoteMarkerInlineLayout() {
        // 脚注编号与内容需同行（镜像 editor.css）：marker 浮动 + 首子元素去顶距。
        // footnote-content 是块级 div，无此规则时行内 marker 后跟块级元素被拆成
        // 匿名块、编号独占一行（用户实测：导出 HTML 里脚注名与脚注内容换行）。
        // PDF 模板早有同款复刻，HTML 导出此前两套主题都缺
        for (const char *theme : {"light", "dark"}) {
            const QString css = HtmlExporter::bodyCss(theme);
            QVERIFY(css.contains("div[data-type=\"footnoteDef\"]{margin:.25em 0;overflow:hidden;}"));
            QVERIFY(css.contains(".footnote-marker{float:left;margin-right:.35em;font-weight:600;"));
            QVERIFY(css.contains("div[data-type=\"footnoteDef\"] .footnote-content>*:first-child{margin-top:0;}"));
            QVERIFY(css.contains("div[data-type=\"footnoteContainer\"]{margin-top:2em;padding-top:.75em;"));
        }
        QVERIFY(HtmlExporter::bodyCss("light").contains("color:#1967d2;"));
        QVERIFY(HtmlExporter::bodyCss("dark").contains("color:#8ab4f8;"));
    }

    void testBodyCssDocumentLevelOverflowWrap() {
        // 连续无空格长串（2.3 节色块串）必须在文档级断行：无此规则时 li 内长串
        // 横向溢出正文盒（实测 scrollWidth>clientWidth、内容被裁），而编辑器与
        // 两条 PDF 路径均折行——同一文档四处呈现分叉。须挂在 body 上而非
        // 局部元素：anywhere 同时收敛 min-content，避免长串撑爆文档最小宽度
        for (const char *theme : {"light", "dark"}) {
            const QString css = HtmlExporter::bodyCss(theme);
            const int bodyPos = css.indexOf("body{font-family:");
            QVERIFY(bodyPos >= 0);
            const int bodyEnd = css.indexOf('}', bodyPos);
            QVERIFY(bodyEnd > bodyPos);
            QVERIFY2(css.mid(bodyPos, bodyEnd - bodyPos).contains("overflow-wrap:anywhere;"),
                     theme);
        }
    }

    void testRenderScriptUsesTheme() {
        QVERIFY(HtmlExporter::renderScript("dark").contains("'dark'"));
        QVERIFY(HtmlExporter::renderScript("light").contains("'default'"));
    }

    void testRenderScriptMermaidStrictSecurity() {
        // mermaid securityLevel 必须 strict：loose 跳过 sanitizeUrl/DOMPurify，
        // 恶意图表标签（A["<img src=x onerror=…>"]）以原始 HTML 进 SVG、经
        // innerHTML 在导出产物里直接执行（受害者打开即触发）
        const QString js = HtmlExporter::renderScript("light");
        QVERIFY2(js.contains("securityLevel:'strict'"), qPrintable(js));
        QVERIFY(!js.contains("securityLevel:'loose'"));
        QVERIFY(!js.contains("securityLevel: 'loose'"));
    }

    void testInlineImagesFenceAndValidation() {
        // 内联防护三件套：① docDir 围栏（../ 逃逸与目录外绝对路径不内联）；
        // ② 内容级校验（伪装扩展名的非图片文件不内联——曾 ![](../../.ssh/id_rsa)
        // 把任意本地文件 base64 嵌进导出产物）；③ 真实图片正常内联
        QTemporaryDir dir; QVERIFY(dir.isValid());
        QDir docDir(dir.path());
        docDir.mkdir("assets");
        // 1×1 真实 PNG（同 testImageEmbeddedAsMedia 的字节）
        const unsigned char png[] = {0x89,0x50,0x4e,0x47,0x0d,0x0a,0x1a,0x0a,0,0,0,0x0d,0x49,0x48,0x44,0x52,
                                     0,0,0,1,0,0,0,1,0x08,0x06,0,0,0,0x1f,0x15,0xc4,0x89,0,0,0,0x0a,0x49,0x44,0x41,0x54,
                                     0x78,0x9c,0x63,0,1,0,0,0,0x05,0,1,0x0d,0x0a,0x2d,0xb4,0,0,0,0,0x49,0x45,0x4e,0x44,0xae,0x42,0x60,0x82};
        QFile good(dir.path() + "/assets/ok.png");
        QVERIFY(good.open(QIODevice::WriteOnly));
        good.write(reinterpret_cast<const char*>(png), sizeof(png)); good.close();
        // docDir 外的同字节 PNG
        QTemporaryDir outside; QVERIFY(outside.isValid());
        QFile esc(outside.path() + "/secret.png");
        QVERIFY(esc.open(QIODevice::WriteOnly));
        esc.write(reinterpret_cast<const char*>(png), sizeof(png)); esc.close();
        // docDir 内伪装成 .png 的文本
        QFile fake(dir.path() + "/assets/fake.png");
        QVERIFY(fake.open(QIODevice::WriteOnly));
        fake.write("this is definitely not an image, maybe /etc/passwd content");
        fake.close();

        // 围栏内真图 → 内联为 data:
        QString out = HtmlExporter::inlineImages(
            "<p><img src=\"assets/ok.png\"></p>", dir.path());
        QVERIFY2(out.contains("data:image/png;base64,"), qPrintable(out));
        // ../ 逃出 docDir → 保留原 src，不内联
        QString escRel = QString("<p><img src=\"../../%1/secret.png\"></p>")
                             .arg(QFileInfo(outside.path()).fileName());
        // 相对 ../ 在临时目录父层不稳定，直接用绝对路径测围栏（更直接）
        Q_UNUSED(escRel);
        QString out2 = HtmlExporter::inlineImages(
            QString("<p><img src=\"%1/secret.png\"></p>").arg(outside.path()), dir.path());
        QVERIFY2(!out2.contains("data:"), qPrintable(out2));
        QVERIFY(out2.contains("secret.png"));
        // 伪装图片 → 不内联
        QString out3 = HtmlExporter::inlineImages(
            "<p><img src=\"assets/fake.png\"></p>", dir.path());
        QVERIFY2(!out3.contains("data:"), qPrintable(out3));
        QVERIFY(out3.contains("fake.png"));
    }

    void testRenderScriptMermaidRenderIdDecoupledFromHolder() {
        // mermaid.render 不传容器时会先按 id 删除文档中既有元素
        //（removeExistingElements）。若给占位 holder 设置与 render 相同的 id，
        // holder 会被 mermaid 当作残留元素删掉，SVG 写入游离节点，图表消失。
        // render id 必须与 holder.id 解耦（对齐 editor.js 的 NodeView 模式）。
        QString s = HtmlExporter::renderScript("light");
        QVERIFY(!s.contains("holder.id='mermaid"));
        QVERIFY(!s.contains("mermaid.render(holder.id"));
        // 第三参传在流 holder：甘特图布局宽取容器宽，无容器时用 body 宽——
        // PDF 两路径视图宽不同（nativeHf 整纸 / paged 内容宽）导致同源甘特图
        // 高度差 ~21%；传 holder 令布局宽=显示宽（viewBox 宽=holder 宽）
        QVERIFY(s.contains("mermaid.render('mermaid-export-'+(++seq),src,holder)"));
    }

    void testRenderScriptMermaidLoopCapturesHolderPerIteration() {
        // var holder 在循环中被异步 .then 回调共享，多图会全部写入最后一个
        // holder。循环体须以 IIFE 捕获每次迭代的 code/holder。
        QString s = HtmlExporter::renderScript("light");
        QVERIFY(s.contains("(function(code){"));
        QVERIFY(s.contains("})(pres[j]);"));
    }

    void testMermaidRenderScriptIsolated() {
        QString s = HtmlExporter::mermaidRenderScript("dark");
        QVERIFY(s.contains("'dark'"));
        QVERIFY(s.contains("pre code.language-mermaid"));
        QVERIFY(s.contains("__renderAllMermaid"));
        QVERIFY(s.contains("mermaid.render('mermaid-export-'+(++seq),src,holder)"));
        QVERIFY(!s.contains("<script"));   // 片段不含 script 标签，调用方自行包裹
        // 定宽修复（放大病理）：mermaid 根 svg 带 width="100%"，在 PDF 渲染页
        // 容器里拉满宽 + preserveAspectRatio=meet 等比放大到 max-height 帽，
        // 窄高图字号放大 4~6 倍（编辑区 _sanitizeMermaidSvg 早已修同病理，导出
        // 路径漏）。innerHTML 注入后必须按根元素 viewBox 显式定宽 ⇒ 1:1 自然
        // 尺寸；且 viewBox 必须 DOM 级从根 svg 属性取（holder.querySelector），
        // 不得对 SVG 字符串做正则——甘特 defs 内 pattern viewBox 会误取宽度
        QVERIFY(s.contains("holder.querySelector('svg')"));
        QVERIFY(s.contains("setAttribute('width',"));
        QVERIFY(s.contains("svg.style.maxWidth='100%'"));
    }

    void testExportBodyPercentPlaceholdersSurvive() {
        // 组装用多参 .arg()：替换文本不会被重新扫描，正文字面 %1~%9 必须幸存
        //（若有人改成链式单参 .arg(a).arg(b) 会吞掉正文占位符，此测试拦截）
        QTemporaryDir dir; QVERIFY(dir.isValid());
        QString out = dir.path() + "/p.html";
        QString body = "<p>进度 80%1、50%2、33%3、25%4、10%5、1%6、%7、%8、%9、%99</p>";
        HtmlExporter exporter;
        exporter.exportToHtml(body, dir.path(), out, "light");
        QFile f(out);
        QVERIFY(f.open(QIODevice::ReadOnly | QIODevice::Text));
        QString html = QString::fromUtf8(f.readAll());
        f.close();
        QVERIFY(html.contains("80%1"));
        QVERIFY(html.contains("50%2"));
        QVERIFY(html.contains("33%3"));
        QVERIFY(html.contains("25%4"));
        QVERIFY(html.contains("10%5"));
        QVERIFY(html.contains("%99"));
        // 脚本块插在 </body> 前，只断言 body 开头连续段
        QVERIFY(html.contains("<body>" + body));
    }

    void testTableColumnNormalizeScriptShape() {
        // 表格列宽归一（短列保真、长列吸收）：浏览器 auto 布局下 CJK 列的
        // min-content 塌缩为单字宽，富余宽按 max-content 比例尽归长列——「编号」
        // 列放不下「001」（折两行）、「状态」列把「已完成」压成一字一行。与 PDF
        // 导出共用这一份实现（PdfExporter 侧经 __fixTableColumns 兼容层调用）
        const QString js = HtmlExporter::tableColumnNormalizeScript();
        QVERIFY(js.contains("window.__normalizeTableColumns=function(cfg){"));
        // 两趟 intrinsic 测量：min-content 作保底、max-content 作期望
        QVERIFY(js.contains("table.style.width='min-content'"));
        QVERIFY(js.contains("table.style.width='max-content'"));
        // 测量期关断行（拉丁整词/CJK 逐字才是自然断行单元）；字号/边距/字族按
        // cfg 可选——HTML 导出不传（按产物实际渲染样式测），PDF 传打印基准
        QVERIFY(js.contains("all[i].style.overflowWrap='normal';"));
        QVERIFY(js.contains("all[i].style.wordBreak='keep-all';"));
        QVERIFY(js.contains("if(cfg.fontSize)all[i].style.fontSize=cfg.fontSize;"));
        QVERIFY(js.contains("if(cfg.padding)all[i].style.padding=cfg.padding;"));
        QVERIFY(js.contains("if(cfg.fontFamily)table.style.fontFamily=cfg.fontFamily;"));
        // cfg.border 测量期清边框守卫（γ 修复，2026-10-03）：td/th 与 table
        // 两处都要清——只清单元格会残留表级 UA 边框 0.5px 到首末列
        //（w2-gamma border-css-zero 实验证）。守卫式=HTML/PDF 既有调用点
        //（不传该键）构造性无变化
        QVERIFY(js.contains("if(cfg.border)all[i].style.borderWidth=cfg.border;"));
        QVERIFY(js.contains("if(cfg.border)table.style.borderWidth=cfg.border;"));
        // 百分比固定列：colgroup + 每行单元格内联宽都写（paged.js 跨页碎片是
        // cloneNode(false) 浅克隆、丢 colgroup 子元素，只写 colgroup 会换页错列）
        QVERIFY(js.contains("col.style.width=(w[c2]/total*100).toFixed(3)+'%';"));
        QVERIFY(js.contains("rows[r].children[c].style.width=(wp/total*100).toFixed(3)+'%';"));
        QVERIFY(js.contains("table.style.tableLayout='fixed';"));
        // 包装表（PDF nativeHf 的 table.hf）跳过：污染全文档单元格的测量
        QVERIFY(js.contains("if(table.classList.contains('hf'))return;"));
        // 每表可用宽：HTML 导出取表自身盒宽再切 display:table（body.clientWidth
        // 含内边距会用宽 64px ⇒ 分配整体缩水、状态列仍差 5px 折行，实测）；
        // PDF 侧不传 toTable，沿用页宽基准（其表格宽度随内容，量自身盒宽会拿到溢出宽）
        // slack 余量：HTML 导出传 2px（百分比折算+边框折叠吃掉零点几像素，
        // 短列卡在 max-content 边界即折行）；PDF 不传 ⇒ 既有产物逐页不变
        QVERIFY(js.contains("maxW[j]+=(slack||0)"));
        // lift 抬升：minW 超均份额的列 floor 认领超均份额（至多 lift×avail），
        // 过约束压缩分支按占比给超长不可断列让宽（数值行为见 LiftsOverweightColumn）
        QVERIFY(js.contains("Math.min(minW[j],(lift||0.5)*avail)"));
        QVERIFY(js.contains("if(cfg.toTable){"));
        QVERIFY(js.contains("if(table.clientWidth>0)myAvail=table.clientWidth;"));
        QVERIFY(js.contains("table.style.display='table';"));
        // 单一来源：测量/分配抽至 web/table-column-layout.js，本函数运行时读
        // qrc 内联 + 写回壳调用（编辑器 editor.js 共用同一份算法）
        QVERIFY(js.contains("__measureTableColumns"));
        QVERIFY(js.contains("__allocateTableColumns"));
        // 测量前剥掉既有 colgroup：编辑器二次测量时克隆表带着上轮百分比 col
        // 会污染 auto 测量；导出侧旧 colgroup 本来就在写回前移除，行为不变
        QVERIFY(js.contains("oldCg.remove()"));
        // px 模式（DOCX 经 TableColumnNormalizer）：colgroup 写 px（DocxExporter
        // 解析 width:Npx 生成 tblGrid）、跳过单元格内联百分比写回（DOCX 列宽走
        // tcW，无 paged.js 跨页碎片）；百分比原路径两处锚定子串仍逐字保留
        QVERIFY(js.contains("if(cfg.px){col.style.width=Math.round(w[c2])+'px';}"));
        QVERIFY(js.contains("else col.style.width=(w[c2]/total*100).toFixed(3)+'%';"));
        QVERIFY(js.contains("if(!cfg.px){var rows=table.querySelectorAll('tr');"));
        // 闭合括号锚定：if(!cfg.px){ 块的收尾（防错删后百分比路径 brace 失衡）
        QVERIFY(js.contains("}table.style.width='100%';"));
    }

    void testAllocateTableColumnsLiftsOverweightColumn() {
        // 行为单测（QJSEngine 直跑共享模块纯函数，夹具取 1.2 表实测值）：
        // 过约束区间（avail ≤ n×最窄列 maxW）原 floor=avail/n 封顶把内容差异
        // 抹成均分——12 列表 @624px：含 1350px 不可断 URL 的列也只拿 8.3%。
        // lift 偏好（第 5 参缺省 0.5）：minW 超均份额的列 floor 抬到
        // min(minW, lift×avail)，压缩分支按占比让宽；未超均份额的列不变。
        const QString shared = ExportCommon::readResource(QStringLiteral(":/web/table-column-layout.js"));
        static int argc = 1;
        static char arg0[] = "test";
        static char *argv[] = {arg0, nullptr};
        static QCoreApplication app(argc, argv);
        QJSEngine engine;
        // QJSEngine（V4）无 globalThis/window（Chromium 有）：模块 IIFE 末尾回退
        // 到 globalThis，测试侧预定义为全局对象即可，不改生产模块
        engine.evaluate(QStringLiteral("var globalThis=this;var window=this;"));
        QJSValue v = engine.evaluate(shared);
        QVERIFY2(!v.isError(), qPrintable(v.toString()));

        // QVERIFY 宏展开为 return;，不能放进返回值的 lambda——出参式带回错误
        auto allocate = [&engine](const QString &minW, const QString &maxW, double avail,
                                  double slack, QVector<double> *out) -> bool {
            QJSValue r = engine.evaluate(QStringLiteral(
                                              "window.__allocateTableColumns(%1,%2,%3,%4)")
                                              .arg(minW, maxW)
                                              .arg(avail)
                                              .arg(slack));
            if (r.isError()) return false;
            const int n = r.property("length").toInt();
            for (int i = 0; i < n; ++i)
                out->push_back(r.property(static_cast<quint32>(i)).toNumber());
            return true;
        };

        // 夹具＝1.2 表实测：11 短列（min=max=67）+ 列07 含 1350px 不可断串
        //（max=1815）。lift 后列07 份额 ≈29.8%（旧行为 8.3% 均分），短列 ≈6.4%
        QVector<double> w;
        QVERIFY2(allocate("[67,67,67,67,67,67,1350,67,67,67,67,67]",
                          "[67,67,67,67,67,67,1815,67,67,67,67,67]", 624, 2, &w),
                 "evaluate 失败");
        QCOMPARE(w.size(), 12);
        double total = 0;
        for (double x : w) total += x;
        QVERIFY2(total > 0, "总宽必须为正");
        QVERIFY2(w[6] / total > 0.25 && w[6] / total < 0.35,
                 qPrintable(QStringLiteral("列07 份额 %1（期望 0.25~0.35）").arg(w[6] / total)));
        for (int i = 0; i < 12; ++i) {
            if (i == 6) continue;
            QVERIFY2(w[i] / total > 0.04,
                     qPrintable(QStringLiteral("短列 %1 份额 %2 过小").arg(i).arg(w[i] / total)));
        }

        // 回归锚：无超长列（minW 全部 ≤ 均份额）行为与旧公式逐值一致。
        // 旧公式推导：maxW 封顶+slack=[62,182]，sumMax=244≤400 → 等比放大
        // [62×400/244, 182×400/244]
        QVector<double> w2;
        QVERIFY2(allocate("[60,60]", "[60,200]", 400, 2, &w2), "evaluate 失败");
        QCOMPARE(w2.size(), 2);
        QVERIFY(qAbs(w2[0] - 62 * 400 / 244.0) < 0.01);
        QVERIFY(qAbs(w2[1] - 182 * 400 / 244.0) < 0.01);
    }

    void testExportHtmlNormalizesTableColumns() {
        // 导出产物必须自带归一脚本与 load 后自调用：无这一步，CJK 短列被压成
        // 一字一行（用户实测 1.1 节 60 行表：编号列「001」折两行、状态列
        // 「已完成」一字一行）。display 由 CSS 的 block（宽表横向滚动兜底）
        // 切回 table，归一的固定布局才有意义
        QTemporaryDir dir; QVERIFY(dir.isValid());
        const QString out = dir.path() + "/t.html";
        HtmlExporter exporter;
        exporter.exportToHtml("<table><tr><th>编号</th><th>状态</th></tr>"
                              "<tr><td>001</td><td>已完成</td></tr></table>",
                              dir.path(), out, "light");
        QFile f(out);
        QVERIFY(f.open(QIODevice::ReadOnly | QIODevice::Text));
        const QString html = QString::fromUtf8(f.readAll());
        f.close();
        QVERIFY(html.contains("window.__normalizeTableColumns=function(cfg){"));
        QVERIFY(html.contains("window.__normalizeTableColumns({toTable:true,slack:2});"));
        // 全部内联 <script> 语法有效：语法错即整块解析失败，归一静默失效，
        // 而子串断言测不出来（同 PDF 侧 testBuildRenderHtmlInlineScriptsSyntaxValid）
        QRegularExpression re("<script>(.*?)</script>",
                              QRegularExpression::DotMatchesEverythingOption);
        auto it = re.globalMatch(html);
        int n = 0;
        while (it.hasNext()) {
            const QString js = it.next().captured(1);
            // QJSEngine 要求先有 QCoreApplication（本测试族是 QTEST_APPLESS_MAIN）
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
        QVERIFY(n >= 2);   // 渲染驱动脚本 + 列宽归一脚本
    }
};

QTEST_APPLESS_MAIN(TestHtmlExporter)
#include "test_htmlexporter.moc"
