#ifndef MD_EDITOR_APPLICATION_H
#define MD_EDITOR_APPLICATION_H

#include <QApplication>
#include "LanguageManager.h"
#include "ThemeManager.h"

class Application : public QApplication {
    Q_OBJECT
public:
    Application(int &argc, char **argv);
    static Application *instance();
    ThemeManager *themeManager() const;
    LanguageManager *languageManager() const;

private:
    ThemeManager *m_themeManager;
    LanguageManager *m_languageManager;
};

#endif // MD_EDITOR_APPLICATION_H
