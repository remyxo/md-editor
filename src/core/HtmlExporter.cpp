#include "HtmlExporter.h"
#include "ExportCommon.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMimeDatabase>
#include <QRegularExpression>
#include <QSettings>

HtmlExporter::HtmlExporter(QObject *parent) : QObject(parent) {}

QString HtmlExporter::inlineCssFonts(const QString &css, const QString &fontDir)
{
    QRegularExpression re("url\\(fonts/([^)]+)\\)");
    QString out = css;
    int offset = 0;
    QRegularExpressionMatchIterator it = re.globalMatch(css);
    while (it.hasNext()) {
        auto m = it.next();
        QString name = m.captured(1);
        QFile f(fontDir + "/" + name);
        if (!f.open(QIODevice::ReadOnly)) continue;
        QByteArray bytes = f.readAll();
        f.close();
        const QString mime = ExportCommon::mimeForExt(QFileInfo(name).suffix());
        QString data = "url(data:" + mime + ";base64," + QString::fromLatin1(bytes.toBase64()) + ")";
        int start = m.capturedStart(0) + offset;
        int len = m.capturedLength(0);
        out.replace(start, len, data);
        offset += data.length() - len;
    }
    return out;
}

QString HtmlExporter::inlineImages(const QString &html, const QString &docDir)
{
    QRegularExpression re("<img\\b[^>]*src\\s*=\\s*[\"']([^\"']+)[\"']");
    // 内联策略：HTML 产物可嵌 svg/webp（浏览器都认），与 DOCX 白名单不同
    //（Word 只认 png/jpeg/gif）——围栏/内容校验用同一实现，策略各自给
    ExportCommon::ImagePolicy policy;
    policy.mimeWhitelist = {QStringLiteral("image/png"), QStringLiteral("image/jpeg"),
                            QStringLiteral("image/gif"), QStringLiteral("image/webp")};
    policy.svgByTextPrefix = true;
    QString out = html;
    int offset = 0;
    QRegularExpressionMatchIterator it = re.globalMatch(html);
    while (it.hasNext()) {
        auto m = it.next();
        const QString src = m.captured(1);
        // data:/远程 URL 原样保留（远程资源由阅读端自行取，不读本地字节）
        if (src.startsWith("data:") || src.startsWith("http://") || src.startsWith("https://"))
            continue;
        QByteArray bytes;
        QString mime;
        if (!ExportCommon::loadLocalImage(src, docDir, policy, &bytes, &mime))
            continue;
        const QString dataUri = "data:" + mime + ";base64,"
                                + QString::fromLatin1(bytes.toBase64());
        const int start = m.capturedStart(1) + offset;
        const int len = m.capturedLength(1);
        out.replace(start, len, dataUri);
        offset += dataUri.length() - len;
    }
    return out;
}

