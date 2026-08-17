#ifndef MD_EDITOR_SYSTEMTHEMEWATCHER_H
#define MD_EDITOR_SYSTEMTHEMEWATCHER_H

#include <QObject>
#include <QString>

// 系统深浅色监听器：经 xdg-desktop-portal 的 Settings 接口
// （org.freedesktop.portal.Desktop / org.freedesktop.portal.Settings）
// 读取并订阅 color-scheme，翻译成实际主题名（light/dark）。
//
// 设计要点：
// - DBus 层抽象为可注入：生产环境 createForProduction() 建立真实绑定，
//   单测直接调 handleSettingChanged() 模拟 portal 信号
// - 无 portal 环境（DBus 连接失败/接口缺失）静默降级恒 light，不报错
class QDBusInterface;
class QDBusVariant;
class SystemThemeWatcher : public QObject {
    Q_OBJECT
public:
    explicit SystemThemeWatcher(QObject *parent = nullptr);
    ~SystemThemeWatcher() override;

    // 生产工厂：连接会话总线上的 portal 并完成初始读取。
    // portal 不可用时返回降级实例（isActive() == false）
    static SystemThemeWatcher *createForProduction(QObject *parent = nullptr);

    // 当前系统主题：light 或 dark；降级模式恒 light
    QString currentSystemTheme() const { return m_currentTheme; }

    // portal 可用且监听已建立时为 true
    bool isActive() const { return m_active; }

    // 翻译函数：portal color-scheme 值 → 实际主题名
    // 1=prefer-dark → dark；2=prefer-light、0=no-preference、其余 → light
    static QString themeFromColorScheme(uint value);

public slots:
    // 供测试注入：模拟 portal SettingChanged 信号（生产环境由真实 DBus 信号转发进来）。
    // 必须声明为 slot：bus.connect(SLOT(...)) 依赖元对象系统能查到该方法。
    // 参数用 QDBusVariant：QtDBus 要求信号参数为已注册的 DBus 元类型，
    // QVariant 无法注册（bus.connect 恒返回 false），QDBusVariant 才是
    // portal SettingChanged 信号 v 类型负载的正确映射
    void handleSettingChanged(const QString &namespace_, const QString &key, const QDBusVariant &value);

signals:
    // 仅当实际主题与上次不同才发射
    void systemThemeChanged(const QString &actualTheme);

private:
    void connectToPortal();   // 建立真实 DBus 绑定（由 createForProduction 调用）
    bool readInitialValue();  // 初始 ReadOne 读取，成功返回 true

    bool m_active = false;         // portal 绑定是否成功
    QString m_currentTheme = QStringLiteral("light");
    QDBusInterface *m_portal = nullptr;  // Settings 接口代理（可能为空 = 降级）
};

#endif // MD_EDITOR_SYSTEMTHEMEWATCHER_H
