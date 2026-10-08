#include <QtTest>
#include "ui/MenuBar.h"
#include <QAction>
#include <QMenu>
#include <QSignalSpy>

// 插入菜单公式入口（行内/块级）与导出 DOCX 快捷键的菜单契约回归。
// 背景：块级公式的 Ctrl+Shift+M 原由 MainWindow QShortcut 承载，本次改由
// 菜单 QAction 携带（双注册会报 "Ambiguous shortcut"）——本测试锁定菜单侧
// 文案/快捷键/邻接次序/信号，防止改名或接线回归。
class TestMenuBar : public QObject {
    Q_OBJECT
private slots:
    void insertMenu_inlineAndBlockMath();
    void export_shortcuts();
    void mathActions_emitSignals();

private:
    static QMenu *menuByTitle(QMenuBar &mb, const QString &title);
    static QAction *actionByText(QMenuBar &mb, const QString &text);
};

QMenu *TestMenuBar::menuByTitle(QMenuBar &mb, const QString &title)
{
    const QList<QMenu *> menus = mb.findChildren<QMenu *>();
    for (QMenu *m : menus) {
        if (m->title() == title) return m;
    }
    return nullptr;
}

QAction *TestMenuBar::actionByText(QMenuBar &mb, const QString &text)
{
    const QList<QMenu *> menus = mb.findChildren<QMenu *>();
    for (QMenu *m : menus) {
        const QList<QAction *> acts = m->actions();
        for (QAction *a : acts) {
            if (a->text() == text) return a;
        }
    }
    return nullptr;
}

void TestMenuBar::insertMenu_inlineAndBlockMath()
{
    MenuBar mb;
    QMenu *insert = menuByTitle(mb, QStringLiteral("插入(&I)"));
    QVERIFY2(insert, "未找到插入菜单");

    const QList<QAction *> acts = insert->actions();
    int inlineIdx = -1, blockIdx = -1;
    for (int i = 0; i < acts.size(); ++i) {
        if (acts[i]->text() == QStringLiteral("行内公式(&M)")) inlineIdx = i;
        if (acts[i]->text() == QStringLiteral("块级公式(&B)")) blockIdx = i;
    }
    QVERIFY2(inlineIdx >= 0, "插入菜单缺少「行内公式(&M)」");
    QVERIFY2(blockIdx >= 0, "插入菜单缺少「块级公式(&B)」");
    // 块级公式紧邻行内公式之下
    QCOMPARE(blockIdx, inlineIdx + 1);
    // 快捷键（PortableText 输出与平台无关，macOS 下亦为 Ctrl+… 字面）
    QCOMPARE(acts[inlineIdx]->shortcut().toString(QKeySequence::PortableText),
             QStringLiteral("Ctrl+M"));
    QCOMPARE(acts[blockIdx]->shortcut().toString(QKeySequence::PortableText),
             QStringLiteral("Ctrl+Shift+M"));
    // 旧文案「公式(&M)」已不存在
    QVERIFY2(!actionByText(mb, QStringLiteral("公式(&M)")), "旧「公式(&M)」文案残留");
}

void TestMenuBar::export_shortcuts()
{
    MenuBar mb;
    QAction *docx = actionByText(mb, QStringLiteral("导出 DOCX..."));
    QVERIFY2(docx, "未找到「导出 DOCX...」菜单项");
    QCOMPARE(docx->shortcut().toString(QKeySequence::PortableText),
             QStringLiteral("Ctrl+Shift+D"));

    QAction *html = actionByText(mb, QStringLiteral("导出 HTML..."));
    QVERIFY2(html, "未找到「导出 HTML...」菜单项");
    QCOMPARE(html->shortcut().toString(QKeySequence::PortableText),
             QStringLiteral("Ctrl+Shift+H"));
}

void TestMenuBar::mathActions_emitSignals()
{
    MenuBar mb;
    QAction *inlineAct = actionByText(mb, QStringLiteral("行内公式(&M)"));
    QAction *blockAct = actionByText(mb, QStringLiteral("块级公式(&B)"));
    QVERIFY(inlineAct);
    QVERIFY(blockAct);

    QSignalSpy inlineSpy(&mb, &MenuBar::insertMath);
    QSignalSpy blockSpy(&mb, &MenuBar::insertMathBlock);
    inlineAct->trigger();
    blockAct->trigger();
    QCOMPARE(inlineSpy.count(), 1);
    QCOMPARE(blockSpy.count(), 1);
}

QTEST_MAIN(TestMenuBar)
#include "test_menubar.moc"
