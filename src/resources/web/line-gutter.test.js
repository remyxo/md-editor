// @vitest-environment jsdom
// 逐行行号 gutter 渲染测试：行语义（_lineStarts）已有 line-starts.test.js 锁定，
// 本文件锁定渲染层——DOM 结构（#line-gutter 挂载/移除）、行号数量与序列、
// 多行公式同 pos 行槽的均分定位、列宽 --lnw 位数自适应、开关清理。
// jsdom 无布局：_lineY/_gutterNodeRect 以桩替换（返回可预测值）。
import { describe, it, expect, beforeAll, beforeEach } from 'vitest';
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

const api = () => window.editorApi;
const gutter = () => document.getElementById('line-gutter');
const numbers = () => gutter() ? Array.from(gutter().children) : [];
/** 桩：行首 pos → y（pos×10 可预测且互异） */
const stubY = () => { api()._lineY = function(pos) { return pos * 10; }; };

describe('逐行行号 gutter 渲染', () => {
  beforeAll(() => {
    document.body.innerHTML = '<div id="editor"></div>';
    loadEditor();
    window.editorApi.init();
    window.editorApi.bridge = new Proxy({}, { get: () => () => {} });
  });

  beforeEach(() => {
    // 每用例从关闭状态起测（also 清理 --lnw/body class）
    api().applySettings({ lineNumbers: false });
  });

  it('开启：#line-gutter 挂入 #editor，行号数=逻辑行数、序列 1..N', () => {
    api().setContent('<h2>T</h2><p>aa</p><pre><code>a\nb\nc</code></pre><p>tail</p>');
    stubY();
    api().applySettings({ lineNumbers: true });
    expect(gutter()).not.toBeNull();
    expect(gutter().parentNode.id).toBe('editor');
    expect(document.body.classList.contains('line-numbers')).toBe(true);
    // 6 个逻辑行：h2(1) + p(1) + 代码 3 行(3) + tail(1)
    const nums = numbers();
    expect(nums.length).toBe(6);
    expect(nums.map(function(n) { return n.textContent; }))
      .toEqual(['1', '2', '3', '4', '5', '6']);
    // 桩 y=pos*10：行 3 行首 pos（代码第 2 行）应等于其 pos×10
    const starts = api()._lineStarts(api().editor.state.doc);
    expect(parseFloat(nums[2].style.top)).toBe(starts[2] * 10);
  });

  it('多行公式同 pos 行槽：按节点高度均分定位', () => {
    api().setContent(
      '<p>引言</p>'
      + '<div data-type="block-math" data-latex="\\begin{aligned}a \\\\ b \\\\ c\\end{aligned}"></div>'
      + '<p>结语</p>');
    stubY();
    api()._gutterNodeRect = function() { return { top: 100, height: 90 }; };
    api().applySettings({ lineNumbers: true });
    // 行 2/3/4 共属公式（同 pos）：100 / 130 / 160
    const tops = numbers().map(function(n) { return parseFloat(n.style.top); });
    expect(tops).toEqual([starts0(), 100, 130, 160, starts4()]);
    function starts0() { return api()._lineStarts(api().editor.state.doc)[0] * 10; }
    function starts4() { return api()._lineStarts(api().editor.state.doc)[4] * 10; }
  });

  it('列宽 --lnw 随行数位数自适应（1~3 位数 3em，4 位数 3.75em）', () => {
    api().setContent('<p>a</p>');
    stubY();
    api().applySettings({ lineNumbers: true });
    expect(document.documentElement.style.getPropertyValue('--lnw')).toBe('3.00em');
    // 1001 段 → 4 位数行号；setContent 整体替换 #editor，gutter 须自动重挂重渲染
    let html = '';
    for (let i = 0; i < 1001; i++) html += '<p>x</p>';
    api().setContent(html);
    expect(document.documentElement.style.getPropertyValue('--lnw')).toBe('3.75em');
    expect(numbers().length).toBe(1001);
    expect(numbers()[1000].textContent).toBe('1001');
  });

  it('测量失败的行跳过渲染，行号序列不乱（下标即行号）', () => {
    api().setContent('<p>a</p><p>b</p><p>c</p>');
    api()._lineY = function(pos) { return pos === 3 ? null : pos * 10; };   // 行 2 行首测量失败
    api().applySettings({ lineNumbers: true });
    const nums = numbers();
    expect(nums.map(function(n) { return n.textContent; })).toEqual(['1', '3']);
  });

  it('关闭：gutter 移除、body class 与 --lnw 清理', () => {
    api().setContent('<p>a</p>');
    stubY();
    api().applySettings({ lineNumbers: true });
    expect(gutter()).not.toBeNull();
    api().applySettings({ lineNumbers: false });
    expect(gutter()).toBeNull();
    expect(document.body.classList.contains('line-numbers')).toBe(false);
    expect(document.documentElement.style.getPropertyValue('--lnw')).toBe('');
  });

  it('仅 hr（0 逻辑行）：gutter 清空不渲染任何号', () => {
    api().setContent('<hr>');
    stubY();
    api().applySettings({ lineNumbers: true });
    expect(numbers().length).toBe(0);
  });
});
