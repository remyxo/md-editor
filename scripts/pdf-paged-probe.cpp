// PDF paged 路径取证探针（纯诊断工具，不入产品，W3-3 定型）：
// 加载 PdfExporter 抓取的渲染 HTML（HF_DUMP_HTML 产物），复刻 C++ 侧
// 「测量遍 → 注入实测高 → 生产遍」两段流程，但不设 30s 轮询上限，并加两层
// 捕获：① 包装 preview() 拿 e.stack（产线 fallback 只回传 e.message）；
// ② 包装 __verifyContent 在回退还原 body 前抓下指纹双侧原文（pre/post），
// 供离线定位「内容指纹不一致」的具体差异。可选 teardownSec：在第 N 秒复刻
// 产线 pollPrintState 超时路径的 __restoreBody()（验证崩溃是否为拆 DOM 竞态
// 后果——W3-3 用它实锤 paged.polyfill.js:1407 崩溃与产线超时的因果）。
// 配套 runner：scripts/probe-pdf-paged.sh（构建+资源布置+无头运行）。
// 取证结论与用法示例见 ~/Desktop/md-editor优化专项-0927/w3-paged-evidence.md。
// 用法: pdf-paged-probe <dumped.html> <resDir> <outDir> [maxSec] [teardownSec]
#include <QApplication>
#include <QWebEngineView>
#include <QWebEngineSettings>
#include <QTimer>
#include <QFile>
#include <QDir>
#include <QUrl>
#include <QString>
#include <QDateTime>
#include <QRegularExpression>
#include <QJsonDocument>
#include <QJsonObject>
#include <cstdio>

static QString readFile(const QString &p)
{
    QFile f(p);
    if (!f.open(QIODevice::ReadOnly)) { fprintf(stderr, "read fail %s\n", qPrintable(p)); return QString(); }
    return QString::fromUtf8(f.readAll());
}

static void writeFile(const QString &dir, const QString &name, const QString &content)
{
    QFile f(QDir(dir).filePath(name));
    if (!f.open(QIODevice::WriteOnly)) { fprintf(stderr, "write fail %s\n", qPrintable(f.fileName())); return; }
    f.write(content.toUtf8());
}

struct Probe : public QObject {
    Q_OBJECT
public:
    QWebEngineView *view = nullptr;
    QString htmlPath, resDir, outDir, html;
    int phase = 0;                 // 0=测量遍 1=生产遍
    int measureH = 0, measureFn = 0;
    QTimer poll;
    qint64 t0 = 0;
    QStringList timeline;
    QString lastState;
    int lastPages = -1;
    int maxSec = 600;
    bool finishing = false;
    int teardownSec = 0;          // >0：在第 N 秒复刻产线超时路径的 __restoreBody
    bool tornDown = false;

    void note(qint64 el, const QString &line)
    {
        timeline << QStringLiteral("%1ms %2").arg(el).arg(line);
        fprintf(stderr, "[tl] %lldms %s\n", el, qPrintable(line));
    }

    void runPhase(int ph)
    {
        phase = ph;
        lastState.clear();
        lastPages = -1;
        view->setHtml(html, QUrl::fromLocalFile(resDir + QStringLiteral("/probe-base.html")));
    }

