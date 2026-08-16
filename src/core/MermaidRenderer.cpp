#include "MermaidRenderer.h"

#include <QWebEngineView>
#include <QWebEnginePage>
#include <QWebEngineSettings>
#include <QUrl>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>

MermaidRenderer::MermaidRenderer(QObject *parent)
    : QObject(parent), m_view(new QWebEngineView())
{
    m_view->setVisible(false);
    m_view->settings()->setAttribute(QWebEngineSettings::LocalContentCanAccessFileUrls, true);

    // 内联 mermaid.min.js 的 helper 页
    QFile f(":/web/mermaid.min.js");
    QString mermaidJs;
    if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        mermaidJs = QString::fromUtf8(f.readAll());
        f.close();
    }
    QString html = QStringLiteral(
        "<!DOCTYPE html><html><head><meta charset=\"UTF-8\"><script>%1</script></head>"
        "<body></body></html>").arg(mermaidJs);

    connect(m_view, &QWebEngineView::loadFinished, this, &MermaidRenderer::onLoadFinished);
    m_view->setHtml(html);
}

MermaidRenderer::~MermaidRenderer() { delete m_view; }

void MermaidRenderer::renderAll(const QStringList &sources)
{
    m_pending = sources;
    if (m_loaded) onLoadFinished(true);
}

void MermaidRenderer::onLoadFinished(bool ok)
{
    if (!ok) { emit finished({}); return; }
    // 页面加载完成即标记已就绪；必须放在 m_pending 判空之前，否则初次
    // setHtml 的 loadFinished 因 pending 为空早退、m_loaded 永不置位，
    // 后续 renderAll 里的 if (m_loaded) onLoadFinished(true) 将永远不触发。
    m_loaded = true;
    if (m_pending.isEmpty()) return;

    // JS：对每个 source 渲染 SVG，canvas 栅格化 PNG，返回 JSON 数组
    QString script = QStringLiteral(
        "(function(){"
        "mermaid.initialize({startOnLoad:false,securityLevel:'loose',theme:'default',"
        "themeVariables:{fontSize:'16px'}});"
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
        "c.getContext('2d').drawImage(img,0,0,c.width,c.height);"
        "res(c.toDataURL('image/png'));URL.revokeObjectURL(url);};"
        "img.onerror=function(){res('');URL.revokeObjectURL(url);};"
        "img.src=url;});}"
        "Promise.all(sources.map(function(src,i){"
        "return mermaid.render('mmd'+i,src).then(function(r){"
        "var dim=svgDims(r.svg);return toPng(r.svg,dim.w,dim.h).then(function(png){"
        "return {svg:r.svg,png:png,w:Math.round(dim.w),h:Math.round(dim.h)};});"
        "}).catch(function(e){return null;});"
        "})).then(function(arr){return JSON.stringify(arr);});"
        "})()").arg(QString::fromUtf8(QJsonDocument(
            QJsonArray::fromStringList(m_pending)).toJson(QJsonDocument::Compact)));

    int n = m_pending.size();   // clear 前捕获数量，供解析失败回退
    m_pending.clear();
    m_view->page()->runJavaScript(script, [this, n](const QVariant &res) {
        QVector<Result> out;
        QString json = res.toString();
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
