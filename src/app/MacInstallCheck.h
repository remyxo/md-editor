#pragma once
#include <QString>

// macOS「从安装镜像/转位目录运行」检测——MainWindow 启动提示用。
// 纯路径判定，跨平台可测（Linux/Windows 路径恒 false）
namespace MacInstallCheck {

// app 可执行文件目录是否位于临时位置（main.cpp 据此硬阻断运行）：
//   /Volumes/<卷>/<bundle>.app/…  —— app 位于挂载卷根 = DMG 拖拽布局直跑
//                                     （二级卷子目录里的正经安装不拦）
//   /AppTranslocation/…            —— Gatekeeper 转位目录（从 Downloads 等
//                                     处直跑时系统把 app 复制到的随机只读路径）
// 两种位置运行的 app 都属「未安装」状态，且会在 Launch Services 登记
// 多个副本（「打开方式」推荐列表置灰的根因，见 CHANGELOG 0.16.1）
bool isTransientLocation(const QString &appDirPath);

} // namespace MacInstallCheck
