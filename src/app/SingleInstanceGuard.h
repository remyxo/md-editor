#ifndef MD_EDITOR_SINGLEINSTANCEGUARD_H
#define MD_EDITOR_SINGLEINSTANCEGUARD_H

#include <QObject>
#include <QLocalServer>

// Linux/Windows 单实例守卫（macOS 由 Launch Services 天然单实例，不经此类）。
// 机制：每用户一个 QLocalServer（Linux=Unix domain socket，Windows=命名管道）。
// 后续进程 acquire() 时先探测连接：连通即已有实例在跑——把待打开文件路径
// 写给它后返回 false，调用方自退；主实例收到路径经 instanceFileOpenRequested
// 转发开标签并尝试置前（Wayland 下窗口置前受限是已知边界，系统任务栏自行
// 提示）。未连通则清掉可能的崩溃残留 socket 后 listen，成为主实例。
class SingleInstanceGuard : public QObject {
    Q_OBJECT
public:
    explicit SingleInstanceGuard(QObject *parent = nullptr);

    // 尝试成为主实例。true = 本进程为主（server 已监听）；false = 已有实例
    // （fileToOpen 已转发给它，可为空——纯启动聚焦），调用方应立即退出。
    // listen 失败（权限异常等）返回 true 退化为多实例，不阻断启动
    bool acquire(const QString &fileToOpen);

signals:
    // 新实例转发的打开请求（路径可能为空 = 仅置前聚焦）。主实例侧连接后
    // 开标签/置前由调用方编排
    void instanceFileOpenRequested(const QString &path);

private slots:
    void onNewConnection();

private:
    static QString serverKey();

    QLocalServer m_server;
};

#endif // MD_EDITOR_SINGLEINSTANCEGUARD_H
