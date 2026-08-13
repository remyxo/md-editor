#include "app/Application.h"
#include "ui/MainWindow.h"
#include "core/LocalImageSchemeHandler.h"
#include <QLocale>
#include <QWebEngineProfile>
#include <QWebEngineUrlScheme>

// 注册 localimg:// 自定义协议。
// 必须在 QWebEngineProfile 首次创建（即 WebEngine 初始化）前调用，
// 否则协议对 Chromium 不可见、Handler 不会被调用。
//
// flags 取值依据（Qt 文档）：
// - 不设 LocalScheme：否则 localimg 会被当作"本地资源提供者"，只有带 LocalAccessAllowed
//   的协议才能加载它——而 qrc 是内建协议无此标记，于是 qrc 页面加载 localimg 会被拦
//   （"Not allowed to load local resource"，与当初 file:// 同样的坑）。
// - SecureScheme：标记为可信源，避免 qrc（安全源）页面把它当作混合内容拦截。
// - CorsEnabled：跨源/跨协议加载所必需（"includes access from other schemes"），
//   QWebEngineUrlRequestJob::reply 会据此自动生成 CORS 头。
static void registerLocalImageScheme()
{
    QWebEngineUrlScheme scheme(kLocalImageScheme);
    scheme.setFlags(QWebEngineUrlScheme::SecureScheme
                    | QWebEngineUrlScheme::CorsEnabled);
    QWebEngineUrlScheme::registerScheme(scheme);
}

int main(int argc, char *argv[])
{
    // 设置中文语言环境
    QLocale::setDefault(QLocale(QLocale::Chinese, QLocale::China));

    // 自定义协议注册：必须在 WebEngine 初始化（Application 内）之前
    registerLocalImageScheme();

    Application app(argc, argv);

    // QWebEngineProfile 必须在 QApplication 之后初始化
    // 在默认 profile 上挂载 localimg:// 处理器（随 app 存活，覆盖所有 EditorTab）
    auto profile = QWebEngineProfile::defaultProfile();
    LocalImageSchemeHandler *imgHandler = new LocalImageSchemeHandler(&app);
    profile->installUrlSchemeHandler(kLocalImageScheme, imgHandler);
    profile->setHttpAcceptLanguage("zh-CN,zh;q=0.9");

    MainWindow window;
    window.show();

    return app.exec();
}
