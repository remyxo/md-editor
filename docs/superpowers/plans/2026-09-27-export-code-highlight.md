# 导出 PDF/DOCX 代码块语法高亮 实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** PDF 与 DOCX 导出的代码块带语法高亮，观感与编辑器亮色主题一致（颜色+粗体+斜体+diff 背景全量携带）。

**Architecture:** editor.js 侧单一来源预着色——编辑器页面内用同一 `window.lowlight` 实例高亮，把 hljs class 解析为 inline style 的**扁平** span（嵌套已按 CSS 级联合成有效样式）；PDF/DOCX 导出链路统一过一遍该纯函数。PDF 侧 `__splitPreLines` 抽为独立 web 资源并修复多行 token 拆行掉色；DOCX 侧 `parseCodeBlock` 从「只收文本」改为解析扁平样式 span 出彩色 run。

**Tech Stack:** Qt 6.8.x（C++/QWebEngine）、原生 JS（es5 风格，与 editor.js 一致）、lowlight 3.x（tiptap-all.js 内打包）、vitest 3 + jsdom、ctest。

**Spec:** `docs/superpowers/specs/2026-09-27-export-code-highlight-design.md`

## Global Constraints

- **注释全中文、标识符英文**（CLAUDE.md 规则 1）
- **qrc 资源（.js）与 C++ 改动后必须 `cmake --build build`**——qrc 打进二进制，不重编译用户跑旧版（CLAUDE.md 规则 9；告知用户「已修复」前确认二进制时间戳新于源文件）
- **不自动 commit**：各任务只跑测试；全部完成且**用户端到端验证通过后**统一提交（用户既定规则）；提交信息 `feat:` 前缀 + 中文 + 尾行 `Co-Authored-By: Claude Code <noreply@anthropic.com>`
- **版本号 / CHANGELOG 不动**（用户明示才推进）
- **DOCX schema 改动须 macOS Word 双击验证**（WPS/LibreOffice 宽容测不出）；本计划的 Word 验证由用户执行
- **Qt 版本无关**：本改动不触及 `toMarkdown()`/`setHtml()` 转义路径，天然满足兼容基准要求
- JS 测试命令：`cd src/resources/web && npx vitest run`（默认 node 环境，jsdom 按文件头注释 opt-in）
- C++ 测试命令：`cmake --build build -j$(nproc) && cd build && ctest --output-on-failure`
- 临时文件一律放 `/tmp/md-editor-claude-20260927/`（多 harness 并行约定）
- push 前预检钩子会自动对 C++ 改动跑 clang 容器检查 + win11 VM MSVC 编译（无需手动，知悉即可）

---

### Task 1: export-highlight.js 纯函数层（主题规则解析 / 级联合成 / HAST 展平）

**Files:**
- Create: `src/resources/web/export-highlight.js`
- Test: `src/resources/web/export-highlight.test.js`

**Interfaces:**
- Produces: `globalThis.__exportHighlightParseRules(cssText) → rules[]`，规则对象 `{classes: string[], color, weight, fstyle, bg, order}`（属性值为 CSS 原文串或 null）
- Produces: 模块内私有 `resolveStyle(rules, classes)`、`flattenHast(nodes, inherited)`、`escapeHtml(s)`、`styleAttr(st)`（Task 2 复用，不经全局暴露）

- [ ] **Step 1: 写失败测试**

创建 `src/resources/web/export-highlight.test.js`：

```js
// export-highlight.js 纯函数层测试：主题 CSS 规则解析 + 级联合成 + HAST 展平。
// 色值断言用真实 hljs-light.css（GitHub light 主题）原文，不造夹具主题
import { describe, it, expect } from 'vitest';
import { readFileSync } from 'fs';
import { fileURLToPath } from 'url';
import path from 'path';

const W = path.dirname(fileURLToPath(import.meta.url));
const cssText = readFileSync(path.join(W, 'hljs-light.css'), 'utf-8');

// 被 eval 的脚本定义 globalThis.__exportHighlightParseRules（node 环境可直接 eval）
function loadModule() {
    (0, eval)(readFileSync(path.join(W, 'export-highlight.js'), 'utf-8'));
    return globalThis.__exportHighlightParseRules;
}

describe('__exportHighlightParseRules', () => {
    it('解析真实主题：关键字/字符串/注释颜色', () => {
        const rules = loadModule()(cssText);
        const byClasses = (cs) => rules.find((r) => r.classes.join('|') === cs.join('|'));
        expect(byClasses(['hljs-keyword']).color).toBe('#d73a49');
        expect(byClasses(['hljs-string']).color).toBe('#032f62');
        expect(byClasses(['hljs-comment']).color).toBe('#6a737d');
        expect(rules.length).toBeGreaterThan(20);
    });

    it('复合选择器入表（.hljs-title.function_ 两类一条）', () => {
        const rules = loadModule()(cssText);
        const compound = rules.find((r) => r.classes.join('.') === 'hljs-title.function_');
        expect(compound).toBeTruthy();
        expect(compound.color).toBe('#6f42c1');
    });

    it('后代选择器（.hljs-meta .hljs-keyword）按类集合存储', () => {
        const rules = loadModule()(cssText);
        const desc = rules.find((r) => r.classes.join(' ') === 'hljs-meta hljs-keyword');
        expect(desc).toBeTruthy();
    });

    it('粗体/斜体/背景属性入表', () => {
        const rules = loadModule()(cssText);
        const byClasses = (cs) => rules.find((r) => r.classes.join('|') === cs.join('|'));
        expect(byClasses(['hljs-section']).weight).toBe('700');
        expect(byClasses(['hljs-emphasis']).fstyle).toBe('italic');
        expect(byClasses(['hljs-addition']).bg).toBe('#f0fff4');
    });

    it('容器规则（pre code.hljs / code.hljs）被跳过：所有类名均以 hljs 开头', () => {
        const rules = loadModule()(cssText);
        for (const r of rules)
            for (const c of r.classes)
                expect(c === 'hljs' || c.startsWith('hljs-')).toBe(true);
    });
});
```

