// commonmark 0.31.2 全量语料（652 例）两趟收敛棘轮：
//   fixed   = toHtml→fromHtml 一趟即字节还原
//   converge = 第二趟不动（业界金标准，remark 652/652、prosemirror-markdown 98.6%）
//   diverge  = 两趟仍变 → 必须登记进 expected-diverge.txt（只减不增）；
//              新增 diverge = 回归（红），修复 diverge = 提示收缩棘轮（也红，逼更新）
#include <QtTest>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include "../src/core/MarkdownHandler.h"

#ifndef COMMONMARK_SPEC_PATH
#define COMMONMARK_SPEC_PATH "spec-0.31.2.json"
#endif
#ifndef COMMONMARK_RATCHET_PATH
#define COMMONMARK_RATCHET_PATH "expected-diverge.txt"
#endif

class TestCommonmarkCorpus : public QObject {
    Q_OBJECT
private slots:
    void convergence();
};

void TestCommonmarkCorpus::convergence()
{
    QFile sf(QStringLiteral(COMMONMARK_SPEC_PATH));
    QVERIFY2(sf.open(QIODevice::ReadOnly), "spec.json 缺失（见 fixtures/commonmark/README.md）");
    const auto arr = QJsonDocument::fromJson(sf.readAll()).array();
    QCOMPARE(arr.size(), 652);

    QSet<int> ratchet;
    QFile rf(QStringLiteral(COMMONMARK_RATCHET_PATH));
    if (rf.open(QIODevice::ReadOnly | QIODevice::Text)) {
        for (const QString &line : QString::fromUtf8(rf.readAll()).split('\n')) {
            const QString t = line.section('#', 0, 0).trimmed();
            if (!t.isEmpty()) ratchet.insert(t.toInt());
        }
    }

    QSet<int> fixed, converge, diverge;
    for (const QJsonValue &v : arr) {
        const QJsonObject o = v.toObject();
        const int num = o.value("example").toInt();
        const QString md = o.value("markdown").toString();
        const QString r1 = MarkdownHandler::fromHtml(MarkdownHandler::toHtml(md));
        if (r1 == md) { fixed.insert(num); continue; }
        const QString r2 = MarkdownHandler::fromHtml(MarkdownHandler::toHtml(r1));
        (r1 == r2 ? converge : diverge).insert(num);
    }

    QStringList added, removed;
    for (int n : diverge) if (!ratchet.contains(n))
        added << QString::number(n);
    for (int n : ratchet) if (!diverge.contains(n))
        removed << QString::number(n);
    if (!added.isEmpty())
        qWarning("新增不收敛（回归，须修复转换）: %s", qPrintable(added.join(", ")));
    if (!removed.isEmpty())
        qWarning("已收敛可出表（请从 expected-diverge.txt 删除）: %s", qPrintable(removed.join(", ")));
    QVERIFY2(added.isEmpty() && removed.isEmpty(), "棘轮漂移（见上方清单）");

    // 注：brief 原稿 qInfo("%1...", fixed.size(), ...) 为 QString::arg 风格占位符，
    // qInfo 是 printf 风格不会替换——改为 QString::arg 组装后输出（文案不变）
    qInfo("%s", qUtf8Printable(
        QString("commonmark 652: 首趟保真 %1 / 两趟收敛 %2 / 发散 %3（棘轮登记 %4）")
            .arg(fixed.size()).arg(converge.size())
            .arg(diverge.size()).arg(ratchet.size())));
}
QTEST_MAIN(TestCommonmarkCorpus)
#include "test_commonmark_corpus.moc"
