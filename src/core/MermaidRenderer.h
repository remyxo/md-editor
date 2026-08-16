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

signals:
    void finished(const QVector<MermaidRenderer::Result> &results);

private slots:
    void onLoadFinished(bool ok);

private:
    QWebEngineView *m_view = nullptr;
    QStringList m_pending;
    bool m_loaded = false;
};

#endif // MD_EDITOR_MERMAIDRENDERER_H
