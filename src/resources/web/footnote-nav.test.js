// @vitest-environment jsdom
// 脚注跳转纯函数测试：findFootnoteDefPos/findFootnoteRefPos 定位、
// _footnoteValidateId 校验规则（Task 10 复用）。
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

// 加载模式照 footnote-block.test.js：jsdom body 需先备 #editor 挂载点，
// 否则 init 时 element 为空、view 不挂载（失败原因会错报为环境问题）
beforeAll(() => {
  document.body.innerHTML = '<div id="editor"></div>';
  loadEditor();
});

describe('脚注定位助手', () => {
  it('findFootnoteDefPos/RefPos 按 id 定位（多次引用取首个 ref）', () => {
    const api = window.editorApi;
    api.init();
    api.editor.commands.setContent(
      '<p>前<sup data-type="footnoteRef" data-id="1">1</sup></p>' +
      '<p>后<sup data-type="footnoteRef" data-id="1">1</sup></p>' +
      '<div data-type="footnoteContainer">' +
      '<div data-type="footnoteDef" data-id="1"><div class="footnote-content"><p>x</p></div></div>' +
      '</div>');
    const refPos = api.findFootnoteRefPos('1');
    const defPos = api.findFootnoteDefPos('1');
    expect(refPos).toBeGreaterThan(0);
    expect(defPos).toBeGreaterThan(0);
    expect(defPos).toBeGreaterThan(refPos);
    expect(api.findFootnoteRefPos('不存在')).toBe(-1);
    expect(api.findFootnoteDefPos('不存在')).toBe(-1);
  });
  it('首次 ref 在第二次之前（首个引用语义）', () => {
    const api = window.editorApi;
    api.init();
    api.editor.commands.setContent(
      '<p>a<sup data-type="footnoteRef" data-id="1">1</sup></p>' +
      '<p>b<sup data-type="footnoteRef" data-id="1">1</sup></p>' +
      '<div data-type="footnoteContainer">' +
      '<div data-type="footnoteDef" data-id="1"><div class="footnote-content"><p>x</p></div></div>' +
      '</div>');
    const first = api.findFootnoteRefPos('1');
    const doc = api.editor.state.doc;
    let second = -1, seen = 0;
    doc.descendants((n, pos) => {
      if (n.type.name === 'footnoteReference' && n.attrs.id === '1') {
        seen += 1;
        if (seen === 2) second = pos;
      }
    });
    expect(first).toBeLessThan(second);
  });
});

describe('脚注改名校验', () => {
  it('_footnoteValidateId 拒空/非法字符/重名，接受正常改名', () => {
    const api = window.editorApi;
    api.init();
    api.editor.commands.setContent(
      '<p>a<sup data-type="footnoteRef" data-id="1">1</sup></p>' +
      '<div data-type="footnoteContainer">' +
      '<div data-type="footnoteDef" data-id="1"><div class="footnote-content"><p>x</p></div></div>' +
      '<div data-type="footnoteDef" data-id="2"><div class="footnote-content"><p>y</p></div></div>' +
      '</div>');
    expect(api._footnoteValidateId('', '1').ok).toBe(false);
    expect(api._footnoteValidateId('a[b', '1').ok).toBe(false);
    expect(api._footnoteValidateId('a b', '1').ok).toBe(false);
    expect(api._footnoteValidateId('a:b', '1').ok).toBe(false);
    expect(api._footnoteValidateId('2', '1').ok).toBe(false);      // 已存在
    expect(api._footnoteValidateId('2', '1').err).toBe('duplicate');
    expect(api._footnoteValidateId('注A', '1').ok).toBe(true);
    expect(api._footnoteValidateId('1', '1').ok).toBe(true);       // 原名直过
  });

  it('jumpToFootnoteDef/Ref：块边界目标不再产生非法 selection（Selection.near）', () => {
    // jsdom 无 scrollIntoView/CSS.escape 实现——stub/polyfill（跳转链用到）
    Element.prototype.scrollIntoView = Element.prototype.scrollIntoView || function () {};
    if (!window.CSS) window.CSS = {};
    if (!CSS.escape)
      CSS.escape = (s) => String(s).replace(/[^a-zA-Z0-9_ -￿-]/g, (c) => '\\' + c);
    const api = window.editorApi;
    api.setContent(
      '<p>首段文字<sup data-type="footnoteRef" data-id="f1">f1</sup></p>' +
      '<div data-type="footnoteContainer">' +
      '<div data-type="footnoteDef" data-id="f1"><div class="footnote-content"><p>脚注内容文字</p></div></div>' +
      '</div>');
    api.markClean();
    // PM 对非法 selection 端点会在 console.error（"endpoint not pointing into
    // inline content"）——Selection.near 修复后应为零噪音。捕获 console.error
    const errs = [];
    const orig = console.error;
    console.error = (...a) => errs.push(String(a[0]));
    try {
      expect(api.jumpToFootnoteDef('f1')).toBe(true);   // 定义是块级容器（边界目标）
      // 跳定义：光标落进 def 内容（> def 节点位）
      const defPos = api.findFootnoteDefPos('f1');
      expect(api.editor.state.selection.from).toBeGreaterThan(defPos);
      expect(api.jumpToFootnoteRef('f1')).toBe(true);   // 引用是原子节点（后一位常是块界）
      // 跳引用：光标移回引用附近（≤ 引用节点位 + 1）
      const refPos = api.findFootnoteRefPos('f1');
      expect(api.editor.state.selection.from).toBeLessThanOrEqual(refPos + 1);
    } finally {
      console.error = orig;
    }
    expect(errs.filter(e => e.indexOf('endpoint') >= 0)).toEqual([]);
  });
});
