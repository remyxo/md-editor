#include "CdpPrinter.h"

#include <QTcpServer>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QWebSocket>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTimer>
#include <QSaveFile>
#include <QFile>
#include <QDebug>

namespace {
int s_debugPort = 0;
}

bool CdpPrinter::s_methodBroken = false;

void CdpPrinter::enable()
{
    if (s_debugPort > 0) return;
    // 关闭开关（2026-09-19 决策：默认开——提速对所有用户透明；安全敏感/
    // 多用户本机环境可置 MD_EDITOR_NO_CDP_PDF=1 完全不开端口，导出走
    // paged.js 回退链，仅损失性能）
    if (qEnvironmentVariableIsSet("MD_EDITOR_NO_CDP_PDF")) return;
    // inspector server 不接受 0=随机端口（实测报 Invalid port given）——
    // QTcpServer 探空闲端口写入 flags。竞态窗口（探测到 Chromium 绑定之间）
    // 毫秒级；被抢时 Chromium 绑定失败仅日志告警，导出由回退链兜底
    QTcpServer probe;
    if (!probe.listen(QHostAddress::LocalHost)) return;
    s_debugPort = probe.serverPort();
    probe.close();
    QByteArray flags = qgetenv("QTWEBENGINE_CHROMIUM_FLAGS");
    if (!flags.isEmpty()) flags += ' ';
    flags += "--remote-debugging-port=" + QByteArray::number(s_debugPort);
    qputenv("QTWEBENGINE_CHROMIUM_FLAGS", flags);
}

int CdpPrinter::debugPort() { return s_debugPort; }

// CSS 属性转义（模板进 HTML 属性上下文）：仅 " 与 < 需要处理，标题来自
// 导出文件名（用户可命名任意字符）
static QString templateEscape(const QString &s)
{
    QString r = s;
    r.replace('&', QStringLiteral("&amp;"));
    r.replace('<', QStringLiteral("&lt;"));
    r.replace('"', QStringLiteral("&quot;"));
    return r;
}

QString CdpPrinter::headerTemplate(const QString &title)
{
    // 形态对齐现有 @top-center：9pt 灰 #666 居中。模板仅内联样式（不加载外部
    // 资源，这是 Chromium 打印模板的硬约束）
    return QStringLiteral(
               "<div style=\"font-size:9pt;color:#666;width:100%;"
               "text-align:center;\">%1</div>").arg(templateEscape(title));
}

QString CdpPrinter::footerTemplate()
{
    // pageNumber/totalPages 由打印引擎注入为纯文本。右对齐对齐 @bottom-right
    return QStringLiteral(
        "<div style=\"font-size:9pt;color:#666;width:100%;"
        "text-align:right;\">第 <span class=\"pageNumber\"></span> 页 / 共 "
        "<span class=\"totalPages\"></span> 页</div>");
}

CdpPrinter::CdpPrinter(QObject *parent)
    : QObject(parent)
{
}

CdpPrinter::~CdpPrinter() = default;

void CdpPrinter::print(QWebEnginePage *page, const QUrl &targetUrl,
                       const Options &opt, const QString &outputPath)
{
    m_page = page;
    m_targetUrl = targetUrl;
    m_opt = opt;
    m_outputPath = outputPath;
    m_discoverAttempts = 0;

    // 10s 看门狗：HTTP 发现/WebSocket 握手/打印命令任一环节挂死即失败回退
    m_watchdog = new QTimer(this);
    m_watchdog->setSingleShot(true);
    connect(m_watchdog, &QTimer::timeout, this, [this]() {
        if (m_done) return;
        m_done = true;
        emit finished(false, QStringLiteral("CDP 打印超时"));
    });
    m_watchdog->start(10000);

    discoverTarget();
}

void CdpPrinter::discoverTarget()
{
    if (!m_nam) m_nam = new QNetworkAccessManager(this);
    QNetworkRequest req(QUrl(QStringLiteral("http://127.0.0.1:%1/json/list")
                                 .arg(s_debugPort)));
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply *reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (m_done) return;
        const QByteArray body = reply->readAll();
        QJsonParseError e;
        const QJsonDocument doc = QJsonDocument::fromJson(body, &e);
        // 找 url 匹配的 page target（loadFinished 后必已注册；首个 200ms
        // 轮询宽限覆盖 Chromium 注册晚于 loadFinished 的窗口）
        if (e.error == QJsonParseError::NoError && doc.isArray()) {
            for (const auto &v : doc.array()) {
                const QJsonObject t = v.toObject();
                if (t.value("type").toString() != QLatin1String("page")) continue;
                if (QUrl(t.value("url").toString()) != m_targetUrl) continue;
                const QString ws = t.value("webSocketDebuggerUrl").toString();
                if (!ws.isEmpty()) {
                    sendPrintCommand(ws);
                    return;
                }
            }
        }
        if (++m_discoverAttempts < 15) {   // 15 × 200ms = 3s 发现窗口
            QTimer::singleShot(200, this, [this]() { discoverTarget(); });
        } else {
            m_done = true;
            m_watchdog->stop();
            emit finished(false, QStringLiteral("CDP target 未发现"));
        }
    });
}

