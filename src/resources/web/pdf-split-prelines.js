// =============================================================================
// pdf-split-prelines.js — PDF 导出 pre 逐行重排（nativeHf 与 paged 两路径共用）
//
// 迁自 PdfExporter.cpp 内嵌 JS（行为对齐项见 pdf-split-prelines.test.js）。
// paged.js 对 pre 文本节点的字符偏移断点在行间空白区几何不可靠（pre-wrap 下
// 空白无字形），断点落进 "\n ␣␣␣" 这类区域时会把前段尾行整行丢掉
//（R2 实测断页处丢 1 行）。逐行 span 让分页只发生在元素边界——与段落
// 同路径，历轮零丢失。
//
// 本次随「导出语法高亮」修复：元素含换行时（多行 token 如块注释/模板串），
// 其子节点逐行拆分且每行片段以 cloneNode(false) 的同元素包裹——行内样式
//（颜色/粗斜体/背景）跨行逐行保留；原实现递归取子节点会丢包裹元素样式。
//
// 底色与内边距改由逐行 .pl 块承载（CSS 见 PdfExporter 模板 pre .pl 规则）：
// 行块 break-inside:avoid 永不跨页，其背景不会被 Chromium 画到片段容器末端
//（快路径可打印区覆盖整纸，越界底色会一直画到纸底压过页脚，实测灰底
// 242→296.6mm）。pre 自身的底色在这里清掉。
// =============================================================================
(function () {
    'use strict';

    globalThis.__splitPreLines = function () {
        document.querySelectorAll('pre').forEach(function (pre) {
            var host = pre.querySelector('code') || pre;
            // 单行块早退（与原实现一致）：无 .pl 子块，底色/内边距保留在
            // pre 自身——若清零会让单行代码块失去灰底
            if (host.textContent.indexOf('\n') < 0) return;

            // 节点（或子树）按行拆分：返回行数组，每行是节点数组。
            // 元素节点：子节点各自拆分，行边界处合并；每行片段以
            // cloneNode(false) 的该元素包裹（样式保留的核心）
            function splitNode(n) {
                if (n.nodeType === 3) {
                    var tl = [[]];
                    var parts = n.textContent.split('\n');
                    parts.forEach(function (seg, i) {
                        if (i > 0) tl.push([]);
                        if (seg) tl[tl.length - 1].push(document.createTextNode(seg));
                    });
                    return tl;
                }
                var sub = [[]];
                for (var i = 0; i < n.childNodes.length; i++) {
                    var t = splitNode(n.childNodes[i]);
                    for (var j = 0; j < t.length; j++) {
                        if (j > 0) sub.push([]);
                        sub[sub.length - 1] = sub[sub.length - 1].concat(t[j]);
                    }
                }
                var out = [];
                // 有子节点才展开：sub 的空行是行边界，必须保位（旧版跳过会把
                // 多行元素内部的空行/行界抹掉——块注释含空行即静默丢行/并行）
                if (n.childNodes.length) {
                    sub.forEach(function (ln) {
                        if (!ln.length) { out.push([]); return; }   // 空行保位
                        var el = n.cloneNode(false);
                        ln.forEach(function (c) { el.appendChild(c); });
                        out.push([el]);
                    });
                }
                // 空元素（无子节点）不产生任何行（视觉与旧行为等价）
                return out.length ? out : [[]];
            }

            var lines = [[]];
            for (var i = 0; i < host.childNodes.length; i++) {
                var t = splitNode(host.childNodes[i]);
                for (var j = 0; j < t.length; j++) {
                    if (j > 0) lines.push([]);
                    lines[lines.length - 1] = lines[lines.length - 1].concat(t[j]);
                }
            }
            // 尾随换行按 HTML 解析语义丢弃（不补空行）；行内空行用 nbsp 占位保行高
            if (lines.length > 1 && !lines[lines.length - 1].length) lines.pop();

            var frag = document.createDocumentFragment();
            lines.forEach(function (ln) {
                var sp = document.createElement('span');
                sp.className = 'pl';
                if (!ln.length) sp.appendChild(document.createTextNode('\u00A0'));
                else ln.forEach(function (seg) { sp.appendChild(seg); });
                frag.appendChild(sp);
            });
            while (host.firstChild) host.removeChild(host.firstChild);
            host.appendChild(frag);
            pre.style.background = 'transparent';
            pre.style.padding = '0';
        });
    };
})();
