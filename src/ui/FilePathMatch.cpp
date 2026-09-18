#include "FilePathMatch.h"
#include <QFileInfo>

namespace FilePathMatch {

bool sameFilePath(const QString &a, const QString &b)
{
    const QString ca = QFileInfo(a).canonicalFilePath();
    const QString cb = QFileInfo(b).canonicalFilePath();
    // 双侧都能解析到真实路径 → 按 canonical 比较（符号链接/大小写卷等归一）；
    // 任一侧解析不出（文件或中间目录已被删）→ 回退文本归一的绝对路径比较，
    // 防两个不同的不存在路径都 canonical 成空串而误判相等
    if (!ca.isEmpty() && !cb.isEmpty())
        return ca == cb;
    return QFileInfo(a).absoluteFilePath() == QFileInfo(b).absoluteFilePath();
}

} // namespace FilePathMatch
