#ifndef MD_EDITOR_PDFPAGEGEOMETRY_H
#define MD_EDITOR_PDFPAGEGEOMETRY_H

#include <QByteArray>
#include <QVector>

// PDF 页内容几何测量（nativeHf 页脚定位用，2026-09-20）：
// 逐页解析内容流，跟踪图形状态（CTM/qQ 栈 + 文本矩阵），对路径构造
// （m/l/c/v/y/re）、文本定位（Td/TD/Tm/T*/Tj 系）、XObject 绘制
// （Do × Form BBox / 图片单位方）取设备坐标，输出每页内容底沿
// （顶系 pt，与 QPdfDocument::getAllText().bounds() 同一坐标系）。
//
// 为什么需要它：getAllText 只见文本——页底是无文本内容（mermaid SVG 的
// 矢量路径/图片）时底沿漏计，页码覆盖层逐页早移被成品校验拦下回退慢链。
//
// 适用面：Chromium printToPdf 产物（实证无对象流、对象裸露可切分；通用
// PDF 的 ObjStm/xref-stream 不支持）。任何解析失败 → 对应页 valid=false，
// 调用方回退纯文本几何（不劣于无此单元的状态）
class PdfPageGeometry {
public:
    struct PageBottom {
        qreal bottomPt = 0;      // 内容底沿（顶系 pt；0=页顶）
        qreal footerBasePt = -1;   // 页脚标记基线（顶系 pt；-1=无）。识别特征：
                                   // #666 填充色的下半页文本——文档内唯一下
                                   // 半页的该色文本（表头同色但恒在页顶）
        bool valid = false;      // 该页有可识别的绘制操作
    };

    // 解析整本 PDF，逐页返回内容底沿；整体失败/不支持 → 空 QVector
    static QVector<PageBottom> measure(const QByteArray &pdf);
};

#endif // MD_EDITOR_PDFPAGEGEOMETRY_H
