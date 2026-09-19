#include "app/Application.h"
#include "app/MacInstallCheck.h"
#include "ui/MainWindow.h"
#include "core/LocalImageSchemeHandler.h"
#include <QLocale>
#include <QStringList>
#include <QAbstractButton>
#include <QMessageBox>
#include <QSysInfo>
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

    // macOS：从 DMG 挂载卷/Gatekeeper 转位目录运行——硬阻断，弹提示后退出，
    // 不进主界面。在镜像内直跑会让 Launch Services 登记多个应用路径（「打开
    // 方式」推荐列表置灰的根因，见 CHANGELOG 0.16.1）；Finder 双击本身无法
    // 拦截（系统行为），应用进程起来后立即自断是应用侧能做到的最强阻断。
    // 运行时判 macOS 而非 #ifdef：三平台都编译此段，不留只被 macOS CI 编译的死角
    if (QSysInfo::productType() == QStringLiteral("macos")
        && MacInstallCheck::isTransientLocation(QCoreApplication::applicationDirPath())) {
        QMessageBox box(QMessageBox::Warning, QStringLiteral("无法直接运行"),
            QStringLiteral("请先拖曳 md-editor 图标到 Applications 图标以完成安装。\n\n"
                           "在安装镜像内直接运行会导致系统「打开方式」列表异常。"),
            QMessageBox::Ok);
        box.button(QMessageBox::Ok)->setText(QStringLiteral("退出"));
        box.exec();
        return 0;   // 不创建主窗口，应用随对话框关闭退出
    }

    // 解析命令行传入的文件路径：文件管理器「打开方式」双击 .md 时经 .desktop 的 %f 传入。
    QString initialFile;
    const QStringList args = app.arguments();
    for (int i = 1; i < args.size(); ++i) {
        if (!args[i].startsWith(QLatin1Char('-'))) {
            initialFile = args[i];
            break;
        }
    }

    // QWebEngineProfile 必须在 QApplication 之后初始化
    // 在默认 profile 上挂载 localimg:// 处理器（随 app 存活，覆盖所有 EditorTab）
    auto profile = QWebEngineProfile::defaultProfile();
    LocalImageSchemeHandler *imgHandler = new LocalImageSchemeHandler(&app);
    profile->installUrlSchemeHandler(kLocalImageScheme, imgHandler);
    profile->setHttpAcceptLanguage("zh-CN,zh;q=0.9");

    MainWindow window(initialFile);
    window.show();

    return app.exec();
}
