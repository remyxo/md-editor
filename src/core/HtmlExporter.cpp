#include "HtmlExporter.h"
#include <QFile>
#include <QFileInfo>
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
    QRegularExpressionMatchIterator it = re.globalMatch(html);
    while (it.hasNext()) {
        auto m = it.next();
        QString src = m.captured(1);
        if (src.startsWith("data:") || src.startsWith("http://") || src.startsWith("https://"))
            continue;
        QString path = src.startsWith('/') ? src : docDir + "/" + src;
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) continue;
        QByteArray bytes = f.readAll();
        f.close();
        QString mime = mimeForExt(QFileInfo(src).suffix());
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
            "blockquote{border-left:4px solid #30363d;color:#8b949e;padding:4px 16px;margin:.8em 0;}"
            "table{border-collapse:collapse;margin:.8em 0;display:block;overflow-x:auto;}"
            "th,td{border:1px solid #30363d;padding:6px 13px;}th{background:#21262d;font-weight:600;}"
            "tr:nth-child(2n){background:#161b22;}img{max-width:100%;}"
            "hr{border:0;border-top:1px solid #30363d;margin:1.5em 0;}"
            "ul,ol{padding-left:1.5em;}li{margin:.3em 0;}"
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
        "blockquote{border-left:4px solid #d0d7de;color:#57606a;padding:4px 16px;margin:.8em 0;}"
        "table{border-collapse:collapse;margin:.8em 0;display:block;overflow-x:auto;}"
        "th,td{border:1px solid #d0d7de;padding:6px 13px;}th{background:#f6f8fa;font-weight:600;}"
        "tr:nth-child(2n){background:#f6f8fa;}img{max-width:100%;}"
        "hr{border:0;border-top:1px solid #d0d7de;margin:1.5em 0;}"
        "ul,ol{padding-left:1.5em;}li{margin:.3em 0;}"
        "[data-type=\"block-math\"]{display:block;text-align:center;margin:1em 0;overflow-x:auto;}"
        "[data-mermaid]{text-align:center;margin:1em 0;}");
}

QString HtmlExporter::renderScript(const QString &theme)
{
    const QString mermaidTheme = (theme == "dark") ? "dark" : "default";
    return QStringLiteral(
        "window.addEventListener('DOMContentLoaded',function(){"
        "if(window.katex){var es=document.querySelectorAll('[data-latex]');"
        "for(var i=0;i<es.length;i++){var e=es[i],l=e.getAttribute('data-latex');if(!l)continue;"
        "try{katex.render(l,e,{throwOnError:false,displayMode:e.getAttribute('data-type')==='block-math'});}"
        "catch(err){e.textContent=l;}}}"
        "if(window.mermaid){mermaid.initialize({startOnLoad:false,securityLevel:'loose',theme:'%1',"
        "themeVariables:{fontSize:'16px'}});"
        "var pres=document.querySelectorAll('pre code.language-mermaid');var seq=0;"
        "for(var j=0;j<pres.length;j++){var code=pres[j];var src=code.textContent;"
        "var pre=code.parentNode;var holder=document.createElement('div');holder.id='mermaid-export-'+(++seq);"
        "pre.parentNode.replaceChild(holder,pre);"
        "try{mermaid.render(holder.id,src).then(function(res){holder.innerHTML=res.svg;});}"
        "catch(err){holder.textContent=src;}}}"
        "if(window.hljs){hljs.highlightAll();}"
        "});").arg(mermaidTheme);
}

void HtmlExporter::exportToHtml(const QString &html, const QString &docDir,
                                const QString &outputPath, const QString &theme)
{
    QString body = inlineImages(html, docDir);

    // KaTeX CSS + 字体 base64
    QString katexCss = inlineCssFonts(readResource(":/web/katex.min.css"), ":/web/fonts");
    // 高亮主题 CSS
    QString hljsCss = readResource(theme == "dark" ? ":/web/hljs-dark.css" : ":/web/hljs-light.css");
    // 自定义 CSS（覆盖默认）
    QSettings settings("md-editor", "md-editor");
    QString customCss = settings.value("editor/customCss").toString();

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
    out.write(full.toUtf8());
    out.close();
    emit exportFinished(true, outputPath);
}