- [ ] **Step 2: 跑测试确认失败**

Run: `cd src/resources/web && npx vitest run export-highlight.test.js`
Expected: FAIL（`__exportHighlightParseRules is not a function` / export-highlight.js 不存在）

- [ ] **Step 3: 写实现**

创建 `src/resources/web/export-highlight.js`（Task 1 只到纯函数层，主入口 Task 2 补）：

```js
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
    // 复合选择器（.hljs-title.function_，单元素双类）与后代选择器
    //（.hljs-meta .hljs-keyword，空格分隔）都按类集合存储——展平后的 run
    // 类名累积天然编码了祖先链，子集匹配即可命中两种语义
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
                    classes = classes.concat(parts[j].slice(1).split('.'));
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
    // 祖先链，子集匹配等价后代语义）
    function resolveStyle(rules, classes) {
        var cands = [];
        for (var i = 0; i < rules.length; i++) {
            var r = rules[i], hit = true;
            for (var j = 0; j < r.classes.length; j++)
                if (classes.indexOf(r.classes[j]) < 0) { hit = false; break; }
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
```

- [ ] **Step 4: 跑测试确认通过**

Run: `cd src/resources/web && npx vitest run export-highlight.test.js`
Expected: PASS（5 个用例全绿）

- [ ] **Step 5: 暂不提交**（用户规则：验证通过后统一提交）

---

### Task 2: 高亮主函数 + 规则装载/预热 + editor 接线

**Files:**
- Modify: `src/resources/web/export-highlight.js`（补主入口）
- Modify: `src/resources/web/editor.js`（editorApi 方法 + init 预热）
- Modify: `src/resources/web/editor.html:52`（script 引入）
- Modify: `src/resources/app.qrc:148`（qrc 条目）
- Test: `src/resources/web/export-highlight.test.js`（追加 jsdom 用例）

**Interfaces:**
- Consumes: Task 1 的 `parseThemeRules/resolveStyle/flattenHast/escapeHtml/styleAttr`
- Produces: `globalThis.__exportHighlight(html, themeRules?, lowlight?) → html'`（任何失败返回原文）
- Produces: `globalThis.__exportHighlightWarmup()`（init 时预热规则缓存）
- Produces: `window.editorApi.highlightExportHtml(html) → html'`（C++ 侧入口，Task 5 调用）

- [ ] **Step 1: 写失败测试（jsdom 用例，独立文件——vitest 单文件单环境，Task 1 的 node 用例不动）**

创建 `src/resources/web/export-highlight-dom.test.js`：

```js
// @vitest-environment jsdom
// export-highlight.js 主入口测试：真实 lowlight（tiptap-all.js 打包暴露）+
// 真实 hljs-light.css 规则表，端到端断言产物 HTML
import { describe, it, expect } from 'vitest';
import { readFileSync } from 'fs';
import { fileURLToPath } from 'url';
import path from 'path';

const W = path.dirname(fileURLToPath(import.meta.url));

function loadAll() {
    window.eval(readFileSync(path.join(W, 'tiptap-all.js'), 'utf-8'));
    window.eval(readFileSync(path.join(W, 'export-highlight.js'), 'utf-8'));
}

const rules = () => window.__exportHighlightParseRules(
    readFileSync(path.join(W, 'hljs-light.css'), 'utf-8'));

describe('__exportHighlight 主入口', () => {
    it('js 代码块关键字/数字/注释着色', () => {
        loadAll();
        const out = window.__exportHighlight(
            '<pre><code class="language-js">const a = 1; // hi</code></pre>', rules());
        expect(out).toContain('color:#d73a49');   // const 关键字
        expect(out).toContain('color:#005cc5');   // 数字 1
        expect(out).toContain('color:#6a737d');   // 注释
    });

    it('mermaid 块跳过（导出链路另有 SVG 渲染）', () => {
        loadAll();
        const html = '<pre><code class="language-mermaid">graph TD;</code></pre>';
        expect(window.__exportHighlight(html, rules())).toBe(html);
    });

    it('未注册语言与无语言代码块原样返回', () => {
        loadAll();
        const unknown = '<pre><code class="language-foobarlang">x</code></pre>';
        const plain = '<pre><code>x</code></pre>';
        expect(window.__exportHighlight(unknown, rules())).toBe(unknown);
        expect(window.__exportHighlight(plain, rules())).toBe(plain);
    });

    it('实体转义保真：< > & 不被吞（同时断言着色，防跳过路径假绿）', () => {
        loadAll();
        const out = window.__exportHighlight(
            '<pre><code class="language-js">if (a &lt; b &amp;&amp; c &gt; d) {}</code></pre>',
            rules());
        expect(out).toContain('color:#d73a49');   // if 关键字：确认走了高亮路径
        expect(out).toContain('&lt;');
        expect(out).toContain('&amp;');
        expect(out).toContain('&gt;');
    });

    it('规则表为空时整篇原文返回（降级）', () => {
        loadAll();
        const html = '<pre><code class="language-js">const a = 1;</code></pre>';
        expect(window.__exportHighlight(html, [])).toBe(html);
    });

    it('单块高亮抛异常只跳过该块，其余块仍着色', () => {
        loadAll();
        // 注入 stub lowlight：文本含 THROW 时抛异常，否则走真实实例
        const real = window.lowlight;
        const stub = {
            listLanguages: real.listLanguages.bind(real),
            registered: real.registered.bind(real),
            highlight(lang, text) {
                if (text.indexOf('THROW') >= 0) throw new Error('boom');
                return real.highlight(lang, text);
            },
        };
        const html = '<pre><code class="language-js">THROW here</code></pre>'
                   + '<pre><code class="language-js">const a = 1;</code></pre>';
        const out = window.__exportHighlight(html, rules(), stub);
        expect(out).toContain('THROW here');
        expect(out).toContain('color:#d73a49');
    });

    it('嵌套 token（meta 包 keyword）扁平化为单 span 且带继承合成色', () => {
        loadAll();
        // <?php echo 1;?>：hljs 输出 hljs-meta 包内容，内层 echo 为 hljs-keyword
        const out = window.__exportHighlight(
            '<pre><code class="language-php">&lt;?php echo 1;?&gt;</code></pre>', rules());
        // 展平后不应有嵌套 span（产物扁平性契约）
        const m = out.match(/<span[^>]*>(?:[^<]|<(?!\/))*?<span/g);
        expect(m).toBeNull();
        expect(out).toContain('color:#d73a49');
    });
});
```

