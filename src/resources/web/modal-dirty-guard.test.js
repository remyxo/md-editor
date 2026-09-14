// @vitest-environment jsdom
// 模态对话框取消恢复脏状态的版本守卫（审查 P2-12）：
// 旧实现无条件回写 _dirtyBefore——对话框打开期间若有事务置脏（_docVersion
// 递增），取消会把脏标记回退成假干净（关窗不提示、跳过保存）。现以版本
// 比对守卫：版本未变才恢复。注：当前代码路径中对话框期间的版本递增仅来自
// 确认流（不恢复），真实复现窗口为理论性；本测试固化守卫语义防回归。
import { describe, it, expect, beforeAll } from 'vitest';
import { readFileSync } from 'fs';
import { fileURLToPath } from 'url';
import path from 'path';

const W = path.dirname(fileURLToPath(import.meta.url));

beforeAll(() => {
  const libs = ['prosemirror-model.js', 'prosemirror-state.js', 'prosemirror-transform.js',
    'prosemirror-view.js', 'tiptap-core.js', 'lowlight.js', 'searchPlugin.js'];
  for (const f of libs) {
    try { window.eval(readFileSync(path.join(W, f), 'utf-8')); } catch (e) { /* tiptap-all 聚合 */ }
  }
  window.eval(readFileSync(path.join(W, 'katex.min.js'), 'utf-8'));
  window.eval(readFileSync(path.join(W, 'tiptap-all.js'), 'utf-8'));
  window.eval(readFileSync(path.join(W, 'editor.js'), 'utf-8'));
});

function freshEditor() {
  document.body.innerHTML = '<div id="editor"></div>';
  window.editorApi.init();
  window.editorApi.setContent('<p>x</p>');
  window.editorApi.markClean();
}

describe('模态对话框脏状态恢复（版本守卫）', () => {
  it('版本未变：取消恢复打开前的干净态', () => {
    freshEditor();
    expect(window.editorApi._dirty).toBe(false);
    const v = window.editorApi._docVersion;
    const dirtyBefore = window.editorApi._dirty;   // false
    window.editorApi._restoreDirtyIfUnchanged(v, dirtyBefore);
    expect(window.editorApi._dirty).toBe(false);
  });

  it('版本未变：恢复打开前的脏态', () => {
    freshEditor();
    window.editorApi._forceDirty();                 // 打开前已脏
    expect(window.editorApi._dirty).toBe(true);
    const v = window.editorApi._docVersion;
    window.editorApi._restoreDirtyIfUnchanged(v, true);
    expect(window.editorApi._dirty).toBe(true);
  });

  it('对话框期间版本递增：不得回退成假干净', () => {
    freshEditor();
    const v = window.editorApi._docVersion;
    // 模拟对话框期间的异步置脏事务（_docVersion 递增）
    window.editorApi._forceDirty();
    expect(window.editorApi._docVersion).toBeGreaterThan(v);
    // 取消路径尝试恢复旧值（false）——守卫应拒绝
    window.editorApi._restoreDirtyIfUnchanged(v, false);
    expect(window.editorApi._dirty).toBe(true);
  });

  it('公式编辑器打开/取消不改变干净态（集成）', async () => {
    freshEditor();
    const ed = window.editorApi.editor;
    window.editorApi._mathEditor.create('inline');
    expect(window.editorApi._mathEditing).toBe(true);
    // 取消（不确认）并关闭
    window.editorApi._mathEditor._confirmed = false;
    window.editorApi._mathEditor.close();
    await new Promise(r => setTimeout(r, 250));     // 越过 50+150ms 延迟恢复
    expect(window.editorApi._mathEditing).toBe(false);
    expect(window.editorApi._dirty).toBe(false);    // 无变更 → 恢复干净
  });
});
