// @vitest-environment jsdom
// 源码模式搜索高亮两症状（Wave 4 Task 8，Wave 2 用户验收反馈）：
//   ① 跨折行匹配「围 + 1 个空格」：取证结论——折行点二分记录的 lo 是「前缀占满
//      ≥N 行的最小长度」，软折行时第 lo 个字符已溢出第 N 行行尾（正是它把行数
//      撑到 N），真实行首是 lo−1；只有 lo−1 处是硬换行（前缀恰以 \n 结尾）时
//      行首才是 lo。旧代码把 lo 当行首入缓存 → 行归属整体右移 1 字符，跨折行
//      匹配的本行段多画一格（等宽下恰为一个空格宽）。任务书预判的「段宽测量
//      含行尾空白」不成立：overflow-wrap:anywhere 下空格移到次行行首，切片
//      测量本身精确（fidelity/pixel 取证，见 task-8-report.md）。
//   ② 先灰后橘：取证结论——折行点异步测量未完成时 _srcRenderHighlights 在
//      `if (!wrapPoints) return` 提前返回，body.src-search-active 未加上，
//      原生选区（未聚焦不绘制 / Enter 聚焦后灰底）透出整个测量窗口（120 行
//      文档实测约 6.3s），测量完成后才一次性切橘。激活类须在提前返回分支先行
//      加上（有匹配即激活，与底色块是否已就位无关）。
// jsdom 无布局：折行测量用 #src-measure-ta 的 scrollHeight getter mock 行模型
// （等宽容量 CAP 字/行 + \n 强制断行；空内容仍占 1 行——复刻真实 textarea
// rows=1 的 scrollHeight 伪影，fidelity 取证实锤 linesOf('')=1）。
import { describe, it, expect, beforeAll, beforeEach } from 'vitest';
import { readFileSync } from 'fs';
import { fileURLToPath } from 'url';
import path from 'path';

const W = path.dirname(fileURLToPath(import.meta.url));

function loadEditor() {
  window.eval(readFileSync(path.join(W, 'tiptap-all.js'), 'utf-8'));
  window.eval(readFileSync(path.join(W, 'editor.js'), 'utf-8'));
}

// 度量样式后端：#source-view 与两个测量 textarea 的计算样式统一由 Proxy 提供
//（jsdom 不解析 shorthand，paddingTop 等须显式给值，否则测量函数里 parseFloat
// 得 NaN 会静默走坏）。数值与取证环境一致：内容宽 680px、行高 28.8px。
const STYLE = {
  fontFamily: 'TestMono', fontSize: '18px', width: '800px',
  lineHeight: '28.8px',
  paddingTop: '40px', paddingBottom: '40px', padding: '40px 60px',
  paddingLeft: '60px', paddingRight: '60px',
  borderLeftWidth: '0px', borderRightWidth: '0px',
  border: '0px none rgb(0, 0, 0)',
  fontWeight: '400', fontStyle: 'normal',
  font: '18px TestMono'
};

// 折行行模型：容量 CAP 字符/行，\n 强制断行，空内容仍占 1 行
const CAP = 10;
const LH = 28.8;
const PADV = 80;   // paddingTop + paddingBottom，mock scrollHeight 还原用
const PADH = 60;   // paddingLeft（渲染 left 含此偏移）
const PADTOP = 40; // paddingTop 单侧（渲染 top 含此偏移；PADV 是上下双侧和）
function linesOfMock(prefix) {
  const hard = prefix.split('\n');
  let n = 0;
  for (let i = 0; i < hard.length; i++) n += Math.max(1, Math.ceil(hard[i].length / CAP));
  return Math.max(1, n);
}
// 给测量 textarea 装 scrollHeight getter（按当前 value 模拟真实行数×行高+纵向内边距）
function installScrollMock(el) {
  Object.defineProperty(el, 'scrollHeight', {
    configurable: true,
    get() { return linesOfMock(this.value) * LH + PADV; }
  });
}