- [ ] **Step 2: 跑测试确认失败**

Run: `cd src/resources/web && npx vitest run export-highlight-dom.test.js`
Expected: FAIL（`window.__exportHighlight is not a function`）

- [ ] **Step 3: 实现主入口**

在 `export-highlight.js` 的 `globalThis.__exportHighlightParseRules = parseThemeRules;` 之前插入：

```js
    // ---- 规则装载（页面环境）----
    // 首选读页面已加载的 #hljs-light 样式表（editor.html 的 link 初始为启用态，
    // 加载后即使被切到暗色禁用，CSSOM 仍可读）；读不到再异步 fetch 主题文本
    // 补缓存（fetch 失败保持冷缓存，高亮整体降级为原文，导出不中断）
    var _rulesCache = null;

    function loadRules() {
        if (_rulesCache) return _rulesCache;
        try {
            var sheets = document.styleSheets;
            for (var i = 0; i < sheets.length; i++) {
                var ss = sheets[i];
                if (ss.ownerNode && ss.ownerNode.id === 'hljs-light') {
                    var parts = [], rs = ss.cssRules;
                    for (var j = 0; j < rs.length; j++) parts.push(rs[j].cssText);
                    var parsed = parseThemeRules(parts.join('\n'));
                    if (parsed.length) { _rulesCache = parsed; return _rulesCache; }
                }
            }
        } catch (e) { /* 落到 fetch */ }
        return null;
    }

    // init 时预热：同步读样式表；冷缓存则异步 fetch 补（导出时大概率已就绪）
    function warmup() {
        try { loadRules(); } catch (e) { /* 忽略 */ }
        if (_rulesCache && _rulesCache.length) return;
        try {
            fetch('hljs-light.css')
                .then(function (r) { return r.text(); })
                .then(function (t) {
                    var parsed = parseThemeRules(t);
                    if (parsed.length && (!_rulesCache || !_rulesCache.length))
                        _rulesCache = parsed;
                })
                .catch(function () { /* 保持冷缓存，导出时降级 */ });
        } catch (e) { /* fetch 不可用同上 */ }
    }

    // ---- 单块高亮 ----
    // 返回替换后的 code innerHTML；不适用（无语言/mermaid/未注册）或失败返回
    // null（调用方保原样）。相邻同样式 run 合并成一个 span
    function highlightCodeEl(codeEl, ruleTable, ll) {
        var m = /(?:^|\s)language-([A-Za-z0-9_+#.-]+)/.exec(codeEl.className || '');
        var lang = m ? m[1] : '';
        if (!lang || lang === 'mermaid') return null;
        var langs = ll.listLanguages();
        if (langs.indexOf(lang) < 0
            && !(ll.registered && ll.registered(lang))) return null;
        var text = codeEl.textContent;
        if (!text) return null;
        var result = ll.highlight(lang, text);
        var nodes = result.value || result.children || [];
        var runs = flattenHast(nodes, []);
        if (!runs.length) return null;
        var html = '', lastStyle = '';
        for (var i = 0; i < runs.length; i++) {
            var attr = runs[i].classes.length
                ? styleAttr(resolveStyle(ruleTable, runs[i].classes)) : '';
            if (attr !== lastStyle) {
                if (lastStyle) html += '</span>';
                if (attr) html += '<span style="' + attr + '">';
                lastStyle = attr;
            }
            html += escapeHtml(runs[i].text);
        }
        if (lastStyle) html += '</span>';
        return html;
    }

    // ---- 主入口 ----
    // themeRules / lowlight 可注入（测试用）；缺省读页面规则表与 window.lowlight。
    // 任何异常返回原文（降级原则：导出不因高亮失败中断）
    function exportHighlight(html, themeRules, ll) {
        try {
            if (!html || html.indexOf('language-') < 0) return html;
            var ruleTable = themeRules || loadRules();
            if (!ruleTable || !ruleTable.length) return html;
            var low = ll
                || (typeof window !== 'undefined' ? window.lowlight : null);
            if (!low || !low.highlight) return html;
            var doc = new DOMParser().parseFromString(
                '<div id="__exproot">' + html + '</div>', 'text/html');
            var root = doc.getElementById('__exproot');
            var codes = root.querySelectorAll('pre > code');
            var changed = false;
            for (var i = 0; i < codes.length; i++) {
                try {
                    var out = highlightCodeEl(codes[i], ruleTable, low);
                    if (out !== null) { codes[i].innerHTML = out; changed = true; }
                } catch (e) { /* 单块失败保原样 */ }
            }
            return changed ? root.innerHTML : html;
        } catch (e) {
            return html;
        }
    }
```

