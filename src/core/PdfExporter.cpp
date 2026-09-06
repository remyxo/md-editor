#include "PdfExporter.h"
#include "HtmlExporter.h"
#include <QWebEnginePage>
#include <QWebEngineSettings>
#include <QUrl>
#include <QSettings>
#include <QFile>
#include <QFileInfo>

PdfExporter::PdfExporter(QObject *parent)
    : QObject(parent)
    , m_renderView(new QWebEngineView())
{
    m_renderView->setVisible(false);
    m_renderView->settings()->setAttribute(
        QWebEngineSettings::LocalContentCanAccessFileUrls, true);
    m_printPoll.setInterval(100);
    connect(&m_printPoll, &QTimer::timeout, this, &PdfExporter::pollPrintState);
}

PdfExporter::~PdfExporter()
{
    delete m_renderView;
}

// CSS content 字符串转义：\ 与 "；换行替换为空格（页眉单行）
static QString cssContentString(QString text)
{
    text.replace('\\', QStringLiteral("\\\\"));
    text.replace('"', QStringLiteral("\\\""));
    text.replace('\n', QLatin1Char(' '));
    text.replace('\r', QLatin1Char(' '));
    return text;
}

QString PdfExporter::buildRenderHtml(const QString &html, const QString &docDir,
                                     const QString &katexCss, const QString &customCss)
{
    return buildRenderHtml(html, docDir, katexCss, customCss, PdfPageSpec());
}

