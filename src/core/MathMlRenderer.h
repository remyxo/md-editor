#ifndef MD_EDITOR_MATHMLRENDERER_H
#define MD_EDITOR_MATHMLRENDERER_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

class QWebEngineView;

// 离屏 QWebEngine 用 KaTeX 渲染 LaTeX 公式 → MathML，供 DOCX 导出转换为 OMML 原生公式。
// KaTeX 的 renderToString(..., {output:'mathml'}) 是同步调用（不同于 mermaid 的异步
// Promise），故结果直接经 runJavaScript 回调同步读回，无需轮询。渲染失败的项回退为空
// 字符串（调用侧降级为 $...$ 源码文本）。
class MathMlRenderer : public QObject {
    Q_OBJECT
public:
    explicit MathMlRenderer(QObject *parent = nullptr);
    ~MathMlRenderer() override;

    void renderAll(const QStringList &sources);   // 异步

    // 生成渲染脚本：对每个 source 调 katex.renderToString(..., {output:'mathml'})，
    // 返回 JSON 数组字符串（与 KaTeX 源码一一对应，失败项为空串）。
    // 拆为静态便于单测：断言脚本含 output:'mathml' 且不依赖轮询。
    static QString buildRenderScript(const QStringList &sources);

signals:
    void finished(const QVector<QString> &mathmlList);

private slots:
    void onLoadFinished(bool ok);

private:
    QWebEngineView *m_view = nullptr;
    QStringList m_pending;
    bool m_loaded = false;
    bool m_finished = false;   // 30s 看门狗与正常结算的互斥（防 finished 双发）
};

#endif // MD_EDITOR_MATHMLRENDERER_H
