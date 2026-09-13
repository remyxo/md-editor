#include "HtmlExporter.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMimeDatabase>
#include <QRegularExpression>
#include <QSettings>

HtmlExporter::HtmlExporter(QObject *parent) : QObject(parent) {}

QString HtmlExporter::readResource(const QString &path) const
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return QString();
    QString s = QString::fromUtf8(f.readAll());
    f.close();
    return s;
}

QString HtmlExporter::mimeForExt(const QString &ext)
{
    const QString e = ext.toLower();
    if (e == "woff2") return "font/woff2";
    if (e == "woff")  return "font/woff";
    if (e == "ttf")   return "font/ttf";
    if (e == "png")   return "image/png";
    if (e == "jpg" || e == "jpeg") return "image/jpeg";
    if (e == "gif")   return "image/gif";
    if (e == "svg")   return "image/svg+xml";
    if (e == "webp")  return "image/webp";
    return "application/octet-stream";
}

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
        QString mime = mimeForExt(QFileInfo(name).suffix());
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
    QString out = html;
    int offset = 0;
    // docDir 围栏基准：未保存文档（docDir 空/不存在）不做本地内联——相对路径
    // 将落到 CWD，等于无围栏
    const QString canonDocDir = QFileInfo(docDir).canonicalFilePath();
    QRegularExpressionMatchIterator it = re.globalMatch(html);
    while (it.hasNext()) {
        auto m = it.next();
        QString src = m.captured(1);
        if (src.startsWith("data:") || src.startsWith("http://") || src.startsWith("https://"))
            continue;
        if (canonDocDir.isEmpty())
            continue;
        QString path = QDir::isAbsolutePath(src) ? src : QDir(docDir).filePath(src);
        // 围栏：解析后（canonical 化解 ../ 与符号链接）必须仍在文档目录内。
        // 曾无任何校验：![](../../.ssh/id_rsa) 经导出把任意本地文件 base64
        // 嵌进产物（HTML 源码可直接提取字节）
        const QString canon = QFileInfo(path).canonicalFilePath();
        if (canon.isEmpty()
            || !canon.startsWith(canonDocDir + QLatin1Char('/')))
            continue;
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) continue;
        QByteArray bytes = f.readAll();
        f.close();
        // 内容级 MIME 校验（不信任扩展名）+ 大小上限：伪装成图片的任意文件
        // 不得内联（与 DocxExporter::readImageFile 同型防线）。svg 无可靠内容
        // magic，按文本前缀判定（<img> 上下文的 svg 不执行脚本，可安全内联）
        if (bytes.isEmpty() || bytes.size() > 50 * 1024 * 1024)
            continue;
        QString mime;
        const QByteArray head = bytes.left(512);
        if (head.trimmed().startsWith("<svg")
            || head.contains("<svg")) {
            mime = QStringLiteral("image/svg+xml");
        } else {
            QMimeDatabase db;
            const QString realMime = db.mimeTypeForData(bytes).name();
            if (realMime != "image/png" && realMime != "image/jpeg"
                && realMime != "image/gif" && realMime != "image/webp")
                continue;
            mime = realMime;
        }
        QString dataUri = "data:" + mime + ";base64," + QString::fromLatin1(bytes.toBase64());
        int start = m.capturedStart(1) + offset;
        int len = m.capturedLength(1);
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
            "body{font-family:\"Noto Sans SC\",\"DejaVu Sans\",sans-serif;font-size:16px;"
            "line-height:1.7;color:#c9d1d9;background:#0d1117;max-width:860px;margin:0 auto;padding:40px 32px;}"
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
            "th,td{border:1px solid #30363d;padding:6px 13px;}th{background:#21262d;font-weight:600;}"
            "tr:nth-child(2n){background:#161b22;}img{max-width:100%;}"
            "hr{border:0;border-top:1px solid #30363d;margin:1.5em 0;}"
            "ul,ol{padding-left:1.5em;}li{margin:.3em 0;}"
        // 子代选择器 > 限定直接 li：嵌套在任务项内的通用列表 li 若也命中 flex
        // 会被当成 flex 容器，其子元素 p 与嵌套 <ol> 排成一行、序号消失（与 editor.css 对齐）
        "ul[data-type=\"taskList\"]{list-style:none;padding-left:0;}"
        "ul[data-type=\"taskList\"] > li{display:flex;align-items:flex-start;gap:8px;}"
        "ul[data-type=\"taskList\"] > li label{margin-top:3px;}"
        "ul[data-type=\"taskList\"] > li > div > p{margin:0;}"
            "[data-type=\"block-math\"]{display:block;text-align:center;margin:1em 0;overflow-x:auto;}"
            "[data-mermaid]{text-align:center;margin:1em 0;}");
    }
    return QStringLiteral(
        "body{font-family:\"Noto Sans SC\",\"DejaVu Sans\",sans-serif;font-size:16px;"
        "line-height:1.7;color:#24292f;background:#ffffff;max-width:860px;margin:0 auto;padding:40px 32px;}"
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
        "th,td{border:1px solid #d0d7de;padding:6px 13px;}th{background:#f6f8fa;font-weight:600;}"
        "tr:nth-child(2n){background:#f6f8fa;}img{max-width:100%;}"
        "hr{border:0;border-top:1px solid #d0d7de;margin:1.5em 0;}"
        "ul,ol{padding-left:1.5em;}li{margin:.3em 0;}"
        // 子代选择器 > 限定直接 li：嵌套在任务项内的通用列表 li 若也命中 flex
        // 会被当成 flex 容器，其子元素 p 与嵌套 <ol> 排成一行、序号消失（与 editor.css 对齐）
        "ul[data-type=\"taskList\"]{list-style:none;padding-left:0;}"
        "ul[data-type=\"taskList\"] > li{display:flex;align-items:flex-start;gap:8px;}"
        "ul[data-type=\"taskList\"] > li label{margin-top:3px;}"
        "ul[data-type=\"taskList\"] > li > div > p{margin:0;}"
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
        "jobs.push(mermaid.render('mermaid-export-'+(++seq),src).then(function(res){holder.innerHTML=res.svg;})"
        ".catch(function(err){holder.textContent=src;}));"
        "})(pres[j]);}"
        "Promise.all(jobs).then(function(){resolve();});"
        "});};").arg(mermaidTheme);
}