void CdpPrinter::sendPrintCommand(const QString &wsUrl)
{
    m_ws = new QWebSocket();
    m_ws->setParent(this);
    connect(m_ws, &QWebSocket::connected, this, [this]() {
        // 单位换算 mm→inch（CDI printToPdf 尺寸单位英寸）；横向换向宽高互换；
        // 边距即纸边距（页眉页脚绘制在 top/bottom margin 区内）
        const qreal in = 1.0 / 25.4;
        QJsonObject params;
        params["landscape"] = m_opt.landscape;
        params["paperWidth"] = (m_opt.landscape ? m_opt.paperHmm : m_opt.paperWmm) * in;
        params["paperHeight"] = (m_opt.landscape ? m_opt.paperWmm : m_opt.paperHmm) * in;
        params["marginTop"] = m_opt.marginsMm.top() * in;
        params["marginBottom"] = m_opt.marginsMm.bottom() * in;
        params["marginLeft"] = m_opt.marginsMm.left() * in;
        params["marginRight"] = m_opt.marginsMm.right() * in;
        params["printBackground"] = true;
        params["preferCSSPageSize"] = false;
        const bool hf = m_opt.headerEnabled || m_opt.footerEnabled;
        params["displayHeaderFooter"] = hf;
        if (hf) {
            params["headerTemplate"] = m_opt.headerEnabled
                ? headerTemplate(m_opt.headerTitle)
                : QStringLiteral("<span></span>");   // 空页眉：占位防默认模板
            params["footerTemplate"] = m_opt.footerEnabled
                ? footerTemplate()
                : QStringLiteral("<span></span>");
        }
        QJsonObject cmd;
        cmd["id"] = 1;
        cmd["method"] = "Page.printToPdf";
        cmd["params"] = params;
        m_ws->sendTextMessage(QString::fromUtf8(
            QJsonDocument(cmd).toJson(QJsonDocument::Compact)));
    });
    connect(m_ws, &QWebSocket::textMessageReceived, this, [this](const QString &msg) {
        if (m_done) return;
        QJsonParseError e;
        const QJsonDocument doc = QJsonDocument::fromJson(msg.toUtf8(), &e);
        if (e.error != QJsonParseError::NoError
            || !doc.isObject() || doc.object().value("id").toInt() != 1)
            return;   // 事件通知（id 缺省）忽略
        const QJsonObject o = doc.object();
        if (o.contains("error")) {
            const QString msg = o.value("error").toObject()
                                     .value("message").toString();
            // 构建级缺失（wasn't found）：进程内缓存，后续导出免再探测
            if (msg.contains(QLatin1String("wasn't found")))
                markPdfMethodBroken();
            m_done = true;
            m_watchdog->stop();
            emit finished(false, QStringLiteral("CDP 错误: ") + msg);
            return;
        }
        const QByteArray pdf = QByteArray::fromBase64(
            o.value("result").toObject().value("data").toString().toLatin1());
        if (pdf.isEmpty()) {
            m_done = true;
            m_watchdog->stop();
            emit finished(false, QStringLiteral("CDP 返回空 PDF"));
            return;
        }
        // QSaveFile 原子写（对齐 FileManager 语义：不落半截文件）
        QSaveFile f(m_outputPath);
        if (!f.open(QIODevice::WriteOnly) || f.write(pdf) != pdf.size()
            || !f.commit()) {
            m_done = true;
            m_watchdog->stop();
            emit finished(false, QStringLiteral("PDF 写盘失败"));
            return;
        }
        m_done = true;
        m_watchdog->stop();
        emit finished(true, QString());
    });
    connect(m_ws, &QWebSocket::errorOccurred, this, [this]() {
        if (m_done) return;
        m_done = true;
        m_watchdog->stop();
        emit finished(false, QStringLiteral("CDP WebSocket 错误: ")
                               + m_ws->errorString());
    });
    m_ws->open(QUrl(wsUrl));
}