QString HtmlExporter::bodyCss(const QString &theme)
{
    const bool dark = (theme == "dark");
    if (dark) {
        return QStringLiteral(
            // 文档级折行：连续无空格长串（2.3 节色块串）必须能断行，否则横向
            // 撑爆正文盒（实测 li 内 scrollWidth>clientWidth、内容右溢被裁），
            // 而编辑器与两条 PDF 路径均折行——同一份文档四处呈现分叉。PDF 模板
            // 同款规则（见 PdfExporter 基础 CSS）：取 anywhere 而非 break-word，
            // 前者同时收敛 min-content，避免长串撑爆文档最小宽度
            "body{font-family:\"Noto Sans SC\",\"DejaVu Sans\",sans-serif;font-size:16px;"
            "line-height:1.7;color:#c9d1d9;background:#0d1117;max-width:860px;margin:0 auto;"
            "padding:40px 32px;overflow-wrap:anywhere;}"
            "h1,h2,h3,h4,h5,h6{line-height:1.3;margin:1.2em 0 .5em;font-weight:600;}"
            "h1{font-size:2em;border-bottom:1px solid #30363d;padding-bottom:.3em;}"
            "h2{font-size:1.5em;border-bottom:1px solid #30363d;padding-bottom:.3em;}"
            "h3{font-size:1.25em;}p{margin:.6em 0;}"
            "a{color:#58a6ff;text-decoration:none;}a:hover{text-decoration:underline;}"
            "code{background:#161b22;padding:2px 5px;border-radius:4px;font-size:.9em;}"
            "pre{background:#161b22;padding:12px 16px;border-radius:6px;overflow-x:auto;line-height:1.45;}"
            "pre code{background:transparent;padding:0;}"
            "blockquote{border-left:4px solid #0078d4;color:#8b949e;padding:4px 16px;margin:.8em 0;}"
            "table{border-collapse:collapse;margin:.8em 0;display:block;overflow-x:auto;}"
            // 长无断点串（URL/JWT）在固定列宽内折行：与 PDF 导出/PdfExporter 的
            // th,td{overflow-wrap:anywhere} 及 editor.css 对齐（三面已定调，HTML 补齐）
            "th,td{border:1px solid #30363d;padding:6px 13px;overflow-wrap:anywhere;}th{background:#21262d;font-weight:600;}"
            // TipTap 格内 <p> 段边距清零，与 PDF/DOCX 行高对齐，2026-09-26
            "th,td p{margin:0;}"
            "tr:nth-child(2n){background:#161b22;}img{max-width:100%;}"
            "hr{border:0;border-top:1px solid #30363d;margin:1.5em 0;}"
            "ul,ol{padding-left:1.5em;}li{margin:.3em 0;}"
        // 子代选择器 > 限定直接 li：嵌套在任务项内的通用列表 li 若也命中 flex
        // 会被当成 flex 容器，其子元素 p 与嵌套 <ol> 排成一行、序号消失（与 editor.css 对齐）
        "ul[data-type=\"taskList\"]{list-style:none;padding-left:0;}"
        "ul[data-type=\"taskList\"] > li{display:flex;align-items:flex-start;gap:8px;}"
        "ul[data-type=\"taskList\"] > li label{margin-top:3px;}"
        "ul[data-type=\"taskList\"] > li > div > p{margin:0;}"
            // 脚注基础版式（编号与内容同行的关键，自 editor.css 暗色版复刻）
            "[data-type=\"footnoteRef\"]{color:#8ab4f8;}"
            "div[data-type=\"footnoteContainer\"]{margin-top:2em;padding-top:.75em;"
            "border-top:1px solid #3c4043;font-size:.9em;}"
            "div[data-type=\"footnoteDef\"]{margin:.25em 0;overflow:hidden;}"
            ".footnote-marker{float:left;margin-right:.35em;font-weight:600;color:#8ab4f8;}"
            "div[data-type=\"footnoteDef\"] .footnote-content>*:first-child{margin-top:0;}"
            "[data-type=\"block-math\"]{display:block;text-align:center;margin:1em 0;overflow-x:auto;}"
            "[data-mermaid]{text-align:center;margin:1em 0;}");
    }
    return QStringLiteral(
        // 文档级折行：同暗色版（缘由见上），连续无空格长串溢出正文盒
        "body{font-family:\"Noto Sans SC\",\"DejaVu Sans\",sans-serif;font-size:16px;"
        "line-height:1.7;color:#24292f;background:#ffffff;max-width:860px;margin:0 auto;"
        "padding:40px 32px;overflow-wrap:anywhere;}"
        "h1,h2,h3,h4,h5,h6{line-height:1.3;margin:1.2em 0 .5em;font-weight:600;}"
        "h1{font-size:2em;border-bottom:1px solid #eaecef;padding-bottom:.3em;}"
        "h2{font-size:1.5em;border-bottom:1px solid #eaecef;padding-bottom:.3em;}"
        "h3{font-size:1.25em;}p{margin:.6em 0;}"
        "a{color:#0969da;text-decoration:none;}a:hover{text-decoration:underline;}"
        "code{background:#f0f1f3;padding:2px 5px;border-radius:4px;font-size:.9em;}"
        "pre{background:#f6f8fa;padding:12px 16px;border-radius:6px;overflow-x:auto;line-height:1.45;}"
        "pre code{background:transparent;padding:0;}"
        "blockquote{border-left:4px solid #0078d4;color:#57606a;padding:4px 16px;margin:.8em 0;}"
        "table{border-collapse:collapse;margin:.8em 0;display:block;overflow-x:auto;}"
        // 长无断点串（URL/JWT）在固定列宽内折行：与 PDF 导出及 editor.css 的
        // overflow-wrap:anywhere 对齐（暗色主题同）
        "th,td{border:1px solid #d0d7de;padding:6px 13px;overflow-wrap:anywhere;}th{background:#f6f8fa;font-weight:600;}"
        // TipTap 格内 <p> 段边距清零，与 PDF/DOCX 行高对齐，2026-09-26
        "th,td p{margin:0;}"
        "tr:nth-child(2n){background:#f6f8fa;}img{max-width:100%;}"
        "hr{border:0;border-top:1px solid #d0d7de;margin:1.5em 0;}"
        "ul,ol{padding-left:1.5em;}li{margin:.3em 0;}"
        // 子代选择器 > 限定直接 li：嵌套在任务项内的通用列表 li 若也命中 flex
        // 会被当成 flex 容器，其子元素 p 与嵌套 <ol> 排成一行、序号消失（与 editor.css 对齐）
        "ul[data-type=\"taskList\"]{list-style:none;padding-left:0;}"
        "ul[data-type=\"taskList\"] > li{display:flex;align-items:flex-start;gap:8px;}"
        "ul[data-type=\"taskList\"] > li label{margin-top:3px;}"
        "ul[data-type=\"taskList\"] > li > div > p{margin:0;}"
        // 脚注基础版式（编号与内容同行的关键，自 editor.css 复刻）：marker 浮动 +
        // 首子元素去顶距。footnote-content 是块级 div，无此规则时行内 marker 后跟
        // 块级元素被拆成匿名块、编号独占一行（用户实测：导出 HTML 里脚注名与脚注
        // 内容换行）。PDF 模板早有同款复刻，HTML 导出此前两套主题都缺
        "[data-type=\"footnoteRef\"]{color:#1967d2;}"
        "div[data-type=\"footnoteContainer\"]{margin-top:2em;padding-top:.75em;"
        "border-top:1px solid #e0e0e0;font-size:.9em;}"
        "div[data-type=\"footnoteDef\"]{margin:.25em 0;overflow:hidden;}"
        ".footnote-marker{float:left;margin-right:.35em;font-weight:600;color:#1967d2;}"
        "div[data-type=\"footnoteDef\"] .footnote-content>*:first-child{margin-top:0;}"
        "[data-type=\"block-math\"]{display:block;text-align:center;margin:1em 0;overflow-x:auto;}"
        "[data-mermaid]{text-align:center;margin:1em 0;}");
}

