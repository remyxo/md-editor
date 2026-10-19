#include "PlatformChrome.h"
#include "ThemeManager.h"

#include <QApplication>
#include <QDialog>
#include <QEvent>
#include <QGraphicsDropShadowEffect>
#include <QMainWindow>
#include <QWidget>

#ifdef Q_OS_WIN
#include <windows.h>

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_BORDER_COLOR
#define DWMWA_BORDER_COLOR 34
#endif
#ifndef DWMWA_CAPTION_COLOR
#define DWMWA_CAPTION_COLOR 35
#endif
#ifndef DWMWA_SYSTEMBACKDROP_TYPE
#define DWMWA_SYSTEMBACKDROP_TYPE 38
#endif
#ifndef DWMSBT_MAINWINDOW
#define DWMSBT_MAINWINDOW 2
#endif

#pragma comment(lib, "dwmapi.lib")
#endif // Q_OS_WIN

namespace {

bool g_micaActive = false;

QColor tokenOr(const QString &name, QRgb fallback)
{
    if (ThemeManager::instance()) {
        const QColor c = ThemeManager::instance()->tokenColor(name);
        if (c.isValid())
            return c;
    }
    return QColor(fallback);
}

bool isPopupLayer(QWidget *w)
{
    // 弹出层：菜单/组合框弹层/TableGridPopup（Qt::Popup 窗口型）；
    // ToolTip 型不含其中，天然排除。注意必须用 windowType() 精确匹配，
    // 不能用 windowFlags() & Qt::Popup 位与——QMainWindow 的原始 flags
    // 含高位附加位，位与会把主窗误判成弹层（曾导致主窗被挂半透明+阴影，
    // 菜单栏/工具栏未绘制区域透出桌面）
    return w->windowType() == Qt::Popup;
}

void polishPopup(QWidget *w)
{
    // 半透明窗口 + QSS border-radius = 真圆角；阴影渲染在 QSS margin 预留区内
    w->setAttribute(Qt::WA_TranslucentBackground);
    if (!w->graphicsEffect()) {
        auto *shadow = new QGraphicsDropShadowEffect(w);
        shadow->setBlurRadius(14);
        shadow->setOffset(0, 3);
        shadow->setColor(tokenOr(QStringLiteral("shadow"), qRgb(0, 0, 0)));
        w->setGraphicsEffect(shadow);
    }
}

void refreshShadowColor(QWidget *w)
{
    // 主题切换（qApp 级样式表重设 → StyleChange）时同步弹层阴影色
    if (auto *shadow = qobject_cast<QGraphicsDropShadowEffect *>(w->graphicsEffect()))
        shadow->setColor(tokenOr(QStringLiteral("shadow"), qRgb(0, 0, 0)));
}

class ChromeFilter : public QObject {
public:
    explicit ChromeFilter(QObject *parent)
        : QObject(parent)
    {
    }

    bool eventFilter(QObject *obj, QEvent *ev) override
    {
        if (ev->type() != QEvent::Polish && ev->type() != QEvent::StyleChange)
            return QObject::eventFilter(obj, ev);

        auto *w = qobject_cast<QWidget *>(obj);
        if (!w || !w->isWindow())
            return QObject::eventFilter(obj, ev);

        if (ev->type() == QEvent::StyleChange) {
            if (isPopupLayer(w))
                refreshShadowColor(w);
            return QObject::eventFilter(obj, ev);
        }

        // Polish：原生窗口创建前的最后时机
        if (isPopupLayer(w))
            polishPopup(w);
        else if (qobject_cast<QMainWindow *>(w) || qobject_cast<QDialog *>(w))
            PlatformChrome::applyToWindow(w);
        return QObject::eventFilter(obj, ev);
    }
};

#ifdef Q_OS_WIN
void probeMicaSupport()
{
    // 隐藏探针窗口上试设 Mica backdrop：非 Win11 22H2+ 返回失败 → 恒不透明
    QWidget probe;
    HWND hwnd = reinterpret_cast<HWND>(probe.winId());
    MARGINS margins = { -1, -1, -1, -1 };
    DwmExtendFrameIntoClientArea(hwnd, &margins);
    DWORD backdrop = DWMSBT_MAINWINDOW;
    const HRESULT hr = DwmSetWindowAttribute(hwnd, DWMWA_SYSTEMBACKDROP_TYPE,
                                             &backdrop, sizeof(backdrop));
    g_micaActive = SUCCEEDED(hr);
    qInfo("PlatformChrome: Mica %s", g_micaActive ? "可用" : "不可用（降级为不透明）");
}

COLORREF toColorRef(const QColor &c)
{
    return COLORREF((c.blue() << 16) | (c.green() << 8) | c.red());
}
#endif // Q_OS_WIN

} // namespace

namespace PlatformChrome {

void install()
{
    static bool installed = false;
    if (installed)
        return;
    installed = true;

#ifdef Q_OS_WIN
    probeMicaSupport();
#endif

    auto *filter = new ChromeFilter(qApp);
    qApp->installEventFilter(filter);

    // 主题切换 → 既有顶层窗口重应用 DWM；新窗口由事件过滤在 Polish 时覆盖
    if (ThemeManager::instance()) {
        QObject::connect(ThemeManager::instance(), &ThemeManager::themeChanged,
                         qApp, [](const QString &) {
                             const auto tops = qApp->topLevelWidgets();
                             for (QWidget *w : tops) {
                                 if (qobject_cast<QMainWindow *>(w) || qobject_cast<QDialog *>(w))
                                     applyToWindow(w);
                             }
                         });
    }
}

bool isMicaActive()
{
    return g_micaActive;
}

void applyToWindow(QWidget *window)
{
#ifdef Q_OS_WIN
    if (!window || !window->isWindow())
        return;

    const bool dark = ThemeManager::instance()
                      && ThemeManager::instance()->currentTheme() == QLatin1String("dark");
    HWND hwnd = reinterpret_cast<HWND>(window->winId());

    // 标题栏深浅跟随主题（19041+ 属性号 20，1809-1903 回退 19；失败忽略）
    BOOL flag = dark;
    if (FAILED(DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE,
                                     &flag, sizeof(flag)))) {
        DwmSetWindowAttribute(hwnd, 19, &flag, sizeof(flag));
    }

    // 标题栏/窗口边框配色匹配令牌（Win11+；Win10 不支持，调用失败自动忽略）
    const COLORREF caption = toColorRef(tokenOr(QStringLiteral("chrome"), qRgb(0xff, 0xff, 0xff)));
    const COLORREF border = toColorRef(tokenOr(QStringLiteral("strokeFlyout"), qRgb(0xe5, 0xe5, 0xe5)));
    DwmSetWindowAttribute(hwnd, DWMWA_CAPTION_COLOR, &caption, sizeof(caption));
    DwmSetWindowAttribute(hwnd, DWMWA_BORDER_COLOR, &border, sizeof(border));

    // Mica 仅主窗口（对话框按 Fluent 惯例保持实底）
    if (qobject_cast<QMainWindow *>(window) && g_micaActive) {
        window->setAttribute(Qt::WA_TranslucentBackground);
        MARGINS margins = { -1, -1, -1, -1 };
        DwmExtendFrameIntoClientArea(hwnd, &margins);
        DWORD backdrop = DWMSBT_MAINWINDOW;
        DwmSetWindowAttribute(hwnd, DWMWA_SYSTEMBACKDROP_TYPE,
                              &backdrop, sizeof(backdrop));
    }
#else
    Q_UNUSED(window);
#endif
}

} // namespace PlatformChrome
