#include <QMainWindow>
#include "app/Application.h"

int main(int argc, char *argv[])
{
    Application app(argc, argv);
    app.themeManager()->switchTheme("light");

    QMainWindow window;
    window.setWindowTitle("Markdown Editor");
    window.resize(1280, 800);
    window.show();

    return app.exec();
}
