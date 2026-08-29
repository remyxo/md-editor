// @vitest-environment jsdom
// 退出源码模式快速路径回归测试：
// 源码未编辑时退出不得销毁重建编辑器——
//   性能：大文档 setContent 全量重建 >1s（代码块高亮/mermaid/KaTeX NodeView）；
//   正确性：重建副作用会清零撤销历史、把脏标记误清（WYSIWYG 有未保存编辑时
//   进源码看一眼再退出，标签页 ● 前缀被摘除，关闭时无保存提示）。
// 源码已编辑时必须重建（内容已变，走 toHtml 输出全量替换）。
import { describe, it, expect, beforeAll } from 'vitest';
import { readFileSync } from 'fs';
import { fileURLToPath } from 'url';
import path from 'path';

const W = path.dirname(fileURLToPath(import.meta.url));

function loadEditor() {
  window.eval(readFileSync(path.join(W, 'tiptap-all.js'), 'utf-8'));
  window.eval(readFileSync(path.join(W, 'editor.js'), 'utf-8'));
}

let contentChangedCalls = [];
beforeAll(() => {
  // jsdom 未实现 window.scrollTo 与 Element.scrollIntoView，桩掉避免报错
  window.scrollTo = () => {};
  window.HTMLElement.prototype.scrollIntoView = () => {};
  document.body.innerHTML =
    '<div id="editor"></div>' +
    '<div id="source-wrap" style="display:none">' +
    '<pre id="source-overlay"></pre><textarea id="source-view"></textarea></div>';
  loadEditor();
  window.editorApi.init();
  contentChangedCalls = [];
  window.editorApi.bridge = new Proxy({}, {
    get: (t, k) => {
      if (k === 'contentChanged') return (dirty) => { contentChangedCalls.push(dirty); };
      return () => {};
    }
  });
});

describe('退出源码模式快速路径', () => {
  it('源码未编辑：退出不销毁重建编辑器（实例引用不变，内容保留）', () => {
    window.editorApi.setContent('<p>hello world</p>');
    const editorBefore = window.editorApi.editor;
    window.editorApi.enterSourceMode('hello world\n');
    window.editorApi.exitSourceMode('<p>hello world</p>');
    expect(window.editorApi.editor).toBe(editorBefore);
    expect(window.editorApi.isSourceMode).toBe(false);
    expect(window.editorApi.editor.getHTML()).toContain('hello world');
  });

  it('源码未编辑：撤销历史保留（看一眼源码再切回可继续撤销）', () => {
    window.editorApi.setContent('<p>a</p>');
    window.editorApi.editor.commands.insertContent('<p>b</p>');
    expect(window.editorApi.editor.can().undo()).toBe(true);
    window.editorApi.enterSourceMode('a\n\nb\n');
    window.editorApi.exitSourceMode('<p>a</p><p>b</p>');
    expect(window.editorApi.editor.can().undo()).toBe(true);
  });

  it('源码未编辑：脏标记保留（未保存的 WYSIWYG 编辑不因进出源码而清）', () => {
    window.editorApi.setContent('<p>x</p>');
    window.editorApi._markDirty();
    contentChangedCalls = [];
    window.editorApi.enterSourceMode('x\n');
    window.editorApi.exitSourceMode('<p>x</p>');
    expect(window.editorApi._dirty).toBe(true);
    expect(contentChangedCalls).not.toContain(false);
  });

  it('源码已编辑：退出仍全量重建（新内容生效、标记脏）', () => {
    window.editorApi.setContent('<p>old</p>');
    const editorBefore = window.editorApi.editor;
    window.editorApi.enterSourceMode('old\n');
    document.getElementById('source-view').value = 'new content\n';
    window.editorApi.exitSourceMode('<p>new content</p>');
    expect(window.editorApi.editor).not.toBe(editorBefore);   // 编辑器已重建
    expect(window.editorApi.editor.getText()).toContain('new content');
    expect(window.editorApi._dirty).toBe(true);
  });

  it('exitSourceModeOrGetMd 未编辑：单调用内完成切换并返回 true', () => {
    window.editorApi.setContent('<p>hello</p>');
    const editorBefore = window.editorApi.editor;
    window.editorApi.enterSourceMode('hello\n');
    const ret = window.editorApi.exitSourceModeOrGetMd();
    expect(ret).toBe(true);                                   // C++ 免 toHtml
    expect(window.editorApi.isSourceMode).toBe(false);
    expect(window.editorApi.editor).toBe(editorBefore);       // 未重建
  });

  it('exitSourceModeOrGetMd 已编辑：返回待转换 MD，随后 exitSourceMode 重建', () => {
    window.editorApi.setContent('<p>old</p>');
    window.editorApi.enterSourceMode('old\n');
    document.getElementById('source-view').value = 'edited md\n';
    const ret = window.editorApi.exitSourceModeOrGetMd();
    expect(ret).toBe('edited md\n');                          // 返回源码 MD
    expect(window.editorApi.isSourceMode).toBe(true);         // 尚未切换
    window.editorApi.exitSourceMode('<p>edited md</p>');      // C++ 转换后回调
    expect(window.editorApi.isSourceMode).toBe(false);
    expect(window.editorApi.editor.getText()).toContain('edited md');
  });
});
