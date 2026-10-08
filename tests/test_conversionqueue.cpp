// test_conversionqueue — W4-1 转换移线程的时序契约与冻结量化
//
// 契约（对应 src/core/ConversionQueue.h 的设计要点）：
//  1. 同 tab 串行：完成回调按入队顺序在 GUI 线程执行
//  2. 加载代次护栏：bumpLoadEpoch 后，tiedToLoadEpoch 的在途/排队任务
//     完成回调被丢弃（「转换 A 在途时触发 B，A 完成不得覆盖」）；
//     非加载类任务（保存/模式切换）不受代次影响
//  3. 队列析构后在途结果丢弃（回调不再执行——回调捕获的指针不悬垂）
//  4. worker 只在非 GUI 线程执行；完成回调恒在 GUI 线程
//  5. worker 线程与 GUI 线程跑同一输入的 MarkdownHandler 转换逐字节一致
//  6. UI 冻结量化：转换期间 GUI 事件循环的 posted-event 最大派发时延，
//     远小于同步（改前路径）基线——同步基线在本测试内直接模拟
#include <QtTest>
#include <QGuiApplication>
#include <QSemaphore>
#include <QThreadPool>
#include <QThread>
#include <QElapsedTimer>
#include "core/ConversionQueue.h"
#include "core/MarkdownHandler.h"

// 可控 worker 闸：started 确认 worker 已进入，release 放行 worker 返回
struct Gate {
    QSemaphore started;
    QSemaphore release;
};

// 事件循环响应探针：连续 queued 自调用，测「投递 → 派发」最大时延。
// GUI 线程被长任务占死时，queued 调用只能排队，最大时延 ≈ 占死时长
class EventLoopPing : public QObject {
    Q_OBJECT
public:
    qint64 maxLatencyMs = 0;
    int dispatched = 0;

    void arm()
    {
        m_sent.restart();
        QMetaObject::invokeMethod(this, &EventLoopPing::bounce, Qt::QueuedConnection);
    }

private slots:
    void bounce()
    {
        maxLatencyMs = qMax<qint64>(maxLatencyMs, m_sent.elapsed());
        ++dispatched;
        m_sent.restart();
        QMetaObject::invokeMethod(this, &EventLoopPing::bounce, Qt::QueuedConnection);
    }

private:
    QElapsedTimer m_sent;
};

// 代表性输入：行内代码（触发等宽字体族静态）、表格、脚注、强调、围栏
static QString representativeMd()
{
    return QStringLiteral(
        "# 标题一\n\n段落有 **加粗**、*斜体*、`行内代码` 与 [链接](https://example.com)。\n\n"
        "## 标题二\n\n| 列A | 列B |\n|-----|-----|\n| a1 | b1 |\n| a2 | b2 |\n\n"
        "> 引用块文字\n\n```cpp\nint main() { return 0; }\n```\n\n"
        "脚注引用[^1]\n\n[^1]: 脚注定义文字\n\n---\n\n"
        "<span style=\"color:red\">红字</span> 与 <sub>下标</sub>/<sup>上标</sup>\n\n"
        "- 列表项 1\n- 列表项 2\n  - 嵌套项\n\n1. 有序一\n2. 有序二\n");
}

// 大文档合成：用于冻结量化（同步基线需达数百毫秒量级才有区分度）
static QString bigMd(int blocks)
{
    QString md;
    md.reserve(blocks * 400);
    for (int i = 0; i < blocks; ++i) {
        md += QStringLiteral(
            "## 第 %1 节\n\n段落 %1 有 **加粗**、*斜体*、`code%1` 与一些普通文字用于撑起段落长度，"
            "再补一句包含 [链接](https://example.com/%1) 的文字。\n\n"
            "| 列A | 列B | 列C |\n|-----|-----|-----|\n| %1a | %1b | %1c |\n\n"
            "- 项目 %1.1\n- 项目 %1.2\n  - 嵌套 %1.3\n\n"
            "```cpp\n// 代码块 %1\nint f%1(int x) { return x * %1; }\n```\n\n").arg(i);
    }
    return md;
}

class TestConversionQueue : public QObject {
    Q_OBJECT

private slots:
    // ── 契约 1：串行次序 + 完成回调在 GUI 线程 ──
    void testFifoSerialOrder();

    // ── 契约 1b：完成回调内链式入队，新任务不得被 finished 处理器误清 ──
    void testChainedEnqueueFromDone();

    // ── 契约 2：代次护栏（红测试的主目标）──
    void testStaleLoadEpochDropped();
    void testEpochExemptTaskNotDropped();