    // 生产遍注入（复刻 PdfExporter::onLoadFinished 的 m_measuredBoxH 注入 +
    // 探针私货：__ppDbg=1、preview 错误捕获、window.onerror 捕获）
    QString injectProd() const
    {
        return QStringLiteral(
            "window.__pagedBoxH=%1;window.__pagedFnH=%2;window.__ppDbg=1;"
            "window.__probeErr=null;window.__probeDump=null;"
            "window.addEventListener('error',function(e){"
            "window.__probeErr={msg:''+(e.message||''),stack:''+((e.error&&e.error.stack)||'')};});"
            // __verifyContent 包装：在回退还原 body **之前**抓取指纹两侧原文，
            // 供离线定位差异（产品侧只回传首异位，还原后 post 侧即消失）
            "if(window.__verifyContent){"
            "var __ov=window.__verifyContent;"
            "window.__verifyContent=function(){"
            "var r=__ov.apply(this,arguments);"
            "try{"
            "var ps2=document.querySelectorAll('.pagedjs_page_content');"
            "var post='',fn='';"
            "for(var i=0;i<ps2.length;i++){var c=ps2[i].cloneNode(true);"
            "var ds=c.querySelectorAll('div[data-type=\"footnoteDef\"]');"
            "for(var k=0;k<ds.length;k++){fn+=ds[k].textContent;ds[k].remove();}"
            "var sc=c.querySelectorAll('script,style');"
            "for(var q2=0;q2<sc.length;q2++)sc[q2].remove();"
            "post+=c.textContent;}"
            "var fd=document.querySelectorAll('.pagedjs_footnote_area div[data-type=\"footnoteDef\"]');"
            "for(var q3=0;q3<fd.length;q3++)fn+=fd[q3].textContent;"
            "window.__probeDump=JSON.stringify({result:''+r,"
            "pre:window.__preText||'',preFns:window.__preFns||'',"
            "post:post,fn:fn,pages:ps2.length});"
            "}catch(e){}"
            "return r;};}"
            "if(window.PagedPolyfill&&window.PagedPolyfill.preview){"
            "var __orig=window.PagedPolyfill.preview.bind(window.PagedPolyfill);"
            "window.PagedPolyfill.preview=function(){"
            "return __orig().catch(function(e){"
            "window.__probeErr={msg:''+((e&&e.message)||e),"
            "stack:''+((e&&e.stack)||'')};throw e;});};}")
            .arg(measureH).arg(measureFn);
    }

    void finish(const QString &st, int pages, qint64 el);
    void dumpThenQuit(const QString &st, int pages, qint64 el);

public slots:
    void onLoadFinished(bool ok)
    {
        if (!ok) { fprintf(stderr, "load failed\n"); QCoreApplication::exit(3); return; }
        if (phase == 0)
            view->page()->runJavaScript(QStringLiteral("window.__preparePrint()"));
        else
            view->page()->runJavaScript(injectProd(), [this](const QVariant &) {
                view->page()->runJavaScript(QStringLiteral("window.__preparePrint()"));
            });
        t0 = QDateTime::currentMSecsSinceEpoch();
        lastState.clear();
        poll.start(250);
    }

    void pollTick();
};

void Probe::finish(const QString &st, int pages, qint64 el)
{
    poll.stop();
    note(el, QStringLiteral("TERMINAL state=%1 pages=%2").arg(st).arg(pages));
    // 指纹双侧原文先取（__verifyContent 包装产物；还原 body 前的最后现场），
    // 再取错误信息（异步链逐步接力），最后 dumpThenQuit 收尾
    view->page()->runJavaScript(
        QStringLiteral("window.__probeDump||''"),
        [this, st, pages, el](const QVariant &v) {
            if (!v.toString().isEmpty())
                writeFile(outDir, QStringLiteral("probeDump.json"), v.toString());
            view->page()->runJavaScript(
                QStringLiteral("window.__probeErr?JSON.stringify(window.__probeErr):''"),
                [this, st, pages, el](const QVariant &v2) {
                    if (!v2.toString().isEmpty())
                        writeFile(outDir, QStringLiteral("err.json"), v2.toString());
                    dumpThenQuit(st, pages, el);
                });
        });
}

