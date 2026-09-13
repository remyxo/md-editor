#ifndef MD_EDITOR_MERMAIDRENDERER_H
#define MD_EDITOR_MERMAIDRENDERER_H

#include <QObject>
#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>

class QWebEngineView;

// 离屏 QWebEngine 渲染 mermaid 源码 → SVG + PNG（canvas 栅格化），供 DOCX 导出嵌入。
// 渲染异步，结果经 finished 信号回传；渲染失败该项回退为空（调用侧降级为源码代码块）。
class MermaidRenderer : public QObject {
    Q_OBJECT
public:
    struct Result {
        QString svg;        // 完整 SVG 字符串
        QByteArray png;     // PNG 字节
        int cxEmu = 0;      // 宽（EMU）
        int cyEmu = 0;      // 高（EMU）
    };

    explicit MermaidRenderer(QObject *parent = nullptr);
    ~MermaidRenderer() override;

    void renderAll(const QStringList &sources);   // 异步

    // 生成渲染脚本（IIFE 必须 return Promise，runJavaScript 才会等待异步渲染完成）。
    // 拆为静态便于单测：断言 IIFE 返回 Promise 而非 undefined。
    static QString buildRenderScript(const QStringList &sources);

signals:
    void finished(const QVector<MermaidRenderer::Result> &results);

private slots:
    void onLoadFinished(bool ok);

private:
    void pollResults(int n, int attempt);   // 轮询读取 window.__mermaidResults（见 .cpp 注释）

    QWebEngineView *m_view = nullptr;
    QStringList m_pending;
    bool m_loaded = false;
    bool m_finished = false;   // 30s 看门狗与正常结算的互斥（防 finished 双发）
};

#endif // MD_EDITOR_MERMAIDRENDERER_H
