#ifndef MD_EDITOR_SYSTEMTHEMEWATCHER_H
#define MD_EDITOR_SYSTEMTHEMEWATCHER_H

#include <QObject>
#include <QString>
#include <QStyleHints>

// 系统深浅色监听器：统一走 Qt 6.5+ 的 QStyleHints::colorScheme()，
// 跨平台（Windows / macOS / Linux）原生跟随系统深浅色。
//
// 设计要点：
// - 生产环境 createForProduction() 读取初始 colorScheme 并订阅 colorSchemeChanged
// - 无法确定（Unknown）或 QStyleHints 不可用时降级为 light，不报错
// - applyColorScheme() 为纯翻译 + 变化检测，公开以便单测注入（生产由信号驱动）
class SystemThemeWatcher : public QObject {
    Q_OBJECT
public:
    explicit SystemThemeWatcher(QObject *parent = nullptr);
    ~SystemThemeWatcher() override;

    // 生产工厂：读取初始 colorScheme 并订阅 colorSchemeChanged
    static SystemThemeWatcher *createForProduction(QObject *parent = nullptr);

    // 当前系统主题：light 或 dark；降级模式恒 light
    QString currentSystemTheme() const { return m_currentTheme; }

    // 已订阅系统颜色方案变化、可实时跟随时为 true
    bool isActive() const { return m_active; }

    // 翻译函数：Qt::ColorScheme → 实际主题名（Dark→dark，其余→light）
    static QString themeFromColorScheme(Qt::ColorScheme scheme);

    // 应用颜色方案并更新主题，仅当实际主题变化时发射 systemThemeChanged。
    // 供测试注入：模拟系统颜色方案变化（生产环境由 colorSchemeChanged 信号驱动）
    void applyColorScheme(Qt::ColorScheme scheme);

signals:
    // 仅当实际主题与上次不同才发射
    void systemThemeChanged(const QString &actualTheme);

private:
    void connectToSystem();   // 读取初始值 + 订阅 colorSchemeChanged（由 createForProduction 调用）

    bool m_active = false;
    QString m_currentTheme = QStringLiteral("light");
};

#endif // MD_EDITOR_SYSTEMTHEMEWATCHER_H
