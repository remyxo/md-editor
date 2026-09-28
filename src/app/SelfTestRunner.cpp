#include "SelfTestRunner.h"
#include "Application.h"
#include "ui/MainWindow.h"
#include "ui/EditorTab.h"
#include "ui/ExportHtmlAsync.h"
#include "core/DocxExporter.h"
#include "core/HtmlExporter.h"
#include "core/MarkdownHandler.h"
#include "core/PdfExporter.h"
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
#include <QScopeGuard>
#include <QSettings>
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

// —— export 子命令（无头三格式导出 + 计时）——

// 单格式导出结果：finished=收到终态信号；timedOut=超时兜底触发
struct ExportOutcome {
    bool finished = false;
    bool ok = false;
    bool timedOut = false;
    QString error;
    qint64 ms = 0;
};

// 导出完成回执（launch 内部把导出器的 exportFinished/exportError 翻译成它）
using ExportDone = std::function<void(bool ok, const QString &error)>;
// 启动导出器（收到 ExportHtmlAsync 的 HTML 后调用；须在收到终态时调 done）
using ExportLaunch = std::function<void(const QString &html, const ExportDone &done)>;

// 单格式无头导出：ExportHtmlAsync::get（含高亮步，本身异步）→ launch 启动
// 导出器 → QEventLoop 等终态信号。渲染进程挂死/崩溃时信号永不到达——
// timeoutMs 兜底返回而非永挂（PDF 大文档分页以十秒计，上限远大于其余两格式）。
// alive 守卫同 waitJsTrue：迟到回调不触碰已析构 loop；首个终态信号生效
//（o.finished 防其后第二信号覆盖结果）。计时从进入本函数起（含 HTML
// 获取 + 高亮 + 导出器全程），返回后调用方负责 delete 导出器
ExportOutcome runExport(EditorTab *tab, ExportHtmlAsync::Mode mode,
                        int timeoutMs, const ExportLaunch &launch)
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
    ExportHtmlAsync::get(tab, [&, alive](const QString &html) {
        if (!*alive) return;
        launch(html, [&, alive](bool ok, const QString &err) {
            if (!*alive || o.finished) return;
            o.finished = true;
            o.ok = ok;
            o.error = err;
            loop.quit();
        });
    }, mode);
    loop.exec();
    *alive = false;
    o.ms = t.elapsed();
    return o;
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
            w.selfTestToggleSourceMode();                       // → WYSIWYG（慢路径）
            if (!waitJsTrue(w, QStringLiteral("!window.editorApi.isSourceMode"))) {
                qWarning("第 %d 轮退出源码超时", i); return 1;
            }
            const QString md = mdOfCurrentDoc(w);
            roundArr.append(md.size());
            // 注意用 QStringLiteral 而非 QLatin1String：QLatin1String 把字面量
            // 字节按 Latin-1 解码，中文（UTF-8 多字节）永不匹配——注入存活断言
            // 形同虚设（实测踩坑：注入在文档里而 sawInjection 恒 false）
            if (md.contains(QStringLiteral("selftest 注入段"))) sawInjection = true;
            if (!prevMd.isNull() && md == prevMd && convergedAt < 0) convergedAt = i - 1;
            prevMd = md;
        }

        // 判定：收敛 + 注入内容存活 + 无占位符泄漏
        const bool converged = convergedAt >= 0;
        const bool leaked = prevMd.contains(QRegularExpression("ZZ[A-Z]+[0-9]+"));
        const bool ok = converged && sawInjection && !leaked;

        QJsonObject result{
            { "ok", ok }, { "rounds", rounds }, { "converged_at", convergedAt },
            { "load_ms", loadMs }, { "final_md_size", prevMd.size() },
            { "md_sizes", roundArr },
            { "error", converged ? QJsonValue() : QJsonValue("未在轮次内收敛") }};
        const QString outPath = parser.value(outOpt);
        if (!outPath.isEmpty()) {
            QDir().mkpath(QFileInfo(outPath).absolutePath());   // --out 深路径先建目录
            QFile o(outPath);
            o.open(QIODevice::WriteOnly);
            o.write(QJsonDocument(result).toJson());
        }
        qWarning("selftest roundtrip: ok=%d converged_at=%d load=%lldms size=%d",
                 ok, convergedAt, loadMs, int(prevMd.size()));
        return ok ? 0 : 1;
    }
    if (sub == "export") {
        const QString file = parser.value(fileOpt);
        const QString outdir = parser.value(outdirOpt);
        if (file.isEmpty() || outdir.isEmpty()) {
            qWarning("export 需要 --file 与 --outdir"); return 1;
        }

        // 确定性导出设置：把导出器从 QSettings 读的键全部固定为**代码内默认值**
        // （镜像自 PdfExporter.cpp:1199-1220、DocxExporter.cpp:1761-1783、
        // ExportCommon.cpp:114-120、HtmlExporter.cpp:360 读到的默认值：
        // 四边 margin 1.8、orientation=Portrait / paperSize=A4（枚举 static_cast
        // 与导出器默认参数同源）、pdf/docx 页眉页脚开、customCss 空）——消除
        // 用户机器 QSettings 状态差异，golden 特征才可比。析构守卫恢复用户
        // 原值（原本未写过的键 remove 回到默认态），selftest 不留配置副作用
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
        QVector<QPair<QString, QVariant>> savedSettings;
        for (const auto &kv : pinned) {
            const QString key = QString::fromLatin1(kv.first);
            savedSettings.append({ key, settings.value(key) });   // invalid = 原本未写
            settings.setValue(key, kv.second);
        }
        const auto restoreSettings = qScopeGuard([&savedSettings]() {
            QSettings rs;
            for (const auto &kv : savedSettings) {
                if (kv.second.isValid()) rs.setValue(kv.first, kv.second);
                else rs.remove(kv.first);
            }
        });

        QDir().mkpath(outdir);
        const QString base = QFileInfo(file).completeBaseName();
        const QString htmlPath = QDir(outdir).filePath(base + ".html");
        const QString docxPath = QDir(outdir).filePath(base + ".docx");
        const QString pdfPath = QDir(outdir).filePath(base + ".pdf");
        // 清掉上次残留产物：成功判定含「产物落盘」，残留会让失败跑被误判 ok
        for (const QString &p : { htmlPath, docxPath, pdfPath })
            QFile::remove(p);

        MainWindow w(file);          // 构造即加载（不 show——无头）
        if (!waitEditorDocReady(w)) {
            qWarning("编辑器 30s 未就绪"); return 1;
        }
        EditorTab *tab = w.selfTestCurrentTab();
        // 文档目录（解析相对图片路径），与 MainWindow::onExport* 同源
        const QString docDir = QFileInfo(
            tab->webView()->property("filePath").toString()).absolutePath();

        QJsonObject timings, files;
        QStringList errors;
        bool pagedFallback = false;

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
            recordOutcome("html", htmlPath, o, &timings, &files, &errors);
        }
        // DOCX（Docx 模式：live colgroup 注入供 tblGrid + 代码块高亮）
        {
            auto *ex = new DocxExporter();
            const ExportOutcome o = runExport(
                tab, ExportHtmlAsync::Mode::Docx, 120000,
                [ex, &docDir, &docxPath](const QString &html, const ExportDone &done) {
                    QObject::connect(ex, &DocxExporter::exportFinished, ex,
                        [done](bool ok, const QString &) { done(ok, QString()); });
                    QObject::connect(ex, &DocxExporter::exportError, ex,
                        [done](const QString &err) { done(false, err); });
                    ex->exportToDocx(html, docDir, docxPath);
                });
            delete ex;
            recordOutcome("docx", docxPath, o, &timings, &files, &errors);
        }
        // PDF（Pdf 模式：高亮；页眉页脚开 → 分页路径，大文档以十秒计，超时上限
        // 180s）。fallbackUsed：paged.js 失败回退原生分页仍产出 PDF（无页眉
        // 页脚）——不算失败，只记 paged_fallback 供 golden 判读
        {
            auto *ex = new PdfExporter();
            const ExportOutcome o = runExport(
                tab, ExportHtmlAsync::Mode::Pdf, 180000,
                [ex, &docDir, &pdfPath](const QString &html, const ExportDone &done) {
                    QObject::connect(ex, &PdfExporter::exportFinished, ex,
                        [done](bool ok, const QString &) { done(ok, QString()); });
                    QObject::connect(ex, &PdfExporter::exportError, ex,
                        [done](const QString &err) { done(false, err); });
                    ex->exportToPdf(html, docDir, pdfPath);
                });
            pagedFallback = ex->fallbackUsed();
            delete ex;
            recordOutcome("pdf", pdfPath, o, &timings, &files, &errors);
        }

        const bool ok = errors.isEmpty();
        QJsonObject result{
            { "ok", ok },
            { "timings_ms", timings },
            { "files", files },
            { "paged_fallback", pagedFallback },
            { "error", ok ? QJsonValue()
                          : QJsonValue(errors.join(QStringLiteral("; "))) }};
        const QString outPath = parser.value(outOpt);
        if (!outPath.isEmpty()) {
            QDir().mkpath(QFileInfo(outPath).absolutePath());
            QFile o(outPath);
            o.open(QIODevice::WriteOnly);
            o.write(QJsonDocument(result).toJson());
        }
        qWarning("selftest export: ok=%d html=%lldms docx=%lldms pdf=%lldms "
                 "paged_fallback=%d err=%s",
                 ok, qint64(timings.value("html").toDouble()),
                 qint64(timings.value("docx").toDouble()),
                 qint64(timings.value("pdf").toDouble()), pagedFallback,
                 ok ? "-" : qPrintable(errors.join(QStringLiteral("; "))));
        return ok ? 0 : 1;
    }
    if (sub == "perf")
        { qWarning("%s 在后续任务实现", qPrintable(sub)); return 3; }
    qWarning("未知子命令: %s", qPrintable(sub));
    return 2;
}

bool SelfTestRunner::isSelfTestInvocation()
{
    return QCoreApplication::arguments().contains(QStringLiteral("--selftest"));
}