    // ── 契约 2b：bumpLoadEpoch 清除过期排队任务（新加载不等 N×conv）──
    void testBumpPrunesStaleQueuedTasks();

    // ── 契约 3：析构丢弃 ──
    void testDestructorDropsPending();

    // ── 契约 4：线程身份 ──
    void testWorkerOffGuiCompletionOnGui();

    // ── 契约 5：worker 与 GUI 线程转换逐字节一致（真实 MarkdownHandler）──
    void testWorkerConversionByteEqual();

    // ── drained 信号：队列空转观测点 ──
    void testDrainedSignal();

    // ── 契约 6：UI 冻结量化（异步 vs 同步基线）──
    void testEventLoopNotBlockedDuringConversion();
};

void TestConversionQueue::testFifoSerialOrder()
{
    ConversionQueue q;
    QStringList order;
    for (int i = 0; i < 3; ++i) {
        q.enqueue([i]() { return QStringLiteral("r%1").arg(i); },
                  [&order, i](const QString &result, qint64 convMs) {
                      order << result;
                      QVERIFY2(convMs >= 0, "convMs 应为非负耗时");
                      QVERIFY2(QThread::currentThread() == QGuiApplication::instance()->thread(),
                               "完成回调必须在 GUI 线程执行");
                      Q_UNUSED(i);
                  });
    }
    QTRY_COMPARE_WITH_TIMEOUT(order.size(), 3, 10000);
    QCOMPARE(order, QStringList({ QStringLiteral("r0"), QStringLiteral("r1"), QStringLiteral("r2") }));
}

void TestConversionQueue::testChainedEnqueueFromDone()
{
    // 完成回调内链式入队：finished 处理器先腾 m_running 再跑回调，新任务的
    // 回调必须照常执行（回归：曾在回调后统一清空，链式新任务被误杀）
    ConversionQueue q;
    QStringList order;
    q.enqueue([]() { return QStringLiteral("a"); },
              [&q, &order](const QString &result, qint64) {
                  order << result;
                  q.enqueue([]() { return QStringLiteral("b"); },
                            [&order](const QString &r2, qint64) { order << r2; });
              });
    QTRY_COMPARE_WITH_TIMEOUT(order.size(), 2, 10000);
    QCOMPARE(order, QStringList({ QStringLiteral("a"), QStringLiteral("b") }));
}

void TestConversionQueue::testStaleLoadEpochDropped()
{
    // 转换 A 在途（worker 阻塞在闸上）→ 新加载 bump 代次 + 触发转换 B →
    // 放行 A：A 完成回调必须被丢弃（不得覆盖新状态），B 正常应用
    ConversionQueue q;
    Gate gateA;
    QStringList applied;
    int droppedA = 0;

    q.enqueue([&gateA]() -> QString {
                  gateA.started.release();
                  gateA.release.acquire();   // 挂住 A 直到测试放行
                  return QStringLiteral("staleA");
              },
              [&applied, &droppedA](const QString &result, qint64) {
                  ++droppedA;
                  applied << result;
              },
              /*tiedToLoadEpoch=*/true);

    gateA.started.acquire();          // 确认 A 已在 worker 线程在途
    q.bumpLoadEpoch();                // 新 loadFile：作废旧加载转换

    bool bApplied = false;
    q.enqueue([]() { return QStringLiteral("freshB"); },
              [&bApplied](const QString &result, qint64) {
                  QCOMPARE(result, QStringLiteral("freshB"));
                  bApplied = true;
              },
              /*tiedToLoadEpoch=*/true);

    gateA.release.release();          // 放行 A：其完成回调应被丢弃
    QTRY_VERIFY_WITH_TIMEOUT(bApplied, 10000);
    QTest::qWait(50);                 // 给迟到的 A 回调留窗口（若实现错误会触发）
    QCOMPARE(droppedA, 0);
    QVERIFY(applied.isEmpty() || applied.first() == QStringLiteral("freshB"));
}

void TestConversionQueue::testEpochExemptTaskNotDropped()
{
    // 保存/模式切换类任务不受加载代次影响：bump 后在途任务照常完成
    ConversionQueue q;
    Gate gate;
    bool applied = false;

    q.enqueue([&gate]() -> QString {
                  gate.started.release();
                  gate.release.acquire();
                  return QStringLiteral("save");
              },
              [&applied](const QString &result, qint64) {
                  QCOMPARE(result, QStringLiteral("save"));
                  applied = true;
              },
              /*tiedToLoadEpoch=*/false);

    gate.started.acquire();
    q.bumpLoadEpoch();                // 期间发生新加载：不得作废保存类任务
    gate.release.release();
    QTRY_VERIFY_WITH_TIMEOUT(applied, 10000);
}