QString HtmlExporter::mermaidRenderScript(const QString &theme)
{
    const QString mermaidTheme = (theme == "dark") ? "dark" : "default";
    // mermaid.render 无容器分支会先按 render id 删除文档中既有元素
    //（removeExistingElements）。holder 若设同 id 会被它删掉，SVG 写入渲染后
    // 游离的节点，页面上图表消失。因此 holder 不设 id，render id 独立生成
    //（与 editor.js NodeView 的成熟模式一致）。IIFE 捕获每次迭代的 code/holder，
    // 避免 var 提升导致多个 .then 共享同一 holder。返回值是 Promise，
    // 全部渲染完成才 resolve，供 PDF 导出等异步路径等待。
    //
    // 第三参显式传在流 holder（2026-09-23）：甘特图布局宽取容器宽——无容器时
    // mermaid 用 body 宽布局。PDF 两分页路径的渲染视图宽不同（nativeHf 整纸
    // 宽 ~789px、paged 页内容宽 ~656px），且甘特图 viewBox 高恒定（行数驱动）、
    // 宽随容器，最终都以内容宽显示（width="100%"）⇒ 同一甘特图有脚注版（paged）
    // 比无脚注版（nativeHf）高 ~21%（实测 24.2px vs 19.7px 行高）。传在流
    // holder 令布局宽=显示宽，两路径均 1:1（隔离实验：body 789 + holder 653
    // → viewBox 653）。流程图/时序图等内容驱动图型不受容器宽影响。
    return QStringLiteral(
        "window.__renderAllMermaid=function(){"
        "return new Promise(function(resolve){"
        "if(!window.mermaid){resolve();return;}"
        "mermaid.initialize({startOnLoad:false,securityLevel:'strict',theme:'%1',"
        "themeVariables:{fontSize:'16px'}});"
        "var pres=document.querySelectorAll('pre code.language-mermaid');"
        "if(!pres.length){resolve();return;}"
        "var seq=0,jobs=[];"
        "for(var j=0;j<pres.length;j++){(function(code){"
        "var src=code.textContent;var pre=code.parentNode;"
        "var holder=document.createElement('div');"
        // 类名供 PDF 分页路径定位（.mermaid-diagram svg 容纳上限规则）；
        // 自包含 HTML 导出路径无对应 CSS，纯标记无副作用
        "holder.className='mermaid-diagram';"
        "pre.parentNode.replaceChild(holder,pre);"
        "jobs.push(mermaid.render('mermaid-export-'+(++seq),src,holder).then(function(res){holder.innerHTML=res.svg;})"
        ".catch(function(err){holder.textContent=src;}));"
        "})(pres[j]);}"
        "Promise.all(jobs).then(function(){resolve();});"
        "});};").arg(mermaidTheme);
}

