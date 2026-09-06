// @vitest-environment jsdom
// 脚注块级定义 schema 测试：footnoteDef content 'block+'（多段/列表可入）、
// renderHTML 产出 div.footnote-content（MarkdownHandler 块级契约对齐）、
// insertFootnote 创建空定义（block+ 最小内容 = 空段落）。
import { describe, it, expect, beforeAll } from 'vitest';
import { readFileSync } from 'fs';
import { fileURLToPath } from 'url';
import path from 'path';

const W = path.dirname(fileURLToPath(import.meta.url));

function loadEditor() {
  const libs = [
    'prosemirror-model.js', 'prosemirror-state.js', 'prosemirror-transform.js',
    'prosemirror-view.js', 'tiptap-core.js', 'lowlight.js', 'searchPlugin.js',
  ];
  for (const f of libs) {
    try { window.eval(readFileSync(path.join(W, f), 'utf8')); } catch (e) { /* tiptap-all 聚合 */ }
  }
  window.eval(readFileSync(path.join(W, 'katex.min.js'), 'utf8'));
  window.eval(readFileSync(path.join(W, 'tiptap-all.js'), 'utf8'));
  window.eval(readFileSync(path.join(W, 'editor.js'), 'utf8'));
}

// 加载模式照 line-gutter.test.js：jsdom body 需先备 #editor 挂载点，
// 否则 init 时 element 为空、view 不挂载（测试无法触达 schema 层）
beforeAll(() => {
  document.body.innerHTML = '<div id="editor"></div>';
  loadEditor();
});

describe('脚注块级定义 schema', () => {
  it('footnoteDef 接受多块内容（两段 + 列表）', () => {
    const api = window.editorApi;
    api.init();
    api.editor.commands.setContent(
      '<p>正文<sup data-type="footnoteRef" data-id="1">1</sup></p>' +
      '<div data-type="footnoteContainer">' +
      '<div data-type="footnoteDef" data-id="1">' +
      '<span class="footnote-marker">1</span>' +
      '<div class="footnote-content"><p>第一段</p><ul><li>甲</li></ul></div>' +
      '</div></div>');
    const defs = [];
    api.editor.state.doc.descendants(n => {
      if (n.type.name === 'footnoteDef') defs.push(n);
    });
    expect(defs).toHaveLength(1);
    expect(defs[0].childCount).toBe(2);
    expect(defs[0].child(0).type.name).toBe('paragraph');
    expect(defs[0].child(1).type.name).toBe('bulletList');
  });

  it('renderHTML 产出 div.footnote-content（块级契约）', () => {
    const api = window.editorApi;
    api.init();
    api.editor.commands.setContent(
      '<div data-type="footnoteContainer">' +
      '<div data-type="footnoteDef" data-id="1">' +
      '<div class="footnote-content"><p>x</p></div></div></div>');
    expect(api.editor.getHTML()).toContain('<div class="footnote-content">');
  });

  it('insertFootnote 空定义含一个空段落', () => {
    const api = window.editorApi;
    api.init();
    api.editor.commands.setContent('<p>文</p>');
    api.insertFootnote();
    let def = null;
    api.editor.state.doc.descendants(n => {
      if (!def && n.type.name === 'footnoteDef') def = n;
    });
    expect(def).not.toBeNull();
    expect(def.childCount).toBe(1);
    expect(def.child(0).type.name).toBe('paragraph');
  });
});
