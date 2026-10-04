#include "ConversionQueue.h"

#include <QElapsedTimer>
#include <QtConcurrent/QtConcurrentRun>

ConversionQueue::ConversionQueue(QObject *parent)
    : QObject(parent), m_watcher(this)
{
    connect(&m_watcher, &QFutureWatcher<QPair<QString, qint64>>::finished,
            this, [this]() {
        m_active = false;
        const QPair<QString, qint64> r = m_watcher.result();
        // 先腾出 m_running 再执行完成回调：回调内可能链式入队（pump 会把新
        // 任务写进 m_running），后置清空会误杀新任务
        Task finished = std::move(m_running);
        m_running = Task();
        // 代次护栏：加载类任务仅在代次未被新加载作废时应用
        if (finished.done
            && (!finished.tiedToLoadEpoch || finished.loadEpoch == m_loadEpoch)) {
            finished.done(r.first, r.second);
        }
        pump();
        if (!m_active && m_queue.isEmpty())
            emit drained();
    });
}

quint64 ConversionQueue::bumpLoadEpoch()
{
    ++m_loadEpoch;
    // 顺手清掉已过期的排队任务（尚未启动的加载类转换——其完成回调反正会
    // 被丢弃）：外部变更重载风暴时最新加载不再排在 N 个旧转换后面各等一轮。
    // 在途任务不动（已开跑，跑完自然丢弃）
    for (int i = m_queue.size() - 1; i >= 0; --i) {
        if (m_queue[i].tiedToLoadEpoch && m_queue[i].loadEpoch != m_loadEpoch)
            m_queue.removeAt(i);
    }
    if (!m_active && m_queue.isEmpty())
        emit drained();   // 清空即排空（如恰无在途任务，drained 无人再发）
    return m_loadEpoch;
}

void ConversionQueue::enqueue(ConvFn conv, Done done, bool tiedToLoadEpoch)
{
    Task t;
    t.conv = std::move(conv);
    t.done = std::move(done);
    t.tiedToLoadEpoch = tiedToLoadEpoch;
    t.loadEpoch = m_loadEpoch;
    m_queue.append(std::move(t));
    pump();
}

void ConversionQueue::pump()
{
    if (m_active || m_queue.isEmpty())
        return;
    m_active = true;
    m_running = m_queue.takeFirst();
    // worker lambda 只按值捕获转换函数（其内部亦只捕获纯数据），不携带
    // this——队列析构后 future 继续跑完、结果随 watcher 销毁而自然丢弃
    const ConvFn conv = m_running.conv;
    m_watcher.setFuture(QtConcurrent::run([conv]() -> QPair<QString, qint64> {
        QElapsedTimer t;
        t.start();
        QString result = conv();
        return qMakePair(result, t.elapsed());
    }));
}
