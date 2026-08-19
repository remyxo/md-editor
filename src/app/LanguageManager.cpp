#include "LanguageManager.h"
#include <QApplication>
#include <QSettings>
#include <QDebug>

LanguageManager::LanguageManager(QObject *parent)
    : QObject(parent)
    , m_currentLanguage(QStringLiteral("zh_CN"))
{
    // 启动恢复：读偏好并装载（先于 MainWindow 构造 → 启动即正确语言）
    QSettings settings("md-editor", "md-editor");
    const QString saved = settings.value("appearance/language", "zh_CN").toString();
    qWarning() << "[Language] 启动恢复 saved=" << saved;
    switchLanguage(saved);
}

QString LanguageManager::currentLanguage() const
{
    return m_currentLanguage;
}

QStringList LanguageManager::availableLanguages() const
{
    return {QStringLiteral("zh_CN"), QStringLiteral("en_US")};
}

void LanguageManager::switchLanguage(const QString &lang)
{
    // 未知语言忽略（不改变状态、不发信号），与 availableLanguages() 保持一致
    if (!availableLanguages().contains(lang))
        return;

    qWarning() << "[Language] switchLanguage lang=" << lang
               << " current=" << m_currentLanguage;

    if (lang == m_currentLanguage) {
        // 幂等：语言未变时不重装 translator、不发信号，但仍落盘——否则默认的
        // zh_CN 从不写入配置，一直靠 settings.value(..., "zh_CN") 默认值兜底。
        // 一旦配置里残留 en_US（历史英文会话/旧 bug），用户即便在设置里点确定
        // 未改语言也无法把中文写回，重启后仍按 en_US 恢复。
        QSettings settings("md-editor", "md-editor");
        settings.setValue("appearance/language", lang);
        settings.sync();
        return;
    }

    // 卸载旧翻译（zh_CN 无翻译 = 源字符串直通）
    qApp->removeTranslator(&m_translator);

    if (lang == QStringLiteral("en_US")) {
        if (m_translator.load(QStringLiteral(":/i18n/md-editor_en_US.qm"))) {
            qApp->installTranslator(&m_translator);
        } else {
            // 资源缺失：降级（界面保持中文源字符串），但偏好仍记录
            qWarning() << "[LanguageManager] 加载 md-editor_en_US.qm 失败，界面保持中文";
        }
    }

    m_currentLanguage = lang;
    QSettings settings("md-editor", "md-editor");
    settings.setValue("appearance/language", lang);
    settings.sync();

    emit languageChanged(lang);
}
