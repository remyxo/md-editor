#include "Application.h"

Application::Application(int &argc, char **argv)
    : QApplication(argc, argv)
{
    setApplicationName("md-editor");
    setApplicationDisplayName("Markdown Editor");
    setApplicationVersion("0.1.0");
    setOrganizationName("md-editor");
}

Application *Application::instance()
{
    return qobject_cast<Application *>(QCoreApplication::instance());
}