QString HtmlExporter::tableColumnNormalizeScript()
{
    // 表格列宽归一：短列保真、长列吸收。HTML 导出与 PDF 导出共用同一实现
    //（PDF 侧经 __fixTableColumns 兼容层调用，见 PdfExporter 的 preparePrint）。
    //
    // 为何要显式分配：浏览器 auto 布局下 CJK 列可任意字间断行 ⇒ 其 min-content
    // 塌缩为单字宽，富余宽度按 max-content 比例尽归长列——「编号」列放不下三位
    // 数字（001 折两行）、「状态」列把「已完成」压成一字一行（用户实测 HTML 导出
    // 1.1 节 60 行表；PDF 侧同因，见 PdfExporter 该函数原注释）。
    //
    // 测量与分配算法抽至 web/table-column-layout.js（单一来源，编辑器 editor.js
    // 共用）：本函数运行时读 qrc 内联该文件，再定义写回壳 __normalizeTableColumns
    //（两趟测量 → floor/cap 三分支分配 → 写 <colgroup> 百分比 + 每行单元格内联
    // 百分比 + table-layout:fixed）。用百分比而非固定 px：HTML 产物在任意窗口
    // 宽度下按同一比例分配；PDF 页面宽固定，效果等同。列几何自描述在行单元格上
    //（不只靠 colgroup）：paged.js 跨页碎片是 cloneNode(false) 浅克隆、丢 colgroup
    // 子元素，只写 colgroup 会换页错列。
    //
    // cfg（可省）：avail 可用宽 px，缺省取 window.__pageContentWidthPx →
    // body.clientWidth；fontSize/padding/fontFamily 为测量期临时样式，PDF 传
    // 打印基准（10pt / 4px 8px / Noto Sans SC），HTML 导出不传 ⇒ 按产物实际
    // 渲染样式测量；slack 每列余量 px——HTML 导出与 PDF/DOCX 归一均传 2px
    //（2026-09-26 起三面同款；边界列百分比回算差亚像素即折行）；toTable
    // 量表自身盒宽再切 display:table。
    // px 模式（DOCX 经 TableColumnNormalizer 传）：colgroup 写 px 而非百分比、
    // 跳过单元格内联百分比写回——DocxExporter 解析 width:Npx 生成 tblGrid
    const QString shared = ExportCommon::readResource(QStringLiteral(":/web/table-column-layout.js"));
    return shared + QStringLiteral(
        "window.__normalizeTableColumns=function(cfg){"
        "cfg=cfg||{};"
        "var avail=(cfg.avail>0)?cfg.avail:((window.__pageContentWidthPx>0)?"
        "window.__pageContentWidthPx:(document.body?document.body.clientWidth:0));"
        "if(!(avail>0))return;"
        "document.querySelectorAll('table').forEach(function(table){"
        // 包装表（PDF nativeHf 的 table.hf）必须跳过：它包住整篇文档，其
        // querySelectorAll('td,th')/('tr') 覆盖全文档——本函数会给这些单元格写下
        // 「本表格的列宽百分比」（对包装表即每格 100%），污染随后所有内层表格的
        // intrinsic 测量（实测：短列被压成逐字竖排）
        "if(table.classList.contains('hf'))return;"
        // 每表可用宽：优先取表自身盒宽（HTML 导出下表格是 display:block，盒宽即
        // 容器内容宽；body.clientWidth 含内边距，会用宽 64px ⇒ 分配整体缩水、
        // 「已完成」仍差 5px 折行——实测）。PDF 侧不传 toTable：表格已是
        // display:table 且宽度随内容，量自身盒宽会拿到溢出宽，沿用页宽基准
        "var myAvail=avail;"
        "if(cfg.toTable){"
        "if(table.clientWidth>0)myAvail=table.clientWidth;"
        "table.style.display='table';"
        "table.style.overflowX='visible';"
        "}"
        "var m=window.__measureTableColumns(table,cfg);"
        "if(!m)return;"
        "var n=m.minW.length;"
        "var w=window.__allocateTableColumns(m.minW,m.maxW,myAvail,cfg.slack);"
        "var total=0;for(var j=0;j<n;j++)total+=w[j];"
        "if(!(total>0))return;"
        "var old=table.querySelector('colgroup');if(old)old.remove();"
        "var cg=document.createElement('colgroup');"
        "for(var c2=0;c2<n;c2++){"
        "var col=document.createElement('col');"
        "if(cfg.px){col.style.width=Math.round(w[c2])+'px';}"
        "else col.style.width=(w[c2]/total*100).toFixed(3)+'%';"
        "cg.appendChild(col);"
        "}"
        "table.insertBefore(cg,table.firstChild);"
        "if(!cfg.px){var rows=table.querySelectorAll('tr');"
        "for(var r=0;r<rows.length;r++){"
        "var idx=0;"
        "for(var c=0;c<rows[r].children.length;c++){"
        "var sp=Math.max(1,parseInt(rows[r].children[c].getAttribute('colspan')||'1',10));"
        "var wp=0;for(var q=0;q<sp&&idx+q<n;q++)wp+=w[idx+q];"
        "rows[r].children[c].style.width=(wp/total*100).toFixed(3)+'%';"
        "idx+=sp;"
        "}"
        "}"
        "}"
        "table.style.width='100%';"
        "table.style.minWidth='0';"
        "table.style.tableLayout='fixed';"
        "});"
        "};");
}

