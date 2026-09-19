#pragma once
#include <QString>

// macOS「从安装镜像/转位目录运行」检测——MainWindow 启动提示用。
// 纯路径判定，跨平台可测（Linux/Windows 路径恒 false）
namespace MacInstallCheck {

// app 可执行文件目录是否位于临时位置：
//   /Volumes/…           —— DMG 挂载卷（未拖到「应用程序」就直跑）
//   /AppTranslocation/…  —— Gatekeeper 转位目录（从 Downloads 等处直跑时
//                          系统把 app 复制到的随机只读路径）
// 两种位置运行的 app 都属「未安装」状态，且会在 Launch Services 登记
// 多个副本（「打开方式」推荐列表置灰的根因，见 CHANGELOG 0.16.1）
bool isTransientLocation(const QString &appDirPath);

} // namespace MacInstallCheck
