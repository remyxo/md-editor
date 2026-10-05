#include "SelfTestRunner.h"
#include "Application.h"
#include "ui/MainWindow.h"
#include "ui/EditorTab.h"
#include "ui/ExportCoordinator.h"
#include "ui/ExportHtmlAsync.h"
#include "core/DocxExporter.h"
#include "core/ExportCommon.h"
#include "core/HtmlExporter.h"
#include "core/JsEscape.h"
#include "core/MarkdownHandler.h"
#include "core/PdfExporter.h"
#include "core/TableColumnNormalizer.h"
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>
#include <QVariant>
#include <QVariantMap>
#include <QWebEnginePage>
#include <functional>
#include <memory>
#include <utility>

namespace {

// 轮询 JS 表达式直到为 true / 超时（编辑器就绪、模式翻转等异步等待）。
// 回调可能迟到于循环退出：loop 是栈对象，waitJsTrue 返回后析构，迟到的
// 回调再调 loop.quit() 是 use-after-free（quit 只对「存活且未运行」的循环
// 幂等，对已析构的循环是悬垂解引用——实测就绪轮询页面加载繁忙期多趟
// 回调突发时必崩）。alive 守卫：函数返回前置 false，迟到回调直接返回。
bool waitJsTrue(MainWindow &w, const QString &expr, int timeoutMs = 30000)
{
    QElapsedTimer t;
    t.start();
    QEventLoop loop;
    const std::shared_ptr<bool> alive = std::make_shared<bool>(true);
    bool ok = false;
    QTimer poller;
    QObject::connect(&poller, &QTimer::timeout, &loop, [&]() {
        w.selfTestCurrentTab()->webView()->page()->runJavaScript(
            expr, [&, alive](const QVariant &v) {
                if (!*alive) return;
                if (v.toBool()) { ok = true; loop.quit(); }
                else if (t.elapsed() > timeoutMs) loop.quit();
            });
    });
    poller.start(50);
    loop.exec();
    *alive = false;
    return ok;
}

// 取 JS 字符串表达式结果（同步等待单次回调）。渲染进程崩溃/繁忙时回调可能
// 永不到达——timeoutMs 兜底超时返回空串并告警；alive 守卫同 waitJsTrue，
// 超时退出后迟到回调不触碰已析构的 loop（got 区分「超时」与「合法空串」）
QString evalJsString(MainWindow &w, const QString &expr, int timeoutMs = 30000)
{
    QString out;
    bool got = false;
    QEventLoop loop;
    const std::shared_ptr<bool> alive = std::make_shared<bool>(true);
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &loop, [&]() { loop.quit(); });
    timeout.start(timeoutMs);
    w.selfTestCurrentTab()->webView()->page()->runJavaScript(
        expr, [&, alive](const QVariant &v) {
            if (!*alive) return;
            out = v.toString();
            got = true;
            loop.quit();
        });
    loop.exec();
    *alive = false;
    if (!got)
        qWarning("evalJsString %dms 未返回（渲染进程疑似崩溃）: %s",
                 timeoutMs, qPrintable(expr));
    return out;
}

// 取 JS 表达式的 QVariant 原型结果（同步等待单次回调）。与 evalJsString 同
// 骨架，但保留原始类型：exitSourceModeOrGetMd 返回 bool（快速路径已完成）
// 或 string（待转换 MD），必须按 userType 判型（MainWindow::onToggleSourceMode
// 同款），toString 会把 JS true 变 "true" 无法区分两条路径。
QVariant evalJsValue(MainWindow &w, const QString &expr, int timeoutMs = 30000)
{
    QVariant out;
    bool got = false;
    QEventLoop loop;
    const std::shared_ptr<bool> alive = std::make_shared<bool>(true);
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &loop, [&]() { loop.quit(); });
    timeout.start(timeoutMs);
    w.selfTestCurrentTab()->webView()->page()->runJavaScript(
        expr, [&, alive](const QVariant &v) {
            if (!*alive) return;
            out = v;
            got = true;
            loop.quit();
        });
    loop.exec();
    *alive = false;
    if (!got)
        qWarning("evalJsValue %dms 未返回（渲染进程疑似崩溃）: %s",
                 timeoutMs, qPrintable(expr));
    return out;
}

QString mdOfCurrentDoc(MainWindow &w, int timeoutMs = 30000)
{
    // 与 writeTabToFile 同源：WYSIWYG 取 getSaveSnapshot().html 经 fromHtml
    QString out;
    bool got = false;
    QEventLoop loop;
    const std::shared_ptr<bool> alive = std::make_shared<bool>(true);
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &loop, [&]() { loop.quit(); });
    timeout.start(timeoutMs);
    w.selfTestCurrentTab()->webView()->page()->runJavaScript(
        QStringLiteral("window.editorApi.getSaveSnapshot()"),
        [&, alive](const QVariant &v) {
            if (!*alive) return;
            out = v.toMap().value("html").toString();
            got = true;
            loop.quit();
        });
    loop.exec();
    *alive = false;
    if (!got)
        qWarning("mdOfCurrentDoc %dms 未返回（渲染进程疑似崩溃）", timeoutMs);
    return MarkdownHandler::fromHtml(out);
}

// 编辑器就绪等待（roundtrip / export 共用）：editorApi 存在 + 不在加载锁内 +
// **文档已有实际文字**。后者不可省：文件内容由 TabWidget 在 pageLoadFinished
// 后异步经 setContent 推入，此前编辑器是初始空文档（getHTML="<p></p>"），
// editorApi/_loading 两个条件在空档期即满足——首轮切换会抓到空文档，
// fromHtml("<p></p>")="&nbsp;"，退出重建把全文冲成注入段（实测踩坑）。
// 空文档判据用 doc 文字而非 getHTML 长度：不依赖序列化形状。
bool waitEditorDocReady(MainWindow &w)
{
    return waitJsTrue(w, QStringLiteral(
        "!!(window.editorApi && window.editorApi.editor"
        " && !window.editorApi._loading"
        " && window.editorApi.editor.state.doc.textContent"
        ".replace(/\\s/g,'').length > 0)"));
}

