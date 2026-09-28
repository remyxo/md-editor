// @vitest-environment jsdom
// 全链路往返矩阵：真实 TipTap（tiptap-all.js + editor.js）× 真实 C++ 转换
// （mdconv 子进程桥）。链路对齐生产编排：
//   加载 = tohtml → <body> 提取 → setContent（同 EditorTab::loadFile）
//   保存 = getSaveSnapshot().html → fromhtml（同 writeTabToFile）
//   进源码 = getSaveSnapshot → fromhtml → enterSourceMode（同 onToggleSourceMode）
//   退出已编辑 = exitSourceModeOrGetMd 返回 MD → tohtml → exitSourceMode(html)
import { describe, it, expect, beforeAll } from 'vitest';
import { readFileSync } from 'fs';
import { spawnSync } from 'child_process';
import { accessSync, constants } from 'fs';
import path from 'path';
import { fileURLToPath } from 'url';

const W = path.dirname(fileURLToPath(import.meta.url));
const MDCONV = process.env.MDCONV_PATH
  || path.join(W, '..', '..', '..', 'build', 'tests', 'mdconv');

function mdconvAvailable() {
  try { accessSync(MDCONV, constants.X_OK); return true; } catch { return false; }
}
function mdconv(mode, input) {
  const r = spawnSync(MDCONV, [mode], { input, encoding: 'utf8', timeout: 60000 });
  if (r.status !== 0) throw new Error(`mdconv ${mode} 失败: ${r.stderr}`);
  return r.stdout;
}
// <body> 提取：与 EditorTab.cpp:221-229 同款
function bodyOf(fullHtml) {
  const s = fullHtml.indexOf('<body'), e = fullHtml.indexOf('</body>');
  if (s < 0 || e <= s) return fullHtml;
  return fullHtml.slice(fullHtml.indexOf('>', s) + 1, e).trim();
}

const FIXTURES = [
  { name: '标题与段落', md: '# 一级\n\n## 二级\n\n正文段落。\n' },
  { name: '行内强调矩阵', md: '**加粗**、*斜体*、~~删除~~、==高亮==、H~2~O、x^2^、`code`。\n' },
  { name: '表格对齐', md: '| 左 | 中 | 右 |\n|---|:---:|---:|\n| a | b | c |\n| 甲 | 乙 | 丙 |\n' },
  { name: '任务列表', md: '- [ ] 待办\n- [x] 完成\n' },
  { name: '脚注', md: '引用[^1]。\n\n[^1]: 脚注定义。\n' },
  { name: '公式', md: '行内 $a^2+b^2=c^2$。\n\n块级：\n\n$$\nE=mc^2\n$$\n' },
  { name: '嵌套围栏', md: '````md\n```js\ncode\n```\n````\n' },
  { name: 'HTML 字面', md: '<kbd>Ctrl</kbd>+<kbd>S</kbd> 与 <small>小字</small>。<span style="color:red">红</span>\n' },
  { name: 'br 与软换行', md: '第一行<br>第二行\n软换行行\n' },
  { name: '引用与列表嵌套', md: '> 引用\n>\n> - 列表项\n> - 另一项\n' },
  { name: 'CJK 长行', md: '甲乙丙丁戊己庚辛壬癸'.repeat(8) + ' 内容混排 **加粗** 与 `code`。\n' },
  { name: '链接与图片', md: '[链接](https://example.com "标题") 与 ![图](assets/x.png)\n' },
];

let rounds;
function chainRound(md) {
  const html = bodyOf(mdconv('tohtml', md));
  window.editorApi.setContent(html);
  const snap = window.editorApi.getSaveSnapshot();
  return mdconv('fromhtml', snap.html);
}

