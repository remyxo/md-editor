#ifndef MD_EDITOR_MARKDOWNHANDLER_H
#define MD_EDITOR_MARKDOWNHANDLER_H

#include <QString>

class MarkdownHandler {
public:
    // extractFootnotes=false：嵌套转换（脚注定义内容）——[^x] 引用与定义行均按字面
    static QString toHtml(const QString &markdown, bool extractFootnotes = true);
    static QString toPlainText(const QString &markdown);
    static QString extractTitle(const QString &markdown);
    static QString fromHtml(const QString &html);
};

#endif // MD_EDITOR_MARKDOWNHANDLER_H