并把文件尾的全局暴露改为：

```js
    globalThis.__exportHighlightParseRules = parseThemeRules;
    globalThis.__exportHighlight = exportHighlight;
    globalThis.__exportHighlightWarmup = warmup;
```

- [ ] **Step 4: editor 接线**

`editor.html:52` `<script src="table-column-layout.js"></script>` 之后加一行：

```html
<script src="export-highlight.js"></script>
```

`editor.js`：`getExportHtml`（约 2091–2113 行）方法结束的 `},` 之后追加：

```js
    // 导出前代码块语法高亮（PDF/DOCX；实现见 export-highlight.js，与编辑器
    // 同一 lowlight 实例 + hljs-light 色板）。任何失败返回原文，导出不中断
    highlightExportHtml: function(html) {
        return window.__exportHighlight ? window.__exportHighlight(html) : html;
    },
```

`editor.js` 的 `init: function()`（约 1876 行）末尾（编辑器创建成功路径的收尾处）追加：

```js
        // 导出高亮规则表预热：同步读样式表，冷则异步补（见 export-highlight.js）
        if (window.__exportHighlightWarmup) window.__exportHighlightWarmup();
```

`app.qrc:148` `<file>web/table-column-layout.js</file>` 之后加：

```xml
        <file>web/export-highlight.js</file>
```

- [ ] **Step 5: 跑测试确认通过**

Run: `cd src/resources/web && npx vitest run`
Expected: 全部 PASS（含 Task 1 的 node 用例与其他既有用例）

- [ ] **Step 6: 暂不提交**

---

### Task 3: PDF 拆行器抽取为 web 资源 + 多行 token 保色修复

**Files:**
- Create: `src/resources/web/pdf-split-prelines.js`
- Create: `src/resources/web/pdf-split-prelines.test.js`
- Modify: `src/core/PdfExporter.cpp`（删内嵌定义约 330–382 行，改为 script 引入）
- Modify: `src/resources/app.qrc`（qrc 条目）

**Interfaces:**
- Produces: `globalThis.__splitPreLines()`（行为同现状 + 元素样式跨行保留）；PdfExporter 渲染页与 vitest 共用

- [ ] **Step 1: 写失败测试**

创建 `src/resources/web/pdf-split-prelines.test.js`：

```js
// @vitest-environment jsdom
// __splitPreLines 行为测试：pre 按源码行拆块级 .pl span；导出高亮的
// inline 样式 span 跨行 token 拆行后逐行保色（本次修复的回归点）
import { describe, it, expect } from 'vitest';
import { readFileSync } from 'fs';
import { fileURLToPath } from 'url';
import path from 'path';

const W = path.dirname(fileURLToPath(import.meta.url));

function loadModule() {
    window.eval(readFileSync(path.join(W, 'pdf-split-prelines.js'), 'utf-8'));
}

function lines() {
    return Array.from(document.querySelectorAll('pre code .pl'));
}

describe('__splitPreLines', () => {
    it('多行着色 token 拆行后每行片段保留颜色', () => {
        loadModule();
        document.body.innerHTML =
            '<pre><code id="c"><span style="color:#d73a49">/* multi\nline */</span>after\n<span style="font-weight:700">bold</span></code></pre>';
        window.__splitPreLines();
        const ls = lines();
        expect(ls.length).toBe(3);
        // 行1：着色 span 内含 "/* multi"
        const s1 = ls[0].querySelector('span[style*="#d73a49"]');
        expect(s1).toBeTruthy();
        expect(s1.textContent).toBe('/* multi');
        // 行2：同色克隆 span + 其后裸文本 after（跨行保色回归点）
        const s2 = ls[1].querySelector('span[style*="#d73a49"]');
        expect(s2).toBeTruthy();
        expect(s2.textContent).toBe('line */');
        expect(ls[1].textContent).toBe('line */after');
        // 行3：粗体 span 保留
        expect(ls[2].querySelector('span[style*="font-weight:700"]')).toBeTruthy();
        expect(ls[2].textContent).toBe('bold');
    });

    it('纯文本代码块拆行为 .pl，行数正确', () => {
        loadModule();
        document.body.innerHTML =
            '<pre><code id="c">a\nb\nc</code></pre>';
        window.__splitPreLines();
        expect(lines().length).toBe(3);
        expect(lines().map((x) => x.textContent)).toEqual(['a', 'b', 'c']);
    });

    it('空行用 nbsp 占位；尾随换行丢弃', () => {
        loadModule();
        document.body.innerHTML =
            '<pre><code id="c">a\n\nb\n</code></pre>';
        window.__splitPreLines();
        const ls = lines();
        expect(ls.length).toBe(3);          // 尾随 \n 不补第 4 行
        expect(ls[1].textContent).toBe('\u00A0');
    });

    it('单行 pre 不拆（无 .pl）且保留自身底色（与原实现早退行为一致）', () => {
        loadModule();
        document.body.innerHTML = '<pre><code id="c">only</code></pre>';
        const pre = document.querySelector('pre');
        window.__splitPreLines();
        expect(lines().length).toBe(0);
        expect(pre.style.background).toBe('');   // 未清零：单行底色仍由 pre 自身承载
        expect(pre.style.padding).toBe('');
    });

    it('pre 底色与内边距清零（多行路径）', () => {
        loadModule();
        document.body.innerHTML = '<pre><code id="c">a\nb</code></pre>';
        const pre = document.querySelector('pre');
        window.__splitPreLines();
        expect(pre.style.background).toBe('transparent');
        expect(pre.style.padding).toBe('0px');
    });
});
```