// 事件循环响应探针（fix I-2：切源码模式退出侧冻结护栏；与
// tests/test_conversionqueue.cpp 的 EventLoopPing 同构——selftest 非 QtTest
// 工程，测试文件内类型不可复用，此处独立实现，且无 Q_OBJECT：invokeMethod
// functor 版不需要元对象，免 moc）。queued 自调用连发，测「投递 → 派发」
// 最大时延——GUI 线程被长任务占死时 queued 调用只能排队，最大时延 ≈ 占死
// 时长。arm/stop 圈定测量窗口（退出转换期间），stop 后 bounce 不再续投
class FreezeProbe : public QObject {
public:
    qint64 maxLatencyMs = 0;
    int dispatched = 0;

    void arm()
    {
        m_running = true;
        maxLatencyMs = 0;
        dispatched = 0;
        m_sent.restart();
        QMetaObject::invokeMethod(this, [this]() { bounce(); }, Qt::QueuedConnection);
    }

    void stop() { m_running = false; }

private:
    void bounce()
    {
        if (!m_running) return;
        maxLatencyMs = qMax<qint64>(maxLatencyMs, m_sent.elapsed());
        ++dispatched;
        m_sent.restart();
        QMetaObject::invokeMethod(this, [this]() { bounce(); }, Qt::QueuedConnection);
    }

    QElapsedTimer m_sent;
    bool m_running = false;
};

// —— export 子命令（无头三格式导出 + 计时）——

// 单格式导出结果：finished=收到终态信号；timedOut=超时兜底触发
struct ExportOutcome {
    bool finished = false;
    bool ok = false;
    bool timedOut = false;
    QString error;
    qint64 ms = 0;
};

// 导出完成回执（launch/Coordinator 内部把导出器的 exportFinished/exportError
// 翻译成它）
using ExportDone = std::function<void(bool ok, const QString &error)>;
// 启动导出器（收到 ExportHtmlAsync 的 HTML 后调用；须在收到终态时调 done）
using ExportLaunch = std::function<void(const QString &html, const ExportDone &done)>;

// 通用终态等待框架（W4-4 自 runExport 的超时骨架泛化；HTML/DOCX 经 runExport
// 复用，PDF 直接消费）：start 在当前轮次发起异步导出，终态时须调 done（恰
// 一次）。渲染进程挂死/崩溃时终态永不到达——timeoutMs 兜底返回而非永挂。
// alive 守卫同 waitJsTrue（[&, alive] 范式：按值捕获副本）：start 把它**按
// 值**捕获进在途异步回调——shared_ptr 副本与帧上原件同控制块，runAwait 返回
// （超时或终态）后 *alive=false 对副本可见，迟到回调据此不再触碰已析构 loop /
// 不再启动导出。**不得**按引用捕获：alive 形参锚定本函数栈帧，超时返回后帧
// 销毁，迟到回调经悬垂引用读死帧（UB，W4-4 评审 I-1）；
// 首个终态生效（o.finished 防其后第二信号覆盖结果）。计时从进入本函数起
//（含内容获取 + 高亮 + 导出器全程）
ExportOutcome runAwait(int timeoutMs,
                       const std::function<void(const std::shared_ptr<bool> &alive,
                                                const ExportDone &done)> &start)
{
    ExportOutcome o;
    QElapsedTimer t;
    t.start();
    QEventLoop loop;
    const std::shared_ptr<bool> alive = std::make_shared<bool>(true);
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &loop, [&, alive]() {
        if (!*alive || o.finished) return;
        o.timedOut = true;
        o.error = QStringLiteral("导出超时（%1ms）").arg(timeoutMs);
        loop.quit();
    });
    timeout.start(timeoutMs);
    start(alive, [&loop, &o, alive](bool ok, const QString &err) {
        if (!*alive || o.finished) return;
        o.finished = true;
        o.ok = ok;
        o.error = err;
        loop.quit();
    });
    loop.exec();
    *alive = false;
    o.ms = t.elapsed();
    return o;
}

// 单格式无头导出（HTML/DOCX 用；PDF 自 W4-4 起改走 ExportCoordinator，见
// runExportSuite PDF 分支注释）：ExportHtmlAsync::get（含高亮步，本身异步）→
// launch 启动导出器 → runAwait 等终态。返回后调用方负责 delete 导出器
ExportOutcome runExport(EditorTab *tab, ExportHtmlAsync::Mode mode,
                        int timeoutMs, const ExportLaunch &launch)
{
    return runAwait(timeoutMs, [&](const std::shared_ptr<bool> &alive,
                                   const ExportDone &done) {
        // done/alive/launch 全部**按值**捕获：三者的按引用捕获都会被迟到的
        // get 闭包读取——done 是 runAwait 传入的临时 std::function（本 start
        // 表达式结束后即析构，HtmlExporter 同步 emit 终态时调用悬垂 function
        // 曾实测 SIGSEGV）；alive 是 runAwait 栈帧形参的引用（超时返回后帧
        // 销毁）；launch 绑定调用方实参（runExport 返回后未必存活）。拷贝
        // 发生在本表达式内（临时存活期），随 get 闭包按值传递覆盖整个异步期。
        // alive 副本与帧上原件同控制块，runAwait 复位 *alive 对副本可见，
        // 守卫语义不变（W4-4 评审 I-1）
        ExportHtmlAsync::get(tab, [alive, launch, done](const QString &html) {
            if (!*alive) return;   // runAwait 已返回（超时）：不再启动导出器
            launch(html, done);
        }, mode);
    });
}

