#ifndef MD_EDITOR_MARKDOWNHANDLER_H
#define MD_EDITOR_MARKDOWNHANDLER_H

#include <QString>

class MarkdownHandler {
public:
    static QString toHtml(const QString &markdown);
    static QString toPlainText(const QString &markdown);
    static QString extractTitle(const QString &markdown);
    static QString fromHtml(const QString &html);
};

#endif // MD_EDITOR_MARKDOWNHANDLER_H
