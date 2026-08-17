#include "SystemThemeWatcher.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusArgument>
#include <QVariant>
#include <QVariantMap>
#include <QDebug>

// xdg-desktop-portal Settings 接口常量
static const char kPortalService[] = "org.freedesktop.portal.Desktop";
static const char kPortalPath[] = "/org/freedesktop/portal/desktop";
static const char kPortalSettingsIface[] = "org.freedesktop.portal.Settings";
static const char kAppearanceNamespace[] = "org.freedesktop.appearance";
static const char kColorSchemeKey[] = "color-scheme";

SystemThemeWatcher::SystemThemeWatcher(QObject *parent)
    : QObject(parent)
{
}

SystemThemeWatcher::~SystemThemeWatcher()
{
}

QString SystemThemeWatcher::themeFromColorScheme(uint value)
{
    // 1=prefer-dark；2=prefer-light、0=no-preference 均按亮色处理
    return value == 1 ? QStringLiteral("dark") : QStringLiteral("light");
}

SystemThemeWatcher *SystemThemeWatcher::createForProduction(QObject *parent)
{
    auto *watcher = new SystemThemeWatcher(parent);
    watcher->connectToPortal();
    return watcher;
}

void SystemThemeWatcher::connectToPortal()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    // 无 portal 服务则静默降级（恒 light）
    if (!bus.isConnected()
        || !bus.interface()->isServiceRegistered(
               QString::fromLatin1(kPortalService))) {
        qWarning() << "[SystemThemeWatcher] portal 不可用，跟随系统降级为亮色";
        return;
    }

    m_portal = new QDBusInterface(QString::fromLatin1(kPortalService),
                                  QString::fromLatin1(kPortalPath),
                                  QString::fromLatin1(kPortalSettingsIface),
                                  bus, this);

    if (!readInitialValue()) {
        // 初始读取失败：释放接口代理，降级
        qWarning() << "[SystemThemeWatcher] 读取 color-scheme 失败，降级为亮色";
        m_portal->deleteLater();
        m_portal = nullptr;
        return;
    }
    m_active = true;

    // 订阅 SettingChanged(namespace, key, value) 信号。
    // 个别 Qt 小版本上 QVariant 参数的信号匹配偏严格，connect 可能返回 false：
    // 此时仅失去实时性（初始读取值仍正确），qWarning 记录后保持 m_active，
    // 不视为失败，也不改用其他连接方式（SLOT 宏形式版本无关，6.4.2/6.8.x 均可工作）
    if (!bus.connect(QString::fromLatin1(kPortalService),
                     QString::fromLatin1(kPortalPath),
                     QString::fromLatin1(kPortalSettingsIface),
                     QStringLiteral("SettingChanged"),
                     this, SLOT(handleSettingChanged(QString,QString,QVariant)))) {
        qWarning() << "[SystemThemeWatcher] SettingChanged 订阅失败，仅失去实时性（初始值仍有效）";
    }
}

bool SystemThemeWatcher::readInitialValue()
{
    if (!m_portal || !m_portal->isValid())
        return false;

    // ReadOne(namespace, key) → QDBusVariant 外层 QVariant
    QDBusMessage reply = m_portal->call(QStringLiteral("ReadOne"),
                                        QString::fromLatin1(kAppearanceNamespace),
                                        QString::fromLatin1(kColorSchemeKey));
    if (reply.type() != QDBusMessage::ReplyMessage)
        return false;

    // 应答形如 QDBusVariant(v uint 1)：层层解包取 uint。
    // QDBusMessage 无 argumentAt()，取 arguments() 首元素
    const QVariant outer = reply.arguments().value(0);
    QVariant inner = outer;
    for (int i = 0; i < 3 && inner.userType() == qMetaTypeId<QDBusVariant>(); ++i) {
        inner = inner.value<QDBusVariant>().variant();
        // portal 的 ReadOne 返回 v 类型，剥一层 QDBusVariant 后可能还有一层
        // qdbusxml2cpp 风格的包装，循环内统一处理
    }
    bool ok = false;
    const uint value = inner.toUInt(&ok);
    if (!ok)
        return false;

    m_currentTheme = themeFromColorScheme(value);
    return true;
}

void SystemThemeWatcher::handleSettingChanged(const QString &namespace_,
                                              const QString &key,
                                              const QVariant &value)
{
    // 只关心 appearance 命名空间的 color-scheme
    if (namespace_ != QString::fromLatin1(kAppearanceNamespace)
        || key != QString::fromLatin1(kColorSchemeKey))
        return;

    // 信号负载是 QDBusVariant 包装的 uint
    QVariant inner = value;
    if (inner.userType() == qMetaTypeId<QDBusVariant>())
        inner = inner.value<QDBusVariant>().variant();
    bool ok = false;
    const uint schemeValue = inner.toUInt(&ok);
    if (!ok) {
        qWarning() << "[SystemThemeWatcher] color-scheme 信号负载异常，忽略";
        return;
    }

    const QString newTheme = themeFromColorScheme(schemeValue);
    if (newTheme != m_currentTheme) {
        m_currentTheme = newTheme;
        emit systemThemeChanged(newTheme);
    }
}