// 单格式结果入账：计时、产物路径、失败/超时/缺文件原因汇总。
// 成功判定 = exportFinished(ok) **且产物落盘**（golden 工具链依赖文件存在，
// 残留旧文件已在导出前清除，此处存在即本次产出）
void recordOutcome(const char *fmt, const QString &path, const ExportOutcome &o,
                   QJsonObject *timings, QJsonObject *files, QStringList *errors)
{
    const QString f = QString::fromLatin1(fmt);
    timings->insert(f, double(o.ms));
    files->insert(f, path);
    if (o.timedOut) {
        errors->append(f + QStringLiteral(": ") + o.error);
    } else if (!o.ok) {
        errors->append(f + QStringLiteral(": ")
                       + (o.error.isEmpty() ? QStringLiteral("exportFinished(false)")
                                           : o.error));
    } else if (!QFileInfo::exists(path)) {
        errors->append(f + QStringLiteral(": 成功信号但产物缺失 ") + path);
    }
}

// 确定性导出设置固定（export / perf 共用）：把导出器从 QSettings 读的键全部
// 固定为**代码内默认值**（镜像自 PdfExporter.cpp:1199-1220、DocxExporter.cpp:
// 1761-1783、ExportCommon.cpp:114-120、HtmlExporter.cpp:360 读到的默认值：
// 四边 margin 1.8、orientation=Portrait / paperSize=A4（枚举 static_cast 与
// 导出器默认参数同源）、pdf/docx 页眉页脚开、customCss 空）——消除用户机器
// QSettings 状态差异，golden 特征才可比、perf 导出计时才可跨机器对齐（PDF
// 页眉页脚开关直接决定走分页路径与否，计时不固定即失真）。析构恢复用户
// 原值（原本未写过的键 remove 回到默认态），selftest 不留配置副作用
class PinnedExportSettings {
public:
    PinnedExportSettings()
    {
        const std::pair<const char *, QVariant> pinned[] = {
            { "export/marginTop", 1.8 },
            { "export/marginBottom", 1.8 },
            { "export/marginLeft", 1.8 },
            { "export/marginRight", 1.8 },
            { "export/orientation", static_cast<int>(QPageLayout::Portrait) },
            { "export/paperSize", static_cast<int>(QPageSize::A4) },
            { "export/pdfHeaderEnabled", true },
            { "export/pdfFooterEnabled", true },
            { "export/docxHeaderEnabled", true },
            { "export/docxFooterEnabled", true },
            { "editor/customCss", QString() },
        };
        QSettings settings;
        for (const auto &kv : pinned) {
            const QString key = QString::fromLatin1(kv.first);
            m_saved.append({ key, settings.value(key) });   // invalid = 原本未写
            settings.setValue(key, kv.second);
        }
    }
    ~PinnedExportSettings()
    {
        QSettings rs;
        for (const auto &kv : m_saved) {
            if (kv.second.isValid()) rs.setValue(kv.first, kv.second);
            else rs.remove(kv.first);
        }
    }
private:
    QVector<QPair<QString, QVariant>> m_saved;
};

// 三格式导出套件结果（runExportSuite 产出）
struct ExportSuiteOut {
    QJsonObject timings;        // {"html":ms,"docx":ms,"pdf":ms}
    QJsonObject files;          // 产物路径
    bool pagedFallback = false; // PDF：paged.js 失败回退原生分页
    bool docxNormalized = false;// DOCX：经 TableColumnNormalizer 实际归一（GUI 同路径）
    QStringList errors;         // 空 = 三格式全部成功且产物落盘
};

