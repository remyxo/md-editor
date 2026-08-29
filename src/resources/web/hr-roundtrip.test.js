// @vitest-environment jsdom
// <hr> 水平线真实 TipTap 链路回归（2026-08-29 修复后首次有真 <hr/> 进入编辑器）：
//   C++ toHtml 现把 <hr> 标签行 / 段落后的 --- 还原为独立 <hr/>（此前被
//   escapeUnclosedHtmlTags 转义为字面文本）。本测试验证 TipTap 侧：
//   1. <hr/> 解析为 horizontalRule 节点（非文本）
//   2. 编辑器 HTML 输出保留 <hr>（C++ fromHtml 再转回 ---）
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

beforeAll(() => {
  window.scrollTo = () => {};
  window.HTMLElement.prototype.scrollIntoView = () => {};
  window.IntersectionObserver = undefined;   // mermaid 降级路径，与本测试无关
  document.body.innerHTML =
    '<div id="editor"></div>' +
    '<div id="source-wrap" style="display:none">' +
    '<pre id="source-overlay"></pre><textarea id="source-view"></textarea></div>';
  loadEditor();
  window.editorApi.init();
  window.editorApi.bridge = new Proxy({}, { get: () => () => {} });
});

describe('<hr> 水平线 TipTap 链路', () => {
  it('<hr/> 解析为 horizontalRule 节点并原样输出', () => {
    window.editorApi.setContent('<p>前文</p><hr/><p>后文</p>');
    const ed = window.editorApi.editor;
    let hrCount = 0;
    ed.state.doc.descendants(n => { if (n.type.name === 'horizontalRule') hrCount++; });
    expect(hrCount).toBe(1);
    // 不应残留字面文本 <hr>（转义路径回归）
    expect(ed.state.doc.textContent).not.toContain('<hr>');
    // 编辑器 HTML 输出保留 <hr>（供 C++ fromHtml → ---）
    expect(ed.getHTML()).toContain('<hr>');
    expect(ed.getHTML()).toContain('<p>前文</p>');
    expect(ed.getHTML()).toContain('<p>后文</p>');
  });
});