- [ ] **Step 2: 跑测试确认失败**

Run: `cd src/resources/web && npx vitest run pdf-split-prelines.test.js`
Expected: FAIL（文件不存在）

- [ ] **Step 3: 写实现**

创建 `src/resources/web/pdf-split-prelines.js`（从 PdfExporter.cpp 内嵌串迁出并加保色修复；注释保留原有关键说明）：

```js
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
                sub.forEach(function (ln) {
                    if (!ln.length) return;   // 空元素（无子节点）不产生行
                    var el = n.cloneNode(false);
                    ln.forEach(function (c) { el.appendChild(c); });
                    out.push([el]);
                });
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
```

- [ ] **Step 4: PdfExporter 接线**

`PdfExporter.cpp`：
1. 删除 `preparePrint` 内嵌串里的 `window.__splitPreLines=function(){...};` 整段定义及其上方 5 行中文注释（约 330–382 行，从注释「代码块逐行重排（仅 paged 路径调用）…」起到定义收尾 `"};"` 止——历史背景说明已随实现迁入 JS 文件头注释，此处注释一并删除）。两处调用（`window.__splitPreLines();`，约 570/580 行）**保留不动**。
2. 在 `libs` 初始化处（约 777 行 `QString libs;`）改为无条件引入拆行器（nativeHf 与 paged 两路径都要）：

```cpp
    // 拆行器无条件引入（nativeHf 与 paged 两路径都调用 __splitPreLines；
    // 行为实现迁至 web/pdf-split-prelines.js，vitest 可测）
    QString libs = QStringLiteral("<script src=\"pdf-split-prelines.js\"></script>");
```

（原 `QString libs;` 声明行删除，后续 `if (needKatex) libs += ...` 保持不变。）

3. `app.qrc` 中 `<file>web/export-highlight.js</file>` 之后加：

```xml
        <file>web/pdf-split-prelines.js</file>
```

- [ ] **Step 5: 跑 JS 测试与 C++ 测试**

Run: `cd src/resources/web && npx vitest run pdf-split-prelines.test.js`
Expected: PASS（5 用例）

Run: `cmake --build build -j$(nproc) && cd build && ctest -R test_pdfexporter --output-on-failure`
Expected: PASS（既有断言 `out.contains("__splitPreLines")` 与调用点顺序断言仍成立——标识符与调用仍出现在内联脚本中）

- [ ] **Step 6: 暂不提交**

---

### Task 4: DocxExporter parseCodeBlock 解析高亮 span

**Files:**
- Modify: `src/core/DocxExporter.cpp:884-923`（parseCodeBlock 重写）+ 新增 style 提取辅助函数（放 `extractAlign` 后约 406 行处）
- Test: `tests/test_docxexporter.cpp`（追加用例）

**Interfaces:**
- Consumes: 既有 `extractColor(style, prop)`、`xmlEscape`、`xmlSanitize`
- Produces: parseCodeBlock 对外签名不变（`static QString parseCodeBlock(QXmlStreamReader &r, ParseState &s)`），输入约定扩展：pre 内允许扁平 `<span style="color:…;font-weight:…;font-style:…;background-color:…">`

- [ ] **Step 1: 写失败测试**

在 `tests/test_docxexporter.cpp` 追加（类内私有槽，与既有用例同风格）：

```cpp
private slots:
    // 导出高亮（export-highlight.js）产物：扁平 inline-style span → 彩色 run。
    // 多行 token（块注释）按行拆段后每行保持颜色
    void testCodeBlockHighlightColors()
    {
        const QString xml = exportDocumentXml(QStringLiteral(
            "<pre><code class=\"language-cpp\">"
            "<span style=\"color:#d73a49\">int</span> main() { "
            "<span style=\"color:#6a737d\">/* c\nmulti */</span> }"
            "</code></pre>"));
        // 关键字 run 颜色（extractColor 输出大写 hex）
        QVERIFY(xml.contains(QLatin1String("<w:color w:val=\"D73A49\"/>")));
        // 多行注释拆行后两行都保持注释色（跨行保色回归点）
        QCOMPARE(xml.count(QLatin1String("<w:color w:val=\"6A737D\"/>")), 2);
        // 无样式文本 run 不带 w:color（等宽字体 run 仍存在）
        QVERIFY(xml.contains(QLatin1String("w:ascii=\"Consolas\"")));
    }

    void testCodeBlockHighlightBoldItalicShd()
    {
        const QString xml = exportDocumentXml(QStringLiteral(
            "<pre><code class=\"language-md\">"
            "<span style=\"color:#005cc5;font-weight:700\"># head</span>\n"
            "<span style=\"color:#24292e;font-style:italic\">*em*</span>\n"
            "<span style=\"color:#22863a;background-color:#f0fff4\">+ add</span>"
            "</code></pre>"));
        // 粗体 + 颜色同 run；斜体；diff 背景底纹
        QVERIFY(xml.contains(QLatin1String("<w:b/>")));
        QVERIFY(xml.contains(QLatin1String("<w:i/>")));
        QVERIFY(xml.contains(QLatin1String("<w:shd w:val=\"clear\" w:color=\"auto\" w:fill=\"F0FFF4\"/>")));
        // rPr 子元素 schema 序：rFonts → b → i → color → sz → shd（顺序违规
        // 触发 Word 修复框；用整段 rPr 正则锁定）
        QVERIFY(QRegularExpression(
                    R"(<w:rPr><w:rFonts[^>]*/><w:b/><w:color w:val="005CC5"/><w:sz w:val="22"/></w:rPr>)")
                    .match(xml)
                    .hasMatch());
    }

    void testCodeBlockPlainRegression()
    {
        // 无高亮链路（降级/旧文档）：纯文本代码块行为不变——逐行段落、灰底、
        // 无 w:color
        const QString xml = exportDocumentXml(QStringLiteral(
            "<pre><code class=\"language-cpp\">int main()\n{\n\treturn 0;\n}</code></pre>"));
        QVERIFY(!xml.contains(QLatin1String("<w:color w:val=")));
        QVERIFY(xml.contains(QLatin1String("w:fill=\"F2F2F2\"")));
        // 4 行 → 4 个代码段落（pPr 前缀计数）
        QCOMPARE(xml.count(QLatin1String("<w:shd w:val=\"clear\" w:color=\"auto\" w:fill=\"F2F2F2\"/>")), 4);
        QVERIFY(xml.contains(QLatin1String("return 0;")));
    }
```

