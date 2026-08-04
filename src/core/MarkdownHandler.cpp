#include "MarkdownHandler.h"
#include <QTextDocument>
#include <QRegularExpression>

QString MarkdownHandler::toHtml(const QString &markdown)
{
    QTextDocument doc;
    doc.setMarkdown(markdown);
    return doc.toHtml();
}

QString MarkdownHandler::toPlainText(const QString &markdown)
{
    QTextDocument doc;
    doc.setMarkdown(markdown);
    return doc.toPlainText();
}

QString MarkdownHandler::extractTitle(const QString &markdown)
{
    // 提取第一个 H1 标题
    QRegularExpression re("^#\\s+(.+)$", QRegularExpression::MultilineOption);
    QRegularExpressionMatch m = re.match(markdown);
    return m.hasMatch() ? m.captured(1).trimmed() : QString();
}
