// @vitest-environment jsdom
// vendored-lowlight-plugin.js 行为契约测试：
//   1. setContent 同步路径零高亮调用（异步分批空闲补全）
//   2. 空闲补全后代码块内出现高亮 span（语言块 highlight / 无语言块 highlightAuto）
//   3. 编辑增量：单块击键只重算该块（其他块缓存命中，不再调 lowlight）
//   4. 撤销后高亮保持（缓存命中重建 decoration）
import { describe, it, expect, beforeAll } from 'vitest';
import { readFileSync } from 'fs';
import { fileURLToPath } from 'url';
import path from 'path';

const W = path.dirname(fileURLToPath(import.meta.url));

function loadEditor() {
  window.eval(readFileSync(path.join(W, 'tiptap-all.js'), 'utf-8'));
  window.eval(readFileSync(path.join(W, 'vendored-lowlight-plugin.js'), 'utf-8'));
  window.eval(readFileSync(path.join(W, 'editor.js'), 'utf-8'));
}

let hlCalls = 0, autoCalls = 0;
function countLowlight() {
  // 替换 lowlight 方法属性计数（vendored plugin 经 options.lowlight 对象访问，生效）
  const origH = window.lowlight.highlight.bind(window.lowlight);
  const origA = window.lowlight.highlightAuto.bind(window.lowlight);
  hlCalls = 0; autoCalls = 0;
  window.lowlight.highlight = (...a) => { hlCalls++; return origH(...a); };
  window.lowlight.highlightAuto = (...a) => { autoCalls++; return origA(...a); };
}

const sleep = (ms) => new Promise(r => setTimeout(r, ms));
// 等空闲批次跑完：56 块以内 6 块/批 × setTimeout(16ms) 级别，留足余量
async function waitIdleBlocks(n, timeout = 4000) {
  const t0 = Date.now();
  while (Date.now() - t0 < timeout) {
    await sleep(40);
    if (document.querySelectorAll('pre code .hljs-keyword, pre code .hljs-string, pre code .hljs-title').length >= n) return true;
  }
  return false;
}

beforeAll(() => {
  window.scrollTo = () => {};
  window.HTMLElement.prototype.scrollIntoView = () => {};
  window.IntersectionObserver = undefined;   // mermaid 走降级路径，与本测试无关
  document.body.innerHTML =
    '<div id="editor"></div>' +
    '<div id="source-wrap" style="display:none">' +
    '<pre id="source-overlay"></pre><textarea id="source-view"></textarea></div>';
  loadEditor();
  window.editorApi.init();
  window.editorApi.bridge = new Proxy({}, { get: () => () => {} });
});

const DOC = () =>
  '<pre><code class="language-js">const a = 1;\nfunction foo() { return "bar"; }</code></pre>' +
  '<pre><code>plain auto block</code></pre>' +
  '<pre><code class="language-js">const b = 2;\nconst c = a + b;</code></pre>';

describe('vendored 异步代码块高亮', () => {
  it('setContent 同步零高亮调用（不阻塞加载关键路径）', () => {
    countLowlight();
    window.editorApi.setContent(DOC());
    expect(hlCalls + autoCalls).toBe(0);
  });

  it('空闲分批补全：语言块与无语言块均出现高亮 span', async () => {
    const ok = await waitIdleBlocks(2);   // js/auto 各至少一个 token span
    expect(ok).toBe(true);
    expect(hlCalls).toBeGreaterThanOrEqual(2);   // 两个 js 块走 highlight
    expect(autoCalls).toBeGreaterThanOrEqual(1); // 无语言块走 highlightAuto
  });

  it('编辑增量：单块击键只重算该块（其他块缓存命中）', async () => {
    // 确保初始批次全部完成（3 块全算）
    await sleep(300);
    countLowlight();
    const ed = window.editorApi.editor;
    // 光标进第一个代码块末尾，插入文本
    let pos = -1;
    ed.state.doc.descendants((n, p) => {
      if (n.type.name === 'codeBlock' && pos < 0) { pos = p; return false; }
      return true;
    });
    const textPos = pos + 1 + 'const a = 1;'.length;
    ed.chain().setTextSelection(textPos).insertContent('X').run();
    expect(hlCalls + autoCalls).toBe(1);   // 只有被编辑块重算
    // 新内容已同步高亮（不需要等空闲批次）
    const firstCode = document.querySelector('pre code');
    expect(firstCode.querySelector('.hljs-keyword')).toBeTruthy();
  });

  it('撤销后高亮保持（缓存命中重建 decoration）', async () => {
    countLowlight();
    window.editorApi.editor.commands.undo();
    expect(hlCalls).toBeLessThanOrEqual(1);   // 内容回滚的块重算 ≤1，其余缓存
    const firstCode = document.querySelector('pre code');
    expect(firstCode.querySelector('.hljs-keyword')).toBeTruthy();
  });
});