- [ ] **Step 2: 跑测试确认失败**

Run: `cmake --build build -j$(nproc) && cd build && ctest -R test_docxexporter --output-on-failure`
Expected: FAIL（新用例红：现实现跳过 span 标签、无 w:color）

- [ ] **Step 3: 实现**

`DocxExporter.cpp` 在 `extractAlign`（约 399–405 行）之后加辅助函数：

```cpp
// style 属性中提取 font-weight / font-style 布尔标记（导出高亮 span 的
// 粗斜体；主题值为 700/bold、italic，兼容 >=600 数值权重）
static bool extractFontFlag(const QString &style, const QString &prop)
{
    QRegularExpression re("(?:^|;\\s*)" + prop + "\\s*:\\s*([^;]+)",
                          QRegularExpression::CaseInsensitiveOption);
    auto m = re.match(style);
    if (!m.hasMatch()) return false;
    const QString v = m.captured(1).trimmed();
    if (v.compare(QLatin1String("bold"), Qt::CaseInsensitive) == 0) return true;
    bool ok = false;
    const int n = v.toInt(&ok);
    return ok && n >= 600;
}

static bool extractItalicFlag(const QString &style)
{
    QRegularExpression re("(?:^|;\\s*)font-style\\s*:\\s*([^;]+)",
                          QRegularExpression::CaseInsensitiveOption);
    auto m = re.match(style);
    return m.hasMatch()
           && m.captured(1).trimmed().compare(QLatin1String("italic"),
                                              Qt::CaseInsensitive) == 0;
}
```

（`extractItalicFlag` 单列而非复用 `extractFontFlag`：italic 无数值权重语义。）

`parseCodeBlock`（884–923 行）整体替换为：

```cpp
static QString parseCodeBlock(QXmlStreamReader &r, ParseState &s)
{
    Q_UNUSED(s);
    // 收集 <pre> 内片段流：导出高亮链路（getEditorHtmlAsync →
    // highlightExportHtml）把代码块内容替换为扁平 inline-style span（嵌套
    // 已在 JS 侧按 CSS 级联合成有效样式，此处无需样式栈——仍留一层防御性
    // 栈以容忍意外嵌套：内层非空值就近覆盖）。无高亮时（降级/旧链路）只有
    // 纯文本，行为与改造前一致
    struct Seg { QString text; QString color; QString bg; bool bold; bool italic; };
    struct Frame { QString color; QString bg; bool bold; bool italic; };
    QVector<Frame> stack;
    QList<Seg> segs;
    auto pushText = [&segs, &stack](const QString &text) {
        if (text.isEmpty()) return;
        QString color, bg;
        bool bold = false, italic = false;
        for (int i = stack.size() - 1; i >= 0; --i) {   // 内层非空值就近覆盖
            if (color.isEmpty()) color = stack[i].color;
            if (bg.isEmpty()) bg = stack[i].bg;
            bold = bold || stack[i].bold;
            italic = italic || stack[i].italic;
        }
        segs.append({text, color, bg, bold, italic});
    };
    QString textAcc;
    while (!r.atEnd()) {
        auto tt = r.readNext();
        if (tt == QXmlStreamReader::EndElement
            && r.name().toString().compare(QLatin1String("pre"), Qt::CaseInsensitive) == 0)
            break;
        if (tt == QXmlStreamReader::StartElement) {
            pushText(textAcc);
            textAcc.clear();
            const QString tag = r.name().toString().toLower();
            if (tag == "span") {
                const QString sty = r.attributes().value("style").toString();
                stack.append({extractColor(sty, "color"),
                              extractColor(sty, "background-color"),
                              extractFontFlag(sty, "font-weight"),
                              extractItalicFlag(sty)});
            }
            // 其他行内标签（b/i 等）不处理：扁平链路全部语义已在 span style 中
            continue;
        }
        if (tt == QXmlStreamReader::EndElement) {
            if (r.name().toString().compare(QLatin1String("span"), Qt::CaseInsensitive) != 0)
                continue;
            pushText(textAcc);
            textAcc.clear();
            if (!stack.isEmpty()) stack.removeLast();
            continue;
        }
        if (tt == QXmlStreamReader::Characters)
            textAcc += r.text();
    }
    pushText(textAcc);

    // 逐行生成 <w:p>，等宽字体 + 灰底；行内按片段生成多个 <w:r>。
    // CT_PPrBase 子元素须按 schema 序列（… → shd → … → spacing → ind → …），
    // ind 不得早于 spacing——顺序违规触发 Word「无法读取的内容」修复框
    QString pPr = "<w:pPr>"
                  "<w:shd w:val=\"clear\" w:color=\"auto\" w:fill=\"F2F2F2\"/>"
                  "<w:spacing w:before=\"0\" w:after=\"0\" w:line=\"240\" w:lineRule=\"auto\"/>"
                  "<w:ind w:left=\"360\" w:right=\"360\"/>"
                  "</w:pPr>";

    // run 的 rPr 子元素按 CT_RPr schema 序：rFonts → b → i → color → sz → shd
    auto runXml = [](const Seg &seg) {
        QString rpr = "<w:rPr><w:rFonts w:ascii=\"Consolas\" w:hAnsi=\"Consolas\" w:cs=\"Consolas\"/>";
        if (seg.bold) rpr += "<w:b/>";
        if (seg.italic) rpr += "<w:i/>";
        if (!seg.color.isEmpty()) rpr += "<w:color w:val=\"" + seg.color + "\"/>";
        rpr += "<w:sz w:val=\"22\"/>";
        if (!seg.bg.isEmpty())
            rpr += "<w:shd w:val=\"clear\" w:color=\"auto\" w:fill=\"" + seg.bg + "\"/>";
        rpr += "</w:rPr>";
        return "<w:r>" + rpr + "<w:t xml:space=\"preserve\">"
               + xmlEscape(seg.text) + "</w:t></w:r>";
    };

    QString result;
    QString lineRuns;            // 当前行已生成的 run XML
    bool lineHasRun = false;
    auto closeLine = [&]() {
        if (lineHasRun)
            result += "<w:p>" + pPr + lineRuns + "</w:p>\n";
        else
            result += "<w:p>" + pPr + "</w:p>\n";   // 空行仍需显式段落，否则 Word 折叠
        lineRuns.clear();
        lineHasRun = false;
    };
    for (const Seg &seg : segs) {
        const QStringList pieces = seg.text.split('\n');
        for (int i = 0; i < pieces.size(); ++i) {
            if (i > 0) closeLine();
            if (pieces[i].isEmpty()) continue;
            Seg piece = seg;
            piece.text = pieces[i];
            lineRuns += runXml(piece);
            lineHasRun = true;
        }
    }
    closeLine();
    return result;
}
```

