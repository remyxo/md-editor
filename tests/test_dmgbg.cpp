#include <QtTest>
#include <QFile>
#include <QImage>
#include <QColor>
#include <QTemporaryDir>
#include <QProcess>
#include <QDir>
#include <QFileInfo>

// gen-dmg-background.py——DMG「拖拽安装」布局的背景图生成器（纯 Python 标准库，
// 运行器零依赖）。本测试驱动其核心契约：
//   1. 输出是合法 PNG（QImage 能解码，尺寸正确）
//   2. 渐变背景：顶行与底行颜色不同且都是浅色系（窗口观感）
//   3. 箭头提示：左右图标连线中点区域存在明显非背景色像素（拖拽方向指引）
class TestDmgBackground : public QObject {
    Q_OBJECT
private slots:
    void generatesValidPngWithGradientAndArrow();

private:
    // QTEST_APPLESS_MAIN 无 QCoreApplication 实例，applicationDirPath() 返回空——
    // 用编译期 __FILE__ 的绝对路径定位脚本（tests/ → ../scripts/）
    QString scriptPath() const {
        return QFileInfo(QStringLiteral(__FILE__)).dir().filePath("../scripts/gen-dmg-background.py");
    }
};

void TestDmgBackground::generatesValidPngWithGradientAndArrow()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString out = dir.filePath("background.png");

    QProcess py;
    py.start("python3", {scriptPath(), out, "660", "400"});
    QVERIFY2(py.waitForFinished(10000),
             ("python3 运行失败: " + py.errorString()).toUtf8());
    QCOMPARE(py.exitStatus(), QProcess::NormalExit);
    QCOMPARE(py.exitCode(), 0);

    QImage img(out);
    QVERIFY2(!img.isNull(), "输出的不是合法 PNG");
    QCOMPARE(img.width(), 660);
    QCOMPARE(img.height(), 400);

    // 渐变：顶/底行中点颜色可区分（垂直渐变）
    const QColor top = img.pixelColor(330, 2);
    const QColor bottom = img.pixelColor(330, 397);
    QVERIFY(top != bottom);
    // 浅色系（不干扰 Finder 图标文字可读性）
    QVERIFY(top.lightness() > 200);
    QVERIFY(bottom.lightness() > 180);

    // 箭头：左图标(≈175,190)与右图标(≈485,190)之间应有一排非渐变背景色像素。
    // 渐变是逐行均匀的——同列上下 ±40px 处的颜色应几乎一致；箭头所在行则明显偏离
    const QColor above = img.pixelColor(330, 150);
    const QColor below = img.pixelColor(330, 230);
    const QColor mid = img.pixelColor(330, 190);
    const int verticalDrift = qAbs(above.lightness() - below.lightness());
    QVERIFY2(verticalDrift < 6, "对照区自身应近乎均匀（渐变验证前提）");
    QVERIFY2(qAbs(mid.lightness() - above.lightness()) > verticalDrift + 20,
             "中点应有明显深于背景的箭头像素");
}

QTEST_APPLESS_MAIN(TestDmgBackground)
#include "test_dmgbg.moc"
