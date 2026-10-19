#include "ThemeManager.h"
#include "PlatformChrome.h"

#include <QApplication>
#include <QColor>
#include <QFile>
#include <QRegularExpression>

ThemeManager *ThemeManager::s_instance = nullptr;

ThemeManager::ThemeManager(QObject *parent)
    : QObject(parent)
    , m_currentTheme("")
{
    s_instance = this;
}

ThemeManager::~ThemeManager()
{
    if (s_instance == this)
        s_instance = nullptr;
}

QString ThemeManager::currentTheme() const
{
    return m_currentTheme;
}

ThemeManager *ThemeManager::instance()
{
    return s_instance;
}

QColor ThemeManager::tokenColor(const QString &name) const
{
    const auto it = m_tokens.constFind(name);
    if (it == m_tokens.constEnd())
        return QColor();

    const QColor hex(it.value());
    if (hex.isValid())
        return hex;

    // rgba(r,g,b,a) 形式（shadow 等），QColor 命名色不支持，手动解析
    static const QRegularExpression rx(
        QStringLiteral("rgba\\((\\d+),(\\d+),(\\d+),(\\d+)\\)"));
    const auto m = rx.match(it.value());
    if (!m.hasMatch())
        return QColor();
    return QColor(m.captured(1).toInt(), m.captured(2).toInt(),
                  m.captured(3).toInt(), m.captured(4).toInt());
}

void ThemeManager::switchTheme(const QString &name)
{
    if (name == m_currentTheme)
        return;

    applyTheme(name);
    m_currentTheme = name;
    emit themeChanged(name);
}

void ThemeManager::applyTheme(const QString &name)
{
    // 令牌表缺失时静默容错（与旧 .qss 缺失行为一致，见 test_application）
    m_tokens.clear();
    QFile tokensFile(QStringLiteral(":/themes/%1.tokens").arg(name));
    if (tokensFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const QStringList lines = QString::fromUtf8(tokensFile.readAll()).split(QLatin1Char('\n'));
        for (const QString &line : lines) {
            const QString trimmed = line.trimmed();
            if (trimmed.isEmpty() || trimmed.startsWith(QLatin1Char('#')))
                continue;
            const int eq = trimmed.indexOf(QLatin1Char('='));
            if (eq <= 0)
                continue;
            m_tokens.insert(trimmed.left(eq).trimmed(), trimmed.mid(eq + 1).trimmed());
        }
        tokensFile.close();
    }

    // Mica 生效时窗口基底与 chrome 系令牌转半透明，透出系统材质
    //（仅 Windows 11 22H2+，其余平台 isMicaActive() 恒 false）
    if (PlatformChrome::isMicaActive()) {
        static const char *kTranslucent[] = { "windowBg", "chrome", "sideBg" };
        for (const char *key : kTranslucent) {
            const QColor c = tokenColor(QLatin1String(key));
            if (c.isValid())
                m_tokens.insert(QLatin1String(key),
                                QStringLiteral("rgba(%1,%2,%3,200)")
                                    .arg(c.red()).arg(c.green()).arg(c.blue()));
        }
    }

    QFile tplFile(QStringLiteral(":/themes/template.qss"));
    if (!tplFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        m_tokens.clear();
        return;
    }
    QString styleSheet = QString::fromUtf8(tplFile.readAll());
    tplFile.close();

    for (auto it = m_tokens.constBegin(); it != m_tokens.constEnd(); ++it)
        styleSheet.replace(QLatin1Char('@') + it.key() + QLatin1Char('@'), it.value());

    // 残留占位符 = 模板与令牌表键集合不一致，尽早暴露
    static const QRegularExpression leftover(QStringLiteral("@[A-Za-z][A-Za-z0-9_]*@"));
    const auto m = leftover.match(styleSheet);
    if (m.hasMatch())
        qWarning("ThemeManager: 模板存在未展开占位符 %s（检查 themes/*.tokens 键集合）",
                 qPrintable(m.captured(0)));

    qApp->setStyleSheet(styleSheet);
}
