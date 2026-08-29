// @vitest-environment jsdom
// 强调包裹行内代码往返保真测试：真实 editor.js 全扩展链路 setContent → getContent。
// 背景：默认 Code mark 的 excludes:"_"（排除全部其它 mark）导致 `<em><code>X</code></em>`
// 经 TipTap 解析后 em/strong 被丢弃，getHTML 塌陷为 `<code>X</code>`（`*`code`*` 往返丢强调）。
// 修复：editor.js 用 Code.extend({ excludes: '' }) 允许强调/加粗等 mark 与 code 共存。
// 此处锁定 TipTap 侧形态不塌陷；C++ 侧 toHtml 保护（md4c 缺陷）与 fromHtml 还原由
// tests/test_markdownhandler.cpp 覆盖。
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
    try { window.eval(readFileSync(path.join(W, f), 'utf-8')); } catch (e) { /* 单独扩展由 tiptap-all 聚合 */ }
  }
  window.eval(readFileSync(path.join(W, 'katex.min.js'), 'utf-8'));
  window.eval(readFileSync(path.join(W, 'tiptap-all.js'), 'utf-8'));
  window.eval(readFileSync(path.join(W, 'editor.js'), 'utf-8'));
}

describe('强调包裹行内代码往返（真实 TipTap 链路）', () => {
  beforeAll(() => {
    document.body.innerHTML = '<div id="editor"></div>';
    loadEditor();
    window.editorApi.init();
  });

  it('em/strong 包 code 经 TipTap 不塌陷（含 mark 标记 == 内容）', () => {
    const cases = [
      // toHtml 统一输出的形态（em/strong 在外）
      { src: '<p><em><code>==</code></em></p>', want: '<p><em><code>==</code></em></p>' },
      { src: '<p><strong><code>==</code></strong></p>', want: '<p><strong><code>==</code></strong></p>' },
      { src: '<p><em><code>code</code></em></p>', want: '<p><em><code>code</code></em></p>' },
      { src: '<p>文字 <strong><code>code</code></strong> 尾</p>', want: '<p>文字 <strong><code>code</code></strong> 尾</p>' },
      { src: '<p><em><code>a</code> 与 <code>b</code> 文本</em></p>', want: '<p><em><code>a</code> 与 <code>b</code> 文本</em></p>' },
      // TipTap 可能输出的反序（code 在外），应归一为强调在外
      { src: '<p><code><em>==</em></code></p>', want: '<p><em><code>==</code></em></p>' },
      // 其它包裹符（strike/mark/sub/sup）包 code 同样不得塌陷——CodeAllowingMarks
      // 允许 code 与任意 mark 共存，这些形态是 toHtml 各包裹保护的标准输出。
      // 注意 mark/sub/sup 的 code mark rank 不同：TipTap 归一为 code 在外
      // （<code><mark>）而非 toHtml 输出的包裹符在外；标记不塌陷即可，code-outer
      // 形态由 fromHtml 归一化还原（C++ 侧覆盖）。strike 保持包裹符在外。
      { src: '<p><s><code>code</code></s></p>', want: '<p><s><code>code</code></s></p>' },
      { src: '<p><mark><code>code</code></mark></p>', want: '<p><code><mark>code</mark></code></p>' },
      { src: '<p><sub><code>code</code></sub></p>', want: '<p><code><sub>code</sub></code></p>' },
      { src: '<p><sup><code>code</code></sup></p>', want: '<p><code><sup>code</sup></code></p>' },
      { src: '<p>文字 <s><code>a</code></s> 与 <mark><code>b</code></mark> 尾</p>',
        want: '<p>文字 <s><code>a</code></s> 与 <code><mark>b</mark></code> 尾</p>' },
      // 链式级联丢弃补救（wrapDiscardedEmphCode，2026-08-29）产出的邻接形态：
      // 扁平化 strong 边界 + 补包 strong 相邻。ProseMirror 合并同 mark 相邻节点，
      // getHTML 输出合并后的单个 strong 内嵌 code——标记不丢即可，MD 形态由
      // fromHtml 对「strong 含文本+code」的既有扁平化还原（C++ 侧覆盖）。
      { src: '<p><strong>约，</strong><strong><code>c1</code></strong><strong>按，</strong></p>',
        want: '<p><strong>约，<code>c1</code>按，</strong></p>' },
      // 补包 strong 与普通文本/字面 ** 相邻（用户行的 \x01 场景）
      { src: '<p>、<strong><code>\\x01</code></strong> 与</p>',
        want: '<p>、<strong><code>\\x01</code></strong> 与</p>' },
    ];
    for (const c of cases) {
      window.editorApi.setContent(c.src);
      const out = window.editorApi.getContent();
      expect(out, `input ${c.src}`).toBe(c.want);
    }
  });

  it('多轮 setContent 稳定（同源内容往返不漂移）', () => {
    const src = '<p><em><code>==</code></em></p>';
    window.editorApi.setContent(src);
    let out = window.editorApi.getContent();
    for (let i = 0; i < 2; i++) {
      window.editorApi.setContent(out);
      out = window.editorApi.getContent();
    }
    expect(out).toBe('<p><em><code>==</code></em></p>');
  });
});
