#ifndef MD_EDITOR_THEMEMANAGER_H
#define MD_EDITOR_THEMEMANAGER_H

#include <QObject>
#include <QString>

class ThemeManager : public QObject {
    Q_OBJECT
public:
    explicit ThemeManager(QObject *parent = nullptr);

    QString currentTheme() const;
    QStringList availableThemes() const;

public slots:
    void switchTheme(const QString &name);

signals:
    void themeChanged(const QString &name);

private:
    void loadThemeFile(const QString &name);
    QString m_currentTheme;
};

#endif // MD_EDITOR_THEMEMANAGER_H
