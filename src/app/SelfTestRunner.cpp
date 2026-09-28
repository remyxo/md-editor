#include "SelfTestRunner.h"
#include "Application.h"
#include "ui/MainWindow.h"
#include "ui/EditorTab.h"
#include "core/MarkdownHandler.h"
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
#include <QTimer>
#include <QVariant>
#include <QVariantMap>
#include <QWebEnginePage>
#include <memory>

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

// 取 JS 字符串表达式结果（同步等待单次回调）
QString evalJsString(MainWindow &w, const QString &expr)
{
    QString out;
    QEventLoop loop;
    w.selfTestCurrentTab()->webView()->page()->runJavaScript(
        expr, [&](const QVariant &v) { out = v.toString(); loop.quit(); });
    loop.exec();
    return out;
}

QString mdOfCurrentDoc(MainWindow &w)
{
    // 与 writeTabToFile 同源：WYSIWYG 取 getSaveSnapshot().html 经 fromHtml
    QString out;
    QEventLoop loop;
    w.selfTestCurrentTab()->webView()->page()->runJavaScript(
        QStringLiteral("window.editorApi.getSaveSnapshot()"),
        [&](const QVariant &v) { out = v.toMap().value("html").toString(); loop.quit(); });
    loop.exec();
    return MarkdownHandler::fromHtml(out);
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
        // 就绪 = editorApi 存在 + 不在加载锁内 + **文档已有实际文字**。
        // 后者不可省：文件内容由 TabWidget 在 pageLoadFinished 后异步经
        // setContent 推入，此前编辑器是初始空文档（getHTML="<p></p>"），
        // editorApi/_loading 两个条件在空档期即满足——首轮切换会抓到空文档，
        // fromHtml("<p></p>")="&nbsp;"，退出重建把全文冲成注入段（实测踩坑）。
        // 空文档判据用 doc 文字而非 getHTML 长度：不依赖序列化形状。
        if (!waitJsTrue(w, QStringLiteral(
                "!!(window.editorApi && window.editorApi.editor"
                " && !window.editorApi._loading"
                " && window.editorApi.editor.state.doc.textContent"
                ".replace(/\\s/g,'').length > 0)"))) {
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
    if (sub == "export" || sub == "perf")
        { qWarning("%s 在后续任务实现", qPrintable(sub)); return 3; }
    qWarning("未知子命令: %s", qPrintable(sub));
    return 2;
}

bool SelfTestRunner::isSelfTestInvocation()
{
    return QCoreApplication::arguments().contains(QStringLiteral("--selftest"));
}
