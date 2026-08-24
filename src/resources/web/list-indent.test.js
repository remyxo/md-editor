// 列表缩进 CSS 回归测试
//
// 背景：editor.css 顶部全局 reset（* { margin:0; padding:0 }）会清掉浏览器
// 默认的 ul/ol padding-inline-start。若不为 .ProseMirror 列表显式恢复
// padding-left，嵌套列表与上级列表项将渲染在同一水平位置（无缩进）。
// 本测试静态断言这些规则存在，防止再次被误删。
import { describe, it, expect } from 'vitest';
import { readFileSync } from 'fs';
import { fileURLToPath } from 'url';
import { dirname, join } from 'path';

const cssPath = join(dirname(fileURLToPath(import.meta.url)), 'editor.css');
const css = readFileSync(cssPath, 'utf8');

/** 去掉注释后查找选择器所在规则块（支持分组选择器：匹配首个 { 前的其余选择器） */
function ruleFor(selector) {
  const noComments = css.replace(/\/\*[\s\S]*?\*\//g, '');
  const re = new RegExp(
    selector.replace(/[.*+?^${}()|[\]\\]/g, '\\$&') + '\\s*(?:,[^{]*)?\\{([^}]*)\\}'
  );
  const m = noComments.match(re);
  return m ? m[1] : null;
}

describe('列表缩进规则', () => {
  it('普通无序列表（非 taskList）有 padding-left 缩进', () => {
    const rule = ruleFor('.ProseMirror ul:not([data-type="taskList"])');
    expect(rule).not.toBeNull();
    expect(rule).toMatch(/padding-left:\s*[\d.]+em/);
  });

  it('有序列表有 padding-left 缩进', () => {
    const rule = ruleFor('.ProseMirror ol');
    expect(rule).not.toBeNull();
    expect(rule).toMatch(/padding-left:\s*[\d.]+em/);
  });

  it('嵌套任务列表有 padding-left 缩进（覆盖顶层 taskList 的 0）', () => {
    const rule = ruleFor('.ProseMirror li > ul[data-type="taskList"]');
    expect(rule).not.toBeNull();
    expect(rule).toMatch(/padding-left:\s*[\d.]+em/);
  });
});
