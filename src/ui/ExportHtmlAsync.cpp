#include "ExportHtmlAsync.h"
#include "EditorTab.h"
#include <QWebEnginePage>
#include "core/JsEscape.h"
#include "core/MarkdownHandler.h"

// 导出前代码块语法高亮（实现见 web/export-highlight.js：与编辑器同一
// lowlight 实例 + hljs-light 色板）。模板字面量传大 HTML 与源码模式路径
// 同款；任何失败两侧都降级原文，导出不中断
static void highlightForExportAsync(EditorTab *tab, const QString &html,
                                    std::function<void(const QString &)> cb)
{
    if (!tab || !tab->webView() || html.isEmpty()
        || !html.contains(QLatin1String("language-"))) {
        cb(html);
        return;
    }
    tab->webView()->page()->runJavaScript(
        QStringLiteral("window.editorApi.highlightExportHtml(`%1`);")
            .arg(JsEscape::forTemplateLiteral(html)),
        [html, cb](const QVariant &result) {
            const QString s = result.toString();
            cb(s.isEmpty() ? html : s);
        });
}

namespace ExportHtmlAsync {

void get(EditorTab *tab, std::function<void(const QString &)> cb, Mode mode)
{
    if (!tab || !tab->webView())
        return;
    const bool src = tab->isSourceMode();
    // DOCX 导出取 getExportHtml()：live colgroup 的 px 列宽注入序列化 HTML，
    // 供 DocxExporter 生成 tblGrid；源码模式 getSourceContent 经 toHtml 产
    // Qt DOM（无 live colgroup 起步）——两路都经 MainWindow 归一层在 DOCX
    // 页宽基准下重测重分配注入 px colgroup（测量基准含 border 清零统一，
    // 见 TableColumnNormalizer），等宽网格仅在归一失败降级时出现
    const QString call = src ? QStringLiteral("window.editorApi.getSourceContent();")
                             : (mode == Mode::Docx
                                    ? QStringLiteral("window.editorApi.getExportHtml();")
                                    : QStringLiteral("window.editorApi.getContent();"));
    tab->webView()->page()->runJavaScript(call,
        [tab, src, mode, cb](const QVariant &result) {
            QString s = result.toString();
            const QString html = src ? MarkdownHandler::toHtml(s) : s;
            if (mode == Mode::Plain) {
                cb(html);
                return;
            }
            highlightForExportAsync(tab, html, cb);
        });
}

}   // namespace ExportHtmlAsync
