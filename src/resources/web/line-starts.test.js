// @vitest-environment jsdom
// 逻辑行枚举 _lineStarts 回归测试：与状态栏行号 _lineColAt 语义同源。
//
// 背景：编辑器边缘行号（原 CSS counter 按顶级节点计数）与状态栏（按可视行
// 计数）语义不一致——代码块/多行引用被当成 1 行。重做为逐行号 gutter 后，
// 行的枚举必须与状态栏完全一致：_lineStarts 枚举每个逻辑可视行的行首 pos，
// 本测试用「全 pos 交叉一致性」锁定两套实现永不漂移。
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

const doc = () => window.editorApi.editor.state.doc;
const startsOf = () => window.editorApi._lineStarts(doc());
const lineColAt = (pos) => window.editorApi._lineColAt(doc(), pos);

/** 交叉一致性：对每个「TextSelection 可停留位置」（textblock 内容区全部 pos），
 *  _lineColAt(p).line 必须等于「starts 中 ≤ p 的最大行」。例外：
 *  - 表格单元格内位置不参与——同排单元格并排于同一视觉行，线性 pos→行
 *    映射不适用（其 _lineColAt 语义由 cursor-linecol.test.js 直接锁定，
 *    本文件以行数断言锁定枚举侧）；
 *  - 块原子 NodeSelection 的行号语义已由 cursor-linecol.test.js 锁定；
 *  - 块间缝隙 pos（容器 token/相邻原子共享边界）光标不可达。
 *  两套独立实现互相验证——任何一侧语义漂移都会在此暴露。 */
function assertConsistent() {
  const d = doc(), starts = startsOf();
  const positions = [];
  d.descendants(function(node, pos) {
    if (!node.isTextblock) return true;
    for (let p = pos + 1; p <= pos + node.nodeSize - 1; p++) positions.push(p);
    return true;
  });
  for (const p of positions) {
    const $p = d.resolve(p);
    let inTable = false;
    for (let i = 0; i <= $p.depth; i++) {
      const n = $p.node(i).type.name;
      if (n === 'tableRow' || n === 'tableCell' || n === 'tableHeader') { inTable = true; break; }
    }
    if (inTable) continue;
    let k = -1;
    for (let i = 0; i < starts.length; i++) {
      if (starts[i] <= p) k = i; else break;     // starts 单调非严格递增
    }
    const got = lineColAt(p).line, want = k + 1;
    if (got !== want)
      throw new Error(`pos ${p}: _lineColAt=${got}，枚举行=${want}（starts=${JSON.stringify(starts)}）`);
  }
}

/** 动态定位：文本 needle 首字符的 pos（避免手算节点位置） */
function posOf(needle) {
  let hit = null;
  doc().descendants(function(node, pos) {
    if (hit !== null || !node.isText) return hit === null;
    const i = node.text.indexOf(needle);
    if (i >= 0) { hit = pos + i; return false; }
    return true;
  });
  if (hit === null) throw new Error('未找到文本: ' + needle);
  return hit;
}

