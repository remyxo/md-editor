#include "MermaidRenderer.h"

#include <QTimer>
#include <QWebEngineView>
#include <QWebEnginePage>
#include <QWebEngineSettings>
#include <QUrl>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>

MermaidRenderer::MermaidRenderer(QObject *parent)
    : QObject(parent), m_view(new QWebEngineView())
{
    m_view->setVisible(false);
    m_view->settings()->setAttribute(QWebEngineSettings::LocalContentCanAccessFileUrls, true);

    // 通过 qrc 路径加载 mermaid.min.js，而非内联到 <script>。
    // 内联 3.5MB 脚本会导致 setHtml 加载失败（loadFinished ok=false → mermaid 未定义
    // → 渲染结果为空 → DOCX 回退为源码文本）；<script src> 与 PDF 导出路径一致，实测可用。
    connect(m_view, &QWebEngineView::loadFinished, this, &MermaidRenderer::onLoadFinished);
    m_view->setHtml(QStringLiteral(
        "<!DOCTYPE html><html><head><meta charset=\"UTF-8\">"
        "<script src=\"qrc:/web/mermaid.min.js\"></script></head>"
        "<body></body></html>"));
}

MermaidRenderer::~MermaidRenderer() { delete m_view; }

void MermaidRenderer::renderAll(const QStringList &sources)
{
    m_pending = sources;
    if (m_loaded) onLoadFinished(true);
}

QString MermaidRenderer::buildRenderScript(const QStringList &sources)
{
    // IIFE 返回 Promise，仅用于 runJavaScript 等待异步渲染完成——Promise 的
    // resolve 值在 Qt 里会被序列化成空 QVariantMap，无法取回，故真实结果写入
    // window.__mermaidResults，由调用侧在 Promise 结算后再同步 read 回。
    // htmlLabels:false 关闭 foreignObject：否则 SVG 含 foreignObject 时 canvas
    // 被污染，toDataURL 抛 SecurityError，PNG 兜底为空。
    return QStringLiteral(
        "(function(){"
        "mermaid.initialize({startOnLoad:false,securityLevel:'strict',theme:'default',"
        "htmlLabels:false,flowchart:{htmlLabels:false},themeVariables:{fontSize:'16px'}});"
        "var sources=%1;"
        "function svgDims(svg){var d=document.createElement('div');d.style.cssText='position:absolute;left:-9999px';"
        "d.innerHTML=svg;document.body.appendChild(d);var s=d.querySelector('svg');"
        "var w=s&&s.viewBox&&s.viewBox.baseVal? s.viewBox.baseVal.width : (s? s.getBoundingClientRect().width : 0);"
        "var h=s&&s.viewBox&&s.viewBox.baseVal? s.viewBox.baseVal.height : (s? s.getBoundingClientRect().height : 0);"
        "document.body.removeChild(d);return {w:w||400,h:h||200};}"
        "function toPng(svg,w,h){return new Promise(function(res){"
        "var svgBlob=new Blob([svg],{type:'image/svg+xml;charset=utf-8'});"
        "var url=URL.createObjectURL(svgBlob);var img=new Image();"
        "img.onload=function(){var c=document.createElement('canvas');c.width=Math.ceil(w*2);c.height=Math.ceil(h*2);"
        "try{c.getContext('2d').drawImage(img,0,0,c.width,c.height);res(c.toDataURL('image/png'));}"
        "catch(e){res('');}URL.revokeObjectURL(url);};"
        "img.onerror=function(){res('');URL.revokeObjectURL(url);};"
        "img.src=url;});}"
        "return Promise.all(sources.map(function(src,i){"
        "return mermaid.render('mmd'+i,src).then(function(r){"
        "var dim=svgDims(r.svg);return toPng(r.svg,dim.w,dim.h).then(function(png){"
        "return {svg:r.svg,png:png,w:Math.round(dim.w),h:Math.round(dim.h)};});"
        "}).catch(function(e){return null;});"
        "})).then(function(arr){window.__mermaidResults=JSON.stringify(arr);});"
        "})()").arg(QString::fromUtf8(QJsonDocument(
            QJsonArray::fromStringList(sources)).toJson(QJsonDocument::Compact)));
}

void MermaidRenderer::onLoadFinished(bool ok)
{
    if (!ok) { emit finished({}); return; }
    // 页面加载完成即标记已就绪；必须放在 m_pending 判空之前，否则初次
    // setHtml 的 loadFinished 因 pending 为空早退、m_loaded 永不置位，
    // 后续 renderAll 里的 if (m_loaded) onLoadFinished(true) 将永远不触发。
    m_loaded = true;
    if (m_pending.isEmpty()) return;

    // JS：对每个 source 渲染 SVG，canvas 栅格化 PNG。渲染脚本返回 Promise，Qt 会等
    // 它结算（渲染完成）。但 Promise 的 resolve 值被序列化成空 QVariantMap，且设置
    // window.__mermaidResults 的链式 .then 微任务晚于本回调执行，故结算后轮询读取。
    QString script = buildRenderScript(m_pending);

    int n = m_pending.size();   // clear 前捕获数量，供解析失败回退
    m_pending.clear();
    m_view->page()->runJavaScript(script, [this, n](const QVariant &) {
        pollResults(n, 0);
    });
}

void MermaidRenderer::pollResults(int n, int attempt)
{
    m_view->page()->runJavaScript("window.__mermaidResults", [this, n, attempt](const QVariant &res) {
        QString json = res.toString();
        if (json.isEmpty() && attempt < 20) {
            // 结果尚未写入（链式 .then 微任务未执行），延迟重试；20×20ms=400ms 兜底
            QTimer::singleShot(20, this, [this, n, attempt]() { pollResults(n, attempt + 1); });
            return;
        }
        QVector<Result> out;
        // 若返回的是 JSON 编码的字符串（双重序列化），先解一层。
        // QJsonDocument 无 isString()，字符串文档经 toVariant() 得到 QString。
        if (json.startsWith('"')) {
            QJsonParseError strErr;
            QJsonDocument strDoc = QJsonDocument::fromJson(json.toUtf8(), &strErr);
            if (strErr.error == QJsonParseError::NoError)
                json = strDoc.toVariant().toString();
        }
        QJsonParseError err;
        QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &err);
        if (err.error == QJsonParseError::NoError && doc.isArray()) {
            const QJsonArray arr = doc.array();
            for (const QJsonValue &v : arr) {
                if (!v.isObject()) { out.append(Result()); continue; }
                QJsonObject o = v.toObject();
                Result r;
                r.svg = o.value("svg").toString();
                QString png = o.value("png").toString();
                if (png.startsWith("data:image/png;base64,"))
                    r.png = QByteArray::fromBase64(png.mid(22).toLatin1());
                r.cxEmu = o.value("w").toInt() * 9525;
                r.cyEmu = o.value("h").toInt() * 9525;
                out.append(r);
            }
        } else {
            for (int i = 0; i < n; ++i) out.append(Result());
        }
        emit finished(out);
    });
}
