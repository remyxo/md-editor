#include "PdfExporter.h"
#include "HtmlExporter.h"
#include <QWebEnginePage>
#include <QWebEngineSettings>
#include <QUrl>
#include <QSettings>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QJsonDocument>
#include <QJsonObject>

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
    // 页面内容宽（CSS px，96dpi）：__fixTableColumns 列宽分配的可用宽基准。
    // 声明在 if(paged) 外——pagedScripts 拼接处也要用
    double pageContentWidthPx = 0;
    // @page 规则（paged 激活时）：置于 @media print 之外——paged.js 在屏幕媒体
    // 上解析分页；margin 顺序 CSS = 上 右 下 左
    QString pageCss;
    if (paged) {
        // 两向统一发显式 mm 尺寸（宽前高后），不用命名尺寸与 landscape 关键字：
        // vendored paged.js 的 pageSizes 表键为 A0-A10/B4/B5（大写）与
        // letter/legal/ledger（小写英寸系），而 Qt QPageSize::key() 返回
        // "Letter"/"Legal"（首字母大写）——查表无大小写归一即失配（A 系与
        // B4/B5 恰好同形幸免），width/height 解析为空 → 尺寸变量永不写入，
        // DOM 页盒停留样式表默认的 8.5×11in 纵向，与所选纸打印版式分叉（内容
        // 被裁、页眉页脚错位；打印纸本身也被分页器注入的 @page{size:letter}
        // 带偏）。纵向 Letter 纯靠默认 8.5×11in 恰为其实际尺寸巧合蒙对，
        // 纵向 Legal（8.5×14in）是真错；横向换向尺寸同病先修（#29），此处
        // 纵向并入同路径——显式 mm 走 Dimension 解析直设 width/height，
        // 命名尺寸表不再被依赖。毫米值定点 1 位小数——与 Qt 点位换算的微差
        // （<0.1mm）对布局无影响
        const QSizeF paperMm = QPageSize(spec.paper).size(QPageSize::Millimeter);
        QString sizeStr;
        if (spec.orientation == QPageLayout::Landscape) {
            // 横向：宽=纵向高（换向），宽前高后、宽>高
            sizeStr = QStringLiteral("%1mm %2mm")
                          .arg(paperMm.height(), 0, 'f', 1)
                          .arg(paperMm.width(), 0, 'f', 1);
        } else {
            // 纵向：宽<高，宽前高后
            sizeStr = QStringLiteral("%1mm %2mm")
                          .arg(paperMm.width(), 0, 'f', 1)
                          .arg(paperMm.height(), 0, 'f', 1);
        }
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
        // mermaid SVG 页高容纳上限：mermaid 渲染的 svg 带 width="100%" + 内联
        // max-width，宽度自适应容器、但高度无约束——高于内容区的图不可分页
        // 拆分 → 分页器 "Unable to layout item" → 内容重复铺排 → 指纹校验拦截
        // 回退原生分页（产物无页眉页脚）。横向 A4 内容高仅约 174mm，常见时序图
        // （实测 735px≈195mm）纵向正常、横向必超——「横向导出无页眉页脚」的
        // 根因。只约束 max-height：宽度归 mermaid 自身机制（width="100%" +
        // 内联 max-width 永不超容器），加 width:auto 会把小图放大到内容区宽
        // （R1 审查实测 300×150 被放大 2.17×）；高度超限时 viewBox 按缺省
        // meet 等比居中（盒宽不变）。2mm 安全间隙防压线亚像素溢出（区底贴合
        // 即触发 Unable to layout）。规则须在 @media print 之外——paged.js 在
        // 屏幕媒体上分页测量（paperMm 与上方 sizeStr 同一次读取共用）
        const qreal capH = (spec.orientation == QPageLayout::Landscape ? paperMm.width()
                                                                       : paperMm.height())
                           - spec.marginsMm.top() - spec.marginsMm.bottom() - 2.0;
        pageCss += QStringLiteral(".mermaid-diagram svg{max-height:%1mm!important;}")
                       .arg(capH, 0, 'f', 1);
        // 普通 <img> 页高容纳上限（与上方 mermaid 上限同型，capH 同源）：高于
        // 内容区的图片是原子元素不可分页拆分 → 分页器 "Unable to layout item"
        // → 内容重复铺排 → 指纹校验拦截回退原生分页（无页眉页脚）；横向 A4
        // 内容高仅约 172mm，竖版截图/长图必超。
        // ① max-width:100% 全量适用（只收缩不放大，height:auto 时纵横比自动
        //    保持，无放大陷阱）——且必须与下方 max-height 一样在 @media print
        //    之外：paged.js 在屏幕媒体上分页测量，仅打印媒体生效的宽度约束挡
        //    不住屏幕布局的横向溢出——超宽图溢出 .pagedjs_sheet（overflow:
        //    hidden）被静默右裁，指纹通过不回退，比回退更隐蔽
        // ② 高度上限只对 :not([width]) 生效（形状调查：导出 HTML 一律来自
        //    TipTap getHTML()，schema 只序列化 src/alt/title/width/height，
        //    常规插入/粘贴/md4c 加载路径均不产 width 属性，选择器覆盖全部常规
        //    图片；显式 width 属性唯一来源是用户字面 HTML）。刻意排除是防放大
        //    陷阱：width:auto!important 会覆盖 width 属性（展示提示层，强度低
        //    于 important CSS），把用户显式缩小的缩略图放大回自然尺寸（R1 审查
        //    实测 300×150 被放大 2.17× 的同型陷阱）——用户显式尺寸超高时回退
        //    保底仍在。width:auto 与 max-height 恰成一对：高度超限时按纵横比
        //    等比缩宽（CSS 替换元素约束），不超不动。:not([style]) 同理——
        //    当前链路 ProseMirror 解析丢弃非 schema 的 style 属性，排除是零
        //    成本护栏（width:auto!important 会覆盖非 important 的内联样式宽度）
        pageCss += QStringLiteral("img{max-width:100%;}");
        pageCss += QStringLiteral(
                       "img:not([width]):not([style]){max-height:%1mm!important;"
                       "width:auto!important;}")
                       .arg(capH, 0, 'f', 1);
        // 表格长 token 断行：长 URL/连续串无断行点 → 列 min-content 撑大 → auto
        // 布局表格永不小于 min-content → 右溢页宽被 sheet（overflow:hidden）静默
        // 右裁（与图片右裁同型的纯几何损失，指纹拦不住）。与 pre/code 的 anywhere
        // 同型；须在 @media print 之外——paged.js 屏幕分页测量与打印折行必须一致，
        // 否则测量行高失真（打印侧折行更高 → 行溢出页底）
        pageCss += QStringLiteral("th,td{overflow-wrap:anywhere;}");
        // 脚注随页浮动（Word 形态）：pdf-fn 类的 def 由 paged.js Footnotes 处理器
        // 搬入所在页的页底脚注区（.pagedjs_footnote_area，flex 列贴内容区底）。
        // 浮动前提：def 在文档流中位于引用点旁边——__floatFootnotes 已把文末
        // 容器里的 def 移到首个 sup 引用之后，才能落在引用所在页
        pageCss += QStringLiteral(
                       "div[data-type=\"footnoteDef\"].pdf-fn{float:footnote;}");
        // 分页流内临时形态：inline-block 参与行盒（块级会在引用处把段落拆成
        // 匿名块断行）；搬入脚注区后恢复块级占满区宽
        pageCss += QStringLiteral(
                       ".pagedjs_page_content div[data-type=\"footnoteDef\"].pdf-fn"
                       "{display:inline-block;}"
                       ".pagedjs_footnote_area div[data-type=\"footnoteDef\"].pdf-fn"
                       "{display:block;margin:.2em 0;}");
        // 脚注区分隔线（Word 同款：横线之上正文、之下脚注）——仅当页确有脚注
        // （空态 pagedjs_footnote_empty 由引擎 display:none）；字号缩小对齐 Word
        pageCss += QStringLiteral(
                       ".pagedjs_footnote_area>.pagedjs_footnote_content"
                       ":not(.pagedjs_footnote_empty)"
                       "{border-top:1px solid #999;padding-top:.25em;"
                       "font-size:.8em;line-height:1.5;}");
        // paged.js 会在引用位自动插 [data-footnote-call]::after{
        // content:counter(footnote)} 自动编号——与源码 sup 编号重复，抑制之
        pageCss += QStringLiteral(
                       "[data-footnote-call]::after{content:none!important;}");
        // 脚注碎片禁用 text-align-last:justify：上游 Footnotes.handleAlignment
        // 对溢出拆分的碎片无条件打 justify（正文 Breaks 处理器的同款方法还要求
        // text-align:justify，脚注版没有这一前置）——配合上游内置规则
        // [data-align-last-split-element='justify']，拆分残留碎片的尾行被撑满
        // 全宽、逐字拉开（多段脚注跨页拆分实测：一行短文逐字散布整行）。本
        // 模板正文左对齐，碎片尾行恢复默认（auto=随 text-align）即可
        pageCss += QStringLiteral(
                       ".pagedjs_footnote_area [data-align-last-split-element='justify']"
                       "{text-align-last:auto;}");
        // 内容宽 = 纸宽 − 左右边距（横向换向取短边），mm→px 与 Chromium CSS px 一致
        const double contentWmm = (spec.orientation == QPageLayout::Landscape ? paperMm.height() : paperMm.width())
                                  - spec.marginsMm.left() - spec.marginsMm.right();
        pageContentWidthPx = contentWmm / 25.4 * 96.0;
    }
    // 旧 @page{margin:0}（Qt QPageLayout 边距方案）仅 legacy 路保留；
    // paged 路径下若同存，@media print 内的后位规则会级联覆盖新 margin
    const QString legacyPageZero = paged ? QString()
                                         : QStringLiteral("@page{margin:0;}");

    // 打印前置入口：mermaid 先渲染（SVG 定型），paged 后跑（分页测量正确）。
    // 完成态走 window.__printState 状态机（Qt 的 runJavaScript 不等待 Promise，
    // C++ 侧轮询此标志）：working=进行中；paged=分页完成（微量边距打印，
    // 见 pagedPrintMargins——页边距由 @page 承载）；
    // legacy=无 paged 直接过（用户边距）；fallback=paged 失败——还原 body 后
    // 走 legacy 打印，绝不把被 paged.js wrapContent 掏空的 body 交给 printToPdf
    const QString preparePrint = QStringLiteral(
        "window.__printState='idle';"
        "window.__restoreBody=function(){"
        "var t=document.querySelector(\"body > template[data-ref='pagedjs-content']\");"
        "if(t){var c=t.content;while(c.firstChild){document.body.insertBefore(c.firstChild,t);}t.remove();}"
        "var ps=document.querySelector('body > .pagedjs_pages');if(ps)ps.remove();"
        "};"
        // 脚注随页浮动前置（仅 paged 路径调用）：文末容器里的 def 移到各自首个
        // sup 引用之后并打 pdf-fn 类（float:footnote 指令由注入 CSS 承载），
        // paged.js 分页时将其搬入引用所在页的页底脚注区。同一 id 的后续引用
        // 不再移动（Word/DOCX 同款：一脚注一锚点）；无引用的 def 留在文末
        // 容器（保留容器分隔线形态）；全部移走后容器只剩空白则整体移除
        "window.__floatFootnotes=function(){"
        "var defs={};"
        "document.querySelectorAll('div[data-type=\"footnoteDef\"]').forEach(function(d){"
        "var id=d.getAttribute('data-id');if(id!==null)defs[id]=d;});"
        "var seen={};"
        "var sups=document.querySelectorAll('sup[data-type=\"footnoteRef\"]');"
        "for(var i=0;i<sups.length;i++){"
        "var sup=sups[i],id=sup.getAttribute('data-id');"
        "if(!id||seen[id]||!defs[id])continue;"
        "seen[id]=1;"
        "sup.insertAdjacentElement('afterend',defs[id]);"
        "defs[id].classList.add('pdf-fn');"
        "}"
        "var c=document.querySelector('div[data-type=\"footnoteContainer\"]');"
        "if(c&&!c.querySelector('div[data-type=\"footnoteDef\"]')&&!c.textContent.trim())c.remove();"
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
        // 表格列宽归一化（仅 paged 路径调用）：超宽表格右溢、跨页列错位、短列被
        // 挤压三个症状的根治。症状：① 列 min-content 之和 > 页宽（12 列 + 长
        // URL）→ auto 布局右溢被裁；② paged.js 跨页碎片是 cloneNode(false) 浅
        // 克隆——保 style 属性、丢 colgroup 子元素，碎片 auto 布局按自身内容重算
        // 列宽 → 换页前后列不对齐；③ 直接量取屏幕 auto 布局的列宽再锁定，短列
        // （编号/状态）被长列（备注/URL）的 max-content 按比例挤到单字宽，
        // 「001」「已完成」逐字竖排折行。方案：两趟 intrinsic 测量（min-content
        // 作保底、max-content 作期望；测量时内联打印字号/边距/字族——打印 CSS
        // 不作用于屏幕，且暂关 anywhere：拉丁整词/CJK 逐字才是自然断行单元）
        // → 「短列保真、长列吸收」分配：floor=min(max, 3×min, 等份额)，常态把
        // 富余按 slack 比例摊给长列 → 百分比固定列（colgroup + 每行单元格内联
        // 宽 + table-layout:fixed）：列几何与内容解耦且自描述在行上（碎片浅克
        // 隆也带走），换页前后一致
        "window.__fixTableColumns=function(){"
        "var avail=(window.__pageContentWidthPx>0)?window.__pageContentWidthPx:"
        "(document.body?document.body.clientWidth:0);"
        "if(!(avail>0))return;"
        "document.querySelectorAll('table').forEach(function(table){"
        "var firstRow=table.querySelector('tr');"
        "if(!firstRow||!firstRow.children.length)return;"
        "var saved=[],all=table.querySelectorAll('td,th');"
        "for(var i=0;i<all.length;i++){"
        "saved.push([all[i],all[i].getAttribute('style')]);"
        "all[i].style.fontSize='10pt';"
        "all[i].style.padding='4px 8px';"
        "all[i].style.overflowWrap='normal';"
        "}"
        "var savedTable=table.getAttribute('style');"
        "table.style.borderCollapse='collapse';"
        "table.style.fontFamily='Noto Sans SC,DejaVu Sans,sans-serif';"
        "table.style.tableLayout='auto';"
        "var expand=function(){"
        "var out=[],ci=0;"
        "for(var i=0;i<firstRow.children.length;i++){"
        "var span=Math.max(1,parseInt(firstRow.children[i].getAttribute('colspan')||'1',10));"
        "var w=firstRow.children[i].getBoundingClientRect().width/span;"
        "for(var s=0;s<span;s++)out[ci++]=w;"
        "}"
        "return out;"
        "};"
        "table.style.width='min-content';"
        "var minW=expand();"
        "table.style.width='max-content';"
        "var maxW=expand();"
        "var n=minW.length,floor=[],sumMax=0,sumFloor=0;"
        "for(var j=0;j<n;j++){"
        "if(!(maxW[j]>0))maxW[j]=(minW[j]>0)?minW[j]:1;"
        "if(!(minW[j]>0))minW[j]=maxW[j];"
        "floor[j]=Math.min(maxW[j],Math.min(minW[j]*3,avail/n));"
        "sumMax+=maxW[j];sumFloor+=floor[j];"
        "}"
        "var w=[];"
        "if(sumMax<=avail){"
        "for(j=0;j<n;j++)w.push(maxW[j]*avail/sumMax);"
        "}else if(sumFloor<=avail){"
        "var k=(avail-sumFloor)/(sumMax-sumFloor);"
        "for(j=0;j<n;j++)w.push(floor[j]+(maxW[j]-floor[j])*k);"
        "}else{"
        "for(j=0;j<n;j++)w.push(floor[j]*avail/sumFloor);"
        "}"
        "var total=0;for(j=0;j<n;j++)total+=w[j];"
        "if(!(total>0))return;"
        "for(i=0;i<all.length;i++){"
        "if(saved[i][1]===null)all[i].removeAttribute('style');"
        "else all[i].setAttribute('style',saved[i][1]);"
        "}"
        "if(savedTable===null)table.removeAttribute('style');"
        "else table.setAttribute('style',savedTable);"
        "var old=table.querySelector('colgroup');if(old)old.remove();"
        "var cg=document.createElement('colgroup');"
        "for(var c2=0;c2<n;c2++){"
        "var col=document.createElement('col');"
        "col.style.width=(w[c2]/total*100).toFixed(3)+'%';"
        "cg.appendChild(col);"
        "}"
        "table.insertBefore(cg,table.firstChild);"
        "var rows=table.querySelectorAll('tr');"
        "for(var r=0;r<rows.length;r++){"
        "var idx=0;"
        "for(var c=0;c<rows[r].children.length;c++){"
        "var sp=Math.max(1,parseInt(rows[r].children[c].getAttribute('colspan')||'1',10));"
        "var wp=0;for(var q=0;q<sp&&idx+q<n;q++)wp+=w[idx+q];"
        "rows[r].children[c].style.width=(wp/total*100).toFixed(3)+'%';"
        "idx+=sp;"
        "}"
        "}"
        "table.style.width='100%';"
        "table.style.minWidth='0';"
        "table.style.tableLayout='fixed';"
        "});"
        "};"
        // 内容指纹自检（保底零丢失）：分页前快照 body 文本（覆盖所有块类型——
        // 段落/标题/表格/代码/公式/图注），分页后汇总 .pagedjs_page_content 文本
        // 比对（页眉页脚在 .pagedjs_margin 盒内，不计入）。任何静默丢失（未知的
        // 内容形态触发未设防的断点路径）→ 立即还原 body 走 fallback 原生打印——
        // 保底零丢失优先于页眉页脚。文本指纹对图片维度空转（img 无文本，丢失/
        // 裁切/重复铺排均不改 textContent），故文本比对后另比对 img src 多重集
        "window.__verifyContent=function(){"
        "try{"
        "var pages=document.querySelectorAll('.pagedjs_page_content');"
        "if(!pages.length)return '无分页内容';"
        // 正文指纹：页面内容盒文本（剔除 def——随页浮动后 def 进各页脚注盒，
        // 无引用的 def 留在内容盒，两种去向都不属正文流）；def 文本单独走
        // 字符多重集（见下方脚注指纹）
        "var post='',fnText='';"
        "for(var i=0;i<pages.length;i++){"
        "var c=pages[i].cloneNode(true);"
        "var ds=c.querySelectorAll('div[data-type=\"footnoteDef\"]');"
        "for(var k=0;k<ds.length;k++){fnText+=ds[k].textContent;ds[k].remove();}"
        "post+=c.textContent;"
        "}"
        // 浮动进脚注盒的 def（.pagedjs_area 内独立盒，内容盒之外）
        "var fd=document.querySelectorAll('.pagedjs_footnote_area div[data-type=\"footnoteDef\"]');"
        "for(var q=0;q<fd.length;q++)fnText+=fd[q].textContent;"
        "var a=(window.__preText||'').replace(/\\s+/g,'');"
        "var b=post.replace(/\\s+/g,'');"
        "if(a!==b){"
        "var j=0;while(j<Math.min(a.length,b.length)&&a.charAt(j)===b.charAt(j))j++;"
        "return '内容指纹不一致 '+a.length+'→'+b.length+' 首异位@'+j;"
        "}"
        // 脚注指纹（字符多重集）：def 随引用重排（顺序变）、长脚注跨页拆分
        // （分块变），字符集合不变——纯重排/拆分不误伤，真丢失/重复仍拦截
        "var fa=window.__preFns||'';"
        "var fb=fnText.replace(/\\s+/g,'').split('').sort().join('');"
        "if(fa!==fb)return '脚注指纹不一致 '+fa.length+'→'+fb.length;"
        // img 维度指纹（文本比对之后）：图片无文本，文本指纹对图片丢失/裁切/
        // 重复铺排全无感知，须独立比对。签名取 src 多重集（收集后排序 join('|')）
        // 而非仅计数——同 src 合法重复出现 N 次须保留次数语义，总量守恒的
        // 「丢 A 重复 B」替换也唯有多重集能逮住；post 侧与文本比对同覆盖
        // （页面内容盒 + 脚注盒，脚注内图片同样可能被分页手术波及）。签名含
        // base64 data URI 会巨长，返回值截断为前 8 字符+总长，勿整串回填
        // （风格对齐上方文本指纹的 a.length+'→'+b.length）
        "var ims=[];"
        "var fns=document.querySelectorAll('.pagedjs_footnote_content');"
        "for(var n=0;n<pages.length;n++){var pg=pages[n].querySelectorAll('img');"
        "for(var m=0;m<pg.length;m++)ims.push(pg[m]);}"
        "for(n=0;n<fns.length;n++){var fg=fns[n].querySelectorAll('img');"
        "for(m=0;m<fg.length;m++)ims.push(fg[m]);}"
        "var ia=window.__preImgs||'',srcs=[];"
        "for(n=0;n<ims.length;n++)srcs.push(ims[n].src);"
        "srcs.sort();var ib=srcs.join('|');"
        "if(ia!==ib)return '图片指纹不一致 '+ia.slice(0,8)+'('+ia.length+')→'+ib.slice(0,8)+'('+ib.length+')';"
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
        "try{window.__fixTableColumns();}catch(e){console.warn('表格列宽归一化失败: '+(e&&e.message||e));}"
        "try{window.__splitPreLines();}catch(e){console.warn('代码块逐行重排失败: '+(e&&e.message||e));}"
        // 指纹快照在 splitPreLines 后（同基线）、脚注浮动前：正文指纹取
        // 「剔除 def 的 body 克隆」文本——def 即将被移进各页脚注盒（文本顺序
        // 必然改变），其文本单独走字符多重集指纹（__preFns）
        "window.__preFns=(function(){"
        "var t='',ds=document.querySelectorAll('div[data-type=\"footnoteDef\"]');"
        "for(var i=0;i<ds.length;i++)t+=ds[i].textContent;"
        "return t.replace(/\\s+/g,'').split('').sort().join('');})();"
        "window.__preText=(function(){"
        "var c=document.body.cloneNode(true);"
        "var ds=c.querySelectorAll('div[data-type=\"footnoteDef\"]');"
        "for(var i=0;i<ds.length;i++)ds[i].remove();"
        "return c.textContent;})();"
        // img src 多重集快照与文本快照同点：splitPreLines 不动 img，同点即与
        // 比对侧（__verifyContent）完全对称。body 内全部 img 的 src 排序后
        // join('|')——多重集语义：同 src 合法重复出现保留次数（仅计数会被
        // 总量守恒的替换骗过），排序消除分页重排带来的顺序差
        "window.__preImgs=(function(){var s=[],is=document.body.querySelectorAll('img');"
        "for(var i=0;i<is.length;i++)s.push(is[i].src);"
        "s.sort();return s.join('|');})();"
        // 脚注浮动重排最后跑（快照之后）：把 def 移到首个引用旁并打 pdf-fn，
        // 分页引擎随即将其搬入引用所在页的页底脚注区
        "try{window.__floatFootnotes();}catch(e){console.warn('脚注随页浮动重排失败: '+(e&&e.message||e));}"
        "return PagedPolyfill.preview()"
        ".then(function(){"
        // 页脚 counter(pages) 修正：脚注溢出产生的续页走 chunker.clonePage——
        // 它 push 页面元素但不更新 this.total，渲染收尾把
        // --pagedjs-page-count 写成不含续页的旧值，页脚呈现「第3页/共2页」
        // （脚注多段跨页拆分实测）。inline 变量按页元素实际数覆写
        "try{var pw=document.querySelector('.pagedjs_pages');"
        "if(pw)pw.style.setProperty('--pagedjs-page-count',"
        "document.querySelectorAll('.pagedjs_page').length);}catch(e){}"
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
                         "<script>window.__pageContentWidthPx=%1;</script>"
                         "<script>window.PagedConfig={auto:false};</script>"
                         "<script src=\"paged.polyfill.js\"></script>")
              .arg(pageContentWidthPx, 0, 'f', 1)
        : QString();

    // 内容按需注入重型库（2026-09-15 性能取证：无流程图的文档白付 3.5MB
    // mermaid.min.js 加载+解析 ~350ms，无公式的文档白付 katex 272KB+CSS）。
    // 渲染链自带守卫（if(window.mermaid)/if(window.katex)），库缺席即整段跳过；
    // 检测用内联后 body（图片内联不影响两类标记）
    const bool needMermaid = body.contains(QLatin1String("language-mermaid"));
    const bool needKatex = body.contains(QLatin1String("data-latex"));
    QString libs;
    if (needKatex)
        libs += QStringLiteral("<script src=\"katex.min.js\"></script>");
    if (needMermaid)
        libs += QStringLiteral("<script src=\"mermaid.min.js\"></script>");
    // KaTeX CSS（含 base64 字体）同理按需——无公式文档整段省略
    const QString katexCssOnDemand = needKatex ? katexCss : QString();

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
        // % 无需转义——QString::arg 只识别 %1..%99 占位符，%% 会原样保留成无效 CSS（历史缺陷已修）
        "table{border-collapse:collapse;width:100%;margin:0.5em 0;}"
        "th,td{border:1px solid #ccc;padding:4px 8px;font-size:10pt;}"
        "th{background:#f5f5f5;}"
        "img{max-width:100%;}"
        "blockquote{border-left:3px solid #0078d4;padding:4px 12px;"
        "color:#555;margin:0.5em 0;}"
        "[data-type=\"block-math\"]{display:block;text-align:center;margin:1em 0;}"
        "ul[data-type=\"taskList\"]{list-style:none;padding-left:0;}"
        // 子代选择器 > 限定直接 li：嵌套在任务项内的通用列表 li（如 列表2-2 下再嵌 <ol>）
        // 若也命中 flex 会被当成 flex 容器，其子元素 p 与嵌套 <ol> 排成一行、序号消失（与 editor.css 对齐）
        "ul[data-type=\"taskList\"] > li{display:flex;align-items:flex-start;gap:8px;}"
        "ul[data-type=\"taskList\"] > li label{margin-top:3px;}"
        "ul[data-type=\"taskList\"] > li > div > p{margin:0;}"
        // 脚注基础版式（编号与内容同行的关键）：marker 浮动 + 首子元素去顶距。
        // footnote-content 是块级 div（块级定义），无此规则时行内 marker 后跟
        // 块级元素被拆成匿名块、编号独占一行——PDF 模板不加载 editor.css，
        // 编辑器的 float:left 绕排规则须在此复刻（paged/legacy 两路径共用）
        "div[data-type=\"footnoteContainer\"]{margin-top:2em;padding-top:.75em;"
        "border-top:1px solid #bbb;font-size:.85em;}"
        "div[data-type=\"footnoteDef\"]{margin:.25em 0;overflow:hidden;}"
        ".footnote-marker{float:left;margin-right:.4em;font-weight:600;color:#1967d2;}"
        "div[data-type=\"footnoteDef\"] .footnote-content>*:first-child{margin-top:0;}"
        "}"
        "%3"                                    // KaTeX CSS（按需，无公式为空）
        "</style>"
        "%4"                                    // PagedConfig + polyfill（paged 时）
        "%7"                                    // 重型库按需（katex/mermaid）
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
    ).arg(pageCss, legacyPageZero, katexCssOnDemand, pagedScripts, preparePrint,
          body, libs);

    // 注入 mermaid 渲染 helper（定义 window.__renderAllMermaid，返回 Promise，
    // 不自调用——__preparePrint 显式链式调用并等待）。PDF 固定亮色打印。
    fullHtml.replace("</head>",
        QString("<script>%1</script></head>").arg(HtmlExporter::mermaidRenderScript("default")));

    if (!customCss.isEmpty()) {
        // 剥 </style> 序列（与 HtmlExporter 对称）：本地设置携带它会把 <style>
        // 块提前终结并向产物注入任意标记（自注入面）
        QString safeCss = customCss;
        safeCss.remove(QRegularExpression("</\\s*style\\s*>",
                                          QRegularExpression::CaseInsensitiveOption));
        fullHtml.replace("</head>", QString("<style>%1</style></head>").arg(safeCss));
    }
    return fullHtml;
}

