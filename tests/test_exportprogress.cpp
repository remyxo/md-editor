#include <QTest>
#include <QImage>
#include <QImageReader>
#include <QPushButton>
#include "../src/ui/ExportProgressDialog.h"

// 导出进度对话框回归（2026-09-21）：
// ① GIF 资源形状锁——尺寸/帧数/透明索引/每帧有墨。生成脚本曾把 d.arc 语句
//    挤出绘制循环体外，12 帧全变成 7×7 幽灵点（视觉几乎不可见），像素级自检
//    须进测试防再犯
// ② 状态机——导出期间 Esc/reject 不可关闭；错误态出现「关闭」按钮且不自动
//    关闭；成功自动关闭
class TestExportProgress : public QObject {
    Q_OBJECT
private slots:
    void testGifAssetShape() {
        QImageReader r(QStringLiteral(":/icons/export-progress.gif"));
        QVERIFY2(r.canRead(), "qrc 缺 export-progress.gif（app.qrc 未登记？）");
        QCOMPARE(r.size(), QSize(48, 48));
        const int n = r.imageCount();
        QVERIFY(n >= 8);   // 旋转动画帧数下限（成品 12 帧逐帧 30°）
        // 逐帧顺序 read（QImageReader 探测 imageCount 后 jumpToImage 会失败，
        // 顺序读是 GIF 多帧的可靠路径；read 返回 32-bpp 转换帧，判墨走 alpha）
        QImage prev;
        for (int i = 0; i < n; ++i) {
            const QImage fr = r.read();
            QVERIFY2(!fr.isNull(),
                     qPrintable(QStringLiteral("frame %1 解码失败").arg(i)));
            int ink = 0;
            for (int y = 0; y < fr.height(); ++y)
                for (int x = 0; x < fr.width(); ++x)
                    if (qAlpha(fr.pixel(x, y)) > 0)
                        ++ink;
            QVERIFY2(ink > 100,
                     qPrintable(QStringLiteral("frame %1 墨迹仅 %2px").arg(i).arg(ink)));
            if (i == 0)
                prev = fr;
        }
        // 透明底（GIF 透明索引在 read 后体现为 alpha=0 像素）：明暗主题下均
        // 无白底方块的前提
        bool hasTransparent = false;
        for (int y = 0; y < prev.height() && !hasTransparent; ++y)
            for (int x = 0; x < prev.width(); ++x)
                if (qAlpha(prev.pixel(x, y)) == 0) { hasTransparent = true; break; }
        QVERIFY2(hasTransparent, "帧无透明像素：动画会带不透明底色");
    }

    void testDialogStateTransitions() {
        ExportProgressDialog dlg;
        QPushButton *btn = dlg.findChild<QPushButton *>(
            QStringLiteral("closeBtn"));
        QVERIFY(btn != nullptr);
        dlg.show();
        QVERIFY(dlg.isVisible());
        QVERIFY(!btn->isVisibleTo(nullptr));   // 进行中无关闭按钮（不可取消）

        // 导出期间 Esc 无效（reject() 是 protected 覆写，走真实按键路径）
        QTest::keyClick(&dlg, Qt::Key_Escape);
        QVERIFY(dlg.isVisible());

        // 进度文案路径（不崩即可，无 UI 断言）
        dlg.showPagedProgress(3);
        dlg.showWriting(12);
        dlg.showWriting(0);
        QVERIFY(dlg.isVisible());

        // 成功自动关闭
        dlg.showFinished();
        QVERIFY(!dlg.isVisible());

        // 错误态：不自动关闭、出现关闭按钮，点击后关闭
        ExportProgressDialog err;
        QPushButton *errBtn = err.findChild<QPushButton *>(
            QStringLiteral("closeBtn"));
        err.show();
        err.showError(QStringLiteral("页码漂移"));
        QVERIFY(err.isVisible());               // 失败必须留住用户视线
        QVERIFY(errBtn->isVisibleTo(nullptr));
        QTest::keyClick(&err, Qt::Key_Escape);   // 错误态 Esc 等价关闭（放行）
        QVERIFY(!err.isVisible());
    }
};

QTEST_MAIN(TestExportProgress)
#include "test_exportprogress.moc"
