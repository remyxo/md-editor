#ifndef MD_EDITOR_MARKDOWNHANDLER_H
#define MD_EDITOR_MARKDOWNHANDLER_H

#include <QString>

class MarkdownHandler {
public:
    static QString toHtml(const QString &markdown);
    static QString toPlainText(const QString &markdown);
    static QString extractTitle(const QString &markdown);
};

#endif // MD_EDITOR_MARKDOWNHANDLER_H