QMarginsF PdfExporter::pagedPrintMargins()
{
    // paged 路径的 Qt 打印边距为何不是 0（三层根因，2026-09-07 实验证实）：
    // ① Qt 6.8 的 printToPdf 对恰为 0 的边距不按 0 采用，而是套用 ~18mm（51pt）
    //   的有效默认打印边距——「边距由 @page 承载、Qt 布局传 0 防双重边距」的
    //   设计意图被这一行为打破（判别实验：CSS @page 边距 18→10mm 落纸内缩纹丝
    //   不动，证明与 CSS 无关；Qt 边距 5mm → 内缩精确 14.17pt，证明非 0 即被
    //   精确采用）
    // ② 有效边距压缩可打印区（纸 − 2×51pt）→ Chromium shrink-to-fit 把自然宽
    //   = 纸宽的 paged.js 文档等比缩放（实测横向 0.879 / 纵向 0.829）
    // ③ 缩放后 paged.js 的 @media print height:100%!important 把 sheet 盒拉到
    //   ≠ 纸高：横向盒 561.4pt < 网格 595.3pt，.pagedjs_sheet 的 overflow:hidden
    //   裁掉底部 33.9pt——页脚文本（居中于末 51pt 行）整段落在裁切线外，页脚
    //   全丢；纵向盒 893.1 > 841.9 不裁切，但全文 83% 缩放 + 页脚悬高于真实
    //   底边距行 ~80pt（静默劣化）
    // 解法：0.1mm 微量值（用户裁决）避开零值默认边距，偏差 0.28pt 落纸不可见。
    // 依赖「非 0 边距被精确采用」这一行为（Qt 6.8 实测；6.10 不可本地测，行为
    // 如有变化须重取证）。页边距仍由 @page 规则承载，此值仅为骗过零值默认
    return QMarginsF(0.1, 0.1, 0.1, 0.1);
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
    // paged 路径边距由 @page 承载（页面盒自带页边距），Qt 布局不能传 0——Qt 6.8
    // 对恰 0 边距套 ~18mm 默认打印边距，引发 shrink-to-fit 缩放 + 横向 sheet
    // 被裁底 → 页脚全丢（三层根因详见 pagedPrintMargins() 注释），故传 0.1mm
    // 微量值；legacy 路径维持 Qt QPageLayout 边距（设置值为 cm，
    // QPageLayout::Millimeter 需要 mm）
    m_pendingLayout = QPageLayout(QPageSize(static_cast<QPageSize::PageSizeId>(paper)),
        static_cast<QPageLayout::Orientation>(orient),
        spec.pagedActive() ? pagedPrintMargins()
                           : QMarginsF(ml * 10, mt * 10, mr * 10, mb * 10),
        QPageLayout::Millimeter);
    // 原生版式（用户边距）：legacy 路径与 paged 失败/超时回退共用
    m_legacyLayout = QPageLayout(QPageSize(static_cast<QPageSize::PageSizeId>(paper)),
        static_cast<QPageLayout::Orientation>(orient),
        QMarginsF(ml * 10, mt * 10, mr * 10, mb * 10), QPageLayout::Millimeter);

    m_printPoll.stop();
    m_printPollCount = 0;
    m_printStarted = false;
    m_fallbackUsed = false;
    m_lastPagedPages = 0;

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
        m_fallbackUsed = true;
        m_renderView->page()->runJavaScript(
            "try{window.__restoreBody&&window.__restoreBody();}catch(e){}");
        emit pagedFallbackUsed();
        m_renderView->page()->printToPdf(m_pendingOutputPath, m_legacyLayout);
        return;
    }
    // 状态 + 已生成页数一并读（复用同一轮询，零额外开销）：working 期间页数
    // 增长即发 pagedProgress——paged.js 大文档分页以十秒计（2026-09-15 性能
    // 取证 ~0.8s/页），全程静默等待体感差
    m_renderView->page()->runJavaScript(
        "(function(){return JSON.stringify({s:window.__printState||'',"
        "n:document.querySelectorAll('.pagedjs_page').length});})()",
        [this](const QVariant &v) {
            QJsonParseError e;
            const QJsonDocument d = QJsonDocument::fromJson(v.toString().toUtf8(), &e);
            const QJsonObject o = d.object();
            const QString state = e.error == QJsonParseError::NoError
                                      ? o.value("s").toString() : QString();
            const int pages = e.error == QJsonParseError::NoError
                                      ? o.value("n").toInt() : 0;
            if (state == QLatin1String("working") && pages > m_lastPagedPages) {
                m_lastPagedPages = pages;
                emit pagedProgress(pages);
            }
            // 未到终态（idle/working/空）继续轮询；回调与停表存在竞态，须防重
            if (state != QLatin1String("paged") && state != QLatin1String("legacy")
                && state != QLatin1String("fallback"))
                return;
            m_printPoll.stop();
            if (m_printStarted)
                return;
            m_printStarted = true;
            const bool paged = (state == QLatin1String("paged"));
            if (state == QLatin1String("fallback")) {
                m_fallbackUsed = true;
                emit pagedFallbackUsed();
            }
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
