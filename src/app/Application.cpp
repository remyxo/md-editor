#include "Application.h"

Application::Application(int &argc, char **argv)
    : QApplication(argc, argv)
{
    setApplicationName("md-editor");
    setApplicationDisplayName("Markdown Editor");
    setApplicationVersion("0.1.0");
    setOrganizationName("md-editor");

    m_themeManager = new ThemeManager(this);
    m_themeManager->switchTheme("light");
}

ThemeManager *Application::themeManager() const
{
    return m_themeManager;
}

Application *Application::instance()
{
    return qobject_cast<Application *>(QCoreApplication::instance());
}