QString PdfExporter::buildRenderHtml(const QString &html, const QString &docDir,
                                     const QString &katexCss, const QString &customCss,
                                     const PdfPageSpec &spec)
{
    // 图片 base64 内联（相对路径相对 docDir 解析；绝对路径原样；读不到保留 src）
    QString body = HtmlExporter::inlineImages(html, docDir);

    const bool paged = spec.pagedActive();
    // @page 规则（paged 激活时）：置于 @media print 之外——paged.js 在屏幕媒体
    // 上解析分页；margin 顺序 CSS = 上 右 下 左
    QString pageCss;
    if (paged) {
        QString sizeStr = QPageSize(spec.paper).key();   // "A4"/"Letter"/"A5"…
        if (spec.orientation == QPageLayout::Landscape)
            sizeStr += QLatin1String(" landscape");
        pageCss = QStringLiteral("@page{size:%1;margin:%2mm %3mm %4mm %5mm;")
                      .arg(sizeStr,
                           QString::number(spec.marginsMm.top()),
                           QString::number(spec.marginsMm.right()),
                           QString::number(spec.marginsMm.bottom()),
                           QString::number(spec.marginsMm.left()));
        if (spec.headerEnabled)
            pageCss += QStringLiteral(
                           "@top-center{content:\"%1\";font-size:9pt;color:#666;}")
                           .arg(cssContentString(spec.headerTitle));
        if (spec.footerEnabled)
            pageCss += QStringLiteral(
                           "@bottom-right{content:\"第 \" counter(page) \" 页 / 共 \" "
                           "counter(pages) \" 页\";font-size:9pt;color:#666;}");
        pageCss += QLatin1Char('}');
    }
    // 旧 @page{margin:0}（Qt QPageLayout 边距方案）仅 legacy 路保留；
    // paged 路径下若同存，@media print 内的后位规则会级联覆盖新 margin
    const QString legacyPageZero = paged ? QString()
                                         : QStringLiteral("@page{margin:0;}");

    // 打印前置入口：mermaid 先渲染（SVG 定型），paged 后跑（分页测量正确）。
    // 完成态走 window.__printState 状态机（Qt 的 runJavaScript 不等待 Promise，
    // C++ 侧轮询此标志）：working=进行中；paged=分页完成（0 边距打印）；
    // legacy=无 paged 直接过（用户边距）；fallback=paged 失败——还原 body 后
    // 走 legacy 打印，绝不把被 paged.js wrapContent 掏空的 body 交给 printToPdf
    const QString preparePrint = QStringLiteral(
        "window.__printState='idle';"
        "window.__restoreBody=function(){"
        "var t=document.querySelector(\"body > template[data-ref='pagedjs-content']\");"
        "if(t){var c=t.content;while(c.firstChild){document.body.insertBefore(c.firstChild,t);}t.remove();}"
        "var ps=document.querySelector('body > .pagedjs_pages');if(ps)ps.remove();"
        "};"
        // 代码块逐行重排（仅 paged 路径调用）：把 pre 内容按源码行拆成块级 span。
        // paged.js 对 pre 文本节点的字符偏移断点在行间空白区几何不可靠（pre-wrap 下
        // 空白无字形），断点落进 \"。\\n ␣␣␣\" 这类区域时会把前段尾行整行丢掉
        // （R2 实测断页处丢 1 行，断点位置随 mermaid 尺寸微差漂移，抽奖式复现）。
        // 逐行 span 让分页只发生在元素边界——与段落同路径，历轮零丢失
        "window.__splitPreLines=function(){"
        "document.querySelectorAll('pre').forEach(function(pre){"
        "var host=pre.querySelector('code')||pre;"
        "if(host.textContent.indexOf('\\n')<0)return;"
        "var units=[];"
        "(function collect(n){"
        "for(var i=0;i<n.childNodes.length;i++){"
        "var c=n.childNodes[i];"
        "if(c.nodeType===3)units.push(c);"
        "else if(c.nodeType===1){"
        "if(c.textContent.indexOf('\\n')<0)units.push(c);"
        "else collect(c);"
        "}"
        "}"
        "})(host);"
        "var lines=[[]];"
        "units.forEach(function(u){"
        "if(u.nodeType===3){"
        "var s=u.textContent,parts=s.split('\\n'),pos=0;"
        "parts.forEach(function(seg,i){"
        "if(i>0)lines.push([]);"
        "if(seg)lines[lines.length-1].push({text:seg});"
        "pos+=seg.length+1;"
        "});"
        "}else{lines[lines.length-1].push({node:u});}"
        "});"
        // 尾随换行按 HTML 解析语义丢弃（不补空行）；行内空行用 nbsp 占位保行高
        "if(lines.length>1&&!lines[lines.length-1].length)lines.pop();"
        "var frag=document.createDocumentFragment();"
        "lines.forEach(function(ln){"
        "var sp=document.createElement('span');"
        "sp.className='pl';"
        "if(!ln.length)sp.appendChild(document.createTextNode('\\u00A0'));"
        "else{ln.forEach(function(seg){"
        "if(seg.text!==undefined)sp.appendChild(document.createTextNode(seg.text));"
        "else sp.appendChild(seg.node);"
        "});}"
        "frag.appendChild(sp);"
        "});"
        "while(host.firstChild)host.removeChild(host.firstChild);"
        "host.appendChild(frag);"
        "});"
        "};"
        // 表格断点修正（仅 paged 路径注册）：paged.js 的 findOverflow 元素分支只在
        // 节点 top ≥ 页内容区底时才选它做断点——溢出时选中的永远是「下一行」，
        // 而行底与区底压线贴合（实测差 2px）的骑跨行留在本页碎片里，其行盒被
        // removeOverflow + rebuildTableFromBreakToken 的表格手术拉伸到整内容区高
        // （实测 986px，正常 34px），屏幕几何判定在盒内，但打印重排的亚像素差使
        // 整行落到页盒外不绘制——整行静默丢失（用户实测第 17/46 行，DOM 完整、
        // 纸上没有）。onOverflow 钩子（官方扩展点）把断点回退到骑跨行之前：
        // 留 8px 安全间隙（覆盖实测 2px 压线差，小于正常行高 34px），骑跨行整体
        // 移到下页重新完整布局；断点落进行内（td 的 p 上）时整行带走
        "window.__fixTableBreaks=function(){"
        "if(!(window.Paged&&window.Paged.Handler&&window.Paged.registerHandlers))return;"
        "class TableRowBreakFixer extends window.Paged.Handler{"
        "onOverflow(overflow,rendered,bounds){"
        "try{"
        "if(!overflow||!overflow.startContainer)return overflow;"
        "var sc=overflow.startContainer,so=overflow.startOffset;"
        // 原生溢出段语义 = [断点, rendered 末尾]（findOverflow 的 Find End 步：
        // range.setEndAfter(rendered.lastChild)）——removeOverflow 用
        // extractContents 摘走整段。改写 Range 时必须同样拉到 rendered 末尾：
        // 只 selectNode(行) 会让 extractContents 只摘单行，其余溢出行残留在
        // 本页碎片里（打印时被页区裁掉不可见，但 DOM 重复 23 行触发指纹误报）
        "var extend=function(node){"
        "var r=document.createRange();r.selectNode(node);"
        "if(rendered&&rendered.lastChild)r.setEndAfter(rendered.lastChild);"
        "return r;"
        "};"
        "if(sc.nodeType===1&&(sc.nodeName==='TBODY'||sc.nodeName==='THEAD')){"
        "var rows=sc.children;"
        "var row=rows[so];var guard=0;"
        "while(row&&row.previousElementSibling&&guard<200){"
        "var prev=row.previousElementSibling;"
        "var pb=prev.getBoundingClientRect();"
        "if(pb.bottom>bounds.bottom-8&&pb.top>=bounds.top-1){row=prev;guard++;}"
        "else break;"
        "}"
        "if(row&&row!==rows[so]){"
        "return extend(row);"
        "}"
        "return overflow;"
        "}"
        "var node=sc.nodeType===3?sc.parentNode:sc;"
        "if(!node||!node.closest)return overflow;"
        "var cell=node.closest('td,th');"
        "if(!cell)return overflow;"
        "var tr=cell.closest('tr');"
        "if(!tr)return overflow;"
        "return extend(tr);"
        "}catch(e){return overflow;}"
        "}"
        "}"
        "window.Paged.registerHandlers(TableRowBreakFixer);"
        "};"
        // 内容指纹自检（保底零丢失）：分页前快照 body 文本（覆盖所有块类型——
        // 段落/标题/表格/代码/公式/图注），分页后汇总 .pagedjs_page_content 文本
        // 比对（页眉页脚在 .pagedjs_margin 盒内，不计入）。任何静默丢失（未知的
        // 内容形态触发未设防的断点路径）→ 立即还原 body 走 fallback 原生打印——
        // 保底零丢失优先于页眉页脚
        "window.__verifyContent=function(){"
        "try{"
        "var pages=document.querySelectorAll('.pagedjs_page_content');"
        "if(!pages.length)return '无分页内容';"
        // 脚注渲染进 .pagedjs_area 内独立的 footnote 盒（内容盒之外），一并计入
        "var fns=document.querySelectorAll('.pagedjs_footnote_content');"
        "var post='';for(var i=0;i<pages.length;i++)post+=pages[i].textContent;"
        "for(var k=0;k<fns.length;k++)post+=fns[k].textContent;"
        "var a=(window.__preText||'').replace(/\\s+/g,'');"
        "var b=post.replace(/\\s+/g,'');"
        "if(a!==b){"
        "var j=0;while(j<Math.min(a.length,b.length)&&a.charAt(j)===b.charAt(j))j++;"
        "return '内容指纹不一致 '+a.length+'→'+b.length+' 首异位@'+j;"
        "}"
        "return true;"
        "}catch(e){return '校验异常: '+(e&&e.message||e);}"
        "};"
        "window.__preparePrint=function(){"
        "if(window.__printState==='working')return window.__printState;"
        "window.__printState='working';"
        "var fallback=function(e){"
        "console.warn('paged.js 分页失败，回退原生分页: '+(e&&e.message||e));"
        "try{window.__restoreBody();}catch(x){}"
        "window.__printState='fallback';return 'fallback';};"
        "return window.__renderAllMermaid()"
        ".then(function(){"
        "if(!window.PagedPolyfill){window.__printState='legacy';return 'legacy';}"
        "try{window.__fixTableBreaks();}catch(e){console.warn('表格断点修正注册失败: '+(e&&e.message||e));}"
        "try{window.__splitPreLines();}catch(e){console.warn('代码块逐行重排失败: '+(e&&e.message||e));}"
        // 指纹快照必须在 splitPreLines 之后（它重排 pre 文本，前后快照要同基线）
        "window.__preText=document.body.textContent;"
        "return PagedPolyfill.preview()"
        ".then(function(){"
        "var ok=window.__verifyContent();"
        "if(ok!==true){"
        "console.warn('分页内容校验失败，回退原生分页: '+ok);"
        "try{window.__restoreBody();}catch(x){}"
        "window.__printState='fallback';return 'fallback';"
        "}"
        "window.__printState='paged';return 'paged';"
        "},fallback);"
        "})"
        ".catch(fallback);"
        "};");
    // rAF 垫片（仅 paged 路径）：渲染视图是永不可见的隐藏控件，Chromium 不派发
    // 合成帧，requestAnimationFrame 永不触发——paged.js 渲染队列
    // （Queue.tick=requestAnimationFrame）会饿死，preview() 永不 settle。
    // 本页一次性打印、无动画，rAF 用定时器等价替换；必须在 polyfill 之前注入
    // （Queue 构造时捕获全局 requestAnimationFrame）
    const QString pagedScripts = paged
        ? QStringLiteral("<script>"
                         "window.requestAnimationFrame=function(cb){"
                         "return setTimeout(function(){cb(performance.now());},0);};"
                         "</script>"
                         "<script>window.PagedConfig={auto:false};</script>"
                         "<script src=\"paged.polyfill.js\"></script>")
        : QString();

    QString fullHtml = QString(
        "<!DOCTYPE html><html><head><meta charset=\"UTF-8\"><style>"
        "%1"                                    // @page 分页规则（paged 激活时）
        "@media print{"
        "%2"                                    // @page{margin:0}（仅 legacy）
        "body{font-family:\"Noto Sans SC\",\"DejaVu Sans\",sans-serif;"
        "font-size:11pt;line-height:1.6;color:#333;}"
        "h1{font-size:1.7em;margin-top:1em;margin-bottom:0.3em;}"
        "h2{font-size:1.3em;margin-top:0.8em;margin-bottom:0.2em;}"
        "h3{font-size:1.1em;margin-top:0.6em;margin-bottom:0.2em;}"
        "p{margin-top:0;margin-bottom:0.3em;}"
        // pre 用 pre-wrap 取代 overflow-x:auto：滚动容器让分页器 breakToken 卡死
        // （Unable to layout item → 静默丢段，R2 实测丢 8 行）；折行后每个行盒都是
        // 合法断点，超一页高的代码块也能逐行分页，长 token 由 anywhere 兜底折行
        "pre{background:#f4f5f7;padding:10px 14px;border-radius:4px;"
        "font-family:monospace;font-size:9.5pt;line-height:1.35;margin:0.5em 0;"
        "white-space:pre-wrap;overflow-wrap:anywhere;}"
        // 逐行重排的行 span（__splitPreLines 生成）：块级让分页落在行间元素边界；
        // avoid 兜底长行折行后的行盒（单逻辑行 ≤1 页高时无副作用）
        "pre .pl{display:block;break-inside:avoid;}"
        "code{background:#f0f0f0;padding:1px 3px;border-radius:2px;font-size:0.9em;"
        "overflow-wrap:anywhere;}"
        // 表行不跨页拆分（与下方 __fixTableBreaks 分工：CSS 兜住「整行越界」的
        // 常规断点路径——paged.js 只对 td/th 的 avoid 走「整行带走」工作区，上游
        // 注释：take the whole row, not just the cell，只加 tr 时行内断点仍穿透；
        // 骑跨行压线贴合区底的场景 CSS 永远查不到——那是 onOverflow 回退修复的）
        "tr,th,td{break-inside:avoid;}"
        "table{border-collapse:collapse;width:100%%;margin:0.5em 0;}"
        "th,td{border:1px solid #ccc;padding:4px 8px;font-size:10pt;}"
        "th{background:#f5f5f5;}"
        "img{max-width:100%%;}"
        "blockquote{border-left:3px solid #0078d4;padding:4px 12px;"
        "color:#555;margin:0.5em 0;}"
        "[data-type=\"block-math\"]{display:block;text-align:center;margin:1em 0;}"
        "ul[data-type=\"taskList\"]{list-style:none;padding-left:0;}"
        // 子代选择器 > 限定直接 li：嵌套在任务项内的通用列表 li（如 列表2-2 下再嵌 <ol>）
        // 若也命中 flex 会被当成 flex 容器，其子元素 p 与嵌套 <ol> 排成一行、序号消失（与 editor.css 对齐）
        "ul[data-type=\"taskList\"] > li{display:flex;align-items:flex-start;gap:8px;}"
        "ul[data-type=\"taskList\"] > li label{margin-top:3px;}"
        "ul[data-type=\"taskList\"] > li > div > p{margin:0;}"
        "}"
        "%3"                                    // KaTeX CSS
        "</style>"
        "%4"                                    // PagedConfig + polyfill（paged 时）
        "<script src=\"katex.min.js\"></script>"
        "<script src=\"mermaid.min.js\"></script>"
        "<script>"
        "%5"                                    // __preparePrint
        "window.addEventListener('DOMContentLoaded',function(){"
        "var els=document.querySelectorAll('[data-latex]');"
        "for(var i=0;i<els.length;i++){"
        "var el=els[i];"
        "var latex=el.getAttribute('data-latex');"
        "if(!latex)continue;"
        "try{"
        "katex.render(latex,el,{throwOnError:false,displayMode:el.dataset.type==='block-math'});"
        "}catch(e){el.textContent=latex;}"
        "}"
        "});"
        "</script>"
        "</head><body>%6</body></html>"
    ).arg(pageCss, legacyPageZero, katexCss, pagedScripts, preparePrint, body);

    // 注入 mermaid 渲染 helper（定义 window.__renderAllMermaid，返回 Promise，
    // 不自调用——__preparePrint 显式链式调用并等待）。PDF 固定亮色打印。
    fullHtml.replace("</head>",
        QString("<script>%1</script></head>").arg(HtmlExporter::mermaidRenderScript("default")));

    if (!customCss.isEmpty()) {
        fullHtml.replace("</head>", QString("<style>%1</style></head>").arg(customCss));
    }
    return fullHtml;
}

