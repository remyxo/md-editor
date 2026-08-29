// @vitest-environment jsdom
// Mermaid 可见优先渲染回归测试：
//   离屏图不渲染（IntersectionObserver 待可见），进入视口才渲染；
//   无 IntersectionObserver 环境降级为立即渲染；
//   SVG 结果缓存命中不重复调 mermaid.render（退出源码重建/重进文档免重画）；
//   主题切换跳过尚未渲染的占位图（保持占位，可见时以新主题渲染）；
//   编辑面板改源（update）立即重绘，不等可见。
import { describe, it, expect, beforeAll } from 'vitest';
import { readFileSync } from 'fs';
import { fileURLToPath } from 'url';
import path from 'path';

const W = path.dirname(fileURLToPath(import.meta.url));

function loadEditor() {
  window.eval(readFileSync(path.join(W, 'tiptap-all.js'), 'utf-8'));
  window.eval(readFileSync(path.join(W, 'editor.js'), 'utf-8'));
}

// 假 IntersectionObserver：手动触发回调，验证可见性驱动的渲染时机
class FakeIO {
  constructor(cb) {
    this.cb = cb;
    this.disconnected = false;
    FakeIO.instances.push(this);
  }
  observe(el) { this.el = el; }
  disconnect() { this.disconnected = true; }
  trigger(intersecting) {
    if (!this.disconnected) this.cb([{ target: this.el, isIntersecting: intersecting }]);
  }
}
FakeIO.instances = [];

let renderCalls = [];
function mockMermaid() {
  renderCalls = [];
  window.mermaid = {
    initialize: () => {},
    render: (id, source) => {
      renderCalls.push(source);
      return Promise.resolve({
        svg: '<svg viewBox="0 0 40 10" width="40"><rect width="9" height="9"/></svg>'
      });
    }
  };
  window.__mermaidInited = false;
}

const MD = (src) =>
  '<pre><code class="language-mermaid">' + src + '</code></pre>';
const blockAt = () => document.querySelector('.mermaid-block');

beforeAll(() => {
  window.scrollTo = () => {};
  window.HTMLElement.prototype.scrollIntoView = () => {};
  document.body.innerHTML =
    '<div id="editor"></div>' +
    '<div id="source-wrap" style="display:none">' +
    '<pre id="source-overlay"></pre><textarea id="source-view"></textarea></div>';
  loadEditor();
  window.editorApi.init();
  window.editorApi.bridge = new Proxy({}, { get: () => () => {} });
});

async function flush() {
  // 等 mermaid.render 的 Promise 微任务链（含缓存路径）
  await Promise.resolve();
  await Promise.resolve();
  await Promise.resolve();
}

describe('Mermaid 可见优先渲染', () => {
  it('无 IntersectionObserver：降级立即渲染（兼容老环境）', async () => {
    window.IntersectionObserver = undefined;
    mockMermaid();
    window.editorApi.setContent(MD('graph TD;A-->B;'));
    await flush();
    expect(renderCalls).toContain('graph TD;A-->B;');
    expect(blockAt().querySelector('svg')).toBeTruthy();
  });

  it('离屏不渲染：构造 NodeView 不调 mermaid.render，进入视口才渲染', async () => {
    FakeIO.instances = [];
    window.IntersectionObserver = FakeIO;
    mockMermaid();
    window.editorApi.setContent(MD('graph LR;X-->Y;'));
    await flush();
    // 离屏：保持占位，未渲染
    expect(renderCalls.length).toBe(0);
    expect(blockAt().querySelector('svg')).toBeFalsy();
    // 进入视口：渲染并断开观察
    const io = FakeIO.instances[FakeIO.instances.length - 1];
    io.trigger(true);
    await flush();
    expect(renderCalls).toContain('graph LR;X-->Y;');
    expect(blockAt().querySelector('svg')).toBeTruthy();
    expect(io.disconnected).toBe(true);
  });

  it('SVG 缓存：同内容重建（退出源码）不重复渲染', async () => {
    FakeIO.instances = [];
    window.IntersectionObserver = FakeIO;
    mockMermaid();
    // 第一轮：渲染进缓存
    window.editorApi.setContent(MD('graph TD;C1-->C2;'));
    FakeIO.instances[FakeIO.instances.length - 1].trigger(true);
    await flush();
    expect(renderCalls.length).toBe(1);
    // 第二轮：同内容重建（模拟退出源码 setContent），命中缓存
    window.editorApi.setContent(MD('graph TD;C1-->C2;'));
    FakeIO.instances[FakeIO.instances.length - 1].trigger(true);
    await flush();
    expect(renderCalls.length).toBe(1);   // 未再次调用 mermaid.render
    expect(blockAt().querySelector('svg')).toBeTruthy();
  });

  it('主题切换：跳过尚未渲染的占位图（不触发离屏渲染）', async () => {
    FakeIO.instances = [];
    window.IntersectionObserver = FakeIO;
    mockMermaid();
    window.editorApi.setContent(MD('graph TD;P-->Q;'));
    await flush();
    expect(renderCalls.length).toBe(0);   // 仍为占位
    window.editorApi.setTheme(document.body.classList.contains('dark') ? 'light' : 'dark');
    await flush();
    expect(renderCalls.length).toBe(0);   // 占位图未被主题切换触发渲染
    // 占位图此后进入视口：以当前（新）主题渲染
    FakeIO.instances[FakeIO.instances.length - 1].trigger(true);
    await flush();
    expect(renderCalls.length).toBe(1);
  });

  it('编辑改源（update）：立即重绘，不等可见', async () => {
    FakeIO.instances = [];
    window.IntersectionObserver = FakeIO;
    mockMermaid();
    window.editorApi.setContent(MD('graph TD;U1-->U2;'));
    await flush();
    expect(renderCalls.length).toBe(0);   // 离屏占位
    // 模拟编辑面板改源：替换节点 attrs（走 NodeView.update 路径）
    const ed = window.editorApi.editor;
    let pos = -1;
    ed.state.doc.descendants((n, p) => {
      if (n.type.name === 'mermaid') { pos = p; return false; }
      return true;
    });
    ed.view.dispatch(ed.state.tr.setNodeMarkup(pos, undefined, { source: 'graph TD;NEW;' }));
    await flush();
    expect(renderCalls).toContain('graph TD;NEW;');   // update 立即渲染
    expect(blockAt().querySelector('svg')).toBeTruthy();
  });
});
