#include "Application.h"

Application::Application(int &argc, char **argv)
    : QApplication(argc, argv)
{
    setApplicationName("md-editor");
    setApplicationDisplayName("Markdown Editor");
    setApplicationVersion(MD_EDITOR_VERSION);
    setOrganizationName("md-editor");

    m_themeManager = new ThemeManager(this);
    m_themeManager->switchTheme("light");

    m_languageManager = new LanguageManager(this);
}

ThemeManager *Application::themeManager() const
{
    return m_themeManager;
}

LanguageManager *Application::languageManager() const
{
    return m_languageManager;
}

Application *Application::instance()
{
    return qobject_cast<Application *>(QCoreApplication::instance());
}
