#include "TableColumnNormalizer.h"
#include "HtmlExporter.h"

#include <QPointer>
#include <QTimer>
#include <QCoreApplication>
#include <QWebEngineView>
#include <QWebEnginePage>
#include <QWebEngineSettings>

TableColumnNormalizer::TableColumnNormalizer(QObject *parent)
    : QObject(parent), m_view(new QWebEngineView())
{
    m_view->setVisible(false);
    m_view->settings()->setAttribute(QWebEngineSettings::LocalContentCanAccessFileUrls, true);
    connect(m_view, &QWebEngineView::loadFinished, this, &TableColumnNormalizer::onLoadFinished);
}

TableColumnNormalizer::~TableColumnNormalizer() { delete m_view; }

TableColumnNormalizer *TableColumnNormalizer::instance()
{
    static QPointer<TableColumnNormalizer> s_inst;
    if (s_inst.isNull()) {
        s_inst = new TableColumnNormalizer();
        // aboutToQuit 主动销毁：QWebEngineView 须先于 WebEngine 全局回收删除
        //（同 MermaidRenderer；static 局部变量免捕获——lambda 捕获 static 存储
        // 对象是 clang 硬错、GCC 宽容的既训）
        QObject::connect(qApp, &QCoreApplication::aboutToQuit, qApp, []() {
            delete s_inst;   // QPointer 随之置空
        });
    }
    return s_inst;
}

QString TableColumnNormalizer::buildNormalizeScript(int availTwips)
{
    // 与 PDF __fixTableColumns 同构：单一来源脚本在前，随后壳调用。
    // px:true——colgroup 写 px（DocxExporter 解析 width:Npx 生成 tblGrid）、
    // 跳过单元格内联百分比写回（DOCX 列宽走 tcW，无 paged.js 跨页碎片）。
    // 11pt = 单元格与正文统一字号（2026-09-24 定案：四面统一，单元格不设
    // 特例；DOCX 与 PDF 渲染/测量同基准，折行/行高与 PDF 逐行相等）；
    // '0 8px' 对应显式 tblCellMar 120 缇（= PDF th,td 横向 padding 8px，测量与渲染同基准）；
    // avail 单位换算 twips → px（96dpi：1px = 15 缇）。
    // slack:2：边界列（floor 恰=min-content）百分比回算少 0.02px 即折行，与 HTML 导出同款余量
    // IIFE 同步返回 body.innerHTML（非 Promise，runJavaScript 可直接取回字符串；
    // Promise 的 resolve 值会被序列化成空 QVariantMap——MermaidRenderer 既训）
    return HtmlExporter::tableColumnNormalizeScript()
         + QStringLiteral(
               "(function(){"
               "window.__normalizeTableColumns({px:true,avail:%1,slack:2,fontSize:'11pt',"
               "padding:'0 8px',fontFamily:'Noto Sans SC,DejaVu Sans,sans-serif'});"
               "return document.body?document.body.innerHTML:'';"
               "})()").arg(QString::number(availTwips / 15.0, 'f', 1));
}

quint64 TableColumnNormalizer::normalize(const QString &html, const QString &docDir,
                                         int availTwips)
{
    // 单例并发防御：上一轮未结算时返回 0 且不发信号（调用方按返回值就地
    // 降级）——busy 路径若广播空结果，会把共享单例的其他已连接请求一并
    // 误降级（消费到别人的失败），故降级决策上移到调用方
    if (m_busy) return 0;
    m_busy = true;
    const quint64 id = ++m_seq;
    m_pending = html;
    m_availTwips = availTwips;
    m_baseUrl = docDir.isEmpty()
                    ? QUrl()
                    : QUrl::fromLocalFile(docDir + QStringLiteral("/"));
    m_finished = false;
    // 30s 看门狗：loadFinished 永不触发（渲染进程僵死）时导出无声失联——
    // 超时按降级结算（调用侧用原 html 走旧路径），对齐 MermaidRenderer 范式。
    // 携本代 id：代 N 的滞留定时器在代 N+1 进行中误触发时被 id 不匹配挡下
    //（跨代误报根治——旧版共享 m_finished 标志存在窄窗口竞态）
    QTimer::singleShot(30000, this, [this, id]() {
        if (m_finished || id != m_seq) return;
        settle(QString());
    });
    m_view->setHtml(m_pending, m_baseUrl);
    return id;
}

void TableColumnNormalizer::onLoadFinished(bool ok)
{
    if (!ok) { settle(QString()); return; }
    if (m_pending.isEmpty()) return;   // 看门狗结算已清空 m_pending 后迟到的 loadFinished：无任务，丢弃
    m_view->page()->runJavaScript(buildNormalizeScript(m_availTwips),
                                  [this](const QVariant &res) {
                                      // JS 异常 → res 无效 → toString 空 → 降级
                                      settle(res.toString());
                                  });
}

void TableColumnNormalizer::settle(const QString &html)
{
    if (m_finished) return;
    m_finished = true;
    m_busy = false;
    const quint64 id = m_seq;   // id 先于 emit 捕获：busy 已清、新请求或已 ++m_seq，捕获值即本代序号不受影响
    m_pending.clear();
    emit finished(id, html);
}
