// @vitest-environment jsdom
// 脚注往返保真测试：真实 editor.js 全扩展链路 setContent → getContent
//（对应 CLAUDE.md 往返保真原则：MD→HTML→编辑器解析→HTML 不得丢失语义信息）
import { describe, it, expect, beforeAll } from 'vitest';
import { readFileSync } from 'fs';
import { fileURLToPath } from 'url';
import path from 'path';

const W = path.dirname(fileURLToPath(import.meta.url));

// 与 tiptap-gethtml 手动验证相同的最小加载集（prosemirror 全家 + tiptap-core +
// 单独扩展 + editor.js）。tiptap-all.js 聚合了全部，直接 eval 它最省事。
function loadEditor() {
  const libs = [
    'prosemirror-model.js', 'prosemirror-state.js', 'prosemirror-transform.js',
    'prosemirror-view.js', 'tiptap-core.js', 'lowlight.js', 'searchPlugin.js',
  ];
  for (const f of libs) {
    try { window.eval(readFileSync(path.join(W, f), 'utf-8')); } catch (e) { /* 单独扩展由 tiptap-all 聚合 */ }
  }
  window.eval(readFileSync(path.join(W, 'katex.min.js'), 'utf-8'));
  window.eval(readFileSync(path.join(W, 'tiptap-all.js'), 'utf-8'));
  window.eval(readFileSync(path.join(W, 'editor.js'), 'utf-8'));
}

describe('脚注往返保真（真实 TipTap 链路）', () => {
  beforeAll(() => {
    document.body.innerHTML = '<div id="editor"></div>';
    loadEditor();
    window.editorApi.init();
  });

  it('footnoteRef 解析保留 data-type/data-id（不被上标 mark 吞掉）', () => {
    const src = '<p>引用<sup data-type="footnoteRef" data-id="1">1</sup>。</p>';
    window.editorApi.setContent(src);
    const out = window.editorApi.getContent();
    expect(out).toContain('data-type="footnoteRef"');
    expect(out).toContain('data-id="1"');
  });

  it('footnoteDef 内容不含 marker 文本（marker 与正文分离）', () => {
    const src =
      '<div data-type="footnoteContainer">' +
      '<div data-type="footnoteDef" data-id="1">' +
      '<span class="footnote-marker">1</span>' +
      '<span class="footnote-content">脚注内容</span>' +
      '</div></div>';
    window.editorApi.setContent(src);
    const out = window.editorApi.getContent();
    // footnote-content 序列化后正文应恰为“脚注内容”，不含 marker 前缀
    const m = out.match(/footnote-content"[^>]*>([^<]*)</);
    expect(m ? m[1] : '').toBe('脚注内容');
  });

  it('footnoteDef 往返多次 marker 不叠加（连续 setContent 两次）', () => {
    const src =
      '<div data-type="footnoteContainer">' +
      '<div data-type="footnoteDef" data-id="注1">' +
      '<span class="footnote-marker">注1</span>' +
      '<span class="footnote-content">ABC</span>' +
      '</div></div>';
    window.editorApi.setContent(src);
    let out = window.editorApi.getContent();
    window.editorApi.setContent(out);
    out = window.editorApi.getContent();
    const m = out.match(/footnote-content"[^>]*>([^<]*)</);
    expect(m ? m[1] : '').toBe('ABC');
  });
});
