#ifndef TIPTAP_SERIALIZE_H
#define TIPTAP_SERIALIZE_H

// TipTap 链路近似：getHTML() 的 innerHTML 序列化。
// 真实链路里 toHtml 的产物必经浏览器 DOM 解析 → TipTap 序列化，属性值在这一步
// 「先实体解码、再统一转义」（& → &amp;、< → &lt;、> → &gt;、" → &quot;）。
// 测试若只跑 toHtml → fromHtml 的 C++ 直连，就会漏掉「toHtml 端未做属性转义 /
// fromHtml 端未做实体解码」这类**只在真实链路暴露**的问题——阶段 1 修掉的公式
// `&` 逐轮膨胀（&amp; → &amp;amp;）与 data-latex 截断即属此类，此前测试全绿。
// 与 test_roundtrip_matrix.cpp 的 tipTapNormalizeInlineOverlap 同源：
// 端到端验证必须走真实链路（见项目记忆），测试对编辑器行为做保守近似。

#include <QRegularExpression>
#include <QString>

static QString simulateTipTapSerialization(const QString &html)
{
    static const QRegularExpression tagRe(QStringLiteral("<[^>]*>"));
    static const QRegularExpression attrRe(
        QStringLiteral("([A-Za-z_:][-A-Za-z0-9_:.]*)\\s*=\\s*\"([^\"]*)\""));
    QString out;
    int off = 0;
    auto it = tagRe.globalMatch(html);
    while (it.hasNext()) {
        auto tm = it.next();
        out += html.mid(off, tm.capturedStart() - off);
        const QString tag = tm.captured();
        QString rebuilt;
        int aoff = 0;
        auto ait = attrRe.globalMatch(tag);
        while (ait.hasNext()) {
            auto am = ait.next();
            rebuilt += tag.mid(aoff, am.capturedStart() - aoff);
            QString v = am.captured(2);
            // DOM 解析：实体解码
            v.replace("&lt;", "<").replace("&gt;", ">").replace("&quot;", "\"")
             .replace("&#39;", "'").replace("&apos;", "'").replace("&amp;", "&");
            // innerHTML 序列化：统一转义
            v.replace("&", "&amp;").replace("<", "&lt;")
             .replace(">", "&gt;").replace("\"", "&quot;");
            rebuilt += am.captured(1) + QStringLiteral("=\"") + v + QLatin1Char('"');
            aoff = am.capturedStart() + am.capturedLength();
        }
        rebuilt += tag.mid(aoff);
        out += rebuilt;
        off = tm.capturedStart() + tm.capturedLength();
    }
    out += html.mid(off);
    return out;
}

#endif   // TIPTAP_SERIALIZE_H