describe('逻辑行枚举 _lineStarts（与状态栏行号同源）', () => {
  beforeAll(() => {
    document.body.innerHTML = '<div id="editor"></div>';
    loadEditor();
    window.editorApi.init();
    window.editorApi.bridge = new Proxy({}, { get: () => () => {} });
  });

  beforeEach(() => { /* setContent 由各用例自理 */ });

  it('空段落文档：1 个逻辑行，行首为段落起始', () => {
    window.editorApi.setContent('<p></p>');
    const starts = startsOf();
    expect(starts.length).toBe(1);
    expect(starts[0]).toBe(0);
    assertConsistent();
  });

  it('标题+多段落：每块 1 行，行首 = 块起始 pos', () => {
    window.editorApi.setContent('<h2>T</h2><p>aa</p><p>bb</p>');
    // h2 start=0, p(aa) start=3, p(bb) start=7
    expect(startsOf()).toEqual([0, 3, 7]);
    assertConsistent();
  });

  it('代码块内每个 \\n 产出一行，行首 = 换行后首字符 pos', () => {
    window.editorApi.setContent('<pre><code>a\nb\nc</code></pre>');
    // code 内容 1..7：'a'(1) '\n'(2) 'b'(3) '\n'(4) 'c'(5) → 行首 0 / 3 / 5
    expect(startsOf()).toEqual([0, 3, 5]);
    assertConsistent();
  });

  it('段内 hardBreak 产出一行，行首 = br 之后 pos', () => {
    window.editorApi.setContent('<p>x<br>y</p>');
    // x(1) hardBreak(2, nodeSize=1) y(3)：行首 0 / 3
    expect(startsOf()).toEqual([0, 3]);
    assertConsistent();
  });

  it('引用多段与列表嵌套：行随内部 textblock 递增', () => {
    window.editorApi.setContent(
      '<blockquote><p>a</p><p>b<br>c</p></blockquote>'
      + '<ul><li>l1<ul><li>l2</li></ul></li></ul>');
    // 引用 p(a)=1 + p(b<br>c)=2 → 3 行；li(l1)=1 + 嵌套 li(l2)=1 → 2 行；共 5 行
    expect(startsOf().length).toBe(5);
    assertConsistent();
  });

  it('表格按行占行：多行单元格抬高行数，行首取行内首个可寻址段', () => {
    window.editorApi.setContent(
      '<p>前</p>'
      + '<table><tbody>'
      + '<tr><th>H</th></tr>'
      + '<tr><td>x1<br>y1</td><td>z1</td></tr>'
      + '</tbody></table>'
      + '<p>后</p>');
    // 前=1行，表头行=1行，数据行=2行（最高单元格），后=1行 → 共 5 行
    expect(startsOf().length).toBe(5);
    assertConsistent();
  });

  it('块级原子：图片/mermaid 各占 1 行，多行公式占 latex 可见行数', () => {
    window.editorApi.setContent(
      '<p>引言</p>'
      + '<img src="a.jpg" alt="i">'
      + '<pre><code class="language-mermaid">flowchart TD\nA--&gt;B</code></pre>'
      + '<div data-type="block-math" data-latex="\\begin{aligned}a \\\\ b \\\\ c\\end{aligned}"></div>'
      + '<p>结语</p>');
    // 引言1 + 图片1 + mermaid1 + 公式3 + 结语1 = 7 行
    expect(startsOf().length).toBe(7);
    assertConsistent();
  });

  it('hr 不占行：仅 hr 的文档无逻辑行（状态栏兜底 1）', () => {
    window.editorApi.setContent('<hr>');
    expect(startsOf()).toEqual([]);
    assertConsistent();
  });

  it('覆盖文档全 pos 交叉一致性（各块型合体）', () => {
    window.editorApi.setContent(
      '<h1>报告</h1>'
      + '<p>intro <strong>bo</strong> ld</p>'
      + '<pre><code>l1\nl2\nl3</code></pre>'
      + '<blockquote><p>项目A<br>日期B</p><p>范围C</p></blockquote>'
      + '<hr>'
      + '<ul><li>i1</li><li>i2<ol><li>n1</li></ol></li></ul>'
      + '<table><tbody><tr><th>H1</th><th>H2</th></tr>'
      + '<tr><td>a1<br>b1</td><td>a2</td></tr></tbody></table>'
      + '<img src="x.jpg" alt="x">'
      + '<div data-type="block-math" data-latex="E=mc^2"></div>'
      + '<h2>章节D</h2>');
    assertConsistent();
    // 锚点抽查：D 所在行 = h1(1)+p(1)+代码3+引用3+hr0+列表3+表头1+数据行2+图1+公式1 = 17
    const dLine = lineColAt(posOf('D') + 1).line;
    expect(dLine).toBe(17);
    // 行 17 的行首 = h2 块起始 101（枚举侧锚点）；行内可达位置（内容首位 102）
    // 的状态栏行号同为 17——注意不能查 101 本身：它是公式块末端与 h2 起始共享
    // 的缝隙 pos，_lineColAt 按相邻原子边界先认领为公式行
    const starts = startsOf();
    expect(starts.length).toBe(17);
    expect(starts[16]).toBe(101);
    expect(lineColAt(starts[16] + 1).line).toBe(17);
  });
});
