#ifndef MD_EDITOR_APPLICATION_H
#define MD_EDITOR_APPLICATION_H

#include <QApplication>
#include "ThemeManager.h"

class Application : public QApplication {
    Q_OBJECT
public:
    Application(int &argc, char **argv);
    static Application *instance();
    ThemeManager *themeManager() const;

private:
    ThemeManager *m_themeManager;
};

#endif // MD_EDITOR_APPLICATION_H
