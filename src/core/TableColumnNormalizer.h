#ifndef MD_EDITOR_TABLECOLUMNNORMALIZER_H
#define MD_EDITOR_TABLECOLUMNNORMALIZER_H

#include <QObject>
#include <QString>
#include <QUrl>

class QWebEngineView;

// 离屏 QWebEngine 载入待导出 HTML，按 DOCX 基准跑表格列宽归一（与 PDF 导出
// 同一份 table-column-layout.js，经 HtmlExporter::tableColumnNormalizeScript
// 内联），回传含 px colgroup 的 body.innerHTML 供 DocxExporter 生成 tblGrid。
// 编辑器 live colgroup 的比例是编辑器窗口基准，缩放到 Word 页宽后短列装不下
// 11pt 字（一字一行）、长列独占——归一在目标页宽下重测重分配，与 PDF 同构。
// 异步，结果经 finished 回传；空串 = 失败/降级信号（调用侧用原 html 走旧路径）
class TableColumnNormalizer : public QObject {
    Q_OBJECT
public:
    explicit TableColumnNormalizer(QObject *parent = nullptr);
    ~TableColumnNormalizer() override;

    // 进程级单例（同 MermaidRenderer：隐藏视图常驻免冷启动，aboutToQuit 销毁）
    static TableColumnNormalizer *instance();

    // 归一 HTML 中的表格列宽。availTwips：DOCX 页内容宽（ExportCommon::
    // pageContentWidthTwips，JS 侧 ÷15 转 px）；docDir：相对图片解析基准
    //（表内图片参与 min/max 测量；空 = 未保存文档，无相对图片）。
    // 返回请求序号（>0）；单例忙（上一轮未结算）返回 0 且**不发信号**——
    // busy 路径若广播空结果，会把挤进来的其他请求一并误降级，故降级决策
    // 上移到调用方（就地用原 html 导出）
    quint64 normalize(const QString &html, const QString &docDir, int availTwips);

    // 归一脚本（共享脚本 + DOCX 基准 cfg 拼装），拆静态便于单测断言 cfg 参数
    static QString buildNormalizeScript(int availTwips);

signals:
    void finished(quint64 requestId, const QString &normalizedHtml);
    // requestId：本次请求序号，调用方匹配后才消费（并发双导出共享单例不互抢）；空串 = 失败/降级信号

private slots:
    void onLoadFinished(bool ok);

private:
    void settle(const QString &html);   // 结算（与 30s 看门狗互斥，防 finished 双发）

    QWebEngineView *m_view = nullptr;
    QString m_pending;          // 待归一 HTML（loadFinished 后消费）
    QUrl m_baseUrl;             // 相对图片解析基准
    int m_availTwips = 0;
    quint64 m_seq = 0;          // 请求序号（单调递增）；看门狗跨代误报防线
    bool m_finished = false;    // 看门狗与正常结算的互斥
    bool m_busy = false;        // 单例并发防御（busy 时新请求立即降级）
};

#endif // MD_EDITOR_TABLECOLUMNNORMALIZER_H
