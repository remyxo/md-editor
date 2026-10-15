#include "SingleInstanceGuard.h"

#include <QLocalSocket>

// 每用户独立的 server key：同机多用户各持自己的单实例。USER 未设的
// 非常规环境退化为全局唯一——单用户桌面场景不受影响
QString SingleInstanceGuard::serverKey()
{
    return QStringLiteral("md-editor-single-instance-")
           + qEnvironmentVariable("USER");
}

SingleInstanceGuard::SingleInstanceGuard(QObject *parent)
    : QObject(parent)
{
    connect(&m_server, &QLocalServer::newConnection,
            this, &SingleInstanceGuard::onNewConnection);
}

bool SingleInstanceGuard::acquire(const QString &fileToOpen)
{
    // 探测既有实例：300ms 内连通即让位
    QLocalSocket probe;
    probe.connectToServer(serverKey());
    if (probe.waitForConnected(300)) {
        QByteArray payload = fileToOpen.toUtf8();
        payload.append('\n');
        probe.write(payload);
        // 尽力写出：实例僵死时 1s 超时后自退，维持单实例语义
        probe.waitForBytesWritten(1000);
        probe.disconnectFromServer();
        return false;
    }

    // 未连通：首次启动，或上次异常退出残留的 socket 文件（Linux 上 listen
    // 会因文件已存在而失败）——清掉再听
    QLocalServer::removeServer(serverKey());
    // listen 失败不阻断启动：退化为无单实例（与既有行为一致）
    m_server.listen(serverKey());
    return true;
}

void SingleInstanceGuard::onNewConnection()
{
    QLocalSocket *client = m_server.nextPendingConnection();
    if (!client)
        return;
    client->setParent(this);
    // 路径帧以 '\n' 分隔（短路径单帧到达，分帧到达亦正确拼接）
    connect(client, &QLocalSocket::readyRead, this, [this, client]() {
        while (client->canReadLine()) {
            const QByteArray line = client->readLine();
            emit instanceFileOpenRequested(
                QString::fromUtf8(line.constData(), line.size() - 1));
        }
    });
    connect(client, &QLocalSocket::disconnected,
            client, &QLocalSocket::deleteLater);
}