// 驱动一次完整异步折行测量并返回入缓存的折行点
async function measureWrapPoints(text) {
  const api = window.editorApi;
  if (api._srcWrapTimer) { clearTimeout(api._srcWrapTimer); api._srcWrapTimer = null; }
  api._srcWrapCacheMap = null;   // 用例间隔离
  if (api._srcWrapGen) api._srcWrapGen++;
  api._srcWrapPending = null;
  api._srcEnsureAsyncWrapPoints(text);
  let guard = 0;
  while (api._srcWrapPending !== null && guard++ < 5000) {
    await new Promise(r => setTimeout(r, 0));
  }
  const e = api._srcWrapCacheLookup(text);
  return e ? e.points : null;
}

beforeAll(() => {
  // jsdom 未实现 window.scrollTo 与 Element.scrollIntoView，桩掉避免报错
  window.scrollTo = () => {};
  window.HTMLElement.prototype.scrollIntoView = () => {};
  document.body.innerHTML =
    '<div id="editor"></div>' +
    '<div id="source-wrap" style="display:none">' +
    '<pre id="source-overlay"></pre><textarea id="source-view"></textarea></div>';
  // 对 #source-view 与两个测量 textarea 拦截度量属性，其余透传原生
  // getComputedStyle（不影响 init/TipTap 的样式读取）
  const origGcs = window.getComputedStyle.bind(window);
  window.getComputedStyle = (el, pseudo) => {
    const base = origGcs(el, pseudo);
    if (el && (el.id === 'source-view' || el.id === 'src-measure-ta' ||
               el.id === 'src-measure-width-ta')) {
      return new Proxy(base, {
        get(target, prop) {
          if (prop in STYLE) return STYLE[prop];
          const v = target[prop];
          return (typeof v === 'function') ? v.bind(target) : v;
        }
      });
    }
    return base;
  };
  loadEditor();
  window.editorApi.init();
});

beforeEach(() => {
  const api = window.editorApi;
  api._srcSearch = null;
  api._srcWrapCacheMap = null;
  api._srcWrapPending = null;
  if (api._srcWrapTimer) { clearTimeout(api._srcWrapTimer); api._srcWrapTimer = null; }
  document.body.classList.remove('src-search-active');
  document.getElementById('source-overlay').innerHTML = '';
  document.getElementById('source-view').value = '';
  // 测量层就绪并装上 scrollHeight mock（懒创建，首个用例时才落地）
  api._srcEnsureMeasureTa();
  installScrollMock(document.getElementById('src-measure-ta'));
});

describe('折行点真实行首语义（症状①根因）', () => {
  it('软折行：二分命中 lo 已溢出行尾，记录行首 lo−1（旧代码差 1 字符）', async () => {
    // 容量 10：第 1 行 'a'×10，'b' 溢出 → 第 2 行行首 = 下标 10
    const pts = await measureWrapPoints('a'.repeat(10) + 'bc');
    expect(pts).toEqual([0, 10]);
  });

  it('硬换行：行首即 lo（前缀恰以 \\n 结尾），不回归', async () => {
    const pts = await measureWrapPoints('abc\ndef\n');
    expect(pts).toEqual([0, 4, 8]);
  });

  it('硬换行 + 软折行混合：逐行各归其位', async () => {
    // 第 1 行 'abcccccccc'（10 字硬断）、第 2 行 'defghijklm'（软折）、第 3 行 'no'
    const pts = await measureWrapPoints('abcccccccc\ndefghijklmno');
    expect(pts).toEqual([0, 11, 21]);
  });

  it('整行恰好填满（末行无溢出字符）：仍记真实行首', async () => {
    const pts = await measureWrapPoints('a'.repeat(20));
    expect(pts).toEqual([0, 10]);
  });

  it('多行软折行：每行行首均正确（容量 10，共 25 字）', async () => {
    const pts = await measureWrapPoints('a'.repeat(25));
    expect(pts).toEqual([0, 10, 20]);
  });
});