void TestConversionQueue::testBumpPrunesStaleQueuedTasks()
{
    // A（tied）在途 → B（tied，会挡死的慢任务）排队 → bump 清掉 B →
    // C（tied，新代次）入队。放行 A 后：A 完成回调被代次丢弃，C 无需等 B
    // 直接执行。若 B 未被清除：A 完成后 pump 先跑 B，B 挂在闸上 → C 永不
    // 完成（QTRY 超时失败）——确定性断言，不依赖计时时长
    ConversionQueue q;
    Gate gateA, gateB;
    int droppedA = 0;
    bool bStarted = false, cDone = false;

    q.enqueue([&gateA]() -> QString {
                  gateA.started.release();
                  gateA.release.acquire();
                  return QStringLiteral("staleA");
              },
              [&droppedA](const QString &, qint64) { ++droppedA; },
              /*tiedToLoadEpoch=*/true);
    gateA.started.acquire();          // A 已在 worker 在途

    q.enqueue([&gateB, &bStarted]() -> QString {
                  bStarted = true;    // 若 prune 失效此任务才会启动
                  gateB.started.release();
                  gateB.release.acquire();
                  return QStringLiteral("staleB");
              },
              [](const QString &, qint64) {},
              /*tiedToLoadEpoch=*/true);

    q.bumpLoadEpoch();                // 作废 A（在途，跑完丢弃）+ 清除排队中的 B

    q.enqueue([]() { return QStringLiteral("freshC"); },
              [&cDone](const QString &r, qint64) {
                  QCOMPARE(r, QStringLiteral("freshC"));
                  cDone = true;
              },
              /*tiedToLoadEpoch=*/true);

    gateA.release.release();          // 放行 A
    QTRY_VERIFY_WITH_TIMEOUT(cDone, 10000);   // C 不等 B 即完成
    QTest::qWait(50);
    QCOMPARE(droppedA, 0);            // A 完成回调被代次护栏丢弃
    QVERIFY2(!bStarted, "过期排队任务应被 bump 清除（不启动、不占一轮转换）");
    // 收尾：B 若因实现缺陷仍在闸上，放行并排干线程池，避免悬挂 worker
    gateB.release.release();
    QThreadPool::globalInstance()->waitForDone(10000);
}

void TestConversionQueue::testDestructorDropsPending()
{
    // 队列析构（随 EditorTab 关闭）后：在途任务完成回调不得执行
    bool ran = false;
    {
        ConversionQueue *q = new ConversionQueue();
        Gate gate;
        q->enqueue([&gate]() -> QString {
                       gate.started.release();
                       gate.release.acquire();
                       return QStringLiteral("never");
                   },
                   [&ran](const QString &, qint64) { ran = true; });
        gate.started.acquire();
        delete q;                     // 在途时析构：watcher/回调随之销毁
        gate.release.release();       // worker 之后才完成：结果无人接收
        QThreadPool::globalInstance()->waitForDone(10000);
    }
    QTest::qWait(50);
    QVERIFY2(!ran, "队列析构后在途任务的完成回调不得执行");
}

void TestConversionQueue::testWorkerOffGuiCompletionOnGui()
{
    ConversionQueue q;
    QThread *workerThread = nullptr;
    bool done = false;
    q.enqueue([&workerThread]() {
                  workerThread = QThread::currentThread();
                  return QString();
              },
              [&done](const QString &, qint64) {
                  QVERIFY2(QThread::currentThread() == QGuiApplication::instance()->thread(),
                           "完成回调必须在 GUI 线程执行");
                  done = true;
              });
    QTRY_VERIFY_WITH_TIMEOUT(done, 10000);
    QVERIFY2(workerThread && workerThread != QGuiApplication::instance()->thread(),
             "转换必须在非 GUI 线程执行（worker 线程）");
}

