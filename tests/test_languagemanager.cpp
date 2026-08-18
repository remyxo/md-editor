#include <QtTest>
#include <QSettings>
#include <QDir>
#include "app/LanguageManager.h"

// LanguageManager 单测：状态/持久化/信号/幂等/未知语言忽略。
// 通过 QSettings 重定向到临时目录，避免污染真实用户配置。
class TestLanguageManager : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, QDir::tempPath());
    }

    void init() {
        QSettings s("md-editor", "md-editor");
        s.remove("appearance/language");
        s.sync();
    }

    void testAvailableLanguages() {
        LanguageManager m;
        QCOMPARE(m.availableLanguages(), QStringList({"zh_CN", "en_US"}));
    }

    void testDefaultIsChinese() {
        LanguageManager m;  // 无已存偏好 → zh_CN
        QCOMPARE(m.currentLanguage(), QString("zh_CN"));
    }

    void testSwitchAndPersist() {
        LanguageManager m;
        QSignalSpy spy(&m, &LanguageManager::languageChanged);
        m.switchLanguage("en_US");
        QCOMPARE(m.currentLanguage(), QString("en_US"));
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toString(), QString("en_US"));
        QCOMPARE(QSettings("md-editor", "md-editor").value("appearance/language").toString(),
                 QString("en_US"));
    }

    void testIdempotent() {
        LanguageManager m;
        QSignalSpy spy(&m, &LanguageManager::languageChanged);
        m.switchLanguage("en_US");
        m.switchLanguage("en_US");  // 同语言 → 直接返回
        QCOMPARE(spy.count(), 1);
    }

    void testUnknownLanguageIgnored() {
        LanguageManager m;
        QSignalSpy spy(&m, &LanguageManager::languageChanged);
        m.switchLanguage("fr_FR");
        QCOMPARE(m.currentLanguage(), QString("zh_CN"));  // 保持当前语言
        QCOMPARE(spy.count(), 0);
    }

    void testRestoreFromSettings() {
        QSettings("md-editor", "md-editor").setValue("appearance/language", "en_US");
        LanguageManager m;  // 构造时读已存偏好
        QCOMPARE(m.currentLanguage(), QString("en_US"));
    }
};

QTEST_MAIN(TestLanguageManager)
#include "test_languagemanager.moc"
