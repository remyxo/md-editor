#include "app/Application.h"
#include "ui/MainWindow.h"
#include <QLocale>
#include <QWebEngineProfile>

int main(int argc, char *argv[])
{
    // 设置中文语言环境
    QLocale::setDefault(QLocale(QLocale::Chinese, QLocale::China));

    Application app(argc, argv);

    // QWebEngineProfile 必须在 QApplication 之后初始化
    QWebEngineProfile::defaultProfile()->setHttpAcceptLanguage("zh-CN,zh;q=0.9");

    MainWindow window;
    window.show();

    return app.exec();
}
