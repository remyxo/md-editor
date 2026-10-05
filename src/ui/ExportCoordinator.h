#ifndef MD_EDITOR_EXPORTCOORDINATOR_H
#define MD_EDITOR_EXPORTCOORDINATOR_H

#include <QString>
#include <functional>

class EditorTab;
class QObject;

// 导出编排下沉（W4-4，PDF 试点）：「内容获取（ExportHtmlAsync，源码模式经
// toHtml）→ 导出器启动 → 终态结算」的共用编排链，自 MainWindow::onExportPdf
// 下沉；onExportDocx/onExportHtml 暂留原位（试点边界，后续波次同构扩展）。
//
// 职责边界：
//  - Coordinator 只管编排：内容获取、导出器构造/信号连接/回收（deleteLater）、
//    终态翻译成统一 done 回调、用时统计（内容就绪 → 终态）；
//  - 进度/错误的**呈现**完全由调用方经 PdfHooks 决定（GUI = 进度对话框 +
//    状态栏文案；无头 selftest = 钩子全空、只消费 done）——本编译单元零 UI
//    依赖，i18n 文案拼装留在各调用方（翻译上下文不变）；
//  - 不触碰导出版式：PdfExporter 内部（版式常量/分页器/列宽归一）零改动，
//    产物逐字节零漂移由 export-parity-check.sh golden 对账验收。
//
// 时序契约（与下沉前 MainWindow::onExportPdf 逐点对齐）：
//  - exportPdf 异步返回；started/终态钩子在后续事件循环轮次回调；
//  - tab 在内容获取/源码转换中途销毁时回调链被丢弃——started 与 done 均不
//    触发（导出静默取消，与既有「页面销毁后 IPC 回调不执行」语义一致）；
//  - exportError/exportFinished 理论互斥，仍以单发闸保证终态钩子与 done 恰
//    执行一次；导出器在首个终态即 deleteLater 回收。
class ExportCoordinator {
public:
    // 导出终态回调：ok=exportFinished(ok)；err 仅失败时有值（exportError 的
    // 错误详情；exportFinished(false) 无详情，err 为空串）
    using Done = std::function<void(bool ok, const QString &err)>;

    // 呈现钩子（每一项可为空，调用前由 Coordinator 判空）
    struct PdfHooks {
        // 内容就绪、导出即将启动（GUI：此刻创建并显示进度对话框——源码模式
        // 转换期间不弹框，与下沉前一致）
        std::function<void()> started;
        // exportError（空内容/引擎故障）。导出器已由 Coordinator 回收
        std::function<void(const QString &err)> failed;
        // exportFinished（终态）：elapsedMs 为内容就绪起算的用时；fallbackUsed
        // = paged.js 失败/超时已回退原生分页（产物无页眉页脚，最终说明应并入
        // 本终态的状态栏消息）。导出器已由 Coordinator 回收
        std::function<void(bool ok, const QString &outPath, qint64 elapsedMs,
                           bool fallbackUsed)> finished;
        // pagedFallbackUsed：回退进行中的过渡提示（最终说明并入 finished）
        std::function<void()> pagedFallback;
        // pagedProgress(k)：paged.js 分页页数增长
        std::function<void(int pages)> pagedProgress;
        // writingPdf(total)：写盘阶段（total=0 表示页数未知）
        std::function<void(int total)> writing;
    };

    // PDF 导出编排。docDir 由 tab 的 filePath 推导（未保存文档回退 CWD——
    // 与下沉前行为一致：插入图片须先保存，未保存文档无相对图片）。
    // exporterParent：导出器的 QObject 归属（GUI 传窗口——导出中途关窗时
    // 滞留的隐藏渲染视图随窗口销毁兜底回收；无头 selftest 传 nullptr，终态
    // deleteLater 回收）。done 可空（GUI 呈现型调用无需终态回调）
    static void exportPdf(EditorTab *tab, const QString &path,
                          QObject *exporterParent, const PdfHooks &hooks,
                          Done done = {});
};

#endif   // MD_EDITOR_EXPORTCOORDINATOR_H
