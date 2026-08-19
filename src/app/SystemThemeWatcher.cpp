#include "SystemThemeWatcher.h"

#include <QGuiApplication>
#include <QDebug>

SystemThemeWatcher::SystemThemeWatcher(QObject *parent)
    : QObject(parent)
{
}

SystemThemeWatcher::~SystemThemeWatcher() = default;

QString SystemThemeWatcher::themeFromColorScheme(Qt::ColorScheme scheme)
{
    return scheme == Qt::ColorScheme::Dark ? QStringLiteral("dark") : QStringLiteral("light");
}

SystemThemeWatcher *SystemThemeWatcher::createForProduction(QObject *parent)
{
    auto *watcher = new SystemThemeWatcher(parent);
    watcher->connectToSystem();
    return watcher;
}

void SystemThemeWatcher::connectToSystem()
{
    QStyleHints *hints = QGuiApplication::styleHints();
    if (!hints) {
        qWarning() << "[SystemThemeWatcher] 无法获取 QStyleHints，跟随系统降级为亮色";
        return;
    }

    // 初始读取：Unknown 经 themeFromColorScheme 归一为 light（降级不报错）
    applyColorScheme(hints->colorScheme());
    m_active = true;

    // 订阅后续变化（Qt 6.5+）。连接失败仅失去实时性，初始值仍有效
    connect(hints, &QStyleHints::colorSchemeChanged,
            this, &SystemThemeWatcher::applyColorScheme);
}

void SystemThemeWatcher::applyColorScheme(Qt::ColorScheme scheme)
{
    const QString newTheme = themeFromColorScheme(scheme);
    if (newTheme != m_currentTheme) {
        m_currentTheme = newTheme;
        emit systemThemeChanged(newTheme);
    }
}
