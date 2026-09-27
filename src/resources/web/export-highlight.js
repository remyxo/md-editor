// =============================================================================
// export-highlight.js — 导出用代码块语法高亮（PDF/DOCX 单一来源）
//
// 与编辑器同一 lowlight 实例（同语言集）、同一 hljs-light.css 色板（运行时解析，
// 无硬编码色值）。产出扁平 inline-style span：嵌套 token 已按 CSS 级联合成
// 有效样式，消费侧（DocxExporter / PDF 拆行）无需再处理嵌套。
//
// 设计文档：docs/superpowers/specs/2026-09-27-export-code-highlight-design.md
// 仿 table-column-layout.js 模式：editor.html <script> 引入 + vitest 行为测试
// =============================================================================
(function () {
    'use strict';

    // ---- 主题规则解析 ----
    // 只收「纯 .hljs* 类选择器」规则（pre code.hljs 等容器规则跳过）。
    // 复合选择器（.hljs-title.function_）与后代选择器（.hljs-meta .hljs-keyword）
    // 都按类集合存储——展平后的 run 类名累积天然编码了祖先链，子集匹配即可
    // 命中两种语义。
    // 【存储粒度】每个空白分隔的选择器段整段存为一个类名 token（仅去首点、
    // 保留段内点号）：'.hljs-title.function_' → 'hljs-title.function_'。原因：
    // 复合限定类（function_/class_/language_ 等）不以 hljs- 开头，若按点拆开
    // 存储会被下方「全部 hljs*」过滤器丢弃，与测试断言「所有类名均以 hljs
    // 开头」冲突；整段 token 恰以 hljs- 开头，两种断言同时成立
    function parseThemeRules(cssText) {
        var rules = [];
        var re = /([^{}]+)\{([^{}]*)\}/g, m;
        while ((m = re.exec(cssText))) {
            var sels = m[1].split(',');
            for (var i = 0; i < sels.length; i++) {
                var sel = sels[i].trim();
                if (!sel) continue;
                // 每个 whitespace 段必须形如 .a.b（纯类复合段，无元素名）
                var parts = sel.split(/\s+/);
                var classes = [];
                var ok = parts.length > 0;
                for (var j = 0; j < parts.length; j++) {
                    if (!/^(\.[A-Za-z0-9_-]+)+$/.test(parts[j])) { ok = false; break; }
                    // 整段入表（去首点），不按点拆分——见上方【存储粒度】
                    classes = classes.concat([parts[j].slice(1)]);
                }
                if (!ok) continue;
                var allHljs = true;
                for (var k = 0; k < classes.length; k++)
                    if (!(classes[k] === 'hljs' || classes[k].indexOf('hljs-') === 0)) {
                        allHljs = false; break;
                    }
                if (!allHljs || !classes.length) continue;
                var decl = m[2];
                var r = { classes: classes, color: null, weight: null,
                          fstyle: null, bg: null, order: rules.length };
                var mm = /(?:^|;)\s*color\s*:\s*([^;]+)/.exec(decl);
                if (mm) r.color = mm[1].trim();
                mm = /(?:^|;)\s*font-weight\s*:\s*([^;]+)/.exec(decl);
                if (mm) r.weight = mm[1].trim();
                mm = /(?:^|;)\s*font-style\s*:\s*([^;]+)/.exec(decl);
                if (mm) r.fstyle = mm[1].trim();
                mm = /(?:^|;)\s*background(?:-color)?\s*:\s*([^;]+)/.exec(decl);
                if (mm) r.bg = mm[1].trim();
                if (r.color || r.weight || r.fstyle || r.bg) rules.push(r);
            }
        }
        return rules;
    }

    // ---- 级联合成 ----
    // run 的累积类名（外→内，flattenHast 产出）→ 有效样式。
    // 候选 = 类集合为 run 类名子集的规则；按（类数升序, 源序升序）依次应用、
    // 后应用者逐属性覆盖 == CSS 特异性 + 源序级联的近似（扁平类列表恰编码
    // 祖先链，子集匹配等价后代语义）。
    // 【带点 token 匹配】规则类名若含点号（如 'hljs-title.function_'，源自
    // 复合选择器整段存储），按点拆开后逐一要求命中 run 类名——HAST 侧
    // className 是散开的独立类（['hljs-title','function_']），两侧才能对上
    function resolveStyle(rules, classes) {
        var cands = [];
        for (var i = 0; i < rules.length; i++) {
            var r = rules[i], hit = true;
            for (var j = 0; j < r.classes.length; j++) {
                var seg = r.classes[j].split('.');
                for (var q = 0; q < seg.length; q++)
                    if (classes.indexOf(seg[q]) < 0) { hit = false; break; }
                if (!hit) break;
            }
            if (hit) cands.push(r);
        }
        cands.sort(function (a, b) {
            return a.classes.length - b.classes.length || a.order - b.order;
        });
        var st = { color: null, weight: null, fstyle: null, bg: null };
        for (var k = 0; k < cands.length; k++) {
            var c = cands[k];
            if (c.color) st.color = c.color;
            if (c.weight) st.weight = c.weight;
            if (c.fstyle) st.fstyle = c.fstyle;
            if (c.bg) st.bg = c.bg;
        }
        return st;
    }

    // ---- HAST 展平（与 vendored-lowlight-plugin.js parseNodes 同构）----
    // lowlight.highlight 返回 HAST 树；展平为 {text, classes} 序列，
    // 类名沿祖先链外→内累积
    function flattenHast(nodes, inherited) {
        var out = [];
        for (var i = 0; i < nodes.length; i++) {
            var n = nodes[i];
            var classes = (inherited || []).concat(
                n.properties ? (n.properties.className || []) : []);
            if (n.children) out = out.concat(flattenHast(n.children, classes));
            else out.push({ text: n.value, classes: classes });
        }
        return out;
    }

    // ---- HTML 文本转义（hljs 文本节点实体化）----
    function escapeHtml(s) {
        return s.replace(/&/g, '&amp;').replace(/</g, '&lt;')
                .replace(/>/g, '&gt;');
    }

    // 有效样式 → inline style 属性值（只含有值属性）
    function styleAttr(st) {
        var p = [];
        if (st.color) p.push('color:' + st.color);
        if (st.weight) p.push('font-weight:' + st.weight);
        if (st.fstyle) p.push('font-style:' + st.fstyle);
        if (st.bg) p.push('background-color:' + st.bg);
        return p.join(';');
    }

    // ---- 全局暴露（主入口 __exportHighlight 由下方补充）----
    globalThis.__exportHighlightParseRules = parseThemeRules;
})();
