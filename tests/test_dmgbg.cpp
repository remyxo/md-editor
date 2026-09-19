#include <QtTest>
#include <QImage>
#include <QColor>
#include <QFileInfo>

// DMG 拖拽安装背景资产（scripts/dmg-background.png）——CI 打包时原样拷贝进
// 挂载卷，本测试锁定其关键设计要素，防止资产被意外替换/重生成走样：
//   1. @2x 1320×800 + 144dpi（pHYs 5669 px/m）——Finder 按 660×400 逻辑显示
//   2. 上下微渐变（近白，顶浅底深）
//   3. 箭头：图标中心连线中点处的中灰横带（与图标中心 y 对齐，@2x y=380）
//   4. 箭头下方一行中文提示（暗像素存在且水平居中）
// 设计改动须本地重跑 gen-dmg-background.py 重新生成资产并同步本断言；
// 图标位置对应 package-macos.sh AppleScript 的 {185,190}/{475,190}
// （Finder position = 图标视觉中心，真机截图校准）
class TestDmgBackground : public QObject {
    Q_OBJECT
private slots:
    void committedAssetMatchesDesign();

private:
    // QTEST_APPLESS_MAIN 无 QCoreApplication 实例，applicationDirPath() 返回空——
    // 用编译期 __FILE__ 的绝对路径定位资产（tests/ → ../scripts/）
    QString assetPath() const {
        return QFileInfo(QStringLiteral(__FILE__)).dir().filePath(
            QStringLiteral("../scripts/dmg-background.png"));
    }
};

void TestDmgBackground::committedAssetMatchesDesign()
{
    QImage img(assetPath());
    QVERIFY2(!img.isNull(), "scripts/dmg-background.png 不是合法 PNG");
    img = img.convertToFormat(QImage::Format_RGB32);

    // @2x 尺寸（Finder 按 660×400 逻辑显示）
    QCOMPARE(img.size(), QSize(1320, 800));
    // 144dpi = 5669 像素/米——Retina 标注，Finder 据此缩放背景
    QCOMPARE(img.dotsPerMeterX(), 5669);
    QCOMPARE(img.dotsPerMeterY(), 5669);

    // 上下微渐变：顶部浅于底部，且都保持近白（不干扰图标可读性）
    const QColor top = img.pixelColor(660, 40);
    const QColor bottom = img.pixelColor(660, 760);
    QVERIFY(top.lightness() > bottom.lightness());
    QVERIFY(top.lightness() > 240);
    QVERIFY(bottom.lightness() > 230);

    // 箭头：图标中心连线中点（x=660@2x）的箭杆色 ≈ (150,154,161)；
    // 箭杆正上方 60px 处应为纯背景（箭头未越界、非通栏大箭头）
    const QColor shaft = img.pixelColor(634, 380);
    const QColor above = img.pixelColor(634, 320);
    QVERIFY2(qAbs(shaft.red() - 150) < 20
             && qAbs(shaft.green() - 154) < 20
             && qAbs(shaft.blue() - 161) < 20,
             "箭杆中心应为设计灰色 (150,154,161)");
    QVERIFY(above.lightness() - shaft.lightness() > 40);

    // 中文提示：箭头下方 y∈[590,680] 存在明显深于箭头/背景的文字像素，
    // 且水平重心居中（窗口中线 660±40）
    int dark = 0, minX = 1 << 30, maxX = -1;
    for (int y = 590; y < 680; ++y) {
        for (int x = 300; x < 1020; ++x) {
            if (img.pixelColor(x, y).lightness() < 120) {
                ++dark;
                minX = qMin(minX, x);
                maxX = qMax(maxX, x);
            }
        }
    }
    QVERIFY2(dark > 800, "箭头下方应有一行中文提示文字");
    QVERIFY2(qAbs((minX + maxX) / 2 - 660) <= 40,
             "提示文字应水平居中");
}

QTEST_APPLESS_MAIN(TestDmgBackground)
#include "test_dmgbg.moc"
