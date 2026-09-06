// @vitest-environment jsdom
// 脚注孤儿定义清理测试（Task 10 R1）：
// - appendTransaction 级联：最后一个引用被删 → 定义连带删；容器空则连容器删
// - 红线：setContent 全文档替换不级联（文件加载不得静默删内容）
// - 源码模式例外：exitSourceMode 重建后显式清理（排除进入前已孤立的 id）
// - 撤销：一次 undo 同时恢复引用与定义
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

// 加载模式照 footnote-nav.test.js：jsdom body 需先备 #editor 挂载点。
// 另备源码模式脚手架（#source-wrap/#source-view/#source-overlay，照
// modeswitch-search.test.js），供接线级用例真实走 enter/exitSourceMode；
// jsdom 未实现 scrollIntoView/scrollTo（exitSourceMode 恢复光标时调用），桩掉。
beforeAll(() => {
  window.scrollTo = function () {};
  window.HTMLElement.prototype.scrollIntoView = function () {};
  document.body.innerHTML =
    '<div id="editor"></div>' +
    '<div id="source-wrap" style="display:none">' +
    '<pre id="source-overlay"></pre><textarea id="source-view"></textarea></div>';
  loadEditor();
});

function setup(api, html) {
  api.init();
  api.editor.commands.setContent(html);
}

function containerCount(api) {
  let n = 0;
  api.editor.state.doc.descendants((node) => {
    if (node.type.name === 'footnoteContainer') n += 1;
    return true;
  });
  return n;
}

// 两 def 共容器的常用文档（ref1 唯一引用 id=1）
const DOC_TWO_DEFS =
  '<p>a<sup data-type="footnoteRef" data-id="1">1</sup></p>' +
  '<div data-type="footnoteContainer">' +
  '<div data-type="footnoteDef" data-id="1"><div class="footnote-content"><p>x</p></div></div>' +
  '<div data-type="footnoteDef" data-id="2"><div class="footnote-content"><p>y</p></div></div>' +
  '</div>';

// 单 def 容器（删唯一引用后容器应变空）
const DOC_ONE_DEF =
  '<p>a<sup data-type="footnoteRef" data-id="1">1</sup></p>' +
  '<div data-type="footnoteContainer">' +
  '<div data-type="footnoteDef" data-id="1"><div class="footnote-content"><p>x</p></div></div>' +
  '</div>';

