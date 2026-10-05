#ifndef MD_EDITOR_EXPORTHTMLASYNC_H
#define MD_EDITOR_EXPORTHTMLASYNC_H

#include <QString>
#include <functional>

class EditorTab;

// 编辑器 HTML 异步获取（自 MainWindow.cpp:944-973 纯搬移，行为不变）。
// Plain = getContent()；Docx = getExportHtml()（live colgroup 注入，供 tblGrid）；
// Pdf/Docx 追加代码块高亮步（export-highlight.js 单一来源，失败降级原文）。
// 源码模式一律 getSourceContent() 经 toHtml（编辑器文档是进源码前旧快照）。
//
// 调用方契约（终审 I-1 覆盖论证）：本函数自身不做在途检查——tab 处于在途
// 加载（hasPendingRead()/isContentInFlight()）时取到的是载入前文档。现存
// 调用方的守卫：onExportPdf/onExportDocx/onExportHtml 入口拒绝 + 状态栏提示；
// ExportCoordinator::exportPdf 失败结算兜底（selftest 直调面）。新调用方
// 接入前必须自查同款守卫
namespace ExportHtmlAsync {

enum class Mode { Plain, Docx, Pdf };

void get(EditorTab *tab, std::function<void(const QString &)> cb,
         Mode mode = Mode::Plain);

}   // namespace ExportHtmlAsync

#endif   // MD_EDITOR_EXPORTHTMLASYNC_H
