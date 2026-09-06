#include "PdfExporter.h"
#include "HtmlExporter.h"
#include <QWebEnginePage>
#include <QWebEngineSettings>
#include <QUrl>
#include <QSettings>
#include <QFile>

PdfExporter::PdfExporter(QObject *parent)
    : QObject(parent)
    , m_renderView(new QWebEngineView())
{
    m_renderView->setVisible(false);
    m_renderView->settings()->setAttribute(
        QWebEngineSettings::LocalContentCanAccessFileUrls, true);
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

    // 打印前置入口：mermaid 先渲染（SVG 定型），paged 后跑（分页测量正确）；
    // 双 settle 防挂起（沿用 __renderAllMermaid 的 .then/.catch 均真模式）
    const QString preparePrint = QStringLiteral(
        "window.__preparePrint=function(){"
        "return window.__renderAllMermaid()"
        ".then(function(){if(!window.PagedPolyfill)return true;"
        "return PagedPolyfill.preview().then(function(){return true;})"
        ".catch(function(){return true;});})"
        ".catch(function(){return true;});};");
    const QString pagedScripts = paged
        ? QStringLiteral("<script>window.PagedConfig={auto:false};</script>"
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
        "pre{background:#f4f5f7;padding:10px 14px;border-radius:4px;"
        "font-family:monospace;font-size:9.5pt;line-height:1.35;overflow-x:auto;margin:0.5em 0;}"
        "code{background:#f0f0f0;padding:1px 3px;border-radius:2px;font-size:0.9em;}"
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

    // 读取 KaTeX CSS 并内联（字体 url() 通过 qrc: baseUrl 解析）
    QString katexCss;
    QFile cssFile(":/web/katex.min.css");
    if (cssFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        katexCss = QString::fromUtf8(cssFile.readAll());
        cssFile.close();
    }

    // 构建 PDF 渲染页面：图片 base64 内联 + 内联 KaTeX CSS + JS + 渲染脚本
    QString fullHtml = buildRenderHtml(html, docDir, katexCss, customCss);

    m_pendingOutputPath = outputPath;
    // 设置值为 cm，QPageLayout::Millimeter 需要 mm
    m_pendingLayout = QPageLayout(QPageSize(static_cast<QPageSize::PageSizeId>(paper)),
        static_cast<QPageLayout::Orientation>(orient),
        QMarginsF(ml * 10, mt * 10, mr * 10, mb * 10), QPageLayout::Millimeter);

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
    // 先渲染 mermaid（异步），全部完成后再打印，否则图表空白。
    // .then 与 .catch 都返回 true：即使 mermaid 渲染同步抛异常，Promise
    // 也一定会 settle，保证 printToPdf 必定触发，避免 PDF 导出静默挂起。
    m_renderView->page()->runJavaScript(
        "window.__renderAllMermaid().then(function(){return true;}).catch(function(){return true;})",
        [this](const QVariant &) {
            m_renderView->page()->printToPdf(m_pendingOutputPath, m_pendingLayout);
        });
}

void PdfExporter::onPdfPrintingFinished(const QString &filePath, bool success)
{
    if (success)
        emit exportFinished(true, filePath);
    else
        emit exportError("PDF 生成失败");
}