describe('跨折行匹配矩形构造（mock 测量，症状①的渲染面）', () => {
  it('匹配跨软折行：逐行段宽恰至折行点，无 +1 空格幻影格', () => {
    const api = window.editorApi;
    const text = 'a'.repeat(10) + 'bc';
    // 真实行首 [0,10]：匹配 8..12 → 行 1 段 8..10、行 2 段 10..12
    api._srcSearch = { query: 'ab', caseSensitive: false, regex: false, wholeWord: false,
                       matches: [{ from: 8, to: 12 }], current: 0 };
    const saved = api._srcMeasureLineWidth;
    api._srcMeasureLineWidth = (s) => s.length * 9;   // mock 等宽测量：9px/字符
    try {
      const rects = api._srcMeasureMatchRects([0, 10], text);
      expect(rects).toEqual([
        { top: 0, left: 72, width: 18, height: 28.8, matchIndex: 0 },
        { top: 28.8, left: 0, width: 18, height: 28.8, matchIndex: 0 }
      ]);
    } finally {
      api._srcMeasureLineWidth = saved;
      api._srcSearch = null;
    }
  });

  it('硬换行匹配：单段矩形，位置宽度不回归', () => {
    const api = window.editorApi;
    api._srcSearch = { query: 'de', caseSensitive: false, regex: false, wholeWord: false,
                       matches: [{ from: 4, to: 6 }], current: 0 };
    const saved = api._srcMeasureLineWidth;
    api._srcMeasureLineWidth = (s) => s.length * 9;
    try {
      const rects = api._srcMeasureMatchRects([0, 4, 8], 'abc\ndef\n');
      expect(rects).toEqual([
        { top: 28.8, left: 0, width: 18, height: 28.8, matchIndex: 0 }
      ]);
    } finally {
      api._srcMeasureLineWidth = saved;
      api._srcSearch = null;
    }
  });
});

describe('搜索启动全链路（overlay 几何以真实行首落位）', () => {
  it('匹配跨软折行：两块底色各自贴合本行文字（行 1 段宽 9px 而非 18px）', async () => {
    const api = window.editorApi;
    const sv = document.getElementById('source-view');
    const ov = document.getElementById('source-overlay');
    const text = 'a'.repeat(10) + 'bc';   // 'ab' 命中下标 9..11，跨折行
    sv.value = text;
    const saved = api._srcMeasureLineWidth;
    api._srcMeasureLineWidth = (s) => s.length * 9;
    try {
      api._srcStartSearch('ab', false, false, false);
      let guard = 0;
      while (api._srcWrapPending !== null && guard++ < 5000) {
        await new Promise(r => setTimeout(r, 0));
      }
      const e = api._srcWrapCacheLookup(text);
      expect(e.points).toEqual([0, 10]);
      expect(ov.children.length).toBe(2);
      const spans = Array.from(ov.children).map(s => ({
        cls: s.className, left: parseFloat(s.style.left), top: parseFloat(s.style.top),
        width: parseFloat(s.style.width), height: parseFloat(s.style.height) }));
      // overlay 相对边框盒定位：left 加 padLeft=60、top 加 padTop=40
      expect(spans).toEqual([
        { cls: 'src-mark-current', left: 141, top: 40, width: 9, height: 28.8 },
        { cls: 'src-mark-current', left: 60, top: 68.8, width: 9, height: 28.8 }
      ]);
    } finally {
      api._srcMeasureLineWidth = saved;
      api._srcStop();
    }
  });
});

describe('两态过渡观感（症状②：激活类先行，不等折行测量）', () => {
  it('冷缓存启动搜索：src-search-active 同步生效，测量完成后保持，stop 后移除', async () => {
    const api = window.editorApi;
    const sv = document.getElementById('source-view');
    const ov = document.getElementById('source-overlay');
    sv.value = 'a'.repeat(10) + 'bc';
    api._srcStartSearch('bc', false, false, false);
    // 同步断言：测量尚在进行（无底色块）但激活态已就位——textarea 背景即转透明，
    // 原生选区不再以灰底透出
    expect(api._srcWrapPending).not.toBeNull();
    expect(ov.children.length).toBe(0);
    expect(document.body.classList.contains('src-search-active')).toBe(true);
    // 测量完成：自动重渲染出底色块，激活态保持（无灰→橘两态跳变）
    let guard = 0;
    while (api._srcWrapPending !== null && guard++ < 5000) {
      await new Promise(r => setTimeout(r, 0));
    }
    expect(document.body.classList.contains('src-search-active')).toBe(true);
    expect(ov.children.length).toBeGreaterThan(0);
    // 停止搜索：清块并移除激活类（恢复 textarea 正常显示）
    api._srcStop();
    expect(document.body.classList.contains('src-search-active')).toBe(false);
    expect(ov.children.length).toBe(0);
  });

  it('无匹配：不进入激活态', () => {
    const api = window.editorApi;
    document.getElementById('source-view').value = 'no hit here';
    api._srcStartSearch('围栏', false, false, false);
    expect(document.body.classList.contains('src-search-active')).toBe(false);
    api._srcStop();
  });
});

