#include <QtTest>
#include "app/MacInstallCheck.h"

// MacInstallCheck::isTransientLocation——macOS「从安装镜像（DMG 挂载卷）或
// Gatekeeper 转位目录运行」的路径判定，供 MainWindow 启动提示用（详见
// CHANGELOG 0.16.1：直接在挂载卷内运行 app 是 Launch Services 僵尸注册的
// 标准来源）。纯字符串判定，跨平台可测：Linux/Windows 路径恒 false
class TestMacInstallCheck : public QObject {
    Q_OBJECT
private slots:
    void detectsDmgMountAndTranslocation();
    void installedOrDevLocationsPass();
};

using MacInstallCheck::isTransientLocation;

void TestMacInstallCheck::detectsDmgMountAndTranslocation()
{
    // DMG 挂载卷内直跑（未拖到「应用程序」）
    QVERIFY(isTransientLocation(
        "/Volumes/md-editor/md-editor.app/Contents/MacOS"));
    QVERIFY(isTransientLocation(
        "/Volumes/安装盘/md-editor.app/Contents/MacOS"));
    // Gatekeeper 转位：从 Downloads 等处直跑时系统把 app 复制到的随机只读路径
    QVERIFY(isTransientLocation(
        "/AppTranslocation/7BF23A55-2F3D-4C1A-9D2E/md-editor.app/Contents/MacOS"));
    QVERIFY(isTransientLocation(
        "/AppTranslocation/ABC/d/Applications/md-editor.app/Contents/MacOS"));
    // 转位目录嵌在别的前缀下也算（系统版本差异）
    QVERIFY(isTransientLocation(
        "/private/var/folders/xx/AppTranslocation/T/md-editor.app/Contents/MacOS"));
    // 前缀须整段匹配：/Volumesfoo 不是挂载卷
    QVERIFY(!isTransientLocation("/Volumesfoo/md-editor.app/Contents/MacOS"));
    QVERIFY(!isTransientLocation("/Users/x/AppTranslocationHelper/Contents/MacOS"));
}

void TestMacInstallCheck::installedOrDevLocationsPass()
{
    QVERIFY(!isTransientLocation("/Applications/md-editor.app/Contents/MacOS"));
    QVERIFY(!isTransientLocation("/Users/x/Applications/md-editor.app/Contents/MacOS"));
    // Downloads 直跑（未转位时）不拦——提示只针对挂载卷/转位两类确定场景
    QVERIFY(!isTransientLocation("/Users/x/Downloads/md-editor.app/Contents/MacOS"));
    QVERIFY(!isTransientLocation("/opt/md-editor/bin"));
    QVERIFY(!isTransientLocation("C:\\Program Files\\md-editor\\md-editor.exe"));
    QVERIFY(!isTransientLocation("build/src"));   // 相对路径（开发构建）
    QVERIFY(!isTransientLocation(QString()));
}

QTEST_APPLESS_MAIN(TestMacInstallCheck)
#include "test_macinstallcheck.moc"