QString HtmlExporter::katexRenderScript()
{
    // KaTeX 公式渲染段（HTML 与 PDF 导出共用）：DOMContentLoaded 后把每个
    // [data-latex] 元素按内联/块级（data-type="block-math"）渲染；带
    // window.katex 守卫——库按需内联，缺失即整段跳过（产物退化为 LaTeX 原文，
    // 不报错）。原先 PDF 侧是同一段的无守卫副本、且用 dataset.type 取块级标记
    //（与 getAttribute('data-type') 等价）——两处写法分叉，合并回这一份
    return QStringLiteral(
        "window.addEventListener('DOMContentLoaded',function(){"
        "if(window.katex){var es=document.querySelectorAll('[data-latex]');"
        "for(var i=0;i<es.length;i++){var e=es[i],l=e.getAttribute('data-latex');if(!l)continue;"
        "try{katex.render(l,e,{throwOnError:false,displayMode:e.getAttribute('data-type')==='block-math'});}"
        "catch(err){e.textContent=l;}}}"
        "});");
}

QString HtmlExporter::renderScript(const QString &theme)
{
    // 组合式：KaTeX 段 + mermaid helper（只定义 __renderAllMermaid，HTML 路径
    // 在 DOMContentLoaded 内显式自调用，浏览器常驻无需等 Promise）+ hljs 段。
    // 保持与旧版一致的执行时序（KaTeX → Mermaid → highlight）。
    return katexRenderScript()
        + mermaidRenderScript(theme)
        + QStringLiteral(
            "window.addEventListener('DOMContentLoaded',function(){"
            "window.__renderAllMermaid();"
            "if(window.hljs){hljs.highlightAll();}"
            "});");
}

