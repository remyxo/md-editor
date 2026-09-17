#include <QtTest>
#include <QFileOpenEvent>
#include "app/Application.h"

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

private:
    Application *m_app = nullptr;
};

void TestApplication::initTestCase()
{
    // argv 缓冲须比 Application 存续期长（QApplication 可能消费 argv 内容）
    static char argv0[] = "test_application";
    static char *argv[] = { argv0, nullptr };
    int argc = 1;
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

QTEST_APPLESS_MAIN(TestApplication)
#include "test_application.moc"
