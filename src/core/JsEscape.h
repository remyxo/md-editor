#ifndef MD_EDITOR_JSESCAPE_H
#define MD_EDITOR_JSESCAPE_H

#include <QString>

// JS 字符串转义单一来源。W1-7 将把 MainWindow/EditorTab 既有 7 处副本
// 收敛至此；本文件先落最小集（模板字面量），新代码一律用它，不再新增副本
namespace JsEscape {

// 反引号模板字面量安全化：\ ` $ 三字符转义（顺序不可换：先反斜杠）
inline QString forTemplateLiteral(const QString &s)
{
    QString r = s;
    r.replace(QLatin1Char('\\'), QStringLiteral("\\\\"))
     .replace(QLatin1Char('`'), QStringLiteral("\\`"))
     .replace(QLatin1Char('$'), QStringLiteral("\\$"));
    return r;
}

}   // namespace JsEscape

#endif   // MD_EDITOR_JSESCAPE_H