beforeAll(() => {
  window.scrollTo = () => {};
  window.HTMLElement.prototype.scrollIntoView = () => {};
  document.body.innerHTML =
    '<div id="editor"></div>' +
    '<div id="source-wrap" style="display:none">' +
    '<pre id="source-overlay"></pre><textarea id="source-view"></textarea></div>';
  window.eval(readFileSync(path.join(W, 'tiptap-all.js'), 'utf8'));
  window.eval(readFileSync(path.join(W, 'editor.js'), 'utf8'));
  window.editorApi.init();
  window.editorApi.bridge = new Proxy({}, { get: () => () => {} });
});

// gfm 语料（与 C++ test_gfm_corpus 同一文件、同一分节正则）：真实链路 3 轮
import { createRequire } from 'module';
const CORPUS = readFileSync(path.join(W, '..', '..', '..', 'tests', 'fixtures', 'gfm-corpus.md'), 'utf8');
function corpusSections() {
  const re = /<!-- corpus: ([a-z0-9-]+) -->/g;
  const out = []; let m;
  while ((m = re.exec(CORPUS))) {
    const start = re.lastIndex;
    const rest = CORPUS.slice(start);
    const next = rest.search(/<!-- corpus: /);
    out.push({ name: m[1], md: (next < 0 ? rest : rest.slice(0, next)).trim() });
  }
  return out;
}

(mdconvAvailable() ? describe : describe.skip)('全链路往返矩阵（真实 TipTap × mdconv）', () => {
  for (const fx of FIXTURES) {
    it(`${fx.name}：3 轮往返收敛（byteStable 或两趟收敛）`, () => {
      rounds = [fx.md, chainRound(fx.md)];
      rounds.push(chainRound(rounds[1]));
      rounds.push(chainRound(rounds[2]));
      const byteStable = rounds[1] === fx.md;
      const convergedAt = [1, 2].find((k) => rounds[k] === rounds[k + 1]);
      expect(byteStable || convergedAt !== undefined,
        `发散：r1=${JSON.stringify(rounds[1].slice(0, 200))} r2=${JSON.stringify(rounds[2].slice(0, 200))}`)
        .toBe(true);
      // 转义不变式：两趟后反斜杠/反引号计数不再增长
      const esc = (s) => (s.match(/[\\`]/g) || []).length;
      if (!byteStable) expect(esc(rounds[2])).toBe(esc(rounds[3]));
    });
  }

  it('源码编辑退出路径：注入段存活且标脏', () => {
    const md0 = FIXTURES[0].md;
    const html = bodyOf(mdconv('tohtml', md0));
    window.editorApi.setContent(html);
    const md1 = mdconv('fromhtml', window.editorApi.getSaveSnapshot().html);
    window.editorApi.enterSourceMode(md1);                       // 进源码
    const sv = document.getElementById('source-view');
    sv.value += '\n\nvitest 注入段。\n';
    sv.dispatchEvent(new Event('input'));                        // 标脏
    const mdEdited = window.editorApi.exitSourceModeOrGetMd();  // C++ 慢路径起点
    expect(typeof mdEdited).toBe('string');
    const html2 = bodyOf(mdconv('tohtml', mdEdited));
    window.editorApi.exitSourceMode(html2);                      // C++ 同款重建
    expect(window.editorApi._dirty).toBe(true);
    const md2 = mdconv('fromhtml', window.editorApi.getSaveSnapshot().html);
    expect(md2).toContain('vitest 注入段');
  });

  // gfm 语料（与 C++ test_gfm_corpus 同一文件、同一分节正则）
  for (const sec of corpusSections()) {
    it(`gfm 语料[${sec.name}]：真实链路 3 轮收敛`, () => {
      const r = [sec.md];
      for (let k = 0; k < 3; k++) r.push(chainRound(r[k]));
      const ok = r[1] === sec.md || r[2] === r[3];
      expect(ok, `r1=${JSON.stringify(r[1].slice(0, 200))} r3=${JSON.stringify(r[3].slice(0, 200))}`).toBe(true);
    });
  }
});
