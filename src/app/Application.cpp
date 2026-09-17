#include "Application.h"

#include <QFileOpenEvent>

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

bool Application::event(QEvent *e)
{
    if (e->type() == QEvent::FileOpen) {
        // 空 FileOpen（Apple Event 不带文件的个别场景）静默吞掉，不向下传递
        const QString path = static_cast<QFileOpenEvent *>(e)->file();
        if (!path.isEmpty())
            emit fileOpenRequested(path);
        return true;
    }
    return QApplication::event(e);
}
