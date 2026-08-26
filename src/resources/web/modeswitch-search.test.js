// @vitest-environment jsdom
// WYSIWYG ↔ 源码模式切换时搜索计数延续回归测试：
// 源码搜索须在退出时同步回 WYSIWYG 插件 state，否则 _notifySearchCount 读空插件 → 0/0。
import { describe, it, expect, beforeAll } from 'vitest';
import { readFileSync } from 'fs';
import { fileURLToPath } from 'url';
import path from 'path';

const W = path.dirname(fileURLToPath(import.meta.url));

function loadEditor() {
  window.eval(readFileSync(path.join(W, 'tiptap-all.js'), 'utf-8'));
  window.eval(readFileSync(path.join(W, 'editor.js'), 'utf-8'));
}

let calls = [];
beforeAll(() => {
  // jsdom 未实现 window.scrollTo，桩掉避免 stderr 噪音
  window.scrollTo = () => {};
  document.body.innerHTML =
    '<div id="editor"></div>' +
    '<div id="source-wrap" style="display:none">' +
    '<pre id="source-overlay"></pre><textarea id="source-view"></textarea></div>';
  loadEditor();
  window.editorApi.init();
  calls = [];
  window.editorApi.bridge = new Proxy({}, {
    get: (t, k) => {
      if (k === 'searchMatchChanged') {
        return (total, current) => { calls.push({ total, current }); };
      }
      return () => {};
    }
  });
});

describe('模式切换搜索计数', () => {
  it('WYSIWYG 搜索 → 切源码模式 → 计数延续', () => {
    calls = [];
    // 6 处 hello
    window.editorApi.setContent('<p>hello hello</p><p>hello</p><p>hello hello hello</p>');
    window.editorApi.startSearch('hello', false, false, false);
    const afterWysiwyg = calls[calls.length - 1];
    // 切源码模式（markdown 里同样 6 处 hello）
    window.editorApi.enterSourceMode('# 标题\n\nhello hello\n\nhello\n\nhello hello hello\n');
    const afterSource = calls[calls.length - 1];
    expect(afterWysiwyg).toEqual({ total: 6, current: 1 });
    expect(afterSource).toEqual({ total: 6, current: 1 });
  });

  it('源码模式搜索 → 切回 WYSIWYG → 计数延续', () => {
    calls = [];
    // 先进入源码模式搜索
    window.editorApi.enterSourceMode('aaa bbb\n\naaa\n\nbbb aaa\n');
    window.editorApi.startSearch('aaa', false, false, false);
    const afterSource = calls[calls.length - 1];
    // 切回 WYSIWYG（编辑器内容含 3 处 aaa）
    window.editorApi.exitSourceMode('<p>aaa</p><p>aaa bbb aaa</p>');
    const afterWysiwyg = calls[calls.length - 1];
    expect(afterSource).toEqual({ total: 3, current: 1 });
    expect(afterWysiwyg).toEqual({ total: 3, current: 1 });
  });

  it('WYSIWYG 搜索 → 源码改查询 → 切回 WYSIWYG 计数随新查询', () => {
    calls = [];
    window.editorApi.setContent('<p>aaa bbb</p><p>aaa</p><p>bbb</p>');
    window.editorApi.startSearch('aaa', false, false, false);          // WYSIWYG: 2
    window.editorApi.enterSourceMode('# t\n\naaa bbb\n\naaa\n\nbbb\n'); // 源码续 aaa: 2
    const afterEnter = calls[calls.length - 1];
    window.editorApi.startSearch('bbb', false, false, false);          // 源码 bbb: 2
    const afterSrcBbb = calls[calls.length - 1];
    window.editorApi.exitSourceMode('<p>aaa bbb</p><p>aaa</p><p>bbb</p>');
    const afterWysiwyg = calls[calls.length - 1];                       // WYSIWYG bbb: 2
    expect(afterEnter).toEqual({ total: 2, current: 1 });
    expect(afterSrcBbb).toEqual({ total: 2, current: 1 });
    expect(afterWysiwyg).toEqual({ total: 2, current: 1 });
  });

  it('导航到第 3 处后往返切换，位置保留为 3/n', () => {
    calls = [];
    window.editorApi.setContent('<p>aaa aaa aaa</p><p>aaa aaa aaa</p>'); // 6 处 aaa
    window.editorApi.startSearch('aaa', false, false, false);           // current 0 (1/6)
    window.editorApi.searchNext();                                       // current 1 (2/6)
    window.editorApi.searchNext();                                       // current 2 (3/6)
    const atWysiwyg = calls[calls.length - 1];
    window.editorApi.enterSourceMode('aaa aaa aaa\n\naaa aaa aaa\n');    // 源码保留 current 2
    const atSource = calls[calls.length - 1];
    window.editorApi.exitSourceMode('<p>aaa aaa aaa</p><p>aaa aaa aaa</p>');
    const atWysiwyg2 = calls[calls.length - 1];
    expect(atWysiwyg).toEqual({ total: 6, current: 3 });
    expect(atSource).toEqual({ total: 6, current: 3 });
    expect(atWysiwyg2).toEqual({ total: 6, current: 3 });
  });
});
