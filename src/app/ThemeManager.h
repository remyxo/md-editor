#ifndef MD_EDITOR_THEMEMANAGER_H
#define MD_EDITOR_THEMEMANAGER_H

#include <QObject>
#include <QHash>
#include <QString>

class QColor;

class ThemeManager : public QObject {
    Q_OBJECT
public:
    explicit ThemeManager(QObject *parent = nullptr);
    ~ThemeManager() override;

    QString currentTheme() const;

    // 进程内唯一实例（Application 持有）；无实例时返回 nullptr
    static ThemeManager *instance();

    // 按令牌名取色（如 "chrome"/"accent"/"shadow"，键集见 themes/*.tokens）。
    // 主题未加载或令牌缺失时返回无效 QColor，调用方需给回退值。
    QColor tokenColor(const QString &name) const;

public slots:
    void switchTheme(const QString &name);

signals:
    void themeChanged(const QString &name);

private:
    void applyTheme(const QString &name);

    QString m_currentTheme;
    QHash<QString, QString> m_tokens;

    static ThemeManager *s_instance;
};

#endif // MD_EDITOR_THEMEMANAGER_H
