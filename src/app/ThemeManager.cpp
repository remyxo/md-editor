#include "ThemeManager.h"
#include <QFile>
#include <QApplication>

ThemeManager::ThemeManager(QObject *parent)
    : QObject(parent)
    , m_currentTheme("")
{
}

QString ThemeManager::currentTheme() const
{
    return m_currentTheme;
}

QStringList ThemeManager::availableThemes() const
{
    return {"light", "dark"};
}

void ThemeManager::switchTheme(const QString &name)
{
    if (name == m_currentTheme)
        return;

    loadThemeFile(name);
    m_currentTheme = name;
    emit themeChanged(name);
}

void ThemeManager::loadThemeFile(const QString &name)
{
    QString path = QString(":/themes/%1.qss").arg(name);
    QFile file(path);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QString styleSheet = QString::fromUtf8(file.readAll());
        qApp->setStyleSheet(styleSheet);
        file.close();
    }
}
