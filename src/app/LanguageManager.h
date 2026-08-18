#ifndef MD_EDITOR_LANGUAGEMANAGER_H
#define MD_EDITOR_LANGUAGEMANAGER_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QTranslator>

// 界面语言管理器：装载/卸载 QTranslator，持久化 appearance/language，
// 发 languageChanged 信号。源语言=中文（源字符串直通），英文装载 en_US.qm。
class LanguageManager : public QObject {
    Q_OBJECT
public:
    explicit LanguageManager(QObject *parent = nullptr);

    QString currentLanguage() const;        // "zh_CN" | "en_US"
    QStringList availableLanguages() const; // {"zh_CN", "en_US"}

public slots:
    void switchLanguage(const QString &lang);

signals:
    void languageChanged(const QString &lang);

private:
    QString m_currentLanguage;
    QTranslator m_translator;  // 成员持有：installTranslator 不接管所有权
};

#endif // MD_EDITOR_LANGUAGEMANAGER_H
