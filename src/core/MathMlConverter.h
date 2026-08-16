#ifndef MD_EDITOR_MATHMLCONVERTER_H
#define MD_EDITOR_MATHMLCONVERTER_H

#include <QString>

// MathML → OMML（Word 原生公式）转换。
// 输入为 KaTeX renderToString(..., {output:'mathml'}) 的输出（含 <math>/<semantics>
// 包裹与 <annotation> LaTeX 源码，annotation 会被跳过），输出为不含 <m:oMath> 外层的
// OMML 片段（外层由调用方按行内 <m:oMath> / 块 <m:oMathPara> 包裹）。
// 纯函数、无状态，便于单测。
namespace MathMlConverter {
    QString toOMML(const QString &mathml);
}

#endif // MD_EDITOR_MATHMLCONVERTER_H