void PdfExporter::exportToPdf(const QString &html, const QString &docDir, const QString &outputPath,
                              QPageLayout::Orientation orientation,
                              QPageSize::PageSizeId paperSize)
{
    QSettings settings("md-editor", "md-editor");
    double mt = settings.value("export/marginTop", 1.8).toDouble();
    double mb = settings.value("export/marginBottom", 1.8).toDouble();
    double ml = settings.value("export/marginLeft", 1.8).toDouble();
    double mr = settings.value("export/marginRight", 1.8).toDouble();
    int orient = settings.value("export/orientation",
        static_cast<int>(orientation)).toInt();
    int paper = settings.value("export/paperSize",
        static_cast<int>(paperSize)).toInt();
    QString customCss = settings.value("editor/customCss").toString();

    // 页眉页脚规格（任一启用激活 paged.js 分页；标题取导出文件名基名——
    // 用户在保存框确认的名字，可预期，不猜文档首个 H1）
    PdfPageSpec spec;
    spec.paper = static_cast<QPageSize::PageSizeId>(paper);
    spec.orientation = static_cast<QPageLayout::Orientation>(orient);
    spec.marginsMm = QMarginsF(ml * 10, mt * 10, mr * 10, mb * 10);   // 左上右下 mm
    spec.headerEnabled = settings.value("export/pdfHeaderEnabled", true).toBool();
    spec.footerEnabled = settings.value("export/pdfFooterEnabled", true).toBool();
    spec.headerTitle = QFileInfo(outputPath).completeBaseName();

    // 读取 KaTeX CSS 并内联（字体 url() 通过 qrc: baseUrl 解析）
    QString katexCss;
    QFile cssFile(":/web/katex.min.css");
    if (cssFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        katexCss = QString::fromUtf8(cssFile.readAll());
        cssFile.close();
    }

    // 构建 PDF 渲染页面：图片 base64 内联 + 内联 KaTeX CSS + JS + 渲染脚本
    QString fullHtml = buildRenderHtml(html, docDir, katexCss, customCss, spec);

    m_pendingOutputPath = outputPath;
    // paged 路径边距由 @page 承载（页面盒自带页边距），Qt 布局传 0 防双重边距；
    // legacy 路径维持 Qt QPageLayout 边距（设置值为 cm，QPageLayout::Millimeter 需要 mm）
    m_pendingLayout = QPageLayout(QPageSize(static_cast<QPageSize::PageSizeId>(paper)),
        static_cast<QPageLayout::Orientation>(orient),
        spec.pagedActive() ? QMarginsF(0, 0, 0, 0)
                           : QMarginsF(ml * 10, mt * 10, mr * 10, mb * 10),
        QPageLayout::Millimeter);
    // 原生版式（用户边距）：legacy 路径与 paged 失败/超时回退共用
    m_legacyLayout = QPageLayout(QPageSize(static_cast<QPageSize::PageSizeId>(paper)),
        static_cast<QPageLayout::Orientation>(orient),
        QMarginsF(ml * 10, mt * 10, mr * 10, mb * 10), QPageLayout::Millimeter);

    m_printPoll.stop();
    m_printPollCount = 0;
    m_printStarted = false;

    disconnect(m_renderView, &QWebEngineView::loadFinished, this, nullptr);
    disconnect(m_renderView->page(), &QWebEnginePage::pdfPrintingFinished,
               this, nullptr);

    connect(m_renderView, &QWebEngineView::loadFinished,
            this, &PdfExporter::onLoadFinished);
    connect(m_renderView->page(), &QWebEnginePage::pdfPrintingFinished,
            this, &PdfExporter::onPdfPrintingFinished);

    m_renderView->setHtml(fullHtml, QUrl("qrc:/web/"));
}

