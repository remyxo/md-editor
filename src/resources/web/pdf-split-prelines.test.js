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

    it('多行元素内部空行保位（修复轮 1：中行 nbsp，两侧片段保色）', () => {
        loadModule();
        document.body.innerHTML =
            '<pre><code id="c">x<span style="color:#d73a49">c1\n\nc2</span>y</code></pre>';
        window.__splitPreLines();
        const ls = lines();
        expect(ls.length).toBe(3);
        expect(ls[0].textContent).toBe('xc1');
        expect(ls[0].querySelector('span[style*="#d73a49"]').textContent).toBe('c1');
        expect(ls[1].textContent).toBe('\u00A0');   // 空行保位：nbsp 占位
        expect(ls[2].textContent).toBe('c2y');
        expect(ls[2].querySelector('span[style*="#d73a49"]').textContent).toBe('c2');
    });

    it('元素内单个换行拆行不并行（修复轮 1：a<span>\\n</span>b 得两行）', () => {
        loadModule();
        document.body.innerHTML = '<pre><code id="c">a<span>\n</span>b</code></pre>';
        window.__splitPreLines();
        const ls = lines();
        expect(ls.length).toBe(2);
        expect(ls.map((x) => x.textContent)).toEqual(['a', 'b']);
    });
});