// 三格式无头导出套件（export / perf 共用）：清上次残留 → HTML/DOCX/PDF 逐
// 格式导出并计时（超时上限 60s/120s/180s——PDF 大文档分页以十秒计）。
// 产物写 artifactsDir：export 用用户指定目录供 golden 对账；perf 只取计时
// 与成功判定，传临时目录，产物不留盘
ExportSuiteOut runExportSuite(EditorTab *tab, const QString &file,
                              const QString &docDir, const QString &artifactsDir)
{
    QDir().mkpath(artifactsDir);
    const QString base = QFileInfo(file).completeBaseName();
    const QString htmlPath = QDir(artifactsDir).filePath(base + ".html");
    const QString docxPath = QDir(artifactsDir).filePath(base + ".docx");
    const QString pdfPath = QDir(artifactsDir).filePath(base + ".pdf");
    // 清掉上次残留产物：成功判定含「产物落盘」，残留会让失败跑被误判 ok
    for (const QString &p : { htmlPath, docxPath, pdfPath })
        QFile::remove(p);

    ExportSuiteOut out;
    // HTML（Plain：getContent 原文；主题固定 light——不随用户/系统主题漂移）
    {
        auto *ex = new HtmlExporter();
        const ExportOutcome o = runExport(
            tab, ExportHtmlAsync::Mode::Plain, 60000,
            [ex, &docDir, &htmlPath](const QString &html, const ExportDone &done) {
                QObject::connect(ex, &HtmlExporter::exportFinished, ex,
                    [done](bool ok, const QString &) { done(ok, QString()); });
                QObject::connect(ex, &HtmlExporter::exportError, ex,
                    [done](const QString &err) { done(false, err); });
                ex->exportToHtml(html, docDir, htmlPath, QStringLiteral("light"));
            });
        delete ex;   // 导出器持有隐藏渲染视图，结算后立即回收（滞留即泄漏）
        recordOutcome("html", htmlPath, o, &out.timings, &out.files, &out.errors);
    }
    // DOCX（Docx 模式：live colgroup 注入供 tblGrid + 代码块高亮）。与 GUI 路径
    // （MainWindow::onExportDocx）同构：HTML 含 <table> 先经 TableColumnNormalizer
    // 按 DOCX 基准（页内容宽 twips）重测列宽再入导出器，无表直通（免页面
    // 加载）；归一失败（空串）降级原 html（live colgroup 缩放路径仍在
    // DocxExporter 内）——否则 golden 的 DOCX 不是用户会得到的 DOCX（列宽
    // 不同），perf 的 docx_ms 也漏 normalizer 成本
    {
        auto *ex = new DocxExporter();
        const ExportOutcome o = runExport(
            tab, ExportHtmlAsync::Mode::Docx, 120000,
            [ex, &docDir, &docxPath, &out](const QString &html, const ExportDone &done) {
                QObject::connect(ex, &DocxExporter::exportFinished, ex,
                    [done](bool ok, const QString &) { done(ok, QString()); });
                QObject::connect(ex, &DocxExporter::exportError, ex,
                    [done](const QString &err) { done(false, err); });
                if (!html.contains(QLatin1String("<table"))) {
                    ex->exportToDocx(html, docDir, docxPath);
                    return;
                }
                auto *norm = TableColumnNormalizer::instance();
                // busy（上一轮未结算）返回 0 且不发信号：就地降级直出原 html
                //（GUI 同款）。normalizer 连接以导出器为 context：runExport 超时
                // 返回后 delete ex 即断连，单例迟到的 finished 不触碰已析构的
                // runExport 栈帧（UAF 守卫）；单例挂死由 runExport 的 120s 超时
                // 兜底（其内部另有 30s 看门狗）。归一等待计入 docx 计时（与
                // GUI 用户体感同口径）
                const quint64 normId = norm->normalize(
                    html, docDir, ExportCommon::pageContentWidthTwips());
                if (normId == 0) {
                    qWarning("selftest docx: normalizer 忙，降级原 html");
                    ex->exportToDocx(html, docDir, docxPath);
                    return;
                }
                auto conn = std::make_shared<QMetaObject::Connection>();
                *conn = QObject::connect(
                    norm, &TableColumnNormalizer::finished, ex,
                    [conn, ex, html, docDir, docxPath, normId, &out, done](
                            quint64 requestId, const QString &normalized) {
                        // 他轮请求的结果：不是本单，不消费不断连
                        if (requestId != normId) return;
                        QObject::disconnect(*conn);   // 单发：只消费一次结果
                        if (normalized.isEmpty())
                            qWarning("selftest docx: 归一失败，降级原 html");
                        else
                            out.docxNormalized = true;
                        ex->exportToDocx(normalized.isEmpty() ? html : normalized,
                                         docDir, docxPath);
                    });
            });
        delete ex;
        recordOutcome("docx", docxPath, o, &out.timings, &out.files, &out.errors);
    }
    // PDF（Pdf 模式：高亮；页眉页脚开 → 分页路径，大文档以十秒计，超时上限
    // 420s——W3-3 B 起导出器轮询预算按规模自适应（max(30s, 估页数×1.5s+10s)，
    // 185 页档 ≈288s），工装上限须覆盖导出器自身预算 + 回退页打印，否则大档
    // 验收在导出器正常回退前就被工装截杀）。W4-4 起本分支经 ExportCoordinator::
    // exportPdf——与 GUI onExportPdf 同一编排源（内容获取 + 源码 toHtml + 高亮
    // + 导出器启动/回收全链同构），差异仅在呈现钩子为空（无头无声）。
    // fallbackUsed：paged.js 失败/超时回退原生分页仍产出 PDF（无页眉页脚）——
    // 不算失败，只记 paged_fallback 供 golden 判读；由终态钩子带回（原实现
    // 超时兜底返回后直读 exporter——超时场景无终态时该标志不再记账，语义
    // 窄化在预期内：超时本就记入 errors，golden 判读以 errors 优先）
    {
        auto fallbackBox = std::make_shared<bool>(false);
        const ExportOutcome o = runAwait(420000,
            [&tab, &pdfPath, &fallbackBox](const std::shared_ptr<bool> &alive,
                                           const ExportDone &done) {
                ExportCoordinator::PdfHooks hooks;
                // done/alive/fallbackBox **按值**捕获（同 runExport 注释）：
                // hooks 连同其成员 std::function 被 Coordinator 拷进内容回调
                // 与终态 lambda，异步期调用——按引用捕获即挂悬垂临时/死帧
                //（超时返回后迟到终态经悬垂 alive 读 runAwait 已销毁栈帧，
                // W4-4 评审 I-1）。fallbackBox 副本与下方读数原件同控制块，
                // 迟到钩子写入对主流程可见
                hooks.finished = [alive, fallbackBox, done](
                        bool ok, const QString &, qint64, bool fb) {
                    if (!*alive) return;   // 迟到终态：runAwait 已结算/返回
                    *fallbackBox = fb;
                    done(ok, QString());
                };
                hooks.failed = [alive, done](const QString &err) {
                    if (!*alive) return;
                    done(false, err);
                };
                ExportCoordinator::exportPdf(tab, pdfPath, nullptr, hooks, done);
            });
        out.pagedFallback = *fallbackBox;
        recordOutcome("pdf", pdfPath, o, &out.timings, &out.files, &out.errors);
    }
    return out;
}

}   // namespace