void PdfExporter::onLoadFinished(bool ok)
{
    if (!ok) {
        emit exportError("HTML 渲染失败");
        return;
    }
    // 打印前置：KaTeX（同步，已完成）→ mermaid 渲染 → paged.js 分页（任一启用时）。
    // 注意：Qt 的 runJavaScript 不等待 Promise（Qt 6.8 实测回调立即触发），
    // 不能在其回调里直接 printToPdf——否则打印瞬间 body 可能已被 paged.js
    // wrapContent 掏成空 <template>（产出 1 页空白 PDF）。改为触发前置链后
    // 轮询 window.__printState，拿到终态再按对应版式打印
    m_renderView->page()->runJavaScript("window.__preparePrint()");
    m_printPollCount = 0;
    m_printPoll.start();
}

void PdfExporter::pollPrintState()
{
    // 超时兜底（30s × 100ms）：rAF 垫片失效等极端情况导致 __printState 永停
    // working——还原 body 走 legacy 版式，宁可无页眉页脚也不交白纸
    if (++m_printPollCount > 300) {
        m_printPoll.stop();
        if (m_printStarted)
            return;
        m_printStarted = true;
        m_renderView->page()->runJavaScript(
            "try{window.__restoreBody&&window.__restoreBody();}catch(e){}");
        emit pagedFallbackUsed();
        m_renderView->page()->printToPdf(m_pendingOutputPath, m_legacyLayout);
        return;
    }
    m_renderView->page()->runJavaScript("window.__printState",
        [this](const QVariant &v) {
            const QString state = v.toString();
            // 未到终态（idle/working/空）继续轮询；回调与停表存在竞态，须防重
            if (state != QLatin1String("paged") && state != QLatin1String("legacy")
                && state != QLatin1String("fallback"))
                return;
            m_printPoll.stop();
            if (m_printStarted)
                return;
            m_printStarted = true;
            const bool paged = (state == QLatin1String("paged"));
            if (state == QLatin1String("fallback"))
                emit pagedFallbackUsed();
            m_renderView->page()->printToPdf(m_pendingOutputPath,
                                             paged ? m_pendingLayout : m_legacyLayout);
        });
}

void PdfExporter::onPdfPrintingFinished(const QString &filePath, bool success)
{
    if (success)
        emit exportFinished(true, filePath);
    else
        emit exportError("PDF 生成失败");
}
