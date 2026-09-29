#include "PdfExporter.h"
#include "HtmlExporter.h"
#include "ExportCommon.h"
#include "PdfPageGeometry.h"
#include <QWebEnginePage>
#include <QWebEngineSettings>
#include <QUrl>
#include <QSettings>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDir>
#include <QCoreApplication>
#include <QDateTime>
#include <algorithm>
#include <QPdfDocument>

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

// 页眉/页脚与正文的净空（mm）：两条路径同源——页眉底贴正文顶线之上、页脚顶
// 贴正文底线之下的同时各退开这么多，避免与首末行视觉粘连；成品校验的
// 「正文溢出留白带」窗口上界同源取值
static constexpr qreal kHfHugGapMm = 3.0;

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

    // 原生覆盖层路径（nativeHf）：不注入 paged.js，页眉页脚由 thead 重复带 +
    // abs 覆盖层实现（机制经 print-probe 实证，2026-09-19）
    const bool nativeHf = spec.nativeHf;
    const bool paged = spec.pagedActive() && !nativeHf;
    // 页面内容宽（CSS px，96dpi）：__fixTableColumns 列宽分配的可用宽基准。
    // 声明在 if(paged) 外——pagedScripts 拼接处也要用
    double pageContentWidthPx = 0;
    // @page 规则（paged 激活时）：置于 @media print 之外——paged.js 在屏幕媒体
    // 上解析分页；margin 顺序 CSS = 上 右 下 左
    QString pageCss;
    const QSizeF paperMm = QPageSize(spec.paper).size(QPageSize::Millimeter);
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
        // 页眉页脚贴页边距（Word 式）：页眉压在正文顶线之上、页脚紧贴正文
        // 底线之下，而不是居中悬在页边距带中央（旧版式照 paged 默认居中，
        // 墨迹离纸边仅 ~8mm——多数打印机不可打印边距 4-6mm，余量太薄）。
        // paged.js 把 @top-center 里的 vertical-align 翻译成对应页边距盒的
        // align-items（top→flex-start/middle→center/bottom→flex-end，见
        // vendored 源码 PrintMedia 的声明改写），故这里 bottom/top 即"贴向
        // 正文一侧"；padding 留出与正文的净空（hugGapMm，与 nativeHf 同源）
        if (spec.headerEnabled)
            pageCss += QStringLiteral(
                           "@top-center{content:\"%1\";font-size:9pt;color:#666;"
                           "vertical-align:bottom;padding-bottom:%2mm;}")
                           .arg(cssContentString(spec.headerTitle))
                           .arg(kHfHugGapMm, 0, 'f', 2);
        if (spec.footerEnabled)
            // 页码文案三段取自 ExportCommon::kPageText*（单一来源，见该处注释）
            pageCss += QStringLiteral(
                           "@bottom-right{content:\"%1\" counter(page) \"%2\" "
                           "counter(pages) \"%3\";font-size:9pt;color:#666;"
                           "vertical-align:top;padding-top:%4mm;}")
                           .arg(QString::fromUtf8(ExportCommon::kPageTextPrefix),
                                QString::fromUtf8(ExportCommon::kPageTextMid),
                                QString::fromUtf8(ExportCommon::kPageTextSuffix))
                           .arg(kHfHugGapMm, 0, 'f', 2);
        pageCss += QLatin1Char('}');
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
    // min-content 防溢规则（paged 与 nativeHf 共用，2026-09-19 真文档取证）：
    // nativeHf 缺这些帽子时，宽 mermaid SVG 的固有宽度撑爆文档 min-content
    // → Qt printToPdf shrink-to-fit 全局缩印（实测 0.5×）→ 每页消耗 ~2×U
    // → 页码覆盖层逐页早移、成品验码拦下回退。svg 补 max-width 钳住固有宽
    if (paged || nativeHf) {
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
        // 上限 = 版心高（纸高 − 上下边距）− 2mm 安全间隙 − 4mm 墨迹余量（与
        // nativeHf 分页预算同源：预算 = 版心高 − 4mm，图块必须 ≤ 预算，否则
        // 会成为"单块高于一页"的可分页判据触发点而被判回退）。nativeHf 曾额外
        // 扣 8.5mm 重复表头（旧版式里 thead 在正文区顶部占位），2026-09-21
        // 版式重做后页眉带移出正文区
        const qreal capH = (spec.orientation == QPageLayout::Landscape ? paperMm.width()
                                                                       : paperMm.height())
                           - spec.marginsMm.top() - spec.marginsMm.bottom()
                           - 2.0 - 4.0;
        // display:block：svg 行内形态的基线空隙让墨底越内容带 ~12pt（实测
        // 边距内出现图形墨迹）——块级化消除行盒下探
        pageCss += QStringLiteral(
                       ".mermaid-diagram svg{display:block;max-height:%1mm!important;}")
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
        pageCss += QStringLiteral("svg{max-width:100%!important;}");
    }


    if (nativeHf) {
        // 原生覆盖层样式。包装表必须复位内容 CSS 的表格规则——尤其
        // tr,th,td{break-inside:avoid} 会禁止单元格跨页（正文塞在一个大 td
        // 里，禁止分页=每页只剩一个巨块）；边框规则同理不得命中包装层
        //
        // 版式（2026-09-21 对齐 paged.js 定案）：打印区覆盖整张纸（Qt 边距
        // 0.1mm，见 nativeHfPrintMargins），页边距改由流内结构模拟——
        // ① 上边距 = 包装表 thead 带高（用户上边距值），带内文字上边垫
        //    padTopMm 使页眉墨迹落在纸顶 ~7mm（paged.js 的 @top-center 落点）
        // ② 下边距 = 强制分页（__paginateNativeHf 逐页只装"内容高"），
        //    留出的纸底带供页脚覆盖层落位（~8mm 距纸底，同 paged.js）
        // ③ 左右边距 = 内容格 padding（打印区是全纸宽，须自行内缩）
        // 内容格只留左右 padding；宽度由渲染视图尺寸决定（见 exportToPdf 里
        // 的 resize——屏幕布局必须与打印布局同宽，否则分页测量失真。表格
        // 单元格的 width 在 auto 布局里只是"最小列宽"提示，会被表格自身宽度
        // 压掉，钉不住）
        const qreal topMmHf = spec.marginsMm.top();
        // 页眉贴页边距（Word 式）：文字压在页眉带底部（= 正文顶线）之上
        // kHfHugGapMm——与 paged 路径的 vertical-align:bottom + padding-bottom
        // 同源同值。页边距太小时先保净空 [0]，必要时再让文字占满整带
        const qreal padBotMmHf = qMin<qreal>(kHfHugGapMm, qMax<qreal>(0.0, topMmHf - 5.2));
        const qreal bandHmmHf = qMax<qreal>(0.0, topMmHf - padBotMmHf);
        pageCss += QStringLiteral(
            "html,body{margin:0;padding:0;}"
            "table.hf{width:100%;border-collapse:collapse;margin:0;}"
            "table.hf>thead>tr>td{border:none;padding:0 0 %1mm 0;height:%2mm;"
            "vertical-align:bottom;font-size:9pt;color:#666;text-align:center;}"
            "table.hf>tbody>tr>td{border:none;padding:0;break-inside:auto;}"
            "table.hf{table-layout:fixed;}"
            // 字号基准显式钉回 11pt：包装单元格是 <td>，会吃到基础 CSS 的 th,td
            // 字号。历史教训（th,td 曾为 10pt，2026-09-24 起与正文统一为 11pt）：
            // 只要 th,td 字号 ≠ 正文基准，单元格内一切 em 相对值都按该字号解析
            // ——当时标题 h1/h2/h3 各小 10%（实测 h1 22.67px vs paged 侧 24.93px，
            // 即有无脚注两份 PDF 的标题差一档），行内 code 同理（9pt vs 9.9pt）；
            // 绝对值字号（页脚 9pt）不受影响，故只有标题看起来分叉。钉回保留为
            // 防御：th,td 字号将来再变时，em 基准不随动。id 特异性高于 th,td
            "#hfcell{font-size:11pt;padding:0 %3mm 0 %4mm;}")
            .arg(padBotMmHf, 0, 'f', 2)
            .arg(bandHmmHf, 0, 'f', 2)
            .arg(spec.marginsMm.right(), 0, 'f', 2)
            .arg(spec.marginsMm.left(), 0, 'f', 2);
        // 底垫 hack 已撤（2026-09-21）：页脚搬进纸底带后与正文不相压，
        // 22px 单元格底垫失去存在理由——撤销后行高恢复自然，与 paged 同源
        // QString 而非 QStringLiteral：rgb 三通道改由 PageSigColor 常量拼接，
        // 表达式内含 +，不满足 QStringLiteral 仅收字面量的前提
        pageCss += QString(
            // 行原子性（模板 @media print 的 tr,th,td{break-inside:avoid} 生
            // 效）：tr{break-inside:auto} 下 Chromium 行分片绘制越过分页器末
            // 端直插下边距（实测墨底 875pt，越页底 34pt）；avoid 推挤留空的
            // avoid 推挤留空的旧顾虑已随分段堆叠（每片高恰一页）不复存在
            "#numhost{position:absolute;top:0;left:0;width:100%;}"
            // 页码片（每片恰一页高 = 打印片段高）：position:relative 作片内
            // abs 页码的 containing block——页码盒整体落在片段内（纸底带），
            // 见 __stampPageNumbers。色 rgb(102,102,103)：视觉同 #666，蓝通道
            // +1 为文档内唯一标识（表头/mermaid 灰全排除）——PdfPageGeometry
            // 色锚定据此认页码本尊，冒名/漏检双向失灵曾致校正追幻影死锁
            ".hf-pg{position:relative;left:0;width:100%;"
            "font-size:9pt;color:rgb(" + QString::number(PageSigColor::R) + "," +
            QString::number(PageSigColor::G) + "," + QString::number(PageSigColor::B) + ");}");
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
        "};")
        + HtmlExporter::tableColumnNormalizeScript()
        + QStringLiteral(
        // 表格列宽归一（短列保真、长列吸收）——实现已提为 HTML 与 PDF 共用
        //（HtmlExporter::tableColumnNormalizeScript，算法注释与实测证据随实现走）。
        // 本兼容层保持既有函数名与两个调用点不变，并按打印基准传测量样式：
        // 打印 CSS 不作用于屏幕，测量须在打印基准下做，列宽才与打印一致
        //（fontSize 11pt = 单元格字号，测量基准随单元格字号——2026-09-24 起
        // 四面统一正文字号 11pt）。2026-09-26 补 slack:2：边界列百分比回算
        // 差 0.02px 即折行（状态列「已完成」实测），对齐 HTML 导出的余量策略
        "window.__fixTableColumns=function(){window.__normalizeTableColumns("
        "{avail:window.__pageContentWidthPx,slack:2,fontSize:'11pt',padding:'4px 8px',"
        "fontFamily:'Noto Sans SC,DejaVu Sans,sans-serif'});};"
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
        // 连字符规范化：paged.js 的 hyphenateAtBreak 会在断词处插入不换行连字符
        // （\u2011）或软连字符——那是排版产物、不是内容，两侧对称剔除后比对，
        // 否则断点位置一变（如表格预分页后）指纹即误报、整篇回退
        // 连字符规范化（全局 + 局部各一份）：paged.js 的 hyphenateAtBreak 在断词处
        // 插入不换行连字符 ‑（或软连字符）——那是排版产物、不是内容，指纹
        // 两侧对称剔除后才不会误报（断点位置一变，如表格预分页后，就会触发）。
        // __preFns 在函数外调用，故须有全局副本
        "window.__dehyph=function(x){return String(x).replace(/\\u2011|\\u00AD/g,'');};"
        "var dehyph=window.__dehyph;"
        "var post='',fnText='';"
        "for(var i=0;i<pages.length;i++){"
        "var c=pages[i].cloneNode(true);"
        "var ds=c.querySelectorAll('div[data-type=\"footnoteDef\"]');"
        "for(var k=0;k<ds.length;k++){fnText+=ds[k].textContent;ds[k].remove();}"
        // script/style 对称剔除（与 __preText 同款）：mermaid SVG 内嵌
        // <style> 每图 ~1KB CSS，两侧都含或都不含才可比
        "var ps=c.querySelectorAll('script,style');"
        "for(var r=0;r<ps.length;r++)ps[r].remove();"
        "post+=c.textContent;"
        "}"
        // 浮动进脚注盒的 def（.pagedjs_area 内独立盒，内容盒之外）
        "var fd=document.querySelectorAll('.pagedjs_footnote_area div[data-type=\"footnoteDef\"]');"
        "for(var q=0;q<fd.length;q++)fnText+=fd[q].textContent;"
        "var a=dehyph((window.__preText||'').replace(/\\s+/g,''));"
        "var b=dehyph(post.replace(/\\s+/g,''));"
        "if(a!==b){"
        "var j=0;while(j<Math.min(a.length,b.length)&&a.charAt(j)===b.charAt(j))j++;"
        "return '内容指纹不一致 '+a.length+'→'+b.length+' 首异位@'+j;"
        "}"
        // 脚注指纹（字符多重集）：def 随引用重排（顺序变）、长脚注跨页拆分
        // （分块变），字符集合不变——纯重排/拆分不误伤，真丢失/重复仍拦截
        "var fa=window.__preFns||'';"
        "var fb=dehyph(fnText.replace(/\\s+/g,'')).split('').sort().join('');"
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
        // 超长 mermaid 单页缩放：.mermaid-diagram svg 的 max-height 帽把高于
        // 一页的图 meet 等比缩进单页（用户裁决 2026-09-20：宁可整图缩到一页
        // 也不跨页切片——曾按可读宽纵切多片，用户反馈「没有缩放到一页」）
        "window.__preparePrint=function(){"
        "if(window.__printState==='working')return window.__printState;"
        "window.__printState='working';"
        // 脚注区的真实高度无法从纸张/边距推导（字号 0.8em、行高 1.5、边框
        // 1px、内垫 .25em 都是 CSS 值，但 paged.js 还会按内容算行盒），故不
        // 硬编码：第一遍只加载并排一到两页，量到即中断（MEASURE_DONE），C++
        // 取值后**重新加载**并注回 window.__pagedFnH——第二次加载是干净状态
        // （同一次加载里二次 preview 不可行：__restoreBody 后 paged.js 内部
        // 状态指向已移除节点，实测抛 Cannot read properties of null）
        "if(!(window.__pagedBoxH>0)){"
        "try{window.Paged.registerHandlers(class extends window.Paged.Handler{"
        "afterPageLayout(pageElement){"
        "var c=pageElement.querySelector('.pagedjs_page_content');"
        "var a=pageElement.querySelector('.pagedjs_area');"
        // 逐页取**最小值**：带脚注的页比不带脚注的页少一个脚注区高（页内容盒
        // = 面积 − 脚注区），最小值即"最挤的一页"的可用高
        "if(c){var ch=Math.round(c.getBoundingClientRect().height);"
        "if(!window.__measureH||ch<window.__measureH)window.__measureH=ch;}"
        "if(a)window.__measureA=Math.round(a.getBoundingClientRect().height);"
        // 脚注区内容在第 1 页布局时尚未搬入（读到的是空态高），第 2 页再读
        // 文档级第一个脚注区即得第 1 页的定型高
        "window.__pagedPageN=(window.__pagedPageN||0)+1;"
        "var fa0=document.querySelector('.pagedjs_footnote_area');"
        "if(fa0)window.__measureFnH=Math.round(fa0.getBoundingClientRect().height);"
        "window.__measureDbg='高='+window.__measureH+' 面积='+window.__measureA"
        "+' 脚注区='+window.__measureFnH+' 页='+window.__pagedPageN;"
        "if(window.__pagedPageN<2)return;"
        "window.__printState='measured';"
        "throw new Error('MEASURE_DONE');}});}catch(e){window.__measureDbg='注册失败 '+e;}"
        "}"
        "var fallback=function(e){"
        // 测量遍靠抛错中断（只排第一页即止），不是失败：原样返回让 C++ 取高重载
        "if(window.__printState==='measured')return 'measured';"
        "console.warn('paged.js 分页失败，回退原生分页: '+(e&&e.message||e));"
        "try{window.__restoreBody();}catch(x){}"
        "window.__printState='fallback';return 'fallback';};"
        "return window.__renderAllMermaid()"
        ".then(function(){"
        // 测量前置：屏幕媒体禁滚动条。滚动条会吃掉视口宽（实测 ~15px），
        // 屏幕布局随之窄于打印布局——表格列宽归一的 intrinsic 测量、预分页
        // 切块的行高测量与 nativeHf 的分页块高测量全部失真（paged 路径短列被
        // 压成逐字竖排、nativeHf 表格行高偏大 30% 都是此因）。用
        // documentElement 内联样式临时压制：内联样式不经样式表，paged.js 的
        // 样式改写够不着它；压制罩住全部分页前测量，进分页前清除——绝不能
        // 带进打印媒体（html{overflow:hidden} 在打印下会裁掉整份文档）。测量
        // 函数内的 getBoundingClientRect 强制回流，保证在压制生效的布局上取值
        "try{document.documentElement.style.overflow='hidden';}catch(e){}"
        "if(!window.PagedPolyfill){"
        // nativeHf 三条前置（顺序即依赖）：① 代码块逐行重排成行块（与 paged
        // 路径同款），长代码块才能按行断页；② 表格列宽归一（屏幕布局 = 打印
        // 布局后，其两趟 intrinsic 测量与 paged 路径同源，两引擎列宽一致）；
        // ③ 轻量分页切页（依赖前两步的最终块高）
        "if(window.__NATIVE_HF){"
        "try{window.__splitPreLines();}catch(e){console.warn('nativeHf 代码块逐行重排失败: '+(e&&e.message||e));}"
        "try{window.__fixTableColumns();}catch(e){console.warn('nativeHf 列宽归一失败: '+(e&&e.message||e));}"
        "try{console.warn('nativeHf 分页: '+window.__paginateNativeHf("
        "window.__hfPageContentPx,window.__hfPageMarginBottomPx));}"
        "catch(e){console.warn('nativeHf 分页失败: '+(e&&e.message||e));}"
        "}"
        "window.__printState=window.__NATIVE_HF?'nativehf':'legacy';"
        "return window.__printState;}"
        "try{window.__fixTableBreaks();}catch(e){console.warn('表格断点修正注册失败: '+(e&&e.message||e));}"
        "try{window.__fixTableColumns();}catch(e){console.warn('表格列宽归一化失败: '+(e&&e.message||e));}"
        "try{window.__splitPreLines();}catch(e){console.warn('代码块逐行重排失败: '+(e&&e.message||e));}"
        // 指纹快照在 splitPreLines 后（同基线）、脚注浮动前：正文指纹取
        // 「剔除 def 的 body 克隆」文本——def 即将被移进各页脚注盒（文本顺序
        // 必然改变），其文本单独走字符多重集指纹（__preFns）
        // 连字符规范化（本 <script> 块内可用；__verifyContent 在另一块里有同语义
        // 的局部副本）：paged.js 的 hyphenateAtBreak 在断词处插入不换行连字符，
        // 那是排版产物、不是内容——指纹两侧对称剔除后才不会误报回退
        "window.__dehyph=function(x){return String(x).replace(/\\u2011|\\u00AD/g,'');};"
        "window.__preFns=(function(){"
        "var t='',ds=document.querySelectorAll('div[data-type=\"footnoteDef\"]');"
        "for(var i=0;i<ds.length;i++)t+=ds[i].textContent;"
        "return window.__dehyph(t.replace(/\\s+/g,'')).split('').sort().join('');})();"
        "window.__preText=(function(){"
        "var c=document.body.cloneNode(true);"
        "var ds=c.querySelectorAll('div[data-type=\"footnoteDef\"]');"
        "for(var i=0;i<ds.length;i++)ds[i].remove();"
        // script/style 剔除：MarkdownHandler(QTextDocument) 产物自带完整
        // 内嵌文档，其 head 的脚本/样式文本会进快照而 paged 页面盒没有
        // ——指纹假阳性曾致无谓回退（实测 759 字差）
        "var ss=c.querySelectorAll('script,style');"
        "for(var q=0;q<ss.length;q++)ss[q].remove();"
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
        // 表格预分页（2026-09-22）：paged.js 对跨页表格做"行碎片"时会把装不下的
        // 行建成续表碎片并（在部分几何下）留在当前页且撑成整页高，打印时整行不
        // 显示（实测 1.1 表第 11 行、1.2 表首数据行丢失）。根治办法是不让它需要
        // 切表：分页前把**超过一页的外层表**按行切成多张装得下一页的块，paged.js
        // 只需在块之间做正常的块级分页。
        //
        // 切块容量模型（2026-09-22 二次重做；应用路径复现出丢行后逐项实测）：
        // - **续块从页顶开始**（pdf-tbl-cont 带 break-before:page）⇒ 只需保证
        //   单块高 ≤ 单页可用高，与全文档页格无关。
        // - 单页可用高 = 面积高 __pagedContentPx（纸高 − 上下边距）；**该页含脚注
        //   引用**再扣脚注区高（实测值 __pagedFnH，由测量遍量得）——按页判定，
        //   不给整篇一刀切预留（无脚注的页白留一个脚注区高，每页少一行）。
        // - 首块用起始页的剩余高：从流首逐页前推算到本表顶；续块从各自页顶起算，
        //   逐块推进页游标。
        // - **位移累加 push**：切块把内容推到新页 ⇒ 后续内容整体下移，而各表顶的
        //   测量值取自**未切块**的连续流。实测应用路径：1.2 表模型 3730 / 真实
        //   3810，1.3 表模型 4350 / 真实 5337（差近一整页）——后续表按"表顶还有
        //   多少余量"切块时全按错的页算 ⇒ 块溢出 ⇒ paged.js 只能切表 ⇒ 错位碎片。
        // - **余量 ≥20px 且首行放不下就整表挪页**：溢出是亚像素级的（实测 1.1 表
        //   首块 710px + 真实起点 248 = 958，超内容盒 956 **2px** ⇒ 切表 ⇒ 第 11 行
        //   变错位碎片）；挪页只损失 ≤一行 的页尾，换"永不切表"。
        "window.__prepaginateTables=function(A){"
        "try{"
        "if(!(A>0))return 'no-H';"
        "var F=window.__pagedFnH||0;"
        // 通用安全余量：块盒高之外的墨迹（底边框/末行下探）+ 测量到分页的舍入
        // 抖动；20px ≈ 1/3 行高，肉眼不可见
        "var slack=Math.max(A*0.005,20);"
        "var flowTop=document.body.getBoundingClientRect().top;"
        "var refYs=[];"
        "var supRefs=document.querySelectorAll('sup[data-type=\\\"footnoteRef\\\"]');"
        "for(var si=0;si<supRefs.length;si++)refYs.push(supRefs[si].getBoundingClientRect().top-flowTop);"
        "refYs.sort(function(a,b){return a-b;});"
        // 页游标：本页容量 = 面积高；页内含脚注引用（引用点落在本页范围）则扣
        // 脚注区高，与 paged.js 的分配一致——**按页判定**，不给整篇一刀切预留
        // （无脚注的页白留一个脚注区高 = 每页少一行，实测页尾多空 29mm）
        "var capAt=function(top,shift){"
        "for(var q=0;q<refYs.length;q++){var ry=refYs[q]+shift;"
        "if(ry>=top&&ry<top+A)return A-F;}"
        "return A;};"
        "var pageInfo=function(y,shift){"
        "var top=0,guard=0;"
        "while(guard++<600){"
        "var cap=capAt(top,shift);"
        "if(top+cap>y)return {top:top,cap:cap};"
        "top+=cap;}"
        "return {top:0,cap:A};};"
        // 预扫：切块会改 DOM（续表 margin-top:0、删行、插表），屏幕流几何须统一取
        // **切块前**的值——先把各表顶与各行高缓存下来（expando 不参与序列化/打印）
        "var list=[];"
        "var tables=document.querySelectorAll('table');"
        "for(var i=0;i<tables.length;i++){"
        "var t=tables[i];"
        "if(t.classList.contains('hf'))continue;"
        "if(t.parentElement&&t.parentElement.closest('table'))continue;"
        "var all=[].slice.call(t.querySelectorAll('tr'));"
        "if(all.length<3)continue;"
        "var totalH=0;"
        "for(var r0=0;r0<all.length;r0++){all[r0].__pph=all[r0].getBoundingClientRect().height;totalH+=all[r0].__pph;}"
        "list.push({t:t,all:all,y0:t.getBoundingClientRect().top-flowTop,total:totalH,idx:i});}"
        "var push=0;"
        "for(var k2=0;k2<list.length;k2++){"
        "var t=list[k2].t,all=list[k2].all,y0=list[k2].y0,totalH=list[k2].total;"
        "var y=y0+push;"
        "var pi=pageInfo(y,push);"
        // 首块：本页余量连**首行**都放不下 ⇒ 整表挪到下一页（pdf-tbl-head 带
        // break-before:page）。表若落在"塞不进一行"的页尾，paged.js 必然切表，
        // 而切表会触发错位碎片丢行；挪页只损失 ≤一行 的页尾
        "var pageTop=pi.top,headBreak=false;"
        "var lim=pageTop+capAt(pageTop,push)-y-slack;"
        "if(lim<all[0].__pph){headBreak=true;pageTop=pi.top+pi.cap;lim=capAt(pageTop,push)-slack;}"
        "var groups=[],cur=[],used=0,firstLim=lim;"
        "for(var r=0;r<all.length;r++){"
        "if(cur.length&&used+all[r].__pph>lim){groups.push(cur);cur=[];used=0;"
        "var nx=pageInfo(pageTop,push);pageTop=nx.top+nx.cap;"
        "lim=capAt(pageTop,push)-slack;}"
        "cur.push(all[r]);used+=all[r].__pph;}"
        "if(cur.length)groups.push(cur);"
        // 位移推进：末块之后内容落在「末块页顶 + 末块高」；未切块时表尾 = 表顶 +
        // 表高，两者之差即本表切块把后续内容下推的量
        "push+=Math.max(0,(pageTop+used)-(y+totalH));"
        // 诊断（HF_DEBUG）：切块自述——容量模型与实际块规模
        "if(window.__ppDbg){window.__ppInfo=window.__ppInfo||[];(function(){"
        "var s=[];for(var g2=0;g2<groups.length;g2++){var hh=0;"
        "for(var q3=0;q3<groups[g2].length;q3++)hh+=groups[g2][q3].__pph;"
        "s.push(groups[g2].length+'行/'+Math.round(hh)+'px');}"
        "window.__ppInfo.push('表'+list[k2].idx+' y0='+Math.round(y0)+'(带位移'+Math.round(y)+') A='+Math.round(A)"
        "+' F='+Math.round(F)+(headBreak?' 挪页':'')+' 首块容量='+Math.round(firstLim)"
        "+' 块['+s.join(' ')+']');"
        "})();}"
        "if(groups.length<2&&!headBreak)continue;"
        "if(headBreak)t.classList.add('pdf-tbl-head');"
        "var cg=t.querySelector(':scope > colgroup');"
        "for(var k=groups[0].length;k<all.length;k++)all[k].remove();"
        "var prev=t;"
        "for(var g=1;g<groups.length;g++){"
        "var nt=t.cloneNode(false);"
        "nt.classList.add('pdf-tbl-cont');"
        "if(cg)nt.appendChild(cg.cloneNode(true));"
        "var tb=document.createElement('tbody');"
        "for(var q=0;q<groups[g].length;q++)tb.appendChild(groups[g][q]);"
        "nt.appendChild(tb);"
        "prev.parentNode.insertBefore(nt,prev.nextSibling);prev=nt;}"
        "}"
        "return 'ok';"
        "}catch(e){return 'err '+(e&&e.message||e);}"
        "};"
        // 脚注浮动重排**必须在预分页切块之前**：def 变成引用旁的 inline-block 后
        // 会把其后的内容整体下推（实测 1.1 表顶 222 → 真实 248，差 26px），而切块
        // 的"起始页余量"依赖表顶位置——先切块就按未浮动的位置算，首块因此多切一行
        // 而溢出（2px 的溢出即触发 paged.js 切表→错位碎片）。指纹快照在本段之前
        // （__preText 剔除 def、__preFns 是字符多重集），浮动不影响两者
        "try{window.__floatFootnotes();}catch(e){console.warn('脚注随页浮动重排失败: '+(e&&e.message||e));}"
        // 预算基准 = 版心（面积）高 __pagedContentPx（纸高 − 上下边距，由 C++ 按
        // 纸张与用户边距下发）；脚注区高 __pagedFnH 由测量遍量得后注入（第二遍
        // 加载）——测量遍只排一两页即中断，C++ 取高重载（同一次加载里二次 preview
        // 不可行：__restoreBody 后 paged.js 内部状态指向已移除节点，实测抛
        // "Cannot read properties of null"）
        "try{var ppt=window.__prepaginateTables(window.__pagedContentPx>0?window.__pagedContentPx:window.__pagedBoxH);if(ppt!=='ok')console.warn('表格预分页: '+ppt);}catch(e){console.warn('表格预分页失败: '+(e&&e.message||e));}"
        // 滚动条压制（上方 overflow:hidden）要罩住**全部分页前测量**（列宽归一
        // + 预分页切块）：滚动条吃掉 ~15px 视口宽，屏幕布局会比 paged 页内容盒
        // 窄，行高/块高因此偏大。测量到此结束，**清除后**才进分页——绝不能带进
        // 打印媒体（html{overflow:hidden} 在打印下会裁掉整份文档）
        "try{document.documentElement.style.overflow='';}catch(e){}"
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
    // 拆行器无条件引入（nativeHf 与 paged 两路径都调用 __splitPreLines；
    // 行为实现迁至 web/pdf-split-prelines.js，vitest 可测）
    QString libs = QStringLiteral("<script src=\"pdf-split-prelines.js\"></script>");
    if (needKatex)
        libs += QStringLiteral("<script src=\"katex.min.js\"></script>");
    if (needMermaid)
        libs += QStringLiteral("<script src=\"mermaid.min.js\"></script>");
    // KaTeX CSS（含 base64 字体）同理按需——无公式文档整段省略
    const QString katexCssOnDemand = needKatex ? katexCss : QString();

    // nativeHf：正文包装（thead 页眉重复带）+ 页码宿主与注入器 + 状态标志。
    // 页码注入器 __stampPageNumbers(n)：n 由 C++ 第一遍打印数出后经
    // runJavaScript 调入；每片恰一页高（= 打印片段高），片内 abs 页码落在
    // 该页纸底带（~8mm 距纸底，与 paged.js 的 @bottom-right 同落点）。
    // 同脚本另定义 __paginateNativeHf（轻量分页切页器，见函数内注释）
    QString finalBody = body;
    if (nativeHf) {
        // 标题等拼进属性的文本统一走 ExportCommon::htmlEscapeAttr（三导出器共用）
        QString hfScript;
        {
            const QSizeF paperMm = QPageSize(spec.paper).size(QPageSize::Millimeter);
            const qreal hMm = (spec.orientation == QPageLayout::Landscape)
                                  ? paperMm.width() : paperMm.height();
            // 页脚位形（2026-09-21 版式重做定案）：Chromium 打印把每页绘制
            // 硬裁到打印片段内——起始落在片段末之后的 abs 盒会被直接推到次页
            // 顶（探针实证），幽灵文本同理。新版式下打印区覆盖整张纸（Qt 边距
            // 0.1mm），页码盒整体落在片段内的纸底带。**位置由用户下边距推导**
            // （不再写死"距纸底 8mm"——那是照 paged 居中落点硬编码的，离纸边
            // 太近有打印风险）：行盒顶 = 正文底线（纸底 − 下边距）+ 净空
            // kHfHugGapMm**（与 paged 路径 @bottom-right 的
            // vertical-align:top + padding-top 同式同值：都是"行盒顶贴正文底线
            // 再退净空"，两路墨迹实测同高）。行盒内字形上偏 ~1.4mm 使视觉净空
            // 约 4.4mm。右缘退右页边距，与正文右边界对齐
            const qreal footTopOffMm =
                qMax<qreal>(0.0, spec.marginsMm.bottom() - kHfHugGapMm);
            // 轻量分页（__paginateNativeHf）：把正文按"每页只装内容高（纸高 −
            // 上下页边距）"切页，给页首块打 break-before:page。这是 nativeHf
            // 版式模拟"下页边距"的唯一手段——Chromium 打印没有逐页底部保留
            // 机制（tfoot 不逐页重复、@page 边距被 Qt 布局边距覆盖、
            // position:fixed 只在可打印区内重复，探针实验逐条证实），不切页
            // 正文会一路流到纸底压住页脚。测量在**屏幕媒体**里做（打印区 =
            // 整纸 + 基础 CSS 已解 @media print 包装 → 屏幕布局 = 打印布局），
            // 断页点 = "块顶相对当前页顶 + 块高 > 页内容高"贪心判定，切点全部
            // 落在块边界（表格按行、列表按项、引文下钻；代码块由
            // __splitPreLines 拆成的行块天然可断）。表格续页要扣重复表头高。
            // 单块高于一页时搬不动（自然分页自行拆分，成品校验拦截溢出→回退）
            hfScript = QStringLiteral(
                           "<script>"
                           // 就地拆 li：断点落在 li 内部时，把该 li 从断点处一分为二，
                           // 后半段成为新 li（无符号、挂 break-before:page）。ol 的序号
                           // 靠预钉的 data-hf-n 在新增 li 后回填 value 保持
                           "var breakAt=function(node){"
                           "var li=node&&node.closest?node.closest('li'):null;"
                           "if(!li||node.tagName==='LI'){node.style.breakBefore='page';return;}"
                           "var anchor=node,guard=0;"
                           "while(anchor.parentElement&&anchor.parentElement!==li"
                           "&&guard++<64)anchor=anchor.parentElement;"
                           "if(anchor.parentElement!==li){node.style.breakBefore='page';return;}"
                           "if(anchor===li.firstElementChild){"
                           "li.style.breakBefore='page';return;}"
                           "var cont=document.createElement('li');"
                           "cont.style.listStyle='none';"
                           "cont.style.breakBefore='page';"
                           "var par=li.parentNode,moved=[];"
                           "for(var sn=anchor;sn;sn=sn.nextSibling)moved.push(sn);"
                           "for(var mi=0;mi<moved.length;mi++)cont.appendChild(moved[mi]);"
                           "par.insertBefore(cont,li.nextSibling);"
                           "if(par.tagName==='OL'){"
                           "for(var nx=cont.nextElementSibling;nx;nx=nx.nextElementSibling)"
                           "if(nx.tagName==='LI'&&nx.getAttribute('data-hf-n')"
                           "&&!nx.hasAttribute('value'))nx.value=nx.getAttribute('data-hf-n');}};"
                           // ol 序号预钉：拆分前记录每个 li 的当前序号
                           "var pinOl=function(root){"
                           "var ols=root.querySelectorAll('ol');"
                           "for(var i=0;i<ols.length;i++){var ol=ols[i];"
                           "var n=parseInt(ol.getAttribute('start')||'1',10)||1;"
                           "for(var j=0;j<ol.children.length;j++){"
                           "var li=ol.children[j];if(li.tagName!=='LI')continue;"
                           "if(li.hasAttribute('value'))n=parseInt(li.getAttribute('value'),10)||n;"
                           "li.setAttribute('data-hf-n',n);n++;}}};"
                           // 预拆分（先拆后测）：把每个 li 按块级子元素边界拆成若干
                           // li（续段无符号、不改变缩进与间距），于是 li 内部任意块
                           // 边界都变成 Chromium 可靠执行的断点；必须在测量前完成，
                           // 保证"测什么就印什么"（边测边拆会让模拟与成品脱节）
                           "var preSplit=function(root){"
                           "var lis=root.querySelectorAll('li');"
                           "for(var i=0;i<lis.length;i++){"
                           "var li=lis[i];"
                           "if(li.closest('ul[data-type=taskList]'))continue;"
                           "var par=li.parentNode,cur=li;"
                           "var kids=Array.prototype.slice.call(li.children);"
                           "for(var j=1;j<kids.length;j++){"
                           "var k=kids[j];"
                           "if(!BLK[k.tagName])continue;"
                           "var cont=document.createElement('li');"
                           "cont.style.listStyle='none';"
                           "var moved=[];"
                           "for(var sn=k;sn;sn=sn.nextSibling)moved.push(sn);"
                           "for(var mi=0;mi<moved.length;mi++)cont.appendChild(moved[mi]);"
                           "par.insertBefore(cont,cur.nextSibling);"
                           "cur=cont;}}"
                           // 新增 li 会顶掉后续项编号：按预钉序号回填 value
                           "var ols=root.querySelectorAll('ol');"
                           "for(var oi=0;oi<ols.length;oi++){"
                           "var ols2=ols[oi];"
                           "for(var oj=0;oj<ols2.children.length;oj++){"
                           "var l2=ols2.children[oj];"
                           "if(l2.tagName!=='LI')continue;"
                           "var n2=l2.getAttribute('data-hf-n');"
                           "if(n2&&!l2.hasAttribute('value'))l2.value=n2;}}};"
                           // 块级标签白名单：判定容器是否继续下钻（preSplit 与单元
                           // 收集共用，故声明在顶层）。列表项里可能装着整棵嵌套子树
                           // ——只按 li 一层收集会让「6 级嵌套列表」的第一项成为不可
                           // 分巨块，整块被推到下一页、前一页留大片空白
                           "var BLK={P:1,UL:1,OL:1,LI:1,PRE:1,TABLE:1,BLOCKQUOTE:1,"
                           "DIV:1,HR:1,H1:1,H2:1,H3:1,H4:1,H5:1,H6:1,FIGURE:1};"
                           "window.__paginateNativeHf=function(Hpx,MbPx){"
                           "var cell=document.getElementById('hfcell');"
                           "if(!cell||!(Hpx>0))return 'no-cell';"
                           "pinOl(cell);"
                           "if(false)preSplit(cell);"  /* 就地拆 li 手术暂缓（屏幕模拟与打印成品尚有 ~6mm 系统偏差，做成布局中性后再启用——见 CHANGELOG 0.18.0「已知边界」） */
                           "var units=[],base=null;"
                           // 单元 = [断点节点, 顶, 高, 重复表头高, 可断否]。探针实测
                           // break-before:page 只在块边界被可靠执行：顶层块、表格行、
                           // **li（任意层级）**；li 内部的非 li 元素会被忽略。故 li
                           // 内部的断点一律"就地拆 li"（见 breakAt）：把该 li 在断点
                           // 处一分为二、后半段挂断点，让断点回到 Chromium 认的边界上
                           "var add=function(node,top,h,thH,canBreak){"
                           "if(base===null)base=top;"
                           "units.push([node,top,h,thH||0,canBreak?1:0]);};"
                           "var hasBlk=function(el){"
                           "for(var i=0;i<el.children.length;i++)"
                           "if(BLK[el.children[i].tagName])return true;return false;};"
                           // 叶块收集：表格按行（续页扣重复表头）、代码块按
                           // __splitPreLines 拆出的行块、可断块直接成单元；容器
                           // （列表/引文/含块级子元素的 div）继续下钻
                           "var leaf=function(el,safe){"
                           "var t=el.tagName,r=el.getBoundingClientRect();"
                           "var top=r.top+window.scrollY,h=r.height;"
                           "if(t==='TABLE'){"
                           "var th=el.querySelector(':scope > thead');"
                           "var thH=th?th.getBoundingClientRect().height:0;"
                           "var body=el.querySelector(':scope > tbody')||el;"
                           "var rows=body.children,seen=0;"
                           "for(var j=0;j<rows.length;j++){"
                           "if(rows[j].tagName!=='TR')continue;"
                           "var rr=rows[j].getBoundingClientRect();"
                           "add(rows[j],rr.top+window.scrollY,rr.height,seen===0?0:thH,safe);seen++;}"
                           "if(seen===0)add(el,top,h,0,safe);return;}"
                           "if(t==='PRE'){"
                           "var ls=el.querySelectorAll('.pl');"
                           "if(ls.length){"
                           "for(var j=0;j<ls.length;j++){var rr=ls[j].getBoundingClientRect();"
                           "add(ls[j],rr.top+window.scrollY,rr.height,0,safe);}return;}"
                           "add(el,top,h,0,safe);return;}"
                           "if(t==='UL'||t==='OL'){desc(el,el,safe);return;}"
                           // li 本身：**任意层级**都可断（探针实测：break-before 在 li
                           // 上被可靠执行），以其为首叶块的断点节点；li 内部再往下
                           // 的元素不可断（同探针：li 内部的非 li 元素上断点会失效、
                           // 内容错位），交给自然分页
                           "if(t==='LI'){desc(el,el,true);return;}"
                           // 其它容器（引文/块级 div/列表项）：子元素不算直接安全，
                           // 但若在普通 li 内部则靠拆分获得可断性（下方 desc 里判定）
                           "if(hasBlk(el)&&el.children.length){desc(el,el,false);return;}"
                           "add(el,top,h,0,safe);};"
                           // 下钻：keepNode 非空时，本容器（或其后代）的**首个叶块**
                           // 以 keepNode 作断点——列表序号/引用条等前置标记随块走，
                           // 断点落回容器上；且容器要一路穿透到叶块，否则「首个 li
                           // 里装整棵嵌套子树」会被记成一个巨块，整块推走留大片空白
                           // 可断性 = safe（父容器传下的"该断点 Chromium 是否可靠
                           // 执行"判定）：顶层块与 li 自身为真；li 内部的非 li 元素在
                           // Chromium 上 break-before 会被忽略（探针实证），内容一路流
                           // 到自然上限、与贴边距的页脚行盒相压 → 成品校验拦截、整篇
                           // 回退 paged。根治手段是"就地拆 li"（见 preSplit，暂缓启用）
                           "var desc=function(el,keepNode,safe){"
                           "var first=!!keepNode;"
                           "for(var i=0;i<el.children.length;i++){"
                           "var k=el.children[i],t=k.tagName;"
                           "if(k.id==='numhost'||t==='SCRIPT'||t==='STYLE')continue;"
                           "if(first){first=false;"
                           "if(!hasBlk(k)){var rr=k.getBoundingClientRect();"
                           "add(keepNode,rr.top+window.scrollY,rr.height,0,safe);continue;}"
                           "desc(k,keepNode,safe);continue;}"
                           "leaf(k,safe);}};"
                           // 入口：正文格的直接子块均为直接安全断点（keepNode=null）
                           "desc(cell,null,true);"

                           "if(!units.length)return 'no-units';"
                           // 贪心切页：pageTop = 当前页内容顶（屏坐标），budget =
                           // 本页可用高（以表格续行开页时扣重复表头）
                           "var pageTop=units[0][1],budget=Hpx;"
                           // 自然页容量 = 版心高 + 下边距：片段（纸高 − Qt 微量边距）
                           // 比版心高多出下边距那一段，不可断内容会一路流到那里才被
                           // 自然分页截断
                           "var natCap=Hpx+MbPx;"
                           "var breaks=0,tall=0,brkL=[],fills=[],lastBottom=0;"
                           "for(var i=0;i<units.length;i++){"
                           "var u=units[i],top=u[1],h=u[2],thH=u[3];"
                           "if(h>Hpx)tall++;"
                           "if(!u[4]){"
                           // 不可断单元：模拟自然分页——其末尾跨过自然页界时，把
                           // pageTop 同步到该自然页起点（否则后续断点全部漂移）
                           "var np=pageTop;"
                           "while(top+h>np+natCap)np+=natCap;"
                           "if(np!==pageTop){pageTop=np;budget=Hpx;brkL.push(-1);}"
                           "}else if(u[4]&&top-pageTop>0&&(top-pageTop)+h>budget){"
                           // 已在页首的块不得再断（重复 break-before 会插白页）
                           "breakAt(u[0]);"
                           "breaks++;"
                           "fills.push(Math.round(lastBottom-pageTop));"
                           "pageTop=top;budget=Hpx-thH;"
                           "brkL.push(Math.round(top-base));"
                           "}"
                           "lastBottom=top+h;}"
                           "fills.push(Math.round(lastBottom-pageTop));"
                           "window.__hfPagTall=tall;"
                           "window.__hfPagInfo='breaks='+breaks+' units='+units.length"
                           "+' tall='+tall+' H='+Math.round(Hpx)"
                           "+' fills=['+fills.join(',')+']';"
                           "return window.__hfPagInfo;};"
                           "</script>")
                + (spec.footerEnabled
                       ? QStringLiteral("<script>"
                                        "window.__stampPageNumbers=function(n,h1,hc,totalPx){"
                                        "var host=document.getElementById('numhost');"
                                        "if(!host||!(n>0))return false;"
                                        "while(host.firstChild)host.removeChild(host.firstChild);"
                                        // numhost 拉高到全文档流高：末页片段延到页底，末页
                                        // 覆盖层同权（cell 片段止于文档尾，锚点越片段即被
                                        // 裁——实测末页页码消失，此为绕开）
                                        "if(totalPx>0)host.style.height=totalPx+'px';"
                           // 页高分段堆叠：每片恰一页高（几何分段，与内容推挤/
                           // 墨底无关——零 T 模型、零校正；新版式下首片不再扣
                           // thead——页眉带已改在片段顶部，不再占用片段内位移）
                           "for(var k=1;k<=n;k++){"
                           "var d=document.createElement('div');d.className='hf-pg';"
                           "var sp=document.createElement('span');"
                           "sp.style.position='absolute';"
                           "sp.style.right='%2mm';"
                           "sp.style.background='#fff';"
                           "sp.style.padding='0 0 0 6px';"
                           "sp.style.top='calc(100% - %1mm)';"
                           "sp.textContent='%3'+k+'%4'+n+'%5';"
                           "d.appendChild(sp);"
                           "d.style.height=(k===1?h1:hc)+'px';"
                           "d.style.position='relative';"
                           "host.appendChild(d);}"
                           "return true;};"                           "</script>")
                           .arg(footTopOffMm, 0, 'f', 2)
                           .arg(spec.marginsMm.right(), 0, 'f', 2)
                           // 页码文案三段（同 paged 侧与校验正则的单一来源）
                           .arg(QString::fromUtf8(ExportCommon::kPageTextPrefix))
                           .arg(QString::fromUtf8(ExportCommon::kPageTextMid))
                           .arg(QString::fromUtf8(ExportCommon::kPageTextSuffix))
                       : QString());
        }
        const QString hostAndScript =
            QStringLiteral("<div id=\"numhost\"></div>") + hfScript;
        // 包装表恒启用（页眉关时 thead 留空带）：新版式下正文顶 = thead 带高
        // = 用户上边距，页眉开关不得改变正文版式
        finalBody = QStringLiteral(
                        "<table class=\"hf\"><thead><tr><td>%1</td></tr></thead>"
                        "<tbody><tr><td id=\"hfcell\">%2%3</td></tr></tbody></table>")
                        .arg(spec.headerEnabled ? ExportCommon::htmlEscapeAttr(spec.headerTitle) : QString(),
                             body, hostAndScript);
    }
    // __NATIVE_HF 置位在 %5（__preparePrint）之前即可——它在 preparePrint 调用
    // 时（页面加载后）才被读取
    if (nativeHf) {
        // 列宽归一基准：跨页表格短列塌缩（001 折两行/已完成折三行）+
        // 长列霸宽是 auto 布局顽疾——paged 路径早有两趟 intrinsic 测量
        // 修复（__fixTableColumns），nativeHf 缺席即「两套引擎表格布局
        // 不一致」（用户实测）；宽度基准须随路径注入
        const double contentWmm = (spec.orientation == QPageLayout::Landscape
                                       ? paperMm.height() : paperMm.width())
            - spec.marginsMm.left() - spec.marginsMm.right();
        // 每页内容高（px）：__paginateNativeHf 的切页预算 = 纸高 − 上下边距
        // （= 用户设定边距围出的版心高）再扣 4mm 安全余量。余量买的是"盒高
        // 之外的墨迹"：块的盒底落在预算内，但末行行盒下探/表格底边框/单元格
        // 底垫会让墨迹再往下 ~3-4mm（实测无余量时墨迹到 283mm，压进页脚行盒
        // 282mm）。扣掉后墨迹止于 ~279mm，与 paged 的实测页高（~254mm 内容 +
        // 页脚带）同量级。用公称纸高而非打印实测纸高：实测纸高含 Qt/Chromium
        // 亚像素舍入（小 ~0.3mm）
        const double contentHmm = (spec.orientation == QPageLayout::Landscape
                                       ? paperMm.width() : paperMm.height())
            - spec.marginsMm.top() - spec.marginsMm.bottom() - 4.0;
        libs.prepend(QStringLiteral(
                         "<script>window.__NATIVE_HF=true;"
                         "window.__pageContentWidthPx=%1;"
                         "window.__hfPageContentPx=%2;"
                         "window.__hfPageMarginBottomPx=%3;</script>")
                         .arg(contentWmm / 25.4 * 96.0, 0, 'f', 1)
                         .arg(contentHmm / 25.4 * 96.0, 0, 'f', 1)
                         .arg(spec.marginsMm.bottom() / 25.4 * 96.0, 0, 'f', 1));
    } else if (paged) {
        // 表格预分页的预算基准：版心高 = 纸高 − 上下边距（与 nativeHf 的
        // __hfPageContentPx 同源，但不扣 4mm 墨迹余量——切块另有 20% 余量）
        const double pageContentHmm = (spec.orientation == QPageLayout::Landscape
                                           ? paperMm.width() : paperMm.height())
            - spec.marginsMm.top() - spec.marginsMm.bottom();
        libs.prepend(QStringLiteral("<script>window.__pagedContentPx=%1;</script>")
                         .arg(pageContentHmm / 25.4 * 96.0, 0, 'f', 1));
    }

    // 基础文档 CSS 的媒体包装：**nativeHf 与 paged 两条分页路径都不包**，只有
    // legacy（无 paged.js、只打印一次）包着。paged 路径原先靠 paged.js 的
    // PrintMedia 处理器解包——但那是 preview() 里才发生的，而分页前的表格列宽
    // 归一（两趟 intrinsic 测量）与预分页切块**都在屏幕媒体里做**，包着就量在
    // 「无基础 CSS」的裸布局上（实测表格行高 30px，成品 52px ⇒ 切块按 30px/行
    // 算，每块塞进 ~1.7 倍行数 ⇒ paged.js 只能再切碎、页尾大片留白）。不包即
    // 屏幕布局 = 打印布局，与 nativeHf 同款
    const QString mediaOpen = (nativeHf || paged) ? QString() : QStringLiteral("@media print{");
    const QString mediaClose = (nativeHf || paged) ? QString() : QStringLiteral("}");

    QString fullHtml = QString(
        "<!DOCTYPE html><html><head><meta charset=\"UTF-8\"><style>"
        "%1"                                    // @page 分页规则（paged 激活时）
        "%8"                                    // @media print 开（nativeHf 空）
        "%2"                                    // @page{margin:0}（仅 legacy）
        "html,body{margin:0;padding:0;}"
        // ↑ paged 屏幕流与打印页内容盒同宽（2026-09-26 修复）：漏清 UA 默认
        // 8px 边距时屏幕流窄 16px，临界折行多折一行 → 预分页切块量高虚胖 →
        // break-before:page 强制断页留白（实测首页 4-6 行空白）；nativeHf
        // 覆盖层 :257 同款规则本就有，paged/legacy 路径此前靠不到它
        "body{font-family:\"Noto Sans SC\",\"DejaVu Sans\",sans-serif;"
        "font-size:11pt;line-height:1.6;color:#333;"
        // 连续无空格长串（长 URL/色块串）必须能断行，否则横向溢出：paged
        // 路径会被页盒 overflow:hidden 静默裁掉（实测 2.3 节超长列表项墨迹
        // 直达纸边 209.8mm 被切），nativeHf 路径则被推到下一页顶。此前只有
        // th,td 带 overflow-wrap:anywhere（表格列宽归一用），nativeHf 的正文
        // 整体包在包装表单元格里因而全篇继承、paged 路径无此包装故不继承
        // ——同一份文档两条路径折行行为分叉。提到文档级后两路一致；anywhere
        // 而非 break-word：前者同时收敛 min-content，避免长串撑爆文档
        // min-content 触发 shrink-to-fit 全局缩印
        "overflow-wrap:anywhere;}"
        "h1{font-size:1.7em;margin-top:1em;margin-bottom:0.3em;}"
        "h2{font-size:1.3em;margin-top:0.8em;margin-bottom:0.2em;}"
        "h3{font-size:1.1em;margin-top:0.6em;margin-bottom:0.2em;}"
        "p{margin-top:0;margin-bottom:0.3em;}"
        // pre 用 pre-wrap 取代 overflow-x:auto：滚动容器让分页器 breakToken 卡死
        // （Unable to layout item → 静默丢段，R2 实测丢 8 行）；折行后每个行盒都是
        // 合法断点，超一页高的代码块也能逐行分页，长 token 由 anywhere 兜底折行
        // box-decoration-break:clone：跨页代码块的底色随片段盒子走。默认 slice
        // 下 Chromium 会把最后一片的背景一直画到片段容器末端——nativeHf 版式
        // 里可打印区覆盖整张纸（Qt 边距 0.1mm），该末端就是纸底，实测灰底
        // 242→296.6mm 压过页脚画到页边（用户截图）。clone 让每片成为完整盒子，
        // 底色止于内容；与 paged 路径的逐页克隆外观一致
        "pre{background:#f4f5f7;padding:10px 14px;border-radius:4px;"
        "-webkit-box-decoration-break:clone;box-decoration-break:clone;"
        "font-family:monospace;font-size:9.5pt;line-height:1.35;margin:0.5em 0;"
        "white-space:pre-wrap;overflow-wrap:anywhere;}"
        // 逐行重排的行 span（__splitPreLines 生成）：块级让分页落在行间元素边界；
        // avoid 兜底长行折行后的行盒（单逻辑行 ≤1 页高时无副作用）
        "pre .pl{display:block;break-inside:avoid;}"
        // 底色/内边距落在行块上（逐行重排后由 JS 清掉 pre 自身的底色）：
        // 行块不跨页 ⇒ 背景不越界；首末行补垂直内边距与圆角，复刻整块外观
        "pre .pl{background:#f4f5f7;padding:0 14px;}"
        "pre .pl:first-child{padding-top:10px;border-radius:4px 4px 0 0;}"
        "pre .pl:last-child{padding-bottom:10px;border-radius:0 0 4px 4px;}"
        "code{background:#f0f0f0;padding:1px 3px;border-radius:2px;font-size:0.9em;"
        "overflow-wrap:anywhere;}"
        // 代码块内的 <code> 不参与底色：逐行重排后它的子元素是块级 .pl，行内盒
        // 被块级子元素切开，首尾各留一个空行内碎片——各带自己的底色与内边距，
        // 在代码块上下方各画出一条细灰条（用户截图）。底色统一由 .pl 承载
        "pre code{background:none;padding:0;border-radius:0;}"
        // 表行不跨页拆分（与下方 __fixTableBreaks 分工：CSS 兜住「整行越界」的
        // 常规断点路径——paged.js 只对 td/th 的 avoid 走「整行带走」工作区，上游
        // 注释：take the whole row, not just the cell，只加 tr 时行内断点仍穿透；
        // 骑跨行压线贴合区底的场景 CSS 永远查不到——那是 onOverflow 回退修复的）
        "tr,th,td{break-inside:avoid;}"
        // % 无需转义——QString::arg 只识别 %1..%99 占位符，%% 会原样保留成无效 CSS（历史缺陷已修）
        "table{border-collapse:collapse;width:100%;margin:0.5em 0;}"
        "table.pdf-tbl-split{break-before:page;}"
        "table.pdf-tbl-cont{break-before:page;margin-top:0;}"
        // 预分页：整表挪页（本页余量连首行都放不下——表落在"塞不进一行"的页尾时
        // paged.js 必然切表，而切表会触发错位碎片丢行）
        "table.pdf-tbl-head{break-before:page;}"
        // 单元格字号 11pt（= 正文字号，四面统一，2026-09-24 定案；此前 10pt 为
        // 四面唯一特例）。行高继承 body 1.6 → 单元格行 17.6pt + 上下 padding
        // 6pt（4px×2）= 23.6pt
        "th,td{border:1px solid #ccc;padding:4px 8px;font-size:11pt;}"
        "th,td p{margin:0;}"
        // ↑ TipTap 单元格内容包 <p>（2026-09-26 修复）：不清零则 p{margin-bottom:
        // .3em} 每格加 3.3pt，行高 23.6→26.9pt，与 DOCX 摊平单段（无段边距）
        // 不一致——实测 PDF 首页 18→16 行
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
        "%9"                                    // @media print 闭（nativeHf 空）
        "%3"                                    // KaTeX CSS（按需，无公式为空）
        "</style>"
        "%4"                                    // PagedConfig + polyfill（paged 时）
        "%7"                                    // 重型库按需（katex/mermaid）
        "<script>"
        "%5"                                    // __preparePrint
    ) + HtmlExporter::katexRenderScript() + QString(
        // KaTeX 段与 HTML 导出共用同一份（HtmlExporter::katexRenderScript，
        // 带 window.katex 守卫）——原先这里是无守卫的副本
        "</script>"
        "</head><body>%6</body></html>"
    );
    fullHtml = fullHtml.arg(pageCss, legacyPageZero, katexCssOnDemand, pagedScripts,
                            preparePrint, finalBody, libs, mediaOpen, mediaClose);

    // 注入 mermaid 渲染 helper（定义 window.__renderAllMermaid，返回 Promise，
    // 不自调用——__preparePrint 显式链式调用并等待）。PDF 固定亮色打印。
    fullHtml.replace("</head>",
        QString("<script>%1</script></head>").arg(HtmlExporter::mermaidRenderScript("default")));

    if (!customCss.isEmpty()) {
        // 剥 </style> 序列（与 HtmlExporter 共用同一实现，避免安全规则分叉）
        const QString safeCss = ExportCommon::stripStyleClose(customCss);
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

QMarginsF PdfExporter::nativeHfPrintMargins()
{
    // nativeHf 版式（2026-09-21 对齐 paged.js 重做）的 Qt 打印边距：与 paged
    // 路径同为 0.1mm 微量值（避开 Qt 零值默认 18mm 边距的行为，见
    // pagedPrintMargins 注释），但目的相反——paged 路径的打印区**收窄**到
    // @page 盒（paged.js 自画页边距），nativeHf 的打印区要**覆盖整张纸**：
    //   · 页眉带（包装表 thead，高 = 用户上边距）占纸顶，跨页重复
    //   · 页脚覆盖层落纸底带（~8mm 距纸底，与 paged @bottom-right 同带）
    //   · 用户页边距改由流内结构模拟：上 = thead 带、下 = 逐页强制分页
    //     （__paginateNativeHf 每页只装"内容高"）、左右 = 内容格 padding
    // 探针实证：可打印区 = 纸 − Qt 边距；越界绘制=幽灵文本、起始越界的 abs
    // 盒被推到次页顶——故页眉页脚都必须整体落在打印区内，这条是整版方案的
    // 立论基点（docs 取证记录见提交说明）
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
    const QString katexCss = ExportCommon::readResource(QStringLiteral(":/web/katex.min.css"));

    // 构建 PDF 渲染页面：图片 base64 内联 + 内联 KaTeX CSS + JS + 渲染脚本。
    // 路径裁决（2026-09-19 提速）：页眉/页脚启用且无脚注引用 → 原生分页+
    // 覆盖层（thead 重复带 + 边距页码，含页脚时两遍打印定总页数），绕开
    // paged.js 逐页 DOM 手术（取证 ~0.8s/页，16 页 13s）；含脚注引用仍走
    // paged.js（float:footnote 随页浮动是 paged 独有），nativeHf 任一环节
    // 失败也回退 paged 完整链路
    m_spec = spec;
    m_srcHtml = html;
    m_srcDocDir = docDir;
    m_srcKatexCss = katexCss;
    m_srcCustomCss = customCss;
    m_nativeHfPass = 0;
    PdfPageSpec htmlSpec = spec;
    htmlSpec.nativeHf = spec.pagedActive()
        && !html.contains(QLatin1String("data-type=\"footnoteRef\""));
    QString fullHtml = buildRenderHtml(html, docDir, katexCss, customCss, htmlSpec);

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
    // nativeHf 版式：打印区覆盖整张纸（页眉带占纸顶、页脚落纸底带），
    // 用户页边距由流内结构模拟——见 nativeHfPrintMargins 注释
    m_nativeHfLayout = QPageLayout(QPageSize(static_cast<QPageSize::PageSizeId>(paper)),
        static_cast<QPageLayout::Orientation>(orient),
        nativeHfPrintMargins(), QPageLayout::Millimeter);

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

    // 屏幕视口宽必须 = 打印区宽（两条分页路径共用）。二者都在**屏幕媒体**里
    // 量布局：nativeHf 的分页（__paginateNativeHf 量块高定断页点）与两路径
    // 共用的表格列宽归一（__fixTableColumns 两趟 intrinsic 测量）都依赖屏幕
    // 布局 = 打印布局。渲染视图是隐藏控件，停在默认 640×480 视口宽下：
    //   · nativeHf：块高系统性偏大（实测表格行高 +30%）→ 断页点提前、每页
    //     装的内容少于 paged（13 页 vs 11 页）
    //   · paged：列宽分配与 paged 自身页盒排版分叉（短列被压到逐字竖排）
    // 故显式 resize 到打印区宽（公称纸宽 − 2×Qt 微量边距，与
    // __hfPageContentPx 同源）。宽度只在屏幕媒体生效（打印布局由 Qt 页版式
    // 决定），nativeHf 另注入 html{overflow:hidden} 防滚动条吃宽
    if (spec.pagedActive()) {
        // **屏幕布局宽 = paged 页盒内容宽**（纸宽 − 用户左右边距 = 分页后
        // .pagedjs_page_content 的宽），不能取纸宽：页边距在 paged 路径由
        // paged.js 的 @page 承载，分页后正文被搬进只有内容宽（A4/18mm 边距下
        // 658px）的页内容盒重新排版。若屏幕按纸宽（791px）排版，表格不折行
        // （实测行高 30px），而分页后同一行折成两行（52px）——预分页按 30px/行
        // 切块 ⇒ 每块塞进 ~1.7 倍行数 ⇒ paged.js 只能再切碎（丢行路径）且页尾
        // 大片留白；列宽归一的 intrinsic 测量同样失真。
        // nativeHf 例外：其正文横跨整张纸（页边距由 #hfcell 内垫承载），取纸宽
        const QSizeF pm = QPageSize(spec.paper).size(QPageSize::Millimeter);
        const qreal wmm = htmlSpec.nativeHf
            ? (spec.orientation == QPageLayout::Landscape ? pm.height() : pm.width())
                  - pagedPrintMargins().left() - pagedPrintMargins().right()
            : (spec.orientation == QPageLayout::Landscape ? pm.height() : pm.width())
                  - spec.marginsMm.left() - spec.marginsMm.right();
        // 视口再收 2px：打印可打印区实测比公称值小 ~0.5-1.5px（Qt/Chromium
        // 亚像素舍入），屏幕若略宽，边界处的单元格会在成品里多折一行，逐页
        // 累积成数毫米溢出。收紧后屏幕折行只会比打印更保守（模拟略微高估
        // 块高 → 断页略早），不会低估
        m_renderView->resize(qRound(wmm / 25.4 * 96.0) - 2, 1200);
    }

    if (qEnvironmentVariableIsSet("HF_DUMP_HTML")) {
        QFile d("/tmp/hf-full.html");
        if (d.open(QIODevice::WriteOnly)) { d.write(fullHtml.toUtf8()); d.close(); }
    }
    // 记下本次渲染 HTML：若 paged 路径先跑了"测量遍"（只排第一页量页内容盒
    // 真实高），C++ 会用它重新加载并注入实测值再走生产遍
    m_pagedHtml = fullHtml;
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
    // 第二遍（生产遍）：注入测量遍量得的页内容盒真实高（切块按实测高切，
    // 页尾不留白、也不用任何魔法数）
    if (m_measuredBoxH > 0) {
        const bool ppDbg = qEnvironmentVariableIsSet("HF_DEBUG");
        m_renderView->page()->runJavaScript(
            QStringLiteral("window.__pagedBoxH=%1;window.__pagedFnH=%2;window.__ppDbg=%3;")
                .arg(m_measuredBoxH).arg(m_measuredFnH).arg(ppDbg ? 1 : 0));
    }
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
        emit writingPdf(0);
        m_renderView->page()->printToPdf(m_pendingOutputPath, m_legacyLayout);
        return;
    }
    // 状态 + 已生成页数一并读（复用同一轮询，零额外开销）：working 期间页数
    // 增长即发 pagedProgress——paged.js 大文档分页以十秒计（2026-09-15 性能
    // 取证 ~0.8s/页），全程静默等待体感差
    m_renderView->page()->runJavaScript(
        "(function(){return JSON.stringify({s:window.__printState||'',"
        "n:document.querySelectorAll('.pagedjs_page').length,"
        "t:window.__hfPagTall||0,"
        "m:window.__measureH||0,"
        "g:window.__measureDbg||''});})()",
        [this](const QVariant &v) {
            QJsonParseError e;
            const QJsonDocument d = QJsonDocument::fromJson(v.toString().toUtf8(), &e);
            const QJsonObject o = d.object();
            const int pagTall = e.error == QJsonParseError::NoError
                                    ? o.value("t").toInt() : 0;
            const QString state = e.error == QJsonParseError::NoError
                                      ? o.value("s").toString() : QString();
            const int pages = e.error == QJsonParseError::NoError
                                      ? o.value("n").toInt() : 0;
            if (state == QLatin1String("working") && pages > m_lastPagedPages) {
                m_lastPagedPages = pages;
                emit pagedProgress(pages);
            }
            // 测量遍完成（只排了第一页）：取实测页内容盒高 → 重新加载页面并
            // 注入该值，第二次加载走生产路径（切块用实测高，页尾不留白）。
            // 只测量一次（m_measuredBoxH 非 0 即不再进）
            if (state == QLatin1String("measured") && m_measuredBoxH == 0) {
                int h = e.error == QJsonParseError::NoError
                            ? o.value("m").toInt() : 0;
                // 兜底解析：钩子里同一处写下的自述串稳定可读（实测 __measureH
                // 在该上下文里读回 0，原因未明），从中取高度保证功能可用
                if (h <= 0) {
                    const QString dbg = o.value("g").toString();
                    const QRegularExpression re(QStringLiteral("高=(\\d+)"));
                    const auto m2 = re.match(dbg);
                    if (m2.hasMatch())
                        h = m2.captured(1).toInt();
                }
                // 脚注区实测高（同一次测量遍带回）：切块按它预留。两条来源取大者
                // ——① 文档级首个脚注区高（第 2 页读到的是第 1 页的定型高）② 面积
                // 高 − 最挤页的内容盒高（页内容盒 = 面积 − 脚注区，两值相减即得，
                // 不依赖脚注区元素是否已被 paged.js 定型）
                {
                    const QString dbg = o.value("g").toString();
                    const auto mFn = QRegularExpression(QStringLiteral("脚注区=(\\d+)"))
                                         .match(dbg);
                    if (mFn.hasMatch())
                        m_measuredFnH = mFn.captured(1).toInt();
                    const auto mArea = QRegularExpression(QStringLiteral("面积=(\\d+)"))
                                           .match(dbg);
                    if (mArea.hasMatch() && h > 0)
                        m_measuredFnH = qMax(m_measuredFnH,
                                             mArea.captured(1).toInt() - h);
                }
                qWarning("[PdfExport] 测量遍回报 h=%d dbg=%s", h,
                         qPrintable(o.value("g").toString()));
                if (h > 0) {
                    m_measuredBoxH = h;
                    m_printPoll.stop();
                    startPagedExport(m_pagedHtml);
                }
                return;
            }
            // 未到终态（idle/working/空）继续轮询；回调与停表存在竞态，须防重
            if (state != QLatin1String("paged") && state != QLatin1String("legacy")
                && state != QLatin1String("fallback")
                && state != QLatin1String("nativehf"))
                return;
            m_printPoll.stop();
            if (m_printStarted)
                return;
            m_printStarted = true;
            // 写盘阶段进度：paged 终态页数已知（本回调刚读的 DOM 页数）；
            // 其余路径页数不可知（nativeHf 含页脚要第一遍打印后才数得出），
            // 发 0——调用方降级为「正在写出 PDF」文案
            emit writingPdf(state == QLatin1String("paged") ? pages : 0);
            if (state == QLatin1String("nativehf")) {
                // 单块高于一页：轻量分页搬不动它（自然分页会把碎片铺进纸底带
                // 压住页脚），提前改走 paged.js——省一遍白印
                if (pagTall > 0) {
                    fallbackFromNativeHf(
                        QStringLiteral("存在高于一页的不可分块（%1 个）").arg(pagTall));
                    return;
                }
                // 原生覆盖层路径：仅页眉免数页数单遍直出；含页脚先打临时文件
                // 数页数，onPdfPrintingFinished 注入页码后打成品（两遍分页恒等
                // ——覆盖层零布局影响，print-probe 实证不增页）
                if (!m_spec.footerEnabled) {
                    m_renderView->page()->printToPdf(m_pendingOutputPath,
                                                     m_nativeHfLayout);
                } else {
                    m_nativeHfPass = 1;
                    m_nativeHfTemp = QDir::tempPath()
                        + QStringLiteral("/md-editor-hf-%1-%2.pdf")
                              .arg(QCoreApplication::applicationPid())
                              .arg(QDateTime::currentMSecsSinceEpoch() % 100000);
                    m_renderView->page()->printToPdf(m_nativeHfTemp, m_nativeHfLayout);
                }
                return;
            }
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
    // nativeHf 第一遍（数页数）：QPdfDocument 读页数 → 注入页码覆盖层 → 打成品
    if (m_nativeHfPass == 1) {
        m_nativeHfPass = 2;
        if (!success) {
            fallbackFromNativeHf(QStringLiteral("第一遍打印失败"));
            return;
        }
        QPdfDocument doc;
        if (doc.load(filePath) != QPdfDocument::Error::None) {
            fallbackFromNativeHf(QStringLiteral("页数文件读取失败"));
            return;
        }
        const int n = doc.pageCount();
        // 墨底诊断要用的裸 PDF 字节（仅 HF_DEBUG，见下方测量块）：内容流几何
        // 测量需要原始字节（SVG/图片等无文本内容的底沿，QPdfDocument 的文本
        // 接口量不到）——先读再删临时文件
        QByteArray pdfBytes;
        if (qEnvironmentVariableIsSet("HF_DEBUG")) {
            QFile f(filePath);
            if (f.open(QIODevice::ReadOnly))
                pdfBytes = f.readAll();
        }
        QFile::remove(m_nativeHfTemp);
        if (n <= 0) {
            fallbackFromNativeHf(QStringLiteral("页数统计失败"));
            return;
        }
        // 页码分段堆叠（2026-09-20 定稿）：每片高恰一页，分页器把每片文字钳在
        // 该片所在页的片段末——几何分段与内容推挤/墨底无关，零 T 模型、零校正
        // 轮；abs 容器自身按页格分段（其流与 cell 内容推挤解耦）
        {
            const qreal pxPerPt = 96.0 / 72.0;
            // 墨底诊断（仅 HF_DEBUG）：文本底与内容流几何底沿（SVG/图片等无文本
            // 内容）取深，排查页码落位用。整段含一次全 PDF 内容流解析 + 逐页文本
            // 提取，产物不依赖其输出（分段堆叠已无 T 模型）——默认不跑
            if (qEnvironmentVariableIsSet("HF_DEBUG")) {
                const qreal topPt = m_spec.marginsMm.top() / 25.4 * 72.0;
                const qreal mBotPt = m_spec.marginsMm.bottom() / 25.4 * 72.0;
                // 新版式（打印区 = 整纸）：页眉带高 = 用户上边距——内容顶自纸顶
                // = Qt 上边距(0.1mm) + 页眉带
                const qreal qtTopPt = nativeHfPrintMargins().top() / 25.4 * 72.0;
                const qreal headPt = qtTopPt + (m_spec.headerEnabled ? topPt : 0.0);
                const QVector<PdfPageGeometry::PageBottom> geo
                    = PdfPageGeometry::measure(pdfBytes);
                for (int p = 0; p < n; ++p) {
                    const qreal bandBottomPt = doc.pagePointSize(p).height() - mBotPt - 2.0;
                    qreal maxBottomPt = 0;
                    const auto polys = doc.getAllText(p).bounds();
                    for (const auto &poly : polys) {
                        const qreal b = poly.boundingRect().bottom();
                        if (b < bandBottomPt && b > maxBottomPt) maxBottomPt = b;
                    }
                    const qreal textBottomPt = maxBottomPt;
                    if (p < geo.size() && geo[p].valid
                        && geo[p].bottomPt < bandBottomPt
                        && geo[p].bottomPt > maxBottomPt)
                        maxBottomPt = geo[p].bottomPt;
                    if (maxBottomPt <= 0) maxBottomPt = bandBottomPt - 10;
                    qWarning("[PdfExport][hf-geo] p%d 文本底=%.1f 几何底=%.1f 墨底px=%.0f",
                             p + 1, textBottomPt,
                             p < geo.size() && geo[p].valid ? geo[p].bottomPt : -1.0,
                             (maxBottomPt - headPt) * pxPerPt);
                }
            }
            // Chromium 打印以整数 px 计页高/边距（实测每页 ~1px 舍入差，分段堆叠
            // 下逐页上爬 0.75pt）——同径取整消除。每片高 = 打印片段高（纸高扣 Qt
            // 微量边距），不扣 thead：页眉带已移到片段顶部占位，不消耗片段内位移
            const qreal Hc = qRound(doc.pagePointSize(0).height() * pxPerPt)
                - qRound(nativeHfPrintMargins().top() / 25.4 * 72.0 * pxPerPt)
                - qRound(nativeHfPrintMargins().bottom() / 25.4 * 72.0 * pxPerPt);
            m_hfTotal = n;
            m_hfH1Px = Hc + 1.04;   // 漂移校准（旧版式实测 1.036px/页，同值起步）
            m_hfHcPx = Hc + 1.04;
            m_hfTotalPx = n * Hc;
            stampAndPrintHf();
        }
        return;
    }
    // nativeHf 成品遍：逐页校验页码（第 k 页恰为 k/n），不符即回退 paged.js
    // 重导（慢但正确），绝不出错页码成品——校正轮机制已随分段堆叠撤销（每片
    // 高恰一页，无 T 模型可修），保留的只有校验与回退
    // 字体 cmap 可能把「页」映成部首码位（视觉同形），页数字符用通配；
    // 匹配前白名单剥离：提取流可能逐字形插 \r\n、偶发不可见非字符
    if (m_nativeHfPass == 2) {
        m_nativeHfPass = 0;
        if (qEnvironmentVariableIsSet("HF_DEBUG"))
            qWarning("[hf-verify] 成品遍开始 %s", qPrintable(filePath));
        if (!success) {
            fallbackFromNativeHf(QStringLiteral("第二遍打印失败"));
            return;
        }
        QPdfDocument doc;
        if (doc.load(filePath) != QPdfDocument::Error::None) {
            fallbackFromNativeHf(QStringLiteral("成品校验读取失败"));
            return;
        }
        const int stamped = m_hfTotal;
        const int n2 = doc.pageCount();
        // 页码文案的校验侧（与 kPageText* 同源，见 ExportCommon）
        const QRegularExpression re = ExportCommon::pageNumberVerifyPattern();
        // y 反馈优先取内容流几何的色锚定基线（#666 下半页文本 = 页码标记
        // 本尊；表头同色恒在页顶不干扰）——纯底带墨底会被深处内容/SVG 标
        // 签冒名顶替（实测 s-11 页码高 118px 而校验假通过），仅作几何缺位
        // 时的兜底
        QByteArray pdfBytes2;
        {
            QFile f2(filePath);
            if (f2.open(QIODevice::ReadOnly))
                pdfBytes2 = f2.readAll();
        }
        const QVector<PdfPageGeometry::PageBottom> geo2
            = PdfPageGeometry::measure(pdfBytes2);
        QVector<int> hitPage(stamped + 1, -1);   // 页码 k → 落页（0 基）
        QVector<qreal> stripBottom(n2, -1.0);
        QVector<bool> pageOk(n2, false);
        QString dbg;
        bool spillFound = false;   // 正文墨迹落入纸底留白带（轻量分页失守）
        bool allOk = (n2 == stamped);
        // 预收集各页标记 y（色锚定基线优先），水位 = 页间中位数——校正只
        // 追平页间偏差，不追绝对目标（放置公式的系统性落点差 ~20pt 曾把
        // 全套标记推出页界，24 页文档实测错位 22 页）
        const auto pageY = [&](int p) -> qreal {
            return (p < geo2.size() && geo2[p].footerBasePt >= 0)
                ? geo2[p].footerBasePt
                : (p < stripBottom.size() ? stripBottom[p] : -1.0);
        };
        for (int p = 0; p < n2; ++p) {
            const auto sel = doc.getAllText(p);
            const auto polys = sel.bounds();
            // 幽灵文本防线（2026-09-21 像素取证教训）：越片段末绘制的页码
            // 内容流矩阵在而像素无——QPdf 对其 bounds 报全零。匹配文本只取
            // bounds 非退化的字符，不可见页码不得通过验证（矩阵基的色锚定
            // 对此盲，曾致全档幽灵页码假通过）
            QString t;
            for (int ci = 0; ci < sel.text().size() && ci < polys.size(); ++ci)
                if (polys[ci].boundingRect().width() > 0.1)
                    t += sel.text()[ci];
            t.remove(QRegularExpression(
                QStringLiteral("[^0-9第共./⻚页]")));
            QStringList hits;
            auto it = re.globalMatch(t);
            while (it.hasNext()) {
                const auto m = it.next();
                const int k = m.captured(1).toInt();
                const int tot = m.captured(2).toInt();
                hits << QStringLiteral("%1/%2").arg(k).arg(tot);
                if (tot == stamped && k >= 1 && k <= stamped && hitPage[k] < 0)
                    hitPage[k] = p;
            }
            const qreal pageHpt = doc.pagePointSize(p).height();
            const qreal bandBottomPt
                = pageHpt - m_spec.marginsMm.bottom() / 25.4 * 72.0 - 2.0;
            // 内容溢出拦截（2026-09-21 版式重做新增）：可断单元按版心高切页，
            // 不可断内容（深层嵌套列表内部等）交给 Chromium 自然分页——它会
            // 一路流到自然上限（片段末 = 纸底 − Qt 微量边距），比版心底多出
            // 下边距那一段。故溢出判据取「自然上限 + 2.5mm 余量（末行底边框/
            // 行盒下探）」到「页脚行盒顶」之间那条带：正文墨迹落进带内即判
            // 失败——宁可回退 paged（慢但正确），也不出压住页脚的成品
            const qreal natBottomPt
                = pageHpt - (nativeHfPrintMargins().top() + m_spec.marginsMm.top())
                                / 25.4 * 72.0;
            const qreal spillLoPt = natBottomPt + 2.5 / 25.4 * 72.0;
            const qreal spillHiPt
                = pageHpt - (m_spec.marginsMm.bottom() - kHfHugGapMm) / 25.4 * 72.0;
            for (const auto &poly : polys) {
                const QRectF pr = poly.boundingRect();
                const qreal b = pr.bottom();
                // 兜底窗口下界放宽到近页底：页脚已画进下边距带（内容区底
                // 之下），色锚定漏检时的几何兜底须覆盖新落点
                if (b > bandBottomPt - 45.0 && b < pageHpt - 8.0
                    && b > stripBottom[p])
                    stripBottom[p] = b;
                // 页脚自身排除：字体 em 盒比字形高，页脚文本的多边形上缘会
                // 探进净空带造成误判（实测 281.8mm 处无墨迹却有字形盒）。
                // 几何模块已按专属色锚定页脚基线 footerBasePt，±6pt 内的
                // 多边形即页脚本尊，不计入溢出判据
                const qreal fb = (p < geo2.size() && geo2[p].footerBasePt >= 0)
                                     ? geo2[p].footerBasePt : -1000.0;
                if (pr.width() > 0.1 && b > spillLoPt && pr.top() < spillHiPt
                    && qAbs(b - fb) > 6.0) {
                    spillFound = true;
                    if (qEnvironmentVariableIsSet("HF_DEBUG"))
                        qWarning("[hf-spill] p%d poly=%.1f..%.1f x=%.1f..%.1f fb=%.1f 窗=%.1f..%.1f",
                                 p + 1, pr.top(), b, pr.left(), pr.right(), fb,
                                 spillLoPt, spillHiPt);
                }
            }
            dbg += QStringLiteral(" p%1→[%2]").arg(p + 1).arg(hits.join(","));
            // 判据 = 命中列表含本页页码：白名单剥离会把相邻内容数字与页码
            // 粘连出假匹配（实测「第96页/共1810页」），首匹配判据误伤本页
            const QString want = QStringLiteral("%1/%2").arg(p + 1).arg(stamped);
            pageOk[p] = hits.contains(want);
        }
        // 粘连页消元兜底：提取流可能把内容数字交织进页码字形序列（值不可
        // 辨），该页按「有页脚墨迹（色锚定）+ 该页码未被别页认领」判真——
        // 页码按序生成，他页全对时本页只可能是自己
        for (int p = 0; p < stamped && p < n2; ++p) {
            if (pageOk[p])
                continue;
            const qreal yPt = (p < geo2.size() && geo2[p].footerBasePt >= 0)
                ? geo2[p].footerBasePt
                : (p < stripBottom.size() ? stripBottom[p] : -1.0);
            const int claim = (p + 1 < hitPage.size()) ? hitPage[p + 1] : -1;
            if (yPt >= 0 && claim == -1)
                pageOk[p] = true;
        }
        for (int p = 0; p < stamped && p < n2; ++p)
            if (!pageOk[p])
                allOk = false;
        if (spillFound)
            allOk = false;
        if (allOk) {
            // y 一致性终检：各非末页标记基线对页间中位数 ±10pt（「页脚上爬」
            // 即页间相对漂移；绝对落点由放置公式决定，不追）
            QVector<qreal> lv;
            for (int p = 0; p < stamped - 1 && p < n2; ++p)
                if (pageY(p) >= 0)
                    lv.append(pageY(p));
            if (!lv.isEmpty()) {
                // 相对水位（中位数）：分段堆叠的结构位置随版式常数平移
                // （thead/边距/片段钳制的合成落点），绝对目标必与结构位错位
                std::sort(lv.begin(), lv.end());
                const qreal level = lv[lv.size() / 2];
                // 只判可测页：色锚定偶发漏检（实测 p12 页脚像素在位而锚
                // 未命中——变换上下文），不可测且有文本命中的页跳过判定，
                // 否则无 y 反馈的页会被误判（现无校正轮，误判即整篇回退 paged）
                for (int p = 0; p < stamped - 1 && p < n2; ++p) {
                    const qreal y = pageY(p);
                    if (y >= 0 && qAbs(y - level) > 6.0)
                        allOk = false;
                }
                if (qEnvironmentVariableIsSet("HF_DEBUG")) {
                    QString ys;
                    for (int p = 0; p < stamped - 1 && p < n2; ++p)
                        ys += QStringLiteral(" %1:%2").arg(p + 1)
                                  .arg(pageY(p), 0, 'f', 1);
                    qWarning("[hf-ychk] 水位=%.1f%s", level,
                             qPrintable(ys));
                }
            }
        }
        if (allOk) {
            emit exportFinished(true, filePath);
            return;
        }
        qWarning() << "[PdfExport][hf-debug]" << dbg;
        fallbackFromNativeHf(spillFound
                                 ? QStringLiteral("正文溢出纸底留白带")
                                 : QStringLiteral("页码漂移"));
        return;
    }
    if (success)
        emit exportFinished(true, filePath);
    else
        emit exportError("PDF 生成失败");
}

// nativeHf：注入页码覆盖层标记后打成品（片高/总页数由分段堆叠给定）
void PdfExporter::stampAndPrintHf()
{
    // 第一遍已数出页数：成品写盘阶段进度带上总页数
    emit writingPdf(m_hfTotal);
    m_renderView->page()->runJavaScript(
        QStringLiteral("window.__stampPageNumbers(%1,%2,%3,%4)")
            .arg(m_hfTotal)
            .arg(qRound(m_hfH1Px))
            .arg(qRound(m_hfHcPx))
            .arg(qRound(m_hfTotalPx)),
        [this](const QVariant &v) {
            if (!v.toBool()) {
                fallbackFromNativeHf(QStringLiteral("页码注入失败"));
                return;
            }
            m_renderView->page()->printToPdf(m_pendingOutputPath, m_nativeHfLayout);
        });
}

void PdfExporter::fallbackFromNativeHf(const QString &reason)
{
    qWarning() << "[PdfExport] 原生页眉页脚路径失败，回退 paged.js:" << reason;
    if (qEnvironmentVariableIsSet("HF_NO_FALLBACK")) {
        // 取证逃生门：保留现场成品供内容流分析，不进回退链
        emit exportFinished(true, m_pendingOutputPath);
        return;
    }
    QFile::remove(m_nativeHfTemp);
    m_nativeHfPass = 0;
    PdfPageSpec pagedSpec = m_spec;
    pagedSpec.nativeHf = false;
    m_pagedHtml = buildRenderHtml(m_srcHtml, m_srcDocDir, m_srcKatexCss,
                                  m_srcCustomCss, pagedSpec);
    startPagedExport(m_pagedHtml);
}

void PdfExporter::startPagedExport(const QString &pagedFullHtml)
{
    disconnect(m_renderView, &QWebEngineView::loadFinished, this, nullptr);
    disconnect(m_renderView->page(), &QWebEnginePage::pdfPrintingFinished,
               this, nullptr);
    connect(m_renderView, &QWebEngineView::loadFinished,
            this, &PdfExporter::onLoadFinished);
    connect(m_renderView->page(), &QWebEnginePage::pdfPrintingFinished,
            this, &PdfExporter::onPdfPrintingFinished);
    // 回退重载：复位防重闸与进度基线（nativehf 遍已置位）
    m_printStarted = false;
    m_lastPagedPages = 0;
    m_renderView->setHtml(pagedFullHtml, QUrl("qrc:/web/"));
}