- [ ] **Step 4: 跑测试确认通过**

Run: `cmake --build build -j$(nproc) && cd build && ctest -R test_docxexporter --output-on-failure`
Expected: PASS（新 3 用例 + 既有全部用例绿——既有用例中如有对代码块旧行为的断言，须逐条核对仍成立）

- [ ] **Step 5: 暂不提交**

---

### Task 5: MainWindow 导出链路接入（mode 枚举 + 高亮步）

**Files:**
- Modify: `src/ui/MainWindow.cpp:918-939`（getEditorHtmlAsync 重构）+ 新增 `highlightForExportAsync` + 三处调用点（PDF 约 958 行、DOCX 约 1037/1085 行）

**Interfaces:**
- Consumes: Task 2 的 `window.editorApi.highlightExportHtml(html)`；既有 `escapeForJsTemplate`（MainWindow.cpp:686）
- Produces: `enum class ExportHtmlMode { Plain, Docx, Pdf };`（文件级）；`getEditorHtmlAsync(tab, cb, mode = Plain)`

- [ ] **Step 1: 改 getEditorHtmlAsync**

MainWindow.cpp 918–939 行整体替换为：

```cpp
// 导出取内容的链路形态：Plain=直接编辑器 HTML（HTML 导出）；Docx=live
// colgroup 注入（供 DocxExporter 生成 tblGrid）；Pdf=与 Plain 相同取法
//（PDF 表格列宽由渲染页 table-column-layout.js 现场处理，勿注入编辑器窗口
// 宽的 colgroup）。Docx/Pdf 均追加代码块语法高亮步
enum class ExportHtmlMode { Plain, Docx, Pdf };

// 导出前代码块语法高亮（实现见 web/export-highlight.js：与编辑器同一
// lowlight 实例 + hljs-light 色板）。模板字面量传大 HTML 与源码模式路径
// 同款（escapeForJsTemplate）；任何失败两侧都降级原文，导出不中断
static void highlightForExportAsync(EditorTab *tab, const QString &html,
                                    std::function<void(const QString &)> cb)
{
    if (!tab || !tab->webView() || html.isEmpty()
        || !html.contains(QLatin1String("language-"))) {
        cb(html);
        return;
    }
    tab->webView()->page()->runJavaScript(
        QStringLiteral("window.editorApi.highlightExportHtml(`%1`);")
            .arg(escapeForJsTemplate(html)),
        [html, cb](const QVariant &result) {
            const QString s = result.toString();
            cb(s.isEmpty() ? html : s);
        });
}

// 异步取编辑器 HTML：源码模式下编辑器文档是进入源码前的旧快照（内容在
// source-view textarea），取 getSourceContent() 经 toHtml 转换，用户保持源码
// 模式不动；WYSIWYG 模式按 mode 取 getContent()/getExportHtml()
static void getEditorHtmlAsync(EditorTab *tab,
                               std::function<void(const QString &)> cb,
                               ExportHtmlMode mode = ExportHtmlMode::Plain)
{
    if (!tab || !tab->webView())
        return;
    const bool src = tab->isSourceMode();
    // DOCX 导出取 getExportHtml()：live colgroup 的 px 列宽注入序列化 HTML，
    // 供 DocxExporter 生成 tblGrid；源码模式 getSourceContent 经 toHtml 无
    // colgroup，退回等宽网格（现状行为）
    const QString call = src ? QStringLiteral("window.editorApi.getSourceContent();")
                             : (mode == ExportHtmlMode::Docx
                                    ? QStringLiteral("window.editorApi.getExportHtml();")
                                    : QStringLiteral("window.editorApi.getContent();"));
    tab->webView()->page()->runJavaScript(call,
        [tab, src, mode, cb](const QVariant &result) {
            QString s = result.toString();
            const QString html = src ? MarkdownHandler::toHtml(s) : s;
            if (mode == ExportHtmlMode::Plain) {
                cb(html);
                return;
            }
            // PDF/DOCX：代码块语法高亮（单一来源 export-highlight.js；
            // JS/C++ 两侧均可降级原文）
            highlightForExportAsync(tab, html, cb);
        });
}
```