describe('编辑后近似折行点平移（2026-10-05 用户报告：标题+1 字符 → 色块右移 1 字符宽）', () => {
  // 取证（CDP 真文档）：编辑后 _srcGetWrapPoints 走 _srcWrapCacheApprox 返回
  // 旧文本折行点直接配新文本测量——旧行首坐标全体偏小 N（编辑量），
  // x = measure(slice(旧行首, from)) 多含 N 字符 → 色块右移 N 字符宽；
  // 150ms 后重测在超长段落文档上秒-分钟级，偏移长期可见。
  async function waitRender() {
    const ov = document.getElementById('source-overlay');
    let guard = 0;
    while (!ov.querySelector('.src-mark-current') && guard++ < 5000)
      await new Promise(r => setTimeout(r, 0));
    return ov;
  }

  it('单点插入：近似点按编辑量平移，色块 left 与基线一致（现状偏 1 字符=9px）', async () => {
    const api = window.editorApi;
    const sv = document.getElementById('source-view');
    const ov = document.getElementById('source-overlay');
    const text = '## 二、超长超宽色块\n标签先行替换为 ZZ 前缀占位符加以保护';
    sv.value = text;
    const saved = api._srcMeasureLineWidth;
    api._srcMeasureLineWidth = (s) => s.length * 9;
    try {
      api._srcStartSearch('ZZ', false, false, false);
      const o = await waitRender();
      const baseLeft = parseFloat(o.querySelector('.src-mark-current').style.left);

      // 真实编辑链：标题行尾插 '1'（标题 11→12 字符，CAP=10 下都占 2 行——行结构不变）
      const nl = text.indexOf('\n');
      sv.value = text.slice(0, nl) + '1' + text.slice(nl);
      api._srcOnSourceEdit();
      await new Promise(r => setTimeout(r, 0));

      const span2 = ov.querySelector('.src-mark-current');
      expect(span2).toBeTruthy();   // 近似路径必须渲染（不是空窗）
      const afterLeft = parseFloat(span2.style.left);
      expect(afterLeft).toBe(baseLeft);
    } finally {
      api._srcMeasureLineWidth = saved;
    }
  });

  it('单点删除：同款平移（delta 为负）', async () => {
    const api = window.editorApi;
    const sv = document.getElementById('source-view');
    const ov = document.getElementById('source-overlay');
    const text = '## 二、超长超宽色块11\n标签先行替换为 ZZ 前缀占位符加以保护';
    sv.value = text;
    const saved = api._srcMeasureLineWidth;
    api._srcMeasureLineWidth = (s) => s.length * 9;
    try {
      api._srcStartSearch('ZZ', false, false, false);
      const o = await waitRender();
      const baseLeft = parseFloat(o.querySelector('.src-mark-current').style.left);

      // 删标题行尾两个 '1'
      const nl = text.indexOf('\n');
      sv.value = text.slice(0, nl - 2) + text.slice(nl);
      api._srcOnSourceEdit();
      await new Promise(r => setTimeout(r, 0));
      const afterLeft = parseFloat(ov.querySelector('.src-mark-current').style.left);
      expect(afterLeft).toBe(baseLeft);
    } finally {
      api._srcMeasureLineWidth = saved;
    }
  });

  it('多点编辑（不可单点平移）：退化为现状近似（不劣化、不崩溃）', async () => {
    const api = window.editorApi;
    const sv = document.getElementById('source-view');
    const text = '## 标题甲\n标签先行替换为 ZZ 前缀占位符加以保护';
    sv.value = text;
    const saved = api._srcMeasureLineWidth;
    api._srcMeasureLineWidth = (s) => s.length * 9;
    try {
      api._srcStartSearch('ZZ', false, false, false);
      let guard = 0;
      const ov = document.getElementById('source-overlay');
      while (!ov.querySelector('.src-mark-current') && guard++ < 5000)
        await new Promise(r => setTimeout(r, 0));
      // 两处独立编辑（标题+1、正文另一处+2）→ diff 非单区间
      const nl = text.indexOf('\n');
      sv.value = '## 标题甲1' + text.slice(nl, nl + 3) + 'xx' + text.slice(nl + 3);
      api._srcOnSourceEdit();
      await new Promise(r => setTimeout(r, 0));
      // 不崩溃且仍渲染（偏移可容忍，150ms 后重测自愈）
      expect(ov.querySelector('.src-mark-current')).toBeTruthy();
    } finally {
      api._srcMeasureLineWidth = saved;
    }
  });
});

