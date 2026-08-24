// @vitest-environment jsdom
// 状态栏行列计算回归测试：真实 editor.js 链路 setContent → setTextSelection
// → onSelectionUpdate → bridge.cursorChanged 捕获。
//
// 背景：旧实现用 nodesBetween(0, pos) 且条件 start+nodeSize <= pos，
// 跳过包含光标的文本节点（列值冻结/取到前一块末列），行数只统计
// textContent 中的 '\n'（普通块边界不含 '\n'，行号恒为 1）。
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
    try { window.eval(readFileSync(path.join(W, f), 'utf-8')); } catch (e) { /* tiptap-all 聚合 */ }
  }
  window.eval(readFileSync(path.join(W, 'katex.min.js'), 'utf-8'));
  window.eval(readFileSync(path.join(W, 'tiptap-all.js'), 'utf-8'));
  window.eval(readFileSync(path.join(W, 'editor.js'), 'utf-8'));
}

let lastCursor = null;

/** 设置内容并把光标移到 pos（内容区绝对位置），返回状态栏收到的行列 */
function cursorAt(html, pos) {
  window.editorApi.setContent(html);
  lastCursor = null;
  window.editorApi.editor.commands.setTextSelection(pos);
  if (!lastCursor) throw new Error('cursorChanged 未触发（pos=' + pos + '）');
  return lastCursor;
}

/** 动态定位：文本 needle 末尾之后的光标位置（避免手算各节点位置） */
function posAfter(needle) {
  let hit = null;
  window.editorApi.editor.state.doc.descendants(function(node, pos) {
    if (hit !== null || !node.isText) return hit === null;
    const i = node.text.indexOf(needle);
    if (i >= 0) { hit = pos + i + needle.length; return false; }
    return true;
  });
  if (hit === null) throw new Error('未找到文本: ' + needle);
  return hit;
}

/** 把光标移到 pos 并返回状态栏收到的行列（内容已由调用方设置） */
function selectAt(pos) {
  lastCursor = null;
  window.editorApi.editor.commands.setTextSelection(pos);
  if (!lastCursor) throw new Error('cursorChanged 未触发（pos=' + pos + '）');
  return lastCursor;
}

