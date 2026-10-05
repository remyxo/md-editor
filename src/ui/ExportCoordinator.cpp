#include "ExportCoordinator.h"

#include "EditorTab.h"
#include "ExportHtmlAsync.h"
#include "../core/PdfExporter.h"

#include <QElapsedTimer>
#include <QFileInfo>
#include <QObject>
#include <memory>

void ExportCoordinator::exportPdf(EditorTab *tab, const QString &path,
                                  QObject *exporterParent, const PdfHooks &hooks,
                                  Done done)
{
    if (!tab || !tab->webView() || path.isEmpty())
        return;

    // 在途加载守卫（终审 I-1 兜底层）：内容未落地时 ExportHtmlAsync::get 会
    // 拿到载入前文档，导出静默产出错误内容。GUI 入口（MainWindow::onExportPdf）
    // 已先行拒绝并提示；此处兜底直调方（无头 selftest 等非 GUI 入口）——
    // 不走 ExportHtmlAsync::get，直接以失败结算。本类无翻译上下文（见头文件
    // 「i18n 留调用方」契约），err 为错误详情数据，非呈现文案
    if (tab->hasPendingRead() || tab->isContentInFlight()) {
        const QString err = QStringLiteral("文档尚未加载完成，导出已取消");
        if (hooks.failed) hooks.failed(err);
        if (done) done(false, err);
        return;
    }

    // 文档目录（解析相对图片路径）；未保存文档无相对图片（插入图片须先保存），
    // 空 filePath 回退 CWD 亦无影响——口径与下沉前 MainWindow::onExportPdf 同源
    const QString filePath = tab->webView()->property("filePath").toString();
    const QString docDir = QFileInfo(filePath).absolutePath();

    // 异步获取编辑器 HTML 内容（源码模式下取 source-view 当前内容经 toHtml，
    // 否则导出的是进入源码前的旧快照）。tab 中途销毁时本回调不再执行，
    // started/终态钩子与 done 均不触发（导出静默取消）
    ExportHtmlAsync::get(tab,
        [path, docDir, exporterParent, hooks, done](const QString &html) {
            // 用时从内容就绪（进度对话框出现时刻）起算，到 exportFinished——
            // 口径同下沉前（start 计时器在内容回调开头启动）
            QElapsedTimer timer;
            timer.start();
            if (hooks.started) hooks.started();

            auto *exporter = new PdfExporter(exporterParent);
            // 单发结算守卫：exportError/exportFinished 理论互斥，防御式闸保证
            // 终态钩子与 done 恰执行一次（若导出器实现变更双发，先到者生效）
            const auto settled = std::make_shared<bool>(false);
            // 各信号 context 挂导出器自身：终态 deleteLater 后迟到的重复信号
            // 随对象销毁断连，不依赖调用方生命周期
            // 失败路径（空内容/引擎故障）须有反馈且回收导出器（其持有隐藏
            // 渲染视图，滞留即整个渲染进程泄漏到窗口关闭）
            QObject::connect(exporter, &PdfExporter::exportError, exporter,
                    [hooks, done, settled, exporter](const QString &err) {
                if (*settled) return;
                *settled = true;
                if (hooks.failed) hooks.failed(err);
                if (done) done(false, err);
                exporter->deleteLater();
            });
            QObject::connect(exporter, &PdfExporter::exportFinished, exporter,
                    [hooks, done, settled, exporter, timer](bool ok, const QString &out) {
                if (*settled) return;
                *settled = true;
                if (hooks.finished)
                    hooks.finished(ok, out, timer.elapsed(), exporter->fallbackUsed());
                if (done) done(ok, QString());
                exporter->deleteLater();
            });
            // paged.js 失败/超时回退原生分页——打印进行中的过渡提示（最终说明
            // 由呈现层并入 exportFinished 的组合消息）
            QObject::connect(exporter, &PdfExporter::pagedFallbackUsed, exporter,
                    [hooks]() {
                if (hooks.pagedFallback) hooks.pagedFallback();
            });
            // 分页进度：大文档分页以十秒计（~0.8s/页），静默等待体感差——页数
            // 增长即刷新提示，导出完成后被结果消息覆盖
            QObject::connect(exporter, &PdfExporter::pagedProgress, exporter,
                    [hooks](int pages) {
                if (hooks.pagedProgress) hooks.pagedProgress(pages);
            });
            // 写盘阶段（printToPdf 前）：total>0 展示总页数，0 表示页数未知
            QObject::connect(exporter, &PdfExporter::writingPdf, exporter,
                    [hooks](int total) {
                if (hooks.writing) hooks.writing(total);
            });
            exporter->exportToPdf(html, docDir, path);
        }, ExportHtmlAsync::Mode::Pdf);
}
