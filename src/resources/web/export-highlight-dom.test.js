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