describe('状态栏行列计算（真实 TipTap 链路）', () => {
  beforeAll(() => {
    document.body.innerHTML = '<div id="editor"></div>';
    loadEditor();
    window.editorApi.init();
    // 桩 bridge：仅捕获 cursorChanged，其余槽位空实现
    window.editorApi.bridge = new Proxy({}, {
      get: (t, k) => (k === 'cursorChanged')
        ? (line, col, pos) => { lastCursor = { line, col, pos }; }
        : () => {}
    });
  });

  beforeEach(() => { lastCursor = null; });

  it('光标在段内移动时列值跟随（旧实现冻结为 1）', () => {
    // <p>hello</p>：h=1,e=2,l=3,l=4,o=5；光标在 "hel" 之后 = 内容位置 4
    expect(cursorAt('<p>hello</p>', 4)).toEqual({ line: 1, col: 4, pos: 4 });
    expect(cursorAt('<p>hello</p>', 2)).toEqual({ line: 1, col: 2, pos: 2 });
  });

  it('跨段落行号递增（旧实现恒为 1）', () => {
    // 文档 <p>hello</p><p>world</p>：p2 内容区 8..13
    expect(cursorAt('<p>hello</p><p>world</p>', 8)).toEqual({ line: 2, col: 1, pos: 8 });
    expect(cursorAt('<p>hello</p><p>world</p>', 13)).toEqual({ line: 2, col: 6, pos: 13 });
  });

  it('标题 + 段落混合：行号按块计数', () => {
    // <h2>T</h2><p>aa</p><p>bb</p>：h2 节点 0..3，p(aa) 3..7 内容 4..6，p(bb) 7..11 内容 8..10
    expect(cursorAt('<h2>T</h2><p>aa</p><p>bb</p>', 9)).toEqual({ line: 3, col: 2, pos: 9 });
  });

  it('代码块内按 \\n 换行、列值从代码行首计', () => {
    // <pre><code>a\nb\nc</code></pre>：code 内容 1..7（文本 6 字符位置 1..6）；
    // 'a' 后是 \n(2)、'b'(3)，光标在 'b' 之后 = 4 → 代码第 2 行第 2 列
    expect(cursorAt('<pre><code>a\nb\nc</code></pre>', 4)).toEqual({ line: 2, col: 2, pos: 4 });
  });

  it('段内 <br>（hardBreak）视觉换行：行号递增、列值重置', () => {
    // <p>x<br>y</p>：内容 1..5，x(1) hardBreak(2) y(3)；光标在 y 后 = 4
    expect(cursorAt('<p>x<br>y</p>', 4)).toEqual({ line: 2, col: 2, pos: 4 });
  });

  it('行内加粗不影响列值（跨 mark 边界列值连续）', () => {
    // <p>ab<strong>cd</strong>ef</p>：内容 1..8，文本节点 ab(1..3)/cd(3..5)/ef(5..7)
    expect(cursorAt('<p>ab<strong>cd</strong>ef</p>', 4)).toEqual({ line: 1, col: 4, pos: 4 });
    expect(cursorAt('<p>ab<strong>cd</strong>ef</p>', 6)).toEqual({ line: 1, col: 6, pos: 6 });
  });

  it('引用块内多行软换行整体占多行，hr 不占行号（~/Desktop/audit-md-editor.md 场景）', () => {
    // h1(文本行1) + blockquote 段内 2 个换行(文本行2/3/4) + hr(不占) + h2(文本行5)
    const html = '<h1>报告</h1>'
      + '<blockquote><p>项目A<br>日期B<br>范围C</p></blockquote>'
      + '<hr><h2>章节D</h2>';
    window.editorApi.setContent(html);
    // 动态定位：避免手算各节点位置
    const posOf = (needle, after) => {
      let hit = null;
      window.editorApi.editor.state.doc.descendants((node, pos) => {
        if (hit !== null || !node.isText) return hit === null;
        const i = node.text.indexOf(needle);
        if (i >= 0) { hit = pos + i + (after ? 1 : 0); return false; }
        return true;
      });
      if (hit === null) throw new Error('未找到文本: ' + needle);
      return hit;
    };
    // 光标在引用第 3 行「范围C」的 C 之后 → 行 4
    lastCursor = null;
    window.editorApi.editor.commands.setTextSelection(posOf('C', true));
    expect(lastCursor.line).toBe(4);
    expect(lastCursor.col).toBe(4);   // "范围C" 3 字 + 1
    // 光标在 h2「章节D」的 D 之后 → 行 5
    lastCursor = null;
    window.editorApi.editor.commands.setTextSelection(posOf('D', true));
    expect(lastCursor.line).toBe(5);
    expect(lastCursor.col).toBe(4);   // "章节D" 3 字 + 1
  });

  it('光标经过的多行代码块按实际行数计行（非光标块也累计内部 \\n）', () => {
    // <p>intro</p><pre><code>a\nb\nc</code></pre><p>tail</p>
    // 文本行：intro=1，代码 3 行=2/3/4，tail=5
    const html = '<p>intro</p><pre><code>a\nb\nc</code></pre><p>tail</p>';
    // 动态找 'tail' 的 l 之后位置
    let hit = null;
    window.editorApi.setContent(html);
    window.editorApi.editor.state.doc.descendants((node, pos) => {
      if (hit !== null || !node.isText) return hit === null;
      const i = node.text.indexOf('tail');
      if (i >= 0) { hit = pos + i + 1; return false; }
      return true;
    });
    lastCursor = null;
    window.editorApi.editor.commands.setTextSelection(hit);
    expect(lastCursor.line).toBe(5);
    expect(lastCursor.col).toBe(2);   // 't' 之后
  });

  it('mermaid 图片占 1 行：图后文字为新的下行（268→269 场景）', () => {
    // <p>渲染效果</p> + mermaid 原子块（内部源码换行不影响：整体占 1 行） + <p>官方文档</p>
    const html = '<p>渲染效果</p>'
      + '<pre><code class="language-mermaid">flowchart TD\nA--&gt;B</code></pre>'
      + '<p>官方文档</p>';
    window.editorApi.setContent(html);
    // 光标在「官方文档」末尾 → 段1 + mermaid 1 行 + 本段 = 第 3 行
    expect(selectAt(posAfter('官方文档')).line).toBe(3);
    // 点选（NodeSelection）mermaid 图片 → 自身行 2、列 1
    let mermaidPos = null;
    window.editorApi.editor.state.doc.descendants((node, pos) => {
      if (node.type.name === 'mermaid') { mermaidPos = pos; return false; }
      return true;
    });
    lastCursor = null;
    window.editorApi.editor.commands.setNodeSelection(mermaidPos);
    expect(lastCursor.line).toBe(2);
    expect(lastCursor.col).toBe(1);
  });

  it('块级图片占 1 行：图后段落行号随之递增（用户样例场景）', () => {
    // <p>插入的图片：</p> + 块级 image 原子（视觉独占一行） + <p>mermaid流程图：</p>
    const html = '<p>插入的图片：</p>'
      + '<img src="580-640x480.jpg" alt="image">'
      + '<p>mermaid流程图：</p>';
    window.editorApi.setContent(html);
    // 光标在「mermaid流程图：」末尾 → 段1 + 图片 1 行 + 本段 = 第 3 行
    expect(selectAt(posAfter('mermaid流程图：')).line).toBe(3);
    // 点选（NodeSelection）图片 → 自身行 2、列 1
    let imagePos = null;
    window.editorApi.editor.state.doc.descendants((node, pos) => {
      if (node.type.name === 'image') { imagePos = pos; return false; }
      return true;
    });
    lastCursor = null;
    window.editorApi.editor.commands.setNodeSelection(imagePos);
    expect(lastCursor.line).toBe(2);
    expect(lastCursor.col).toBe(1);
  });

  it('块级公式按 latex 源码行数占行（有几行算几行）', () => {
    // 单行公式占 1 行；三行源码公式占 3 行
    const html = '<p>引言</p>'
      + '<div data-type="block-math" data-latex="E=mc^2"></div>'
      + '<div data-type="block-math" data-latex="a=1\nb=2\nc=3"></div>'
      + '<p>结语</p>';
    window.editorApi.setContent(html);
    // 引言=1，单行公式=2，三行公式=3/4/5，结语=6
    const lc = selectAt(posAfter('结语'));
    expect(lc.line).toBe(6);
    expect(lc.col).toBe(3);   // "结语" 2 字 + 1
  });

  it('块级公式 toHtml 折叠为 aligned 单行：按 \\ 分隔还原源码行数（用户样例）', () => {
    // 多行纯公式经 toHtml 折叠为 \begin{aligned}行1 \\ 行2\end{aligned}（无 \n，
    // HTML 属性无法承载换行）。行数须按还原后的源码行数计，而非折叠后的 1 行。
    // 样例：两行公式 (x+1)^2=... 与 x=\sum...
    const html = '<p>引言</p>'
      + '<div data-type="block-math" data-latex="\\begin{aligned}(x+1)^2=x^2+2x+1 \\\\ x=\\sum_{i=1}^{n} i^{2}\\end{aligned}"></div>'
      + '<p>结语</p>';
    window.editorApi.setContent(html);
    // 引言=1，两行公式=2/3，结语=4
    const lc = selectAt(posAfter('结语'));
    expect(lc.line).toBe(4);
    expect(lc.col).toBe(3);   // "结语" 2 字 + 1
    // 点选（NodeSelection）公式：head 在块末端，报告块末行（两行公式末行为 3，
    // 与多行段落光标在块末时报告末行的语义一致；mermaid 单行时首末行相同）
    let mathPos = null;
    window.editorApi.editor.state.doc.descendants((node, pos) => {
      if (node.type.name === 'blockMath') { mathPos = pos; return false; }
      return true;
    });
    lastCursor = null;
    window.editorApi.editor.commands.setNodeSelection(mathPos);
    expect(lastCursor.line).toBe(3);
  });

  it('块级公式折叠 aligned 三行：按 \\ 数还原为 3 行', () => {
    const html = '<p>引言</p>'
      + '<div data-type="block-math" data-latex="\\begin{aligned}a=1 \\\\ b=2 \\\\ c=3\\end{aligned}"></div>'
      + '<p>结语</p>';
    window.editorApi.setContent(html);
    // 引言=1，三行公式=2/3/4，结语=5
    expect(selectAt(posAfter('结语')).line).toBe(5);
  });

  it('块级公式显式 aligned 环境（含 & 对齐列）：仅计可见公式行（不含 begin/end）', () => {
    // 用户显式 \begin{aligned} a &= b \\ c &= d \end{aligned} 折叠为单行（含 &），
    // \begin{}/\end{} 在 WYSIWYG 渲染不可见，不计行；\\ 分隔的是可见公式行。
    // 两行公式 = 2 行
    const html = '<p>引言</p>'
      + '<div data-type="block-math" data-latex="\\begin{aligned} a &= b \\\\ c &= d \\end{aligned}"></div>'
      + '<p>结语</p>';
    window.editorApi.setContent(html);
    // 引言=1，两行公式=2/3，结语=4
    expect(selectAt(posAfter('结语')).line).toBe(4);
  });

  it('块级公式 raw 多行环境输入（含 \\\\\\n）：有 \\\\ 时忽略换行不重复计', () => {
    // 编辑面板多行输入 \begin{aligned}\nx &= 1 \\\ny &= 2\n\end{aligned}（含 \n 与 \\）。
    // 行尾 \\\n 属同一行分隔：按可见公式行计 2 行（\\ 数+1），\n 不叠加。
    const html = '<p>前</p>'
      + '<div data-type="block-math" data-latex="\\begin{aligned}\nx &= 1 \\\\\ny &= 2\n\\end{aligned}"></div>'
      + '<p>后</p>';
    window.editorApi.setContent(html);
    // 前=1，两行公式=2/3，后=4
    expect(selectAt(posAfter('后')).line).toBe(4);
  });

  it('块级公式三行显式 aligned：begin/end 不计行，仅 \\ 分隔的 3 行（用户样例 66→70）', () => {
    // 用户场景：$$ \begin{aligned} x &= a+b \\ y &= c+d \\ z &= x+y \end{aligned} $$
    // 折叠为单行含 2 个 \\ → 3 行可见公式（67/68/69），其后的段落行随之后移。
    const html = '<p>Line 1</p>'
      + '<div data-type="block-math" data-latex="\\begin{aligned} x &= a + b \\\\ y &= c + d \\\\ z &= x + y \\end{aligned}"></div>'
      + '<p>Line2</p>';
    window.editorApi.setContent(html);
    // Line 1=1，三行公式=2/3/4，Line2=5（公式若误计 begin/end 的 5 行则 Line2 落到 7）
    expect(selectAt(posAfter('Line2')).line).toBe(5);
  });

  it('表格按行计行：列数不再被记成行数', () => {
    // 2 行 3 列：旧实现每个单元格各计 1 行（3 列记 3 行）
    const html = '<p>前</p>'
      + '<table><tbody>'
      + '<tr><th>H1</th><th>H2</th><th>H3</th></tr>'
      + '<tr><td>a1</td><td>a2</td><td>a3</td></tr>'
      + '</tbody></table>'
      + '<p>后</p>';
    window.editorApi.setContent(html);
    // 光标在表后段落 → 前=1，表头行=2，数据行=3，后=4
    expect(selectAt(posAfter('后')).line).toBe(4);
    // 光标在第 2 行第 3 列 a3 末尾 → 第 3 行；列值在该单元格内从 1 计
    const inCell = selectAt(posAfter('a3'));
    expect(inCell.line).toBe(3);
    expect(inCell.col).toBe(3);   // "a3" 2 字 + 1
  });

  it('多行单元格抬高整行行高：其后块行号随之递增', () => {
    // 单元格内 <br>（hardBreak）使该单元格占 2 可视行 → 表格行占 2 行
    const html = '<p>前</p>'
      + '<table><tbody><tr><td>x1<br>y1</td><td>z1</td></tr></tbody></table>'
      + '<p>后</p>';
    window.editorApi.setContent(html);
    // 前=1，表格行=2/3（最高单元格 2 行），后=4
    expect(selectAt(posAfter('后')).line).toBe(4);
  });
});
