#include <QtTest>
#include <QFileOpenEvent>
#include "app/Application.h"
#include "app/SingleInstanceGuard.h"

// Application::event() 对 QEvent::FileOpen 的转发（macOS Finder 双击/「打开方式」链路，
// 文件经 Apple Event 到达而非 argv）。QTEST_APPLESS_MAIN：不创建默认 QApplication，
// 由测试自建 Application 实例（仿 test_pdfexporter 的自建 app 模式）。
// ThemeManager::loadThemeFile 对 qrc 缺失静默容错（open 失败即跳过），
// 测试目标无需链接资源文件。
class TestApplication : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanupTestCase();
    void fileOpenEventEmitsSignal();
    void emptyFileOpenEventEmitsNothing();
    void singleInstanceAcquireAndForward();

private:
    Application *m_app = nullptr;
};

void TestApplication::initTestCase()
{
    // argv 缓冲须比 Application 存续期长（QApplication 可能消费 argv 内容）
    static char argv0[] = "test_application";
    static char *argv[] = { argv0, nullptr };
    static int argc = 1;
    m_app = new Application(argc, argv);
}

void TestApplication::cleanupTestCase()
{
    delete m_app;
    m_app = nullptr;
}

void TestApplication::fileOpenEventEmitsSignal()
{
    QSignalSpy spy(m_app, &Application::fileOpenRequested);
    QCoreApplication::postEvent(m_app,
        new QFileOpenEvent(QStringLiteral("/tmp/md-editor-claude/test.md")));
    QCoreApplication::sendPostedEvents();
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("/tmp/md-editor-claude/test.md"));
}

void TestApplication::emptyFileOpenEventEmitsNothing()
{
    QSignalSpy spy(m_app, &Application::fileOpenRequested);
    QCoreApplication::postEvent(m_app, new QFileOpenEvent(QString()));
    QCoreApplication::sendPostedEvents();
    QCOMPARE(spy.count(), 0);
}

// 单实例守卫：主实例 acquire 成功；第二个实例 acquire 失败且待开路径被
// 转发回主实例（空路径的纯聚焦请求同样转发）。socket 事件循环靠
// processEvents 驱动（测试无 exec）
void TestApplication::singleInstanceAcquireAndForward()
{
    SingleInstanceGuard primary;
    QVERIFY(primary.acquire(QString()));

    // 次实例：让位 + 路径转发
    QSignalSpy spy(&primary, &SingleInstanceGuard::instanceFileOpenRequested);
    SingleInstanceGuard secondary;
    QVERIFY(!secondary.acquire(QStringLiteral("/tmp/md-editor-claude/单实例.md")));
    for (int i = 0; i < 20 && spy.count() == 0; ++i)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toString(),
             QStringLiteral("/tmp/md-editor-claude/单实例.md"));

    // 纯聚焦（空路径）转发
    QSignalSpy spy2(&primary, &SingleInstanceGuard::instanceFileOpenRequested);
    SingleInstanceGuard tertiary;
    QVERIFY(!tertiary.acquire(QString()));
    for (int i = 0; i < 20 && spy2.count() == 0; ++i)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    QCOMPARE(spy2.count(), 1);
    QCOMPARE(spy2.at(0).at(0).toString(), QString());
}

QTEST_APPLESS_MAIN(TestApplication)
#include "test_application.moc"
