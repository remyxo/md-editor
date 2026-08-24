// @vitest-environment jsdom
// divBlock 前后空行回归测试（真实 TipTap + jsdom）。
//
// 背景：C++ toHtml 曾把 <div> 还原在 <p> 壳内（<p><div>…</div></p>），
// TipTap 解析非法嵌套时把块级 divBlock 提升出 <p>、空 <p> 壳留在原位，
// 渲染为 div 前后各一条空行。修复后 C++ 产出裸 <div>（与段落同级）。
// 本测试用真实 TipTap 固化两条契约：
//   1. 裸 <div>（修复后形态）解析后 divBlock 前后无空段落；
//   2. 嵌套形态确会产生空段落（记录机制，防止 C++ 侧回归到嵌套形态而无人察觉）。
import { describe, it, expect } from 'vitest';
import { Editor, Node } from '@tiptap/core';
import StarterKit from '@tiptap/starter-kit';

// 与 editor.js buildDivBlockExtension 的 divBlock 定义保持一致
const DivBlock = Node.create({
    name: 'divBlock',
    group: 'block',
    content: 'inline*',
    addAttributes() {
        return {
            style: {
                default: '',
                parseHTML: (el) => el.getAttribute('style') || '',
                renderHTML: (attrs) => attrs.style ? { style: attrs.style } : {},
            },
            align: {
                default: '',
                parseHTML: (el) => el.getAttribute('align') || '',
                renderHTML: (attrs) => attrs.align ? { align: attrs.align } : {},
            },
        };
    },
    parseHTML() {
        return [{ tag: 'div' }];
    },
    renderHTML({ HTMLAttributes }) {
        return ['div', HTMLAttributes, 0];
    },
});

function emptyParaCount(editor) {
    let count = 0;
    editor.state.doc.forEach((n) => {
        if (n.type.name === 'paragraph' && n.textContent === '') count++;
    });
    return count;
}

describe('divBlock 前后空行', () => {
    const BEFORE = '<h1>这是标题1,一般是文章的标题</h1>' +
        '<p>这是普通文本。<br>这是普通文本。</p>';
    const AFTER = '<h2>1. 这是标题2，一般表示章节</h2>';

    it('裸 <div>（C++ 修复后形态）：divBlock 前后无空段落', () => {
        const editor = new Editor({
            extensions: [StarterKit, DivBlock],
            content: BEFORE +
                '<div align="right">这是右对齐标题, 通过html标签实现</div>' + AFTER,
        });
        expect(emptyParaCount(editor)).toBe(0);
        // divBlock 应存在且携带 align
        let found = null;
        editor.state.doc.forEach((n) => {
            if (n.type.name === 'divBlock') found = n;
        });
        expect(found).not.toBeNull();
        expect(found.attrs.align).toBe('right');
        expect(found.textContent).toBe('这是右对齐标题, 通过html标签实现');
        editor.destroy();
    });

    it('嵌套 <p><div>（修复前形态）：会产生空段落（机制记录）', () => {
        const editor = new Editor({
            extensions: [StarterKit, DivBlock],
            content: BEFORE +
                '<p><div align="right">这是右对齐标题, 通过html标签实现</div></p>' + AFTER,
        });
        // 解析器提升块级 div 后留下空 <p> 壳 → 空段落 > 0。
        // 此断言固化「C++ 侧不得输出嵌套形态」的原因。
        expect(emptyParaCount(editor)).toBeGreaterThan(0);
        editor.destroy();
    });
});
