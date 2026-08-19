#include <QtTest>
#include <QStyleHints>
#include "app/SystemThemeWatcher.h"

// SystemThemeWatcher 单测：翻译逻辑 + 变化信号 + 降级行为。
// 颜色方案变化经 applyColorScheme() 注入模拟，不依赖真实系统。
class TestSystemThemeWatcher : public QObject {
    Q_OBJECT
private slots:
    // 翻译函数：Qt::ColorScheme → 实际主题名
    void testThemeFromColorScheme() {
        QCOMPARE(SystemThemeWatcher::themeFromColorScheme(Qt::ColorScheme::Dark), QString("dark"));
        QCOMPARE(SystemThemeWatcher::themeFromColorScheme(Qt::ColorScheme::Light), QString("light"));
        QCOMPARE(SystemThemeWatcher::themeFromColorScheme(Qt::ColorScheme::Unknown), QString("light"));  // 无偏好
    }

    // 默认构造：未连接系统，恒 light，不崩溃
    void testFallbackInactive() {
        SystemThemeWatcher w;
        QCOMPARE(w.isActive(), false);
        QCOMPARE(w.currentSystemTheme(), QString("light"));
    }

    // 模拟颜色方案变化：只在值变化时发射 systemThemeChanged
    void testSignalOnlyOnChange() {
        SystemThemeWatcher w;
        QSignalSpy spy(&w, &SystemThemeWatcher::systemThemeChanged);
        w.applyColorScheme(Qt::ColorScheme::Dark);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(w.currentSystemTheme(), QString("dark"));
        // 同值再来一次：不重复发射
        w.applyColorScheme(Qt::ColorScheme::Dark);
        QCOMPARE(spy.count(), 1);
        // 变回浅色：再次发射
        w.applyColorScheme(Qt::ColorScheme::Light);
        QCOMPARE(spy.count(), 2);
        QCOMPARE(w.currentSystemTheme(), QString("light"));
    }
};

QTEST_MAIN(TestSystemThemeWatcher)
#include "test_systemthemewatcher.moc"
