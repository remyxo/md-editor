#include "app/Application.h"
#include "ui/MainWindow.h"
#include <QLocale>
#include <QWebEngineProfile>

int main(int argc, char *argv[])
{
    // 设置中文语言环境
    QLocale::setDefault(QLocale(QLocale::Chinese, QLocale::China));
    QWebEngineProfile::defaultProfile()->setHttpAcceptLanguage("zh-CN,zh;q=0.9");

    Application app(argc, argv);

    MainWindow window;
    window.show();

    return app.exec();
}
