#ifndef MD_EDITOR_EXPORTHTMLASYNC_H
#define MD_EDITOR_EXPORTHTMLASYNC_H

#include <QString>
#include <functional>

class EditorTab;

// 编辑器 HTML 异步获取（自 MainWindow.cpp:944-973 纯搬移，行为不变）。
// Plain = getContent()；Docx = getExportHtml()（live colgroup 注入，供 tblGrid）；
// Pdf/Docx 追加代码块高亮步（export-highlight.js 单一来源，失败降级原文）。
// 源码模式一律 getSourceContent() 经 toHtml（编辑器文档是进源码前旧快照）
namespace ExportHtmlAsync {

enum class Mode { Plain, Docx, Pdf };

void get(EditorTab *tab, std::function<void(const QString &)> cb,
         Mode mode = Mode::Plain);

}   // namespace ExportHtmlAsync

#endif   // MD_EDITOR_EXPORTHTMLASYNC_H