- [ ] **Step 2: 更新三处调用点**

1. PDF（onExportPdf 内约 958 行）：`getEditorHtmlAsync(tab, [this, path, docDir](...) {...});` 末尾加第三参：

```cpp
        }, ExportHtmlMode::Pdf);
```

2. DOCX（onExportDocx 内约 1085 行）：`/*forDocx=*/true` 改为 `ExportHtmlMode::Docx`

3. HTML 导出（onExportHtml 约 1104 行）：不动（默认 Plain）

- [ ] **Step 3: 编译验证**

Run: `cmake --build build -j$(nproc)`
Expected: 编译通过无警告（lambda 捕获合规——`tab/mode/cb` 均按值捕获，无 GCC 宽容陷阱）

Run: `cd build && ctest --output-on-failure`
Expected: 全部 PASS（无行为回归）

- [ ] **Step 4: 暂不提交**

---

### Task 6: 全量构建 + 全部测试绿 + 二进制新鲜度确认

**Files:** 无新改动（验证任务）

- [ ] **Step 1: 全量构建**（qrc 与 C++ 均已变，必须重建）

Run: `cmake --build build -j$(nproc)`
Expected: 构建成功

- [ ] **Step 2: JS 全量测试**

Run: `cd src/resources/web && npx vitest run`
Expected: 全部 PASS

- [ ] **Step 3: C++ 全量测试**

Run: `cd build && ctest --output-on-failure`
Expected: 全部 PASS

- [ ] **Step 4: 二进制新鲜度确认**（CLAUDE.md 规则 9）

Run: `ls -l build/src/md-editor src/resources/web/export-highlight.js src/resources/web/pdf-split-prelines.js src/core/PdfExporter.cpp src/core/DocxExporter.cpp src/ui/MainWindow.cpp`
Expected: 二进制 mtime 新于所有改动源文件

---

### Task 7: 端到端验证（用户参与）+ 统一提交

**Files:** 无代码改动（验证 + 提交）

- [ ] **Step 1: 准备样本文档**

`/tmp/md-editor-claude-20260927/sample-highlight.md`，覆盖：cpp / js / python 代码块（多语言）、含块注释跨 5+ 行的块（多行 token 拆行）、diff 代码块（增删行背景）、markdown 代码块（标题粗体/强调斜体）、mermaid 块（应照旧渲染为图，不着色）、无语言代码块、长代码块（60+ 行，验证跨页拆行不丢行不丢色）。

- [ ] **Step 2: 导出 PDF + DOCX**

优先由用户在 GUI 里加载样本导出（最直接）；若需无人值守取证，用无头配方，要点：`env -u WAYLAND_DISPLAY QT_QPA_PLATFORM=xcb` 起 `Xvfb :99` 后运行应用，导出 `sample-highlight.pdf` 与 `sample-highlight.docx` 至 `/tmp/md-editor-claude-20260927/`，结束清理进程。

- [ ] **Step 3: 自动断言**

- DOCX：`unzip -p sample-highlight.docx word/document.xml | grep -c 'w:color w:val="D73A49"'` ≥ 1；同法抽 `6A737D`（注释）、`F0FFF4`（diff 底纹）
- PDF：`pdftotext sample-highlight.pdf - | grep -c 'main'` 抽查代码文本在（颜色 pdftotext 不可见，着色由 Step 4 目视把关）

- [ ] **Step 4: 用户目视验证**

- PDF（亮色系统主题）：代码块着色与编辑器亮色主题一致；多行注释跨行颜色连续；跨页代码块每行完整且带色；diff 增删行有绿/红底；mermaid 仍为图
- PDF（**切系统暗色主题再导一次**）：着色仍为亮色色板——覆盖规则装载的样式表/fetch 双路径（暗色下编辑器链接禁用，导出不得跟着变暗）
- DOCX：**用户在 macOS Word 双击打开验证**（无 schema 修复框、颜色/粗斜体/底纹正确、跨行 token 每行带色）

- [ ] **Step 5: 统一提交**（用户验证通过后）

```bash
git add src/resources/web/export-highlight.js \
        src/resources/web/export-highlight.test.js \
        src/resources/web/export-highlight-dom.test.js \
        src/resources/web/pdf-split-prelines.js \
        src/resources/web/pdf-split-prelines.test.js \
        src/resources/web/editor.js \
        src/resources/web/editor.html \
        src/resources/app.qrc \
        src/core/PdfExporter.cpp \
        src/core/DocxExporter.cpp \
        tests/test_docxexporter.cpp \
        src/ui/MainWindow.cpp \
        docs/superpowers/specs/2026-09-27-export-code-highlight-design.md \
        docs/superpowers/plans/2026-09-27-export-code-highlight.md
git commit -m "feat: 导出 PDF/DOCX 代码块语法高亮（与编辑器同引擎同色板）

Co-Authored-By: Claude Code <noreply@anthropic.com>"
```

提交前先 `git status` + `git diff --stat` 复核暂存集（多 harness 并行教训：确认无计划外文件混入）。