void Probe::dumpThenQuit(const QString &st, int pages, qint64 el)
{
    // 链式取数：每步回调发起下一步，避免嵌套事件循环
    if (st == QLatin1String("paged")) {
        view->page()->runJavaScript(QStringLiteral("window.__verifyContent()"),
            [this, st, pages, el](const QVariant &v) {
                writeFile(outDir, QStringLiteral("verify.txt"), v.toString());
                view->page()->runJavaScript(QStringLiteral("window.__preText||''"),
                    [this, st, pages, el](const QVariant &v2) {
                        writeFile(outDir, QStringLiteral("preText.txt"), v2.toString());
                        view->page()->runJavaScript(
                            QStringLiteral("(window.__ppInfo||[]).join('\\n')"),
                            [this, st, pages, el](const QVariant &v3) {
                                writeFile(outDir, QStringLiteral("ppInfo.txt"), v3.toString());
                                // post 侧文本与 __verifyContent 同口径（一并回传脚注侧）
                                view->page()->runJavaScript(QStringLiteral(
                                    "(function(){var ps2=document.querySelectorAll('.pagedjs_page_content');"
                                    "var post='',fn='';"
                                    "for(var i=0;i<ps2.length;i++){var c=ps2[i].cloneNode(true);"
                                    "var ds=c.querySelectorAll('div[data-type=\"footnoteDef\"]');"
                                    "for(var k=0;k<ds.length;k++){fn+=ds[k].textContent;ds[k].remove();}"
                                    "var sc=c.querySelectorAll('script,style');"
                                    "for(var r=0;r<sc.length;r++)sc[r].remove();"
                                    "post+=c.textContent;}"
                                    "var fd=document.querySelectorAll('.pagedjs_footnote_area div[data-type=\"footnoteDef\"]');"
                                    "for(var q=0;q<fd.length;q++)fn+=fd[q].textContent;"
                                    "return JSON.stringify({post:post,fn:fn,pages:ps2.length});})()"),
                                    [this, st, pages, el](const QVariant &v4) {
                                        writeFile(outDir, QStringLiteral("postRaw.json"), v4.toString());
                                        writeFile(outDir, QStringLiteral("timeline.txt"), timeline.join(QLatin1Char('\n')));
                                        writeFile(outDir, QStringLiteral("summary.txt"),
                                            QStringLiteral("state=%1 pages=%2 elapsed=%3ms measureH=%4 measureFn=%5")
                                                .arg(st).arg(pages).arg(el).arg(measureH).arg(measureFn));
                                        fprintf(stderr, "[probe] TERMINAL state=%s pages=%d elapsed=%lldms\n",
                                                qPrintable(st), pages, el);
                                        QCoreApplication::exit(0);
                                    });
                            });
                    });
            });
        return;
    }
    writeFile(outDir, QStringLiteral("timeline.txt"), timeline.join(QLatin1Char('\n')));
    writeFile(outDir, QStringLiteral("summary.txt"),
              QStringLiteral("state=%1 pages=%2 elapsed=%3ms measureH=%4 measureFn=%5")
                  .arg(st).arg(pages).arg(el).arg(measureH).arg(measureFn));
    fprintf(stderr, "[probe] TERMINAL state=%s pages=%d elapsed=%lldms\n",
            qPrintable(st), pages, el);
    QCoreApplication::exit(0);
}