int SelfTestRunner::run(Application &app)
{
    QCommandLineParser parser;
    parser.setOptionsAfterPositionalArgumentsMode(QCommandLineParser::ParseAsOptions);
    parser.addPositionalArgument("subcommand", "roundtrip | export | perf");
    QCommandLineOption fileOpt("file", "输入 md 文件", "path");
    QCommandLineOption roundsOpt("rounds", "切换轮数", "n", "3");
    QCommandLineOption outOpt("out", "结果 JSON 输出路径", "path");
    QCommandLineOption outdirOpt("outdir", "导出目录（export）", "path");
    parser.addOptions({ fileOpt, roundsOpt, outOpt, outdirOpt });
    parser.parse(app.arguments());   // --selftest 非已知选项：parse 不中断，位置参数仍完整
    const QStringList pos = parser.positionalArguments();
    const QString sub = pos.isEmpty() ? QString() : pos.first();

    if (sub == "roundtrip") {
        const QString file = parser.value(fileOpt);
        const int rounds = parser.value(roundsOpt).toInt();
        // 读入 md_0（UTF-8 + CRLF 归一，与 FileManager::readFile 口径一致）
        QFile f(file);
        if (!f.open(QIODevice::ReadOnly)) { qWarning("读不到 %s", qPrintable(file)); return 1; }
        const QString md0 = QString::fromUtf8(f.readAll()).replace("\r\n", "\n");

        QElapsedTimer loadTimer;
        loadTimer.start();
        MainWindow w(file);          // 构造即加载（不 show——无头）
        if (!waitEditorDocReady(w)) {
            qWarning("编辑器 30s 未就绪"); return 1;
        }
        const qint64 loadMs = loadTimer.elapsed();

        // 冻结探针（fix I-2）区分度基线：对输入文档同步跑一次 toHtml——即
        // 「慢路径还原为同步」（改前形态）时退出窗口的冻结量级。仅当基线
        // 足够长（≥200ms；短文档同步转换本就不可感知，断言无区分度且易受
        // 调度噪声误报）才对探针设阈值断言，否则只记录数据
        QElapsedTimer syncT;
        syncT.start();
        const QString syncProbeHtml = MarkdownHandler::toHtml(md0);
        Q_UNUSED(syncProbeHtml);
        const qint64 exitSyncMs = syncT.elapsed();
        const bool freezeAssert = exitSyncMs >= 200;
        const qint64 freezeThreshold = qMax<qint64>(150, exitSyncMs / 4);
        FreezeProbe freezeProbe;
        qint64 freezeMaxMs = 0;
        int freezeDispatched = 0;

        QJsonArray roundArr;
        QString prevMd;
        int convergedAt = -1;
        bool injected = false, sawInjection = false;
        for (int i = 1; i <= rounds; ++i) {
            w.selfTestToggleSourceMode();                       // → 源码
            if (!waitJsTrue(w, QStringLiteral("window.editorApi.isSourceMode"))) {
                qWarning("第 %d 轮进入源码超时", i); return 1;
            }
            if (!injected) {                                    // 第 1 轮注入一次编辑
                evalJsString(w, QStringLiteral(
                    "(function(){var sv=document.getElementById('source-view');"
                    "sv.value += '\\n\\nselftest 注入段。\\n';"
                    "sv.dispatchEvent(new Event('input'));return 'ok';})()"));
                injected = true;
            }
            freezeProbe.arm();                                  // 测量窗口=退出转换期间
            w.selfTestToggleSourceMode();                       // → WYSIWYG（慢路径）
            if (!waitJsTrue(w, QStringLiteral("!window.editorApi.isSourceMode"))) {
                qWarning("第 %d 轮退出源码超时", i); return 1;
            }
            freezeProbe.stop();
            freezeMaxMs = qMax<qint64>(freezeMaxMs, freezeProbe.maxLatencyMs);
            freezeDispatched += freezeProbe.dispatched;
            const QString md = mdOfCurrentDoc(w);
            roundArr.append(md.size());
            // 注意用 QStringLiteral 而非 QLatin1String：QLatin1String 把字面量
            // 字节按 Latin-1 解码，中文（UTF-8 多字节）永不匹配——注入存活断言
            // 形同虚设（实测踩坑：注入在文档里而 sawInjection 恒 false）
            if (md.contains(QStringLiteral("selftest 注入段"))) sawInjection = true;
            if (!prevMd.isNull() && md == prevMd && convergedAt < 0) convergedAt = i - 1;
            prevMd = md;
        }

        // 判定：收敛 + 注入内容存活 + 无占位符泄漏 + 冻结探针
        const bool converged = convergedAt >= 0;
        const bool leaked = prevMd.contains(QRegularExpression("ZZ[A-Z]+[0-9]+"));
        // 冻结判定（fix I-2）：退出转换期间 posted-event 最大派发时延须远小于
        // 同步基线（还原同步即红：toHtml 占死窗口全额计入探针时延）；派发
        // 次数下限证明探针在窗口内确实存活轮转
        const bool freezePass = !freezeAssert
            || (freezeMaxMs <= freezeThreshold && freezeDispatched >= 3);
        const bool ok = converged && sawInjection && !leaked && freezePass;

        QStringList rtErrors;
        if (!converged) rtErrors << QStringLiteral("未在轮次内收敛");
        if (freezeAssert && !freezePass)
            rtErrors << QStringLiteral(
                            "冻结探针超阈: max=%1ms threshold=%2ms dispatched=%3（退出侧同步冻结回归？）")
                            .arg(freezeMaxMs).arg(freezeThreshold).arg(freezeDispatched);

        QJsonObject result{
            { "ok", ok }, { "rounds", rounds }, { "converged_at", convergedAt },
            { "load_ms", loadMs },
            { "source_md_size", md0.size() },   // 输入 md_0 字符量（原死变量改为入账）
            { "final_md_size", prevMd.size() },
            { "md_sizes", roundArr },
            { "freeze", QJsonObject{
                { "sync_to_html_ms", exitSyncMs },
                { "probe_max_latency_ms", freezeMaxMs },
                { "threshold_ms", freezeThreshold },
                { "dispatched", freezeDispatched },
                { "asserted", freezeAssert },
                { "pass", freezePass } } },
            { "error", rtErrors.isEmpty()
                           ? QJsonValue()
                           : QJsonValue(rtErrors.join(QStringLiteral("; "))) }};
        const QString outPath = parser.value(outOpt);
        if (!outPath.isEmpty()) {
            QDir().mkpath(QFileInfo(outPath).absolutePath());   // --out 深路径先建目录
            QFile o(outPath);
            o.open(QIODevice::WriteOnly);
            o.write(QJsonDocument(result).toJson());
        }
        qWarning("selftest roundtrip: ok=%d converged_at=%d load=%lldms size=%d",
                 ok, convergedAt, loadMs, int(prevMd.size()));
        qWarning("selftest roundtrip: 冻结探针 sync_to_html=%lldms asserted=%d "
                 "probe_max=%lldms threshold=%lldms dispatched=%d pass=%d",
                 exitSyncMs, freezeAssert, freezeMaxMs, freezeThreshold,
                 freezeDispatched, freezePass);
        return ok ? 0 : 1;
    }
    if (sub == "export") {
        const QString file = parser.value(fileOpt);
        const QString outdir = parser.value(outdirOpt);
        if (file.isEmpty() || outdir.isEmpty()) {
            qWarning("export 需要 --file 与 --outdir"); return 1;
        }

        const PinnedExportSettings pinned;   // 确定性导出设置（见类注释）

        MainWindow w(file);          // 构造即加载（不 show——无头）
        if (!waitEditorDocReady(w)) {
            qWarning("编辑器 30s 未就绪"); return 1;
        }
        EditorTab *tab = w.selfTestCurrentTab();
        // 文档目录（解析相对图片路径），与 MainWindow::onExport* 同源
        const QString docDir = QFileInfo(
            tab->webView()->property("filePath").toString()).absolutePath();

        ExportSuiteOut suite = runExportSuite(tab, file, docDir, outdir);

        // 源码模式导出冒烟（fix I-2）：ExportHtmlAsync src 分支此前零自动化
        // 覆盖（export 全程 WYSIWYG，src 仅手测兜底）。切源码后按 GUI 源码
        // HTML 导出同款 Mode::Plain 导出一次（src 判定 → getSourceContent →
        // toHtml worker → HtmlExporter 全链），断言产物完整（含 <body）且与
        // WYSIWYG 导出同量级——worker 化链路端到端走通。失败入 errors
        {
            const QString srcPath = QDir(outdir).filePath(
                QFileInfo(file).completeBaseName() + QStringLiteral("-srcsmoke.html"));
            const QFileInfo wysInfo(suite.files.value(QStringLiteral("html")).toString());
            bool switched = false, backed = false;
            ExportOutcome so;
            auto *ex = new HtmlExporter();
            if (wysInfo.exists()) {
                w.selfTestToggleSourceMode();                   // → 源码
                switched = waitJsTrue(w, QStringLiteral("window.editorApi.isSourceMode"));
                if (switched) {
                    QFile::remove(srcPath);
                    so = runExport(
                        tab, ExportHtmlAsync::Mode::Plain, 120000,
                        [ex, &docDir, &srcPath](const QString &html, const ExportDone &done) {
                            QObject::connect(ex, &HtmlExporter::exportFinished, ex,
                                [done](bool ok, const QString &) { done(ok, QString()); });
                            QObject::connect(ex, &HtmlExporter::exportError, ex,
                                [done](const QString &err) { done(false, err); });
                            ex->exportToHtml(html, docDir, srcPath, QStringLiteral("light"));
                        });
                    w.selfTestToggleSourceMode();               // → WYSIWYG（未编辑，快速路径）
                    backed = waitJsTrue(w, QStringLiteral("!window.editorApi.isSourceMode"));
                }
            }
            delete ex;   // 导出器持有隐藏渲染视图，结算后立即回收（滞留即泄漏）

            QString err;
            if (!wysInfo.exists())
                err = QStringLiteral("WYSIWYG html 产物缺失，冒烟无从对照");
            else if (!switched)
                err = QStringLiteral("切源码超时");
            else if (!so.finished)
                err = QStringLiteral("导出超时（120s）");
            else if (!so.ok)
                err = so.error.isEmpty() ? QStringLiteral("exportFinished(false)")
                                         : so.error;
            else if (!backed)
                err = QStringLiteral("切回 WYSIWYG 超时");
            else if (!QFileInfo::exists(srcPath))
                err = QStringLiteral("成功信号但产物缺失 ") + srcPath;
            else {
                const qint64 srcSize = QFileInfo(srcPath).size();
                if (srcSize * 2 < wysInfo.size())
                    err = QStringLiteral("src 产物量级异常（不足 WYSIWYG 一半）");
                else {
                    QFile rf(srcPath);
                    rf.open(QIODevice::ReadOnly);
                    if (!rf.readAll().contains("<body"))
                        err = QStringLiteral("产物缺 <body（非完整 HTML 文档）");
                }
            }
            const bool pass = err.isEmpty();
            suite.timings.insert(QStringLiteral("src_html_smoke"), double(so.ms));
            suite.files.insert(QStringLiteral("src_smoke"), srcPath);
            if (!pass) suite.errors.append(QStringLiteral("src_smoke: ") + err);
        }

        const bool ok = suite.errors.isEmpty();
        QJsonObject result{
            { "ok", ok },
            { "timings_ms", suite.timings },
            { "files", suite.files },
            { "paged_fallback", suite.pagedFallback },
            { "docx_normalized", suite.docxNormalized },
            { "error", ok ? QJsonValue()
                          : QJsonValue(suite.errors.join(QStringLiteral("; "))) }};
        const QString outPath = parser.value(outOpt);
        if (!outPath.isEmpty()) {
            QDir().mkpath(QFileInfo(outPath).absolutePath());
            QFile o(outPath);
            o.open(QIODevice::WriteOnly);
            o.write(QJsonDocument(result).toJson());
        }
        qWarning("selftest export: ok=%d html=%lldms docx=%lldms pdf=%lldms "
                 "paged_fallback=%d docx_normalized=%d err=%s",
                 ok, qint64(suite.timings.value("html").toDouble()),
                 qint64(suite.timings.value("docx").toDouble()),
                 qint64(suite.timings.value("pdf").toDouble()), suite.pagedFallback,
                 suite.docxNormalized,
                 ok ? "-" : qPrintable(suite.errors.join(QStringLiteral("; "))));
        return ok ? 0 : 1;
    }
    if (sub == "perf") {
        const QString file = parser.value(fileOpt);
        const QString outPath = parser.value(outOpt);
        if (file.isEmpty() || outPath.isEmpty()) {
            qWarning("perf 需要 --file 与 --out"); return 1;
        }

        const PinnedExportSettings pinned;   // 与 export 同款固定（导出计时可比）

        QElapsedTimer loadTimer;
        loadTimer.start();
        MainWindow w(file);          // 构造即加载（不 show——无头）
        if (!waitEditorDocReady(w)) {
            qWarning("编辑器 30s 未就绪"); return 1;
        }
        const qint64 loadMs = loadTimer.elapsed();

        // 预热等待：大文档（内容 ≥8192 字符）加载 1.5s 后 EditorTab 会触发一次
        // 丢弃式 getHTML 预热（EditorTab.cpp 冷 JIT 预热注释处）。不等它，这次
        // getHTML 会排在本测量段的首个 getContent 之前执行（渲染进程按到达序
        // 串行执行脚本），冷 JIT 成本计入 js_read_ms，基线随「就绪轮询恰好多
        // 久」漂移；等到 _warmed 再测，口径恒为用户实际体验的预热后状态。小
        // 文档不触发预热（_warmed 恒 false），8s 超时后直接继续，属预期路径。
        waitJsTrue(w, QStringLiteral("!!window.editorApi._warmed"), 8000);

        // 进入源码前的文档文字量：慢路径退出销毁重建后的「就绪」判据基准
        const qlonglong preTextLen = evalJsValue(w, QStringLiteral(
            "window.editorApi.editor"
            " ? window.editorApi.editor.state.doc.textContent.length : -1")).toLongLong();

        // ===== 模式切换分段计时 =====
        // 口径说明：perf **不经** MainWindow::onToggleSourceMode 编排——编排把
        // 各分段埋在回调链里，外部拿不到分段数；此处按编排同款顺序自行调
        // editorApi 同款 JS 与 MarkdownHandler 转换（getContent→fromHtml→
        // enterSourceMode / exitSourceModeOrGetMd / toHtml→exitSourceMode），
        // 分段与状态栏计时（MainWindow.cpp onToggleSourceMode 的【计时】消息）
        // 逐段同源，数字可与用户反馈直接对照。编排路径本身（含 tab/工具栏/
        // 状态栏状态翻转）已由 roundtrip 冒烟（selftest roundtrip）覆盖。
        // 数字仅采集不设阈值——基线阶段的产物就是数字本身。

        // —— 进入源码三段：JS 读取 / fromHtml 转换 / JS 填充 ——
        QJsonObject switchEnter;
        {
            QElapsedTimer perfTotal;
            perfTotal.start();
            const QString html = evalJsValue(
                w, QStringLiteral("window.editorApi.getContent()")).toString();
            const qint64 jsReadMs = perfTotal.elapsed();   // 含 IPC 往返（口径同状态栏）
            QElapsedTimer conv;
            conv.start();
            const QString md = MarkdownHandler::fromHtml(html);
            const qint64 fromHtmlMs = conv.elapsed();
            QElapsedTimer jsFill;
            jsFill.start();
            evalJsValue(w, QStringLiteral("window.editorApi.enterSourceMode(`%1`);")
                            .arg(JsEscape::forTemplateLiteral(md)));
            // -15 减经验 IPC 开销 + qMax 防负：状态栏计时同款（MainWindow.cpp）
            switchEnter = QJsonObject{
                { "js_read_ms", jsReadMs },
                { "from_html_ms", fromHtmlMs },
                { "js_fill_ms", qMax<qint64>(jsFill.elapsed() - 15, 0) },
                { "total_ms", perfTotal.elapsed() }};
        }
        if (!waitJsTrue(w, QStringLiteral("window.editorApi.isSourceMode"), 10000)) {
            qWarning("perf: 进入源码未生效"); return 1;
        }

        // —— 快速路径退出（源码未编辑：JS 单调用内复用内存文档，免 C++ 转换）——
        qint64 switchExitFastMs = 0;
        {
            QElapsedTimer perfTotal;
            perfTotal.start();
            const QVariant r = evalJsValue(
                w, QStringLiteral("window.editorApi.exitSourceModeOrGetMd()"));
            switchExitFastMs = perfTotal.elapsed();
            // userType 判型：JS true=快速路径；string=源码已编辑（此处不应发生）
            if (r.userType() != QMetaType::Bool || !r.toBool()) {
                qWarning("perf: 快速路径退出未命中（源码意外被编辑？）"); return 1;
            }
        }
        if (!waitJsTrue(w, QStringLiteral("!window.editorApi.isSourceMode"), 10000)) {
            qWarning("perf: 快速路径退出未生效"); return 1;
        }

        // —— 再次进入源码（不计时）：只为给慢路径退出制造「已编辑」前置态 ——
        {
            const QString html = evalJsValue(
                w, QStringLiteral("window.editorApi.getContent()")).toString();
            const QString md = MarkdownHandler::fromHtml(html);
            evalJsValue(w, QStringLiteral("window.editorApi.enterSourceMode(`%1`);")
                            .arg(JsEscape::forTemplateLiteral(md)));
        }
        if (!waitJsTrue(w, QStringLiteral("window.editorApi.isSourceMode"), 10000)) {
            qWarning("perf: 二次进入源码未生效"); return 1;
        }
        // 注入一次源码编辑（roundtrip 同款：textarea 追加 + input 事件，使
        // sv.value 偏离 _sourceValue → 下次退出必然走慢路径）
        evalJsValue(w, QStringLiteral(
            "(function(){var sv=document.getElementById('source-view');"
            "sv.value += '\\n\\nselftest 注入段。\\n';"
            "sv.dispatchEvent(new Event('input'));return 'ok';})()"));

        // —— 注入编辑后慢路径退出三段：JS 读取 / toHtml 转换 / JS 重建 ——
        QJsonObject switchExitEdited;
        {
            QElapsedTimer perfTotal;
            perfTotal.start();
            const QVariant r = evalJsValue(
                w, QStringLiteral("window.editorApi.exitSourceModeOrGetMd()"));
            const qint64 jsReadMs = perfTotal.elapsed();
            if (r.userType() == QMetaType::Bool) {
                qWarning("perf: 慢路径退出未命中（注入未生效？）"); return 1;
            }
            QElapsedTimer conv;
            conv.start();
            const QString html = MarkdownHandler::toHtml(r.toString());
            const qint64 toHtmlMs = conv.elapsed();
            QElapsedTimer jsBuild;
            jsBuild.start();
            evalJsValue(w, QStringLiteral("window.editorApi.exitSourceMode(`%1`);")
                            .arg(JsEscape::forTemplateLiteral(html)));
            switchExitEdited = QJsonObject{
                { "js_read_ms", jsReadMs },
                { "to_html_ms", toHtmlMs },
                { "js_build_ms", qMax<qint64>(jsBuild.elapsed() - 15, 0) },
                { "total_ms", perfTotal.elapsed() }};
        }
        // 慢路径重建「就绪」判据（waitEditorDocReady 修复版同思路）：exitSource
        // Mode→setContent 销毁重建编辑器，大文档重建后须等文档文字量恢复到
        // 进入前量级（90%）再进入导出段——避免在半重建状态上采导出数字。
        // 判据用 doc 文字而非 HTML 序列化形状，不依赖序列化行为。
        if (!waitJsTrue(w, QStringLiteral(
                "!!(window.editorApi && window.editorApi.editor"
                " && window.editorApi.editor.state.doc.textContent.length >= %1)")
                .arg(preTextLen * 9 / 10), 30000)) {
            qWarning("perf: 慢路径重建 30s 未就绪"); return 1;
        }

        // ===== 三格式导出计时（Task 4 的导出套件原样复用；产物落临时目录不留盘）=====
        const QString docDir = QFileInfo(
            w.selfTestCurrentTab()->webView()->property("filePath").toString()).absolutePath();
        const QTemporaryDir artifacts;   // 析构即清，基线只要数字
        const ExportSuiteOut suite = runExportSuite(
            w.selfTestCurrentTab(), file, docDir, artifacts.path());

        const bool ok = suite.errors.isEmpty();
        QJsonObject result{
            { "load_ms", loadMs },
            { "switch_enter", switchEnter },
            { "switch_exit_fast_ms", switchExitFastMs },
            { "switch_exit_edited", switchExitEdited },
            { "export_ms", suite.timings },
            { "paged_fallback", suite.pagedFallback },   // pdf 计时走哪条分页路径的判读依据
            { "docx_normalized", suite.docxNormalized }, // docx 计时含 normalizer 归一步的判读依据
            { "ok", ok },
            { "error", ok ? QJsonValue()
                          : QJsonValue(suite.errors.join(QStringLiteral("; "))) }};
        QDir().mkpath(QFileInfo(outPath).absolutePath());
        QFile o(outPath);
        o.open(QIODevice::WriteOnly);
        o.write(QJsonDocument(result).toJson());
        qWarning("selftest perf: ok=%d load=%lldms enter=%lldms exit_fast=%lldms "
                 "exit_edited=%lldms html=%lldms docx=%lldms pdf=%lldms "
                 "docx_normalized=%d err=%s",
                 ok, loadMs,
                 qint64(switchEnter.value("total_ms").toDouble()),
                 switchExitFastMs,
                 qint64(switchExitEdited.value("total_ms").toDouble()),
                 qint64(suite.timings.value("html").toDouble()),
                 qint64(suite.timings.value("docx").toDouble()),
                 qint64(suite.timings.value("pdf").toDouble()),
                 suite.docxNormalized,
                 ok ? "-" : qPrintable(suite.errors.join(QStringLiteral("; "))));
        return ok ? 0 : 1;
    }
    qWarning("未知子命令: %s", qPrintable(sub));
    return 2;
}

bool SelfTestRunner::isSelfTestInvocation()
{
    return QCoreApplication::arguments().contains(QStringLiteral("--selftest"));
}