describe('脚注孤儿定义清理', () => {
  it('删唯一引用 → 定义连带删；容器内还有其他定义则保留', () => {
    const api = window.editorApi;
    setup(api, DOC_TWO_DEFS);
    const refPos = api.findFootnoteRefPos('1');
    expect(refPos).toBeGreaterThan(0);
    api.editor.commands.deleteRange({ from: refPos, to: refPos + 1 });
    expect(api.findFootnoteRefPos('1')).toBe(-1);
    expect(api.findFootnoteDefPos('1')).toBe(-1);          // 定义级联删
    expect(api.findFootnoteDefPos('2')).toBeGreaterThan(0); // 其余定义保留
    expect(containerCount(api)).toBe(1);                    // 容器非空不删
  });

  it('删唯一引用 → 空容器一并删除', () => {
    const api = window.editorApi;
    setup(api, DOC_ONE_DEF);
    const refPos = api.findFootnoteRefPos('1');
    api.editor.commands.deleteRange({ from: refPos, to: refPos + 1 });
    expect(api.findFootnoteDefPos('1')).toBe(-1);
    expect(containerCount(api)).toBe(0);                    // 容器空 → 连容器删
  });

  it('同 id 两引用删其一 → 定义保留；最后一个也没了才清理', () => {
    const api = window.editorApi;
    setup(api,
      '<p>a<sup data-type="footnoteRef" data-id="1">1</sup></p>' +
      '<p>b<sup data-type="footnoteRef" data-id="1">1</sup></p>' +
      '<div data-type="footnoteContainer">' +
      '<div data-type="footnoteDef" data-id="1"><div class="footnote-content"><p>x</p></div></div>' +
      '</div>');
    const first = api.findFootnoteRefPos('1');
    api.editor.commands.deleteRange({ from: first, to: first + 1 });
    expect(api.findFootnoteRefPos('1')).toBeGreaterThan(0); // 还有第二个引用
    expect(api.findFootnoteDefPos('1')).toBeGreaterThan(0); // 定义保留
    expect(containerCount(api)).toBe(1);
    const second = api.findFootnoteRefPos('1');
    api.editor.commands.deleteRange({ from: second, to: second + 1 });
    expect(api.findFootnoteDefPos('1')).toBe(-1);           // 最后一个删了才级联
    expect(containerCount(api)).toBe(0);
  });

  it('红线：setContent 含孤儿定义（无对应引用）→ 原样保留', () => {
    const api = window.editorApi;
    setup(api,
      '<p>plain</p>' +
      '<div data-type="footnoteContainer">' +
      '<div data-type="footnoteDef" data-id="9"><div class="footnote-content"><p>孤儿</p></div></div>' +
      '</div>');
    expect(api.findFootnoteDefPos('9')).toBeGreaterThan(0); // 全文档替换不级联
    expect(containerCount(api)).toBe(1);
  });

  it('全文档替换被守卫跳过后，后续普通删除仍级联（守卫只作用于该事务）', () => {
    const api = window.editorApi;
    setup(api, DOC_TWO_DEFS);
    const refPos = api.findFootnoteRefPos('1');
    api.editor.commands.deleteRange({ from: refPos, to: refPos + 1 });
    expect(api.findFootnoteDefPos('1')).toBe(-1);
  });

  it('renameFootnote 改名 → 不触发清理（新 id 定义保留）', () => {
    const api = window.editorApi;
    setup(api, DOC_ONE_DEF);
    api.renameFootnote('1', '注A');
    expect(api.findFootnoteRefPos('注A')).toBeGreaterThan(0);
    expect(api.findFootnoteDefPos('注A')).toBeGreaterThan(0); // 定义随改名保留
    expect(api.findFootnoteDefPos('1')).toBe(-1);
  });

  it('删除后 undo → 引用与定义同时恢复', async () => {
    const api = window.editorApi;
    setup(api, DOC_ONE_DEF);
    // 隔开 setContent 与删除的历史分组（prosemirror-history 的 newGroupDelay
    // 默认 500ms 内相邻事务并组，否则一次 undo 会连 setContent 一起回退到
    // 初始空文档）。真实场景中加载与用户删除天然间隔远超该窗口。
    await new Promise((r) => setTimeout(r, 600));
    const refPos = api.findFootnoteRefPos('1');
    api.editor.commands.deleteRange({ from: refPos, to: refPos + 1 });
    expect(api.findFootnoteDefPos('1')).toBe(-1);
    api.editor.commands.undo();
    expect(api.findFootnoteRefPos('1')).toBeGreaterThan(0); // 引用回来
    expect(api.findFootnoteDefPos('1')).toBeGreaterThan(0); // 定义同时回来
    expect(containerCount(api)).toBe(1);
  });

  it('源码模式退出清理：排除进入前已孤立的 id，仅清新近孤儿', () => {
    const api = window.editorApi;
    setup(api,
      '<p>a<sup data-type="footnoteRef" data-id="1">1</sup></p>' +
      '<div data-type="footnoteContainer">' +
      '<div data-type="footnoteDef" data-id="1"><div class="footnote-content"><p>x</p></div></div>' +
      '<div data-type="footnoteDef" data-id="9"><div class="footnote-content"><p>旧孤儿</p></div></div>' +
      '</div>');
    // 模拟用户在源码里删掉 ref 1 后退出：重建文档无 ref 1、def 1 成新孤儿；
    // id 9 在进入源码模式时已是孤儿（文件固有），须保留
    api.editor.commands.setContent(
      '<p>a</p>' +
      '<div data-type="footnoteContainer">' +
      '<div data-type="footnoteDef" data-id="1"><div class="footnote-content"><p>x</p></div></div>' +
      '<div data-type="footnoteDef" data-id="9"><div class="footnote-content"><p>旧孤儿</p></div></div>' +
      '</div>');
    api._cleanupFootnoteOrphans({ '9': true });            // exitSourceMode 传入进入前快照
    expect(api.findFootnoteDefPos('1')).toBe(-1);           // 新孤儿被清
    expect(api.findFootnoteDefPos('9')).toBeGreaterThan(0); // 旧孤儿保留
    api._cleanupFootnoteOrphans(null);                      // 无排除：全清
    expect(api.findFootnoteDefPos('9')).toBe(-1);
  });

  it('接线级：enterSourceMode 快照 → 源码删引用 → exitSourceMode 真实链路清理', () => {
    const api = window.editorApi;
    // 文档：正常脚注 1（ref+def）+ 文件固有孤儿 def 9（有 def 无 ref）
    setup(api,
      '<p>a<sup data-type="footnoteRef" data-id="1">1</sup></p>' +
      '<div data-type="footnoteContainer">' +
      '<div data-type="footnoteDef" data-id="1"><div class="footnote-content"><p>x</p></div></div>' +
      '<div data-type="footnoteDef" data-id="9"><div class="footnote-content"><p>旧孤儿</p></div></div>' +
      '</div>');
    // 真实进入源码模式：enterSourceMode 内部记录孤儿快照（含 9）
    const md = 'a[^1]\n\n[^1]: x\n\n[^9]: 旧孤儿\n';
    api.enterSourceMode(md);
    expect(api.isSourceMode).toBe(true);
    // 模拟用户在源码里删掉引用 [^1]（程序化改 value 不触发 input 事件，
    // exitSourceMode 以 value 与 _sourceValue 的差异判 changed，语义一致）
    document.getElementById('source-view').value = 'a\n\n[^9]: 旧孤儿\n';
    // 真实退出：html 为删除引用后的重建文档（C++ toHtml 产物形态）——
    // def 1 成新孤儿，def 9 仍在
    api.exitSourceMode(
      '<p>a</p>' +
      '<div data-type="footnoteContainer">' +
      '<div data-type="footnoteDef" data-id="1"><div class="footnote-content"><p>x</p></div></div>' +
      '<div data-type="footnoteDef" data-id="9"><div class="footnote-content"><p>旧孤儿</p></div></div>' +
      '</div>');
    expect(api.isSourceMode).toBe(false);
    expect(api.findFootnoteDefPos('9')).toBeGreaterThan(0); // 红线：文件固有孤儿存活
    expect(api.findFootnoteDefPos('1')).toBe(-1);           // 用户编辑新孤儿被清理
    expect(api.findFootnoteRefPos('1')).toBe(-1);
  });
});
