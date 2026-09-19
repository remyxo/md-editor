#include "MacInstallCheck.h"

namespace MacInstallCheck {

bool isTransientLocation(const QString &appDirPath)
{
    if (appDirPath.isEmpty())
        return false;
    // 挂载卷内运行 = DMG 未拖出（拖拽安装布局正是为杜绝此事）。
    // 带 / 前缀整段匹配，/Volumesfoo 这类前缀撞名不算
    if (appDirPath.startsWith(QStringLiteral("/Volumes/")))
        return true;
    // 转位目录固定名为 AppTranslocation，夹在路径段中，两侧 / 保证整段匹配
    return appDirPath.contains(QStringLiteral("/AppTranslocation/"));
}

} // namespace MacInstallCheck
