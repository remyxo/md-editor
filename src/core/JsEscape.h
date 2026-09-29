#ifndef MD_EDITOR_JSESCAPE_H
#define MD_EDITOR_JSESCAPE_H

#include <QString>

// JS 字符串转义单一来源。MainWindow/EditorTab 既有 8 处内联副本已全部
// 收敛至此（W1-3）；新代码一律用它，不再新增副本
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

// 单引号字符串安全化：' 与 \ 转义（供 runJavaScript('...') 参数拼接）。
// \n/\r 一并转义——单引号字面量内裸换行即语法错误（Windows 路径
// C:\Users\.. 的 \ 与含 ' 的 Unix 路径都会破坏字面量）。体照抄自
// MainWindow 既有 jsSingleQuoted（转义顺序保持：反斜杠最先，后续
// 替换引入的 \ 不会被二次转义）
inline QString jsSingleQuoted(const QString &s)
{
    QString t(s);
    t.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    t.replace(QLatin1Char('\''), QStringLiteral("\\'"));
    t.replace(QLatin1Char('\n'), QStringLiteral("\\n"));
    t.replace(QLatin1Char('\r'), QStringLiteral("\\r"));
    return t;
}

}   // namespace JsEscape

#endif   // MD_EDITOR_JSESCAPE_H
