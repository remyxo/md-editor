# Mermaid SVG 渲染问题与修复

## 问题概述

Mermaid 图表在 QWebEngineView（Chromium）中渲染时，出现三类问题：SVG 中 `<rect>` 元素负宽/高导致控制台报错、SVG `width="100%"` 导致字体等比放大远超正文、甘特图等复杂图表 viewBox 匹配错误导致整图缩成一小块。

---

## 问题 1：`<rect>` 负宽度导致控制台报错

### 现象

```
[JS] "Error: <rect> attribute width: A negative value is not valid. ("-9")"
[JS] "Error: <rect> attribute width: A negative value is not valid. ("-10")"
```

### 根因

Mermaid v11 对 CJK（中日韩）字符宽度计算存在偏差（[mermaid issue #4950](https://github.com/mermaid-js/mermaid/issues/4950)）。mindmap、flowchart 等图的文本测量结果偏小 → 节点背景矩形 `width = 计算宽度 - 文本实际宽度` 为负数 → 浏览器 SVG 解析器报错。

### 失败的尝试

1. **仅用正则 `width="-[\d.]+"` 替换** — 不处理 `=` 周围的空白字符（如 `width ="-9"`），且 regex 只匹配双引号，Mermaid 各版本输出格式差异导致漏网。

### 最终方案：三层防线

**第一层：字符串正则替换（`_sanitizeMermaidSvg`，在 `innerHTML` 解析前）**

```javascript
// 容忍 = 周围空白、双引号/单引号、整数/小数/科学计数法，覆盖 Mermaid 各版本输出差异
svgString = svgString
    .replace(/\b(width|height)\s*=\s*"-\d+(?:\.\d+)?(?:[eE][+-]?\d+)?"/g, '$1="1"')
    .replace(/\b(width|height)\s*=\s*'-(\d+(?:\.\d+)?(?:[eE][+-]?\d+)?)'/g, "$1='1'");
```

- `\b` 词边界防止误匹配 `stroke-width` 等复合属性
- `\s*` 容忍 `=` 周围可选空白
- clamp 为 `1` 而非 `0`：甘特图 task bar 也是 `<rect>`，width=0 完全不可见

**第二层：viewBox width 替换锚定在 `<svg>` 标签内**

```javascript
// 必须锚定在 <svg 开标签内，防止误匹配 <defs>/<pattern> 等子元素的 viewBox
var vb = svgString.match(/<svg\b[^>]*\bviewBox\s*=\s*"([\d.\s-]+)"/);
if (vb) {
    var vbNums = vb[1].match(/[\d.]+/g);
    if (vbNums && vbNums.length >= 4) {
        // 只替换 <svg> 元素自身的 width 属性
        svgString = svgString.replace(/(<svg\b[^>]*?)\swidth\s*=\s*"[^"]*"/,
                                      '$1 width="' + vbNums[2] + '"');
    }
}
```

**第三层：DOM 后置修正兜底（`_fixNegativeRects`，在 `innerHTML` 解析后）**

```javascript
function _fixNegativeRects(svgEl) {
    if (!svgEl) return;
    var rects = svgEl.querySelectorAll('rect');
    for (var i = 0; i < rects.length; i++) {
        var r = rects[i];
        var w = r.getAttribute('width');
        var h = r.getAttribute('height');
        if (w !== null && parseFloat(w) < 0) r.setAttribute('width', '1');
        if (h !== null && parseFloat(h) < 0) r.setAttribute('height', '1');
    }
}
```

- 在 `innerHTML` 解析后遍历 `<rect>` 元素，直接 `setAttribute` 归零漏网负值
- **关键**：`setAttribute` 是 DOM 属性赋值，不触发浏览器控制台 "A negative value is not valid" 错误（只有标记解析阶段才报错）
- 为 regex 漏网与 mermaid 动态 SVG 场景提供兜底

### 调用位置

两个调用点都会经过完整的字符串消毒 + DOM 兜底：

| 场景 | 函数 | 位置 |
|------|------|------|
| 编辑区内联渲染 | `mermaidNodeView.paint()` | `editor.js:~245` |
| 对话框实时预览 | `_mermaidEditor.updatePreview()` | `editor.js:~1130` |

---

## 问题 2：SVG `width="100%"` 导致字体等比放大

### 现象

Mermaid 渲染的 SVG 中文字体大小远超编辑区正文 18px，看起来比例失调。

### 根因

Mermaid 输出 `width="100%"` 的 SVG 根元素属性。在宽容器中（编辑区 `min-width: 800px`），SVG 被拉伸填满容器宽度。而 Mermaid 的 viewBox 通常只有 ~800-1000px，拉伸后 SVG 内部所有元素（包括文字）等比放大，导致字体远超正文。

### 失败的尝试

1. **不断调小 `themeVariables.fontSize`**（18 → 14 → 13px）— 治标不治本，SVG 仍被拉伸，只是起点小了。且修复根因后 13px 偏小。
2. **CSS `svg { width: auto; max-width: none; }`** — 宽图（甘特图）会溢出编辑区。

### 最终方案

在 `_sanitizeMermaidSvg` 中将 `width="100%"` 替换为 viewBox 的实际宽度，使 SVG 按 **1:1 像素映射**渲染（自然尺寸）：

```javascript
// 提取 <svg> 自身的 viewBox 宽度
var vb = svgString.match(/<svg\b[^>]*\bviewBox\s*=\s*"([\d.\s-]+)"/);
// 替换 width="100%" 为 viewBox 宽度
svgString.replace(/(<svg\b[^>]*?)\swidth\s*=\s*"[^"]*"/,
                  '$1 width="' + vbNums[2] + '"');
```

配合 CSS：
- `svg.style.maxWidth = '100%'` — 窄图自然尺寸，宽图不超出编辑区
- `svg.style.height = 'auto'` — 保持宽高比

字体恢复为 `themeVariables: { fontSize: '16px' }`，与编辑区正文 18px 比例协调。

### 教训

SVG `width="100%"` 在可变宽容器中会产生不可预测的等比缩放。对于需要精确控制内部元素尺寸的 SVG（如 Mermaid 图表），应始终使用 viewBox 的绝对像素值作为 width，用 CSS `max-width` 处理溢出。

---

## 问题 3：甘特图缩成一小块

### 现象

Gantt 甘特图在编辑区渲染时整图缩成极窄的一小块，但对话框预览正常。

### 根因

`_sanitizeMermaidSvg` 中提取 viewBox 的正则 `/viewBox\s*=\s*"[\d.\s-]+"/` 匹配 SVG 字符串中**第一个** `viewBox` 属性。Mermaid 生成的 Gantt 图 `<defs>` 中包含 `<pattern viewBox="0 0 10 10">` 等子元素，正则误匹配 pattern 的 viewBox（宽度 10），将 SVG 根元素的 `width="100%"` 替换为 `width="10"`，2000px 宽的甘特图被缩到 10px。

### 修复

将 viewBox 匹配锚定在 `<svg` 开标签内：

```javascript
// 之前：匹配全文第一个 viewBox（可能命中 <pattern> 等子元素）
var vb = svgString.match(/viewBox\s*=\s*"[\d.\s-]+"/);

// 之后：锚定在 <svg 开标签内，只取 SVG 自身的 viewBox
var vb = svgString.match(/<svg\b[^>]*\bviewBox\s*=\s*"([\d.\s-]+)"/);
```

width 替换同步锚定：
```javascript
// 之前：替换全文第一个 width（<svg> 无 width 属性时会误伤子元素）
svgString.replace(/width\s*=\s*"[^"]*"/, ...);

// 之后：只替换 <svg> 标签自身的 width 属性
svgString.replace(/(<svg\b[^>]*?)\swidth\s*=\s*"[^"]*"/, '$1 width="..."');
```

### 教训

在 SVG 字符串上做正则操作时，必须锚定目标元素，不能依赖"第一个出现就是对的"假设。`<defs>` 中的 `<pattern>`、`<marker>`、`<symbol>` 等元素可能包含与根元素同名的属性。

---

## 关键函数 `_sanitizeMermaidSvg` 设计总结

```javascript
function _sanitizeMermaidSvg(svgString) {
    // 第1步：正则替换负 width/height（容忍空白+引号变体）
    // 第2步：锚定 <svg> 标签，提取 viewBox 宽度 → 替换 width="100%"
    // 第3步（调用方）：innerHTML 解析后，_fixNegativeRects() DOM 兜底
    return svgString;
}
```

**调用方必须执行的完整流程：**
```javascript
dom.innerHTML = _sanitizeMermaidSvg(res.svg);
var svg = dom.querySelector('svg');
if (svg) {
    _fixNegativeRects(svg);          // DOM 兜底
    svg.style.maxWidth = '100%';     // 宽图约束
    svg.style.height = 'auto';       // 保持比例
}
```

**不要**跳过 `_fixNegativeRects()` 调用——它是 regex 漏网的兜底防线。

**不要**修改 `_sanitizeMermaidSvg` 中的 viewBox 正则而不锚定 `<svg` 标签——否则甘特图等复杂图表必然出错。

**不要**把 clamp 值改回 0——甘特图 task bar 的 `<rect>` 需要至少 1px 可见。
