#ifndef MD_EDITOR_FILEPATHMATCH_H
#define MD_EDITOR_FILEPATHMATCH_H

#include <QString>

// 同一文件路径判定（TabWidget「同路径文件重开即聚焦既有标签」的核心谓词）。
// 抽成零依赖小单元便于独立单测——不牵 EditorTab/QWebEngine 链。
namespace FilePathMatch {

// 两个路径是否指向同一文件：
// - 任一侧文件存在 → 按 QFileInfo::canonicalFilePath 比较（相对路径/符号链接/
//   冗余分隔符/.. 回溯全部归一为真实路径）
// - 双侧均不存在（如外部已删除的文件）→ 回退 absoluteFilePath 字符串比较
//   ——防两个不同的不存在路径都 canonical 成空串而误判相等
bool sameFilePath(const QString &a, const QString &b);

} // namespace FilePathMatch

#endif // MD_EDITOR_FILEPATHMATCH_H
