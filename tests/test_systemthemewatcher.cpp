#include <QtTest>
#include <QDBusVariant>
#include "app/SystemThemeWatcher.h"

// SystemThemeWatcher 单测：翻译逻辑 + 变化信号 + 降级行为。
// DBus 层通过 handleSettingChanged 钩子模拟，不依赖真实 portal。
class TestSystemThemeWatcher : public QObject {
    Q_OBJECT
private slots:
    // 翻译函数：portal color-scheme 值 → 实际主题名
    void testThemeFromColorScheme() {
        QCOMPARE(SystemThemeWatcher::themeFromColorScheme(1), QString("dark"));
        QCOMPARE(SystemThemeWatcher::themeFromColorScheme(2), QString("light"));
        QCOMPARE(SystemThemeWatcher::themeFromColorScheme(0), QString("light"));  // 无偏好
    }

    // 无 portal 环境：构造即降级，恒 light，不崩溃
    void testFallbackWithoutPortal() {
        SystemThemeWatcher w;  // 默认构造 = 无 DBus 绑定（降级模式）
        QCOMPARE(w.isActive(), false);
        QCOMPARE(w.currentSystemTheme(), QString("light"));
    }

    // 模拟 portal 信号：只在值变化时发射 systemThemeChanged
    void testSignalOnlyOnChange() {
        SystemThemeWatcher w;
        QSignalSpy spy(&w, &SystemThemeWatcher::systemThemeChanged);
        w.handleSettingChanged("org.freedesktop.appearance", "color-scheme", QDBusVariant(QVariant(1)));
        QCOMPARE(spy.count(), 1);
        QCOMPARE(w.currentSystemTheme(), QString("dark"));
        // 同值再来一次：不重复发射
        w.handleSettingChanged("org.freedesktop.appearance", "color-scheme", QDBusVariant(QVariant(1)));
        QCOMPARE(spy.count(), 1);
        // 变回浅色：再次发射
        w.handleSettingChanged("org.freedesktop.appearance", "color-scheme", QDBusVariant(QVariant(2)));
        QCOMPARE(spy.count(), 2);
        QCOMPARE(w.currentSystemTheme(), QString("light"));
    }

    // namespace/key 不匹配的信号：过滤不响应
    void testIgnoresOtherSettings() {
        SystemThemeWatcher w;
        QSignalSpy spy(&w, &SystemThemeWatcher::systemThemeChanged);
        w.handleSettingChanged("org.freedesktop.appearance", "accent-color", QDBusVariant(QVariant(1)));
        w.handleSettingChanged("org.gnome.desktop", "color-scheme", QDBusVariant(QVariant(1)));
        QCOMPARE(spy.count(), 0);
        QCOMPARE(w.currentSystemTheme(), QString("light"));
    }
};

QTEST_MAIN(TestSystemThemeWatcher)
#include "test_systemthemewatcher.moc"