describe('编辑后局部增量重测（二层：行结构变化场景，2026-10-05）', () => {
  // 平移近似只对「行结构不变」精确；编辑使行跨折行阈值（如 10 字标题加 1
  // 字变 11 字 → 2 行）时行号/行首变化无法用平移表达——局部增量重测从编辑
  // 行续测至最远匹配终点，几行测量内修正（替代全文档分钟级窗口）。
  async function waitPending() {
    const api = window.editorApi;
    let guard = 0;
    while (api._srcWrapPending !== null && guard++ < 5000)
      await new Promise(r => setTimeout(r, 0));
  }

  it('标题加 1 字跨折行阈值（1 行→2 行）：局部重测后色块 top 落正确行', async () => {
    const api = window.editorApi;
    const sv = document.getElementById('source-view');
    const ov = document.getElementById('source-overlay');
    // CAP=10：标题恰 10 字（1 行）；ZZ 在正文行（编辑前行 2）
    const text1 = '0123456789\nZZ 正文测试';
    sv.value = text1;
    const saved = api._srcMeasureLineWidth;
    api._srcMeasureLineWidth = (s) => s.length * 9;
    try {
      api._srcStartSearch('ZZ', false, false, false);
      let guard = 0;
      while (!ov.querySelector('.src-mark-current') && guard++ < 5000)
        await new Promise(r => setTimeout(r, 0));
      expect(parseFloat(ov.querySelector('.src-mark-current').style.top)).toBeCloseTo(PADTOP + LH, 5); // 行 2（含 padTop）

      // 标题加 'x' → 11 字跨阈值 → 标题 2 行 → ZZ 行 3
      sv.value = '0123456789x\nZZ 正文测试';
      api._srcOnSourceEdit();
      await waitPending();          // 局部增量重测完成
      await new Promise(r => setTimeout(r, 0));
      const top = parseFloat(ov.querySelector('.src-mark-current').style.top);
      expect(top).toBeCloseTo(PADTOP + 2 * LH, 5);   // 行 3（真值对照：全量重测同值）
    } finally {
      api._srcMeasureLineWidth = saved;
    }
  });

  it('同段内编辑（ZZ 与编辑点同行群）：left 同样局部修正', async () => {
    const api = window.editorApi;
    const sv = document.getElementById('source-view');
    const ov = document.getElementById('source-overlay');
    // 'aaaaZZbbbb' 恰 10 字 1 行；ZZ 列偏移 4
    const text1 = 'aaaaZZbbbb\n尾巴';
    sv.value = text1;
    const saved = api._srcMeasureLineWidth;
    api._srcMeasureLineWidth = (s) => s.length * 9;
    try {
      api._srcStartSearch('ZZ', false, false, false);
      let guard = 0;
      while (!ov.querySelector('.src-mark-current') && guard++ < 5000)
        await new Promise(r => setTimeout(r, 0));
      expect(parseFloat(ov.querySelector('.src-mark-current').style.left)).toBeCloseTo(PADH + 4 * 9, 5);

      // 段首插 1 字 → 11 字跨阈值 → 'b' 溢出行 2；ZZ 仍在行 1 列 5
      sv.value = 'xaaaaZZbbbb\n尾巴';
      api._srcOnSourceEdit();
      await waitPending();
      await new Promise(r => setTimeout(r, 0));
      const span = ov.querySelector('.src-mark-current');
      expect(parseFloat(span.style.top)).toBeCloseTo(PADTOP, 5);          // 仍行 1（含 padTop）
      expect(parseFloat(span.style.left)).toBeCloseTo(PADH + 5 * 9, 5); // 列 5（含 padLeft）
    } finally {
      api._srcMeasureLineWidth = saved;
    }
  });
});
