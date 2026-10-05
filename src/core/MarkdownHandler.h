#ifndef MD_EDITOR_MARKDOWNHANDLER_H
#define MD_EDITOR_MARKDOWNHANDLER_H

#include <QString>

class MarkdownHandler {
public:
    // extractFootnotes=false：嵌套转换（脚注定义内容）——[^x] 引用与定义行均按字面
    static QString toHtml(const QString &markdown, bool extractFootnotes = true);
    // toHtml + body 提取一体：编辑器加载只需 body 内容，toHtml 返回的完整 HTML
    // 文档（<html><body> 包装）在此去壳。body 提取逻辑的单一来源——原先散布于
    // EditorTab::loadFile（W4-3 上收），未匹配到 <body> 时原样返回整串（不 trim）
    static QString toBodyHtml(const QString &markdown, bool extractFootnotes = true);
    static QString toPlainText(const QString &markdown);
    static QString extractTitle(const QString &markdown);
    static QString fromHtml(const QString &html);
};

#endif // MD_EDITOR_MARKDOWNHANDLER_H
