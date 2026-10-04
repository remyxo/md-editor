#ifndef MD_EDITOR_CONVERSIONQUEUE_H
#define MD_EDITOR_CONVERSIONQUEUE_H

#include <QObject>
#include <QFutureWatcher>
#include <functional>

// 每 tab 一个的 MD↔HTML 转换任务队列：QtConcurrent worker 线程执行转换，
// 完成回调编组回 GUI 线程（W4-1，先例 ghostwriter htmlpreview.cpp 的
// QtConcurrent::run + QFutureWatcher 模式）。
//
// 设计要点：
//  1. 同 tab 串行：同一时刻至多一个转换在 worker 线程跑，按入队顺序完成并
//     在 GUI 线程执行回调——保持同步时代「全部转换在 GUI 线程按序执行」的
//     次序语义。典型受益方是连续保存：新快照恒后落盘，杜绝线程池并行完成
//     导致的旧内容后写覆盖。
//  2. 加载代次护栏：loadFile 发起前 bumpLoadEpoch() 作废在途/排队的加载类
//     转换（tiedToLoadEpoch=true），其完成回调被丢弃——快速连续重载不再把
//     旧内容灌进编辑器；保存/模式切换类任务不受代次影响（新加载不作废
//     用户显式请求的保存/切换）。
//  3. worker 只碰按值捕获的纯数据（不得捕获 this/Qt 控件）；队列析构（随
//     EditorTab）后在途结果直接丢弃、排队任务随之销毁——完成回调里捕获的
//     tab/窗口指针因此不会悬垂执行。
class ConversionQueue : public QObject {
    Q_OBJECT
public:
    // 转换函数：在 worker 线程执行，只读按值捕获的纯数据
    using ConvFn = std::function<QString()>;
    // 完成回调：在 GUI 线程执行；convMs 为 worker 实测转换耗时（不含排队）
    using Done = std::function<void(const QString &result, qint64 convMs)>;

    explicit ConversionQueue(QObject *parent = nullptr);

    // 加载代次 +1 并返回新代次，同时清掉已过期的排队任务（完成回调反正
    // 会被丢弃，留着只是白排一轮队）。loadFile 发起「新内容作废旧转换」时调用
    quint64 bumpLoadEpoch();

    // 转换入队（FIFO 串行）。tiedToLoadEpoch=true 时，完成回调仅在代次
    // 未再变化的情况下执行
    void enqueue(ConvFn conv, Done done, bool tiedToLoadEpoch = false);

signals:
    // 队列完全排空（最后一次完成回调执行后）——测试观测点
    void drained();

private:
    struct Task {
        ConvFn conv;
        Done done;
        bool tiedToLoadEpoch = false;
        quint64 loadEpoch = 0;   // 入队时的加载代次（仅 tiedToLoadEpoch 时比对）
    };

    void pump();

    QList<Task> m_queue;
    Task m_running;
    bool m_active = false;
    quint64 m_loadEpoch = 0;
    QFutureWatcher<QPair<QString, qint64>> m_watcher;
};

#endif // MD_EDITOR_CONVERSIONQUEUE_H
