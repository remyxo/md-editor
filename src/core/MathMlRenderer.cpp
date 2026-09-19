#include "MathMlRenderer.h"

#include <QWebEngineView>
#include <QWebEnginePage>
#include <QWebEngineSettings>
#include <QJsonDocument>
#include <QJsonArray>
#include <QTimer>
#include <QPointer>
#include <QCoreApplication>

MathMlRenderer::MathMlRenderer(QObject *parent)
    : QObject(parent), m_view(new QWebEngineView())
{
    m_view->setVisible(false);
    m_view->settings()->setAttribute(QWebEngineSettings::LocalContentCanAccessFileUrls, true);

    // 通过 qrc 路径加载 katex.min.js（与 PDF 导出路径一致），而非内联脚本。
    connect(m_view, &QWebEngineView::loadFinished, this, &MathMlRenderer::onLoadFinished);
    m_view->setHtml(QStringLiteral(
        "<!DOCTYPE html><html><head><meta charset=\"UTF-8\">"
        "<script src=\"qrc:/web/katex.min.js\"></script></head>"
        "<body></body></html>"));
}

MathMlRenderer::~MathMlRenderer() { delete m_view; }

MathMlRenderer *MathMlRenderer::instance()
{
    static QPointer<MathMlRenderer> s_inst;
    if (s_inst.isNull()) {
        s_inst = new MathMlRenderer();
        // static 局部变量免捕获（lambda 捕获 static 存储对象是 clang 硬错、
        // GCC 宽容——GCC 宽容掩盖 clang/MSVC 差异的既训）
        QObject::connect(qApp, &QCoreApplication::aboutToQuit, qApp, []() {
            delete s_inst;
        });
    }
    return s_inst;
}

void MathMlRenderer::renderAll(const QStringList &sources)
{
    // 单例并发防御（与 MermaidRenderer 同型）：上一轮未结算时本轮降级——
    // 发空结果触发调用方回退 $...$ 源码文本，不互相踩 pending
    if (m_busy) { emit finished({}); return; }
    m_busy = true;
    m_pending = sources;
    if (m_loaded) onLoadFinished(true);
}

QString MathMlRenderer::buildRenderScript(const QStringList &sources)
{
    // IIFE 返回 JSON.stringify(数组)：KaTeX renderToString 为同步调用，返回的字符串
    // 直接作为 runJavaScript 回调的 QVariant 读回。output:'mathml' 使返回值是 MathML
    // 片段（非 HTML），可直接喂给 MathMlConverter::toOMML。throwOnError:false 保证
    // 非法 LaTeX 不抛异常；额外 try/catch 兜底，失败项回退空串由调用侧降级处理。
    return QStringLiteral(
        "(function(){var sources=%1;"
        "return JSON.stringify(sources.map(function(s){"
        "try{return katex.renderToString(s,{output:'mathml',throwOnError:false});}"
        "catch(e){return '';}}));"
        "})()").arg(QString::fromUtf8(QJsonDocument(
            QJsonArray::fromStringList(sources)).toJson(QJsonDocument::Compact)));
}

void MathMlRenderer::onLoadFinished(bool ok)
{
    if (!ok) { m_busy = false; emit finished({}); return; }
    m_loaded = true;
    if (m_pending.isEmpty()) return;

    QString script = buildRenderScript(m_pending);
    int n = m_pending.size();   // clear 前捕获数量，供解析失败回退
    m_pending.clear();
    m_finished = false;
    // 30s 看门狗：渲染挂死时 runJavaScript 回调永不触发、finished 不发——导出
    // 无声失联且离屏视图滞留。超时发空结果（调用侧回退 $...$ 源码文本），
    // 对齐 PdfExporter 的 30s 范式
    QTimer::singleShot(30000, this, [this, n]() {
        if (m_finished) return;
        m_finished = true;
        m_busy = false;
        QVector<QString> fallback;
        for (int i = 0; i < n; ++i) fallback.append(QString());
        emit finished(fallback);
    });
    m_view->page()->runJavaScript(script, [this, n](const QVariant &res) {
        QVector<QString> out;
        QJsonParseError err;
        QJsonDocument doc = QJsonDocument::fromJson(res.toString().toUtf8(), &err);
        if (err.error == QJsonParseError::NoError && doc.isArray()) {
            const QJsonArray arr = doc.array();
            for (const auto &v : arr) out.append(v.toString());
        } else {
            for (int i = 0; i < n; ++i) out.append(QString());
        }
        m_finished = true;   // 看门狗失效（正常结算先到）
        m_busy = false;
        emit finished(out);
    });
}
