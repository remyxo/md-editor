// mdconv CLI 冒烟：子进程桥与直连 MarkdownHandler 输出一致
#include <QtTest>
#include <QProcess>
#include "core/MarkdownHandler.h"

class TestMdconv : public QObject {
    Q_OBJECT
private slots:
    void bridgeMatchesDirect();
};

void TestMdconv::bridgeMatchesDirect()
{
    const QString md = QStringLiteral("# 标题\n\n正文 `code`。\n");
    // 注：QVERIFY2/QCOMPARE 宏内含裸 return;，不能出现在返回 QByteArray 的
    // 函数（含 lambda）里，故 lambda 返回 void、结果经捕获变量带出
    QByteArray out;
    auto run = [&](const char *mode, const QByteArray &in) {
        QProcess p;
        p.start(QStringLiteral(MDCONV_PATH), { QLatin1String(mode) });
        p.write(in);
        p.closeWriteChannel();
        QVERIFY2(p.waitForFinished(30000), "mdconv 30s 未退出");
        QCOMPARE(p.exitStatus(), QProcess::NormalExit);
        QCOMPARE(p.exitCode(), 0);
        out = p.readAllStandardOutput();
    };
    run("tohtml", md.toUtf8());
    const QByteArray html1 = out;
    QVERIFY2(html1.contains("<h1"), "tohtml 应含 <h1>");
    QVERIFY2(html1.contains("<code"), "tohtml 应含 <code>");
    run("fromhtml", html1);
    const QString mdViaCli = QString::fromUtf8(out);
    QCOMPARE(mdViaCli, MarkdownHandler::fromHtml(QString::fromUtf8(html1)));
}
QTEST_MAIN(TestMdconv)
#include "test_mdconv.moc"