void HtmlExporter::exportToHtml(const QString &html, const QString &docDir,
                                const QString &outputPath, const QString &theme)
{
    QString body = inlineImages(html, docDir);

    // 内容按需注入重型库（与 PdfExporter::buildRenderHtml 同型，2026-09-15 性能
    // 取证）：无公式不内联 katex.min.js + base64 字体 CSS、无代码块不内联
    // highlight、无流程图不内联 3.5MB mermaid——纯文本文档产物从 ~4.5MB 瘦身
    // 到数百 KB。渲染脚本自带守卫（if(window.katex)/if(window.hljs)/
    // if(!window.mermaid)），库缺席即整段跳过。检测用原始 html（内联只改 img src）
    const bool needKatex = html.contains(QLatin1String("data-latex"));
    const bool needHljs = html.contains(QLatin1String("<code"));
    const bool needMermaid = html.contains(QLatin1String("language-mermaid"));
    QString katexCss = needKatex
        ? inlineCssFonts(ExportCommon::readResource(":/web/katex.min.css"), ":/web/fonts")
        : QString();
    QString hljsCss = needHljs
        ? ExportCommon::readResource(theme == "dark" ? ":/web/hljs-dark.css" : ":/web/hljs-light.css")
        : QString();
    QString libs;
    if (needKatex)
        libs += QStringLiteral("<script>%1</script>")
                    .arg(ExportCommon::readResource(":/web/katex.min.js"));
    if (needHljs)
        libs += QStringLiteral("<script>%1</script>")
                    .arg(ExportCommon::readResource(":/web/highlight.min.js"));
    if (needMermaid)
        libs += QStringLiteral("<script>%1</script>")
                    .arg(ExportCommon::readResource(":/web/mermaid.min.js"));

    // 自定义 CSS（覆盖默认）。剥 </style> 序列：本地设置携带它会把 <style> 块
    // 提前终结并向产物注入任意标记（自注入面）
    QSettings settings("md-editor", "md-editor");
    QString customCss = settings.value("editor/customCss").toString();
    customCss = ExportCommon::stripStyleClose(customCss);

    QString full = QStringLiteral(
        "<!DOCTYPE html><html><head><meta charset=\"UTF-8\">"
        "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">"
        "<title>Markdown 导出</title>"
        "<style>%1</style><style>%2</style><style>%3</style>%4"
        "</head><body>%5</body></html>")
        .arg(bodyCss(theme), katexCss, hljsCss,
             customCss.isEmpty() ? QString() : QString("<style>%1</style>").arg(customCss),
             body);

    // 渲染脚本（库按需拼接 + 驱动脚本恒定，顺序：KaTeX → highlight → Mermaid）
    QString scripts = libs + QStringLiteral("<script>%1</script>")
                                         .arg(renderScript(theme));
    // 表格列宽归一（与 PDF 导出共用实现）：等 load（字体/内联图片就位，测量才
    // 准）后调用。toTable 交派函数内做——须先量下表格盒宽（display:block 时即
    // 容器内容宽）再切 display:table，顺序反了会拿到收缩后的宽度。不做这步，
    // CJK 短列被压成一字一行（编号「001」折两行、状态「已完成」一字一行）
    scripts += QStringLiteral("<script>") + tableColumnNormalizeScript()
             + QStringLiteral(
                   "window.addEventListener('load',function(){try{"
                   "window.__normalizeTableColumns({toTable:true,slack:2});"
                   "}catch(e){console.warn('表格列宽归一失败: '+(e&&e.message||e));}});")
             + QStringLiteral("</script>");
    full.replace("</body>", scripts + "</body>");

    QFile out(outputPath);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Text)) {
        emit exportError("HTML 写入失败");
        return;
    }
    const QByteArray bytes = full.toUtf8();
    if (out.write(bytes) != bytes.size()) {
        // 磁盘满/写入中断：不得报成功（产物是不完整文件）
        out.close();
        emit exportError("HTML 写入失败：数据未完整落盘");
        return;
    }
    out.close();
    emit exportFinished(true, outputPath);
}