QString HtmlExporter::renderScript(const QString &theme)
{
    // 组合式：KaTeX 段 + mermaid helper（只定义 __renderAllMermaid，HTML 路径
    // 在 DOMContentLoaded 内显式自调用，浏览器常驻无需等 Promise）+ hljs 段。
    // 保持与旧版一致的执行时序（KaTeX → Mermaid → highlight）。
    return QStringLiteral(
        "window.addEventListener('DOMContentLoaded',function(){"
        "if(window.katex){var es=document.querySelectorAll('[data-latex]');"
        "for(var i=0;i<es.length;i++){var e=es[i],l=e.getAttribute('data-latex');if(!l)continue;"
        "try{katex.render(l,e,{throwOnError:false,displayMode:e.getAttribute('data-type')==='block-math'});}"
        "catch(err){e.textContent=l;}}}"
        "});")
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

    // KaTeX CSS + 字体 base64
    QString katexCss = inlineCssFonts(readResource(":/web/katex.min.css"), ":/web/fonts");
    // 高亮主题 CSS
    QString hljsCss = readResource(theme == "dark" ? ":/web/hljs-dark.css" : ":/web/hljs-light.css");
    // 自定义 CSS（覆盖默认）。剥 </style> 序列：本地设置携带它会把 <style> 块
    // 提前终结并向产物注入任意标记（自注入面）
    QSettings settings("md-editor", "md-editor");
    QString customCss = settings.value("editor/customCss").toString();
    customCss.remove(QRegularExpression("</\\s*style\\s*>",
                                        QRegularExpression::CaseInsensitiveOption));

    QString full = QStringLiteral(
        "<!DOCTYPE html><html><head><meta charset=\"UTF-8\">"
        "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">"
        "<title>Markdown 导出</title>"
        "<style>%1</style><style>%2</style><style>%3</style>%4"
        "</head><body>%5</body></html>")
        .arg(bodyCss(theme), katexCss, hljsCss,
             customCss.isEmpty() ? QString() : QString("<style>%1</style>").arg(customCss),
             body);

    // 渲染脚本（顺序：KaTeX → highlight → Mermaid → 渲染）
    QString scripts = QStringLiteral("<script>%1</script><script>%2</script><script>%3</script><script>%4</script>")
        .arg(readResource(":/web/katex.min.js"),
             readResource(":/web/highlight.min.js"),
             readResource(":/web/mermaid.min.js"),
             renderScript(theme));
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
