// gfm/自有语法语料全链路收敛（小棘轮：diverge 登记制，只减不增——同
// test_commonmark_corpus 机制）。原设计 diverge=0 严格；2026-09-28 执行中
// 发现 hr-links 裸 URL 族真实 diverge（Qt 不识别 <https://…> autolink 语法，
// 链接性两趟劣化为转义字面，同 commonmark ex.611 族），改用小棘轮跟踪，
// 修复后出表即恢复严格。
// 链路：md → toHtml → simulateTipTapSerialization → fromHtml（贴近真实
// TipTap 的属性转义环节；重叠 mark 裂链场景由 test_roundtrip_matrix 覆盖，
// 本语料不含该族）
#include <QtTest>
#include <QFile>
#include <QRegularExpression>
#include <QSet>
#include "../src/core/MarkdownHandler.h"
#include "tiptap_serialize.h"

#ifndef GFM_CORPUS_PATH
#define GFM_CORPUS_PATH "gfm-corpus.md"
#endif
#ifndef GFM_RATCHET_PATH
#define GFM_RATCHET_PATH "gfm-expected-diverge.txt"
#endif

class TestGfmCorpus : public QObject {
    Q_OBJECT
private slots:
    void convergence();
};

void TestGfmCorpus::convergence()
{
    QFile f(QStringLiteral(GFM_CORPUS_PATH));
    QVERIFY2(f.open(QIODevice::ReadOnly | QIODevice::Text), "gfm-corpus.md 缺失");
    const QString all = QString::fromUtf8(f.readAll());

    static const QRegularExpression re(
        QStringLiteral("<!-- corpus: ([a-z0-9-]+) -->"));
    auto it = re.globalMatch(all);
    QStringList names;
    QList<QString> bodies;
    while (it.hasNext()) {
        auto m = it.next();
        const int start = m.capturedEnd();
        int end = all.size();
        auto next = re.match(all, start);
        if (next.hasMatch()) end = next.capturedStart();
        names << m.captured(1);
        bodies << all.mid(start, end - start).trimmed();
    }
    QVERIFY2(names.size() >= 12, "语料节数不足（检查分节标记）");

    QSet<QString> ratchet;
    QFile rf(QStringLiteral(GFM_RATCHET_PATH));
    if (rf.open(QIODevice::ReadOnly | QIODevice::Text)) {
        for (const QString &line : QString::fromUtf8(rf.readAll()).split('\n')) {
            const QString t = line.section('#', 0, 0).trimmed();
            if (!t.isEmpty()) ratchet.insert(t);
        }
    }

    QSet<QString> fixed, converge, diverge;
    for (int i = 0; i < names.size(); ++i) {
        const QString &name = names[i];
        const QString md0 = bodies[i];
        const QString html1 = simulateTipTapSerialization(MarkdownHandler::toHtml(md0));
        const QString r1 = MarkdownHandler::fromHtml(html1);
        if (r1 == md0) { fixed.insert(name); continue; }   // 首趟保真
        const QString html2 = simulateTipTapSerialization(MarkdownHandler::toHtml(r1));
        const QString r2 = MarkdownHandler::fromHtml(html2);
        (r1 == r2 ? converge : diverge).insert(name);
        // 两趟收敛但首趟有归一：计入 converge（登记进 00-专项总纲「Wave 0 发现」供后续评估）
    }

    QStringList added, removed;
    for (const QString &n : diverge) if (!ratchet.contains(n))
        added << n;
    for (const QString &n : ratchet) if (!diverge.contains(n))
        removed << n;
    if (!added.isEmpty())
        qWarning("新增不收敛（回归，须修复转换）: %s", qPrintable(added.join(", ")));
    if (!removed.isEmpty())
        qWarning("已收敛可出表（请从 gfm-expected-diverge.txt 删除）: %s", qPrintable(removed.join(", ")));
    QVERIFY2(added.isEmpty() && removed.isEmpty(), "棘轮漂移（见上方清单）");

    qInfo("%s", qUtf8Printable(
        QString("gfm 语料 %1 节: 首趟保真 %2 / 两趟收敛 %3 / 发散 %4（棘轮登记 %5）")
            .arg(names.size()).arg(fixed.size())
            .arg(converge.size()).arg(diverge.size()).arg(ratchet.size())));
}
QTEST_MAIN(TestGfmCorpus)
#include "test_gfm_corpus.moc"
