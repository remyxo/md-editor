// =============================================================================
// docx-parity-harness.cpp — DOCX 两模式表格列宽对齐探针的归一+导出执行器
//（Wave3 Task 1，scripts/probe-docx-table-parity.sh 在 /tmp 工程内编译本文件，
// 链接仓库产品源文件——与 tests/test_docxexporter 同一组编译单元）。
//
// 与产品 GUI 路径（MainWindow::onExportDocx）同构：输入 HTML →
// TableColumnNormalizer::normalize（DOCX 基准 cfg，含测量期 border 清零）→
// DocxExporter::exportToDocx。产物供宿主侧解包比对 word/document.xml 的
// tblGrid（逐列相等 = 两模式测量基准一致的端到端证据）。
//
// 页内容宽走 ExportCommon::pageContentWidthTwips 单一来源；驱动脚本以隔离
// HOME 运行本程序（QSettings 默认 A4 竖版 1.8cm = 9865 缇），宿主导出设置
// 不污染两路测量基准——w2-gamma 取证同款决策。
//
// 退出码：0=成功；1=IO/导出失败；2=归一器忙；3=总超时；4=归一失败降级
//（降级路径不走归一层，两路基准不再可比，探针应判对齐失败）
// =============================================================================
#include <QApplication>
#include <QFile>
#include <QTimer>
#include <cstdio>

#include "core/DocxExporter.h"
#include "core/ExportCommon.h"
#include "core/TableColumnNormalizer.h"

int main(int argc, char **argv)
{
    if (argc < 3) {
        std::fprintf(stderr, "用法: %s <输入.html> <输出.docx> [docDir]\n", argv[0]);
        return 1;
    }
    QApplication app(argc, argv);
    const QString inPath = QFile::decodeName(argv[1]);
    const QString outPath = QFile::decodeName(argv[2]);
    const QString docDir = argc > 3 ? QFile::decodeName(argv[3]) : QString();
    QFile f(inPath);
    if (!f.open(QIODevice::ReadOnly)) {
        std::fprintf(stderr, "读入失败: %s\n", argv[1]);
        return 1;
    }
    const QString html = QString::fromUtf8(f.readAll());
    f.close();
    const int avail = ExportCommon::pageContentWidthTwips();

    auto *exporter = new DocxExporter();
    int rc = 0;
    QObject::connect(exporter, &DocxExporter::exportError, &app, [&](const QString &err) {
        std::fprintf(stderr, "docx 导出失败: %s\n", qPrintable(err));
        rc = 1;
        QTimer::singleShot(0, &app, [&app]() { app.quit(); });
    });
    QObject::connect(exporter, &DocxExporter::exportFinished, &app,
                     [&](bool ok, const QString &) {
                         if (!ok) rc = 1;
                         QTimer::singleShot(0, &app, [&app]() { app.quit(); });
                     });
    // 表格归一（与 GUI 同款：无表格直通不经归一层——探针输入都含表格，
    // 不含表格时两路也无从比对，此处不特判，由 normalize 自行处理）
    auto *norm = TableColumnNormalizer::instance();
    const quint64 id = norm->normalize(html, docDir, avail);
    if (id == 0) {
        std::fprintf(stderr, "归一器忙（上一轮未结算）\n");
        return 2;
    }
    QObject::connect(norm, &TableColumnNormalizer::finished, &app,
                     [&](quint64 requestId, const QString &normalized) {
                         if (requestId != id) return;
                         if (normalized.isEmpty()) {
                             std::fprintf(stderr, "归一失败，降级原 html\n");
                             rc = 4;
                         }
                         exporter->exportToDocx(normalized.isEmpty() ? html : normalized,
                                                docDir, outPath);
                     });
    // 总闸：归一 30s 看门狗 + 导出自身时限之外的超时兜底（僵死不挂死探针；
    // mermaid 渲染较慢，预算 180s 与 selftest 导出套件同量级）
    QTimer::singleShot(180000, &app, [&]() {
        std::fprintf(stderr, "总超时（180s）\n");
        rc = 3;
        app.quit();
    });
    app.exec();
    delete exporter;
    return rc;
}
