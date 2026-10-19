#ifndef MD_EDITOR_PLATFORMCHROME_H
#define MD_EDITOR_PLATFORMCHROME_H

class QWidget;

// 平台外观增强：弹层抛光 + Windows DWM 标题栏/Mica。
//
// install() 在 MainWindow::setupUi 调用一次（须在首次 switchTheme 之前，
// Mica 探测结果影响 ThemeManager 生成样式表的透明度分支）：
//  - 弹层抛光：Qt::Popup 型顶层 widget（QMenu / 组合框弹层 / TableGridPopup）
//    首次 Polish 时设 WA_TranslucentBackground + DropShadowEffect，
//    配合 QSS border-radius 得到真圆角 + 阴影浮层
//  - DWM（仅 Windows）：标题栏深浅色/配色跟随主题；主窗口 Win11 22H2+
//    启用 Mica。主题切换经 ThemeManager::themeChanged 对既有顶层窗口重应用
//
// 其余平台相关函数编译为空实现，三平台无编译死角（同 FileManager 先例）。
namespace PlatformChrome {

void install();

// Mica 是否可用（Windows 11 22H2+ 且 install() 时探测成功），其余平台恒 false
bool isMicaActive();

// 对单个顶层窗口应用 DWM 属性（非 Windows 空实现）；
// 通常无需手动调用——install() 的事件过滤与 themeChanged 已覆盖
void applyToWindow(QWidget *window);

}

#endif // MD_EDITOR_PLATFORMCHROME_H