void TestConversionQueue::testWorkerConversionByteEqual()
{
    // 主线程（GUI）先算出基准——进程级静态（等宽字体族等）在 GUI 线程初始化，
    // 与真实应用「启动即有 GUI 线程」一致；worker 线程随后跑同输入须逐字节一致。
    // 「worker 线程先行初始化静态」的口径由独立探针二进制覆盖（无头 fresh 进程，
    // 见任务报告：Step 1 前置确认实验）
    const QString md = representativeMd();
    const QString htmlGui = MarkdownHandler::toHtml(md);
    const QString mdGui = MarkdownHandler::fromHtml(htmlGui);
    QVERIFY2(htmlGui.contains(QStringLiteral("<body")), "代表输入应产出完整 HTML 文档");

    ConversionQueue q;
    QString htmlWorker;
    QString mdWorker;
    qint64 htmlMs = -1, mdMs = -1;
    q.enqueue([md]() { return MarkdownHandler::toHtml(md); },
              [&](const QString &r, qint64 ms) { htmlWorker = r; htmlMs = ms; });
    QTRY_VERIFY_WITH_TIMEOUT(!htmlWorker.isNull(), 30000);
    q.enqueue([htmlGui]() { return MarkdownHandler::fromHtml(htmlGui); },
              [&](const QString &r, qint64 ms) { mdWorker = r; mdMs = ms; });
    QTRY_VERIFY_WITH_TIMEOUT(!mdWorker.isNull(), 30000);

    QCOMPARE(htmlWorker, htmlGui);   // toHtml：worker 与 GUI 线程逐字节一致
    QCOMPARE(mdWorker, mdGui);       // fromHtml：同上
    QVERIFY2(htmlMs >= 0 && mdMs >= 0, "worker 须回报转换耗时");
}

void TestConversionQueue::testDrainedSignal()
{
    ConversionQueue q;
    QSignalSpy drained(&q, &ConversionQueue::drained);
    q.enqueue([]() { return QStringLiteral("x"); },
              [](const QString &, qint64) {});
    QTRY_COMPARE_WITH_TIMEOUT(drained.size(), 1, 10000);
    // 二次入队后应再次排空发信号
    q.enqueue([]() { return QStringLiteral("y"); },
              [](const QString &, qint64) {});
    QTRY_COMPARE_WITH_TIMEOUT(drained.size(), 2, 10000);
}

void TestConversionQueue::testEventLoopNotBlockedDuringConversion()
{
    // 文档规模自校准：同步基线需 ≥300ms 才有区分度，且机器越快基线越短
    // （2026-10-08 ubuntu-26.04/Qt 6.10.2 基准 CI 实测 bigMd(600) 基线不足
    // 300ms、前置断言失败）——倍增文档直到基线达标，上限 9600 节防失控
    QString md;
    qint64 syncMs = 0;
    qint64 syncMax = 0;
    int blocks = 600;
    for (;;) {
        md = bigMd(blocks);
        EventLoopPing ping;
        ping.arm();
        QElapsedTimer t;
        t.start();
        const QString html = MarkdownHandler::toHtml(md);   // 改前路径：GUI 线程同步转换
        Q_UNUSED(html);
        syncMs = t.elapsed();
        QTest::qWait(200);                                   // 排队中的 ping 派发
        syncMax = ping.maxLatencyMs;
        if (syncMax >= 300 || blocks >= 9600)
            break;
        blocks *= 2;
    }
    QVERIFY2(syncMax >= 300,
             qPrintable(QStringLiteral("同步基线过短（%1ms，文档已加大至上限仍不足），"
                                       "测量口径需再议").arg(syncMax)));
    qWarning("冻结量化: 校准文档 %d 节；同步基线 toHtml=%lldms posted-event 最大派发时延=%lldms",
             blocks, syncMs, syncMax);

    // 改后路径：worker 转换期间事件循环持续派发
    ConversionQueue q;
    EventLoopPing ping;
    ping.arm();
    bool done = false;
    qint64 convMs = 0;
    q.enqueue([md]() { return MarkdownHandler::toHtml(md); },
              [&done, &convMs](const QString &, qint64 ms) { done = true; convMs = ms; });
    QTRY_VERIFY_WITH_TIMEOUT(done, 60000);
    QTest::qWait(200);
    qWarning("冻结量化: 异步转换 convMs=%lldms 期间 posted-event 最大派发时延=%lldms（派发 %d 次）",
             convMs, ping.maxLatencyMs, ping.dispatched);
    QVERIFY2(ping.dispatched >= 5, "转换期间事件循环应有多次派发");
    QVERIFY2(ping.maxLatencyMs <= qMax<qint64>(150, syncMs / 4),
             qPrintable(QStringLiteral("异步转换期间事件循环最大派发时延 %1ms，"
                                       "须远小于同步基线 %2ms")
                            .arg(ping.maxLatencyMs)
                            .arg(syncMs)));
}

QTEST_MAIN(TestConversionQueue)
#include "test_conversionqueue.moc"
