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

// target 元素起点之前、parent 子树内累计的文本字符数（块内相对偏移）
function textOffsetBefore(parent, target) {
  let off = 0;
  function visit(node) {
    if (node === target) return true;
    if (node.nodeType === 3) { off += node.nodeValue.length; return false; }
    if (node.nodeType === 1) {
      for (let c = node.firstChild; c; c = c.nextSibling) {
        if (visit(c)) return true;
      }
    }
    return false;
  }
  visit(parent);
  return off;
}

// 取 vendored plugin 实例（key 以 asyncLowlight 开头，见 AsyncLowlightPlugin）
function findAsyncPlugin() {
  const ed = window.editorApi.editor;
  return ed.state.plugins.find(p => p.key.indexOf('asyncLowlight') === 0);
}
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

  it('普通文本击键走 map 快路径：代码块装饰保位不重建', async () => {
    const ed = window.editorApi.editor;
    const plugin = findAsyncPlugin();
    expect(plugin).toBeTruthy();
    // 快路径前提：空闲批次已算完（pending 清空——非空时必须走慢路径重收集）
    const t0 = Date.now();
    while (plugin.getState(ed.state).pending.length && Date.now() - t0 < 4000) {
      await sleep(40);
    }
    expect(plugin.getState(ed.state).pending.length).toBe(0);

    countLowlight();
    const before = plugin.getState(ed.state);
    const beforeDecos = before.decos.find().map(d => [d.from, d.to])
      .sort((a, b) => a[0] - b[0]);
    expect(beforeDecos.length).toBeGreaterThan(0);

    // 击键前记录第一个代码块内首个高亮 span 的块内字符偏移
    const codeEl = document.querySelector('pre code');
    const span = codeEl.querySelector('.hljs-keyword');
    expect(span).toBeTruthy();
    const offBefore = textOffsetBefore(codeEl, span);

    // 在代码块之前插入普通段落并击键（两个事务均与全部代码块零相交）
    ed.chain().insertContentAt(0, '<p>h</p>').run();
    ed.chain().setTextSelection(2).insertContent('i').run();

    const after = plugin.getState(ed.state);
    // a) decoration 数量一致，且每条整体平移量恒定（DecorationSet.map 保位）
    const afterDecos = after.decos.find().map(d => [d.from, d.to])
      .sort((a, b) => a[0] - b[0]);
    expect(afterDecos.length).toBe(beforeDecos.length);
    const delta = afterDecos[0][0] - beforeDecos[0][0];
    expect(delta).toBeGreaterThan(0);   // 段落(2) + 击键(1) 均插在块之前
    for (let i = 0; i < beforeDecos.length; i++) {
      expect(afterDecos[i][0] - beforeDecos[i][0]).toBe(delta);
      expect(afterDecos[i][1] - beforeDecos[i][1]).toBe(delta);
    }
    // b) 高亮零调用；epoch 不变（未经慢路径重建——慢路径 docChanged 会 epoch+1）
    expect(hlCalls + autoCalls).toBe(0);
    expect(after.epoch).toBe(before.epoch);
    // c) DOM 上高亮 span 仍在，块内字符偏移不变（映射后落在原字符上）
    const span2 = document.querySelector('pre code .hljs-keyword');
    expect(span2).toBeTruthy();
    expect(textOffsetBefore(span2.closest('code'), span2)).toBe(offBefore);
  });
});
