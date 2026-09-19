#include "MacInstallCheck.h"

namespace MacInstallCheck {

bool isTransientLocation(const QString &appDirPath)
{
    if (appDirPath.isEmpty())
        return false;
    if (appDirPath.startsWith(QStringLiteral("/Volumes/"))) {
        // 只拦「app 包位于挂载卷根」的 DMG 拖拽形态：
        //   /Volumes/<卷名>/<bundle>.app/Contents/MacOS
        // 装在二级卷子目录里的正经安装（/Volumes/<卷>/<目录>/…app/…，如外置盘
        // 的 Applications 文件夹）不拦。带 / 前缀整段匹配，/Volumesfoo 不算
        const QString rest = appDirPath.mid(QStringLiteral("/Volumes/").size());
        const qsizetype volEnd = rest.indexOf(QLatin1Char('/'));
        if (volEnd <= 0)
            return false;   // 缺卷名或就是卷路径本身，不是 app
        const QString tail = QStringLiteral("/Contents/MacOS");
        const QString afterVol = rest.mid(volEnd + 1);
        if (!afterVol.endsWith(tail))
            return false;
        return !afterVol.left(afterVol.size() - tail.size())
                    .contains(QLatin1Char('/'));   // 卷名与 bundle 间无更深层级
    }
    // Gatekeeper 转位目录固定名为 AppTranslocation（从 Downloads 等处直跑时
    // 系统把 app 复制到的随机只读路径），夹在路径段中，两侧 / 整段匹配
    return appDirPath.contains(QStringLiteral("/AppTranslocation/"));
}

} // namespace MacInstallCheck