void Probe::pollTick()
{
    const qint64 el = QDateTime::currentMSecsSinceEpoch() - t0;
    // 复刻产线 30s 轮询超时的还原动作（pollPrintState 的 ++m_printPollCount>300
    // 分支）：__restoreBody 在 paged.js 仍在分页时拆走 DOM——验证崩溃是否为其
    // 后果（产线 4000 三跑皆崩、探针不拆则只爬不崩）
    if (teardownSec > 0 && !tornDown && phase == 1 && el > teardownSec * 1000LL) {
        tornDown = true;
        note(el, QStringLiteral("TEARDOWN __restoreBody (产线超时复刻)"));
        view->page()->runJavaScript(
            QStringLiteral("try{window.__restoreBody&&window.__restoreBody();}catch(e){}"));
    }
    view->page()->runJavaScript(
        QStringLiteral(
            "JSON.stringify({s:window.__printState||'',"
            "n:document.querySelectorAll('.pagedjs_page').length,"
            "m:window.__measureDbg||''})"),
        [this, el](const QVariant &v) {
            const QJsonObject o = QJsonDocument::fromJson(v.toString().toUtf8()).object();
            const QString st = o.value(QStringLiteral("s")).toString();
            const int n = o.value(QStringLiteral("n")).toInt();
            if (st != lastState) {
                note(el, QStringLiteral("state=%1 pages=%2").arg(st.isEmpty() ? QStringLiteral("(empty)") : st).arg(n));
                lastState = st;
                lastPages = n;
            } else if (n != lastPages) {
                note(el, QStringLiteral("pages=%1").arg(n));
                lastPages = n;
            }
            if (phase == 0 && st == QLatin1String("measured")) {
                poll.stop();
                const QString dbg = o.value(QStringLiteral("m")).toString();
                writeFile(outDir, QStringLiteral("measured.txt"), dbg);
                const auto mh = QRegularExpression(QStringLiteral("高=(\\d+)")).match(dbg);
                const auto mf = QRegularExpression(QStringLiteral("脚注区=(\\d+)")).match(dbg);
                const auto ma = QRegularExpression(QStringLiteral("面积=(\\d+)")).match(dbg);
                if (mh.hasMatch()) measureH = mh.captured(1).toInt();
                if (mf.hasMatch()) measureFn = mf.captured(1).toInt();
                if (ma.hasMatch() && measureH > 0)
                    measureFn = qMax(measureFn, ma.captured(1).toInt() - measureH);
                fprintf(stderr, "[probe] measured H=%d F=%d (%s)\n", measureH, measureFn, qPrintable(dbg));
                runPhase(1);
                return;
            }
            const bool terminal = (st == QLatin1String("paged") || st == QLatin1String("legacy")
                                   || st == QLatin1String("fallback")
                                   || st == QLatin1String("nativehf"));
            if (terminal && !finishing) {
                finishing = true;
                finish(st, n, el);
                return;
            }
            if (el > maxSec * 1000LL) {
                poll.stop();
                note(el, QStringLiteral("TIMEOUT(probe-limit) state=%1 pages=%2").arg(st).arg(n));
                writeFile(outDir, QStringLiteral("timeline.txt"), timeline.join(QLatin1Char('\n')));
                writeFile(outDir, QStringLiteral("summary.txt"),
                          QStringLiteral("state=%1(still) pages=%2 elapsed=%3ms PROBE-TIMEOUT")
                              .arg(st).arg(n).arg(el));
                // 超时也抓现场：__preText 与 ppInfo 在 window 上持久存在
                view->page()->runJavaScript(
                    QStringLiteral("window.__preText||''"),
                    [this](const QVariant &v2) {
                        writeFile(outDir, QStringLiteral("preText.txt"), v2.toString());
                        view->page()->runJavaScript(
                            QStringLiteral("(window.__ppInfo||[]).join('\\n')"),
                            [this](const QVariant &v3) {
                                writeFile(outDir, QStringLiteral("ppInfo.txt"), v3.toString());
                                fprintf(stderr, "[probe] probe timeout dumps done\n");
                                QCoreApplication::exit(4);
                            });
                    });
            }
        });
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    const QStringList args = app.arguments();
    if (args.size() < 4) { fprintf(stderr, "usage: probe-paged <html> <resDir> <outDir> [maxSec] [teardownSec]\n"); return 2; }
    Probe p;
    p.htmlPath = args[1]; p.resDir = args[2]; p.outDir = args[3];
    if (args.size() > 4) p.maxSec = args[4].toInt();
    if (args.size() > 5) p.teardownSec = args[5].toInt();
    QDir().mkpath(p.outDir);
    p.html = readFile(p.htmlPath);

    p.view = new QWebEngineView();
    p.view->setVisible(false);
    p.view->settings()->setAttribute(QWebEngineSettings::LocalContentCanAccessFileUrls, true);
    // 复刻生产几何（PdfExporter::exportToPdf 的 resize）：paged 路径视口宽 =
    // 页盒内容宽（纸宽 − 用户左右边距）再收 2px——分页前测量（列宽归一/
    // 表格预分页）跑在屏幕布局上，视口宽不同则行高/折行分叉，分页行为失真
    // （A4/18mm 边距：174mm → 657.6px → 658 − 2 = 656）。高 1200 同产线
    p.view->resize(656, 1200);
    QObject::connect(p.view, &QWebEngineView::loadFinished, &p, &Probe::onLoadFinished);
    QObject::connect(&p.poll, &QTimer::timeout, &p, &Probe::pollTick);
    p.poll.setInterval(100);   // 与产线 pollPrintState 同节拍（250ms 时 4000 不崩只爬——节拍影响分页内部任务交错）
    p.runPhase(0);
    const int rc = app.exec();
    p.poll.stop();
    delete p.view;
    return rc;
}

#include "pdf-paged-probe.moc"
