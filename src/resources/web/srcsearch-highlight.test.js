// @vitest-environment jsdom
// 源码模式搜索高亮（2026-10-08 机制重构后的按需局部测量）：
//   历史沿革：Wave 4 Task 8 取证修正了折行点二分的行首语义（软折行行首 = 命中
//   值 −1；\n 后保持原值），旧实现将其物化为「全文档折行点表 + 跨会话缓存」。
//   2026-10-08 用户实测：该机制冷启动 318 行文档需 13-20s；编辑/窗口缩放使
//   整表失效后长时间渲染陈旧坐标（色块漂移）。重构为按需局部测量——
//   行号 = linesOf(slice(0, p+1)) − 1（一次 scrollHeight 读）；行首在
//   [p−256, p+1] 窗口内二分（T8 语义），成本与文档总行数无关、每次渲染现算。
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

// 折行行模型：容量 CAP 字符/行，\n 强制断行，空内容仍占 1 行。
// CAP 可变（let）：resize 用例通过改 CAP 模拟「宽度变化 → 折行布局变化」。
let CAP = 10;
const LH = 28.8;
const PADV = 80;    // paddingTop + paddingBottom，mock scrollHeight 还原用
const PADTOP = 40;  // paddingTop 单侧（渲染 top 含此偏移）
const PADH = 60;    // paddingLeft（渲染 left 含此偏移）
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

// 等 overlay 出齐 n 个色块（渐进分片可能跨帧；短列表单帧同步完成）
async function waitSpans(ov, n) {
  let guard = 0;
  while (ov.children.length < n && guard++ < 5000)
    await new Promise(r => setTimeout(r, 0));
  return Array.from(ov.children).map(s => ({
    cls: s.className, left: parseFloat(s.style.left), top: parseFloat(s.style.top),
    width: parseFloat(s.style.width), height: parseFloat(s.style.height) }));
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
  api._srcRectGen = 0;
  if (api._srcResizeTimer) { clearTimeout(api._srcResizeTimer); api._srcResizeTimer = null; }
  document.body.classList.remove('src-search-active');
  document.getElementById('source-overlay').innerHTML = '';
  document.getElementById('source-view').value = '';
  CAP = 10;
  // 测量层就绪并装上 scrollHeight mock（懒创建，首个用例时才落地）
  api._srcEnsureMeasureTa();
  installScrollMock(document.getElementById('src-measure-ta'));
});

describe('按需局部测量：行首/行号语义（T8 修正语义在新区间二分中的延续）', () => {
  function segsOf(text, from, to) {
    const api = window.editorApi;
    const m = api._srcEnsureMeasureTa();
    const saved = api._srcMeasureLineWidth;
    api._srcMeasureLineWidth = (s) => s.length * 9;   // mock 等宽测量：9px/字符
    try {
      return api._srcMatchSegs(m, text, from, to);
    } finally {
      api._srcMeasureLineWidth = saved;
    }
  }

  it('软折行：行首取溢出字符语义 −1（a×10 + bc，匹配 8..12 跨行）', () => {
    expect(segsOf('a'.repeat(10) + 'bc', 8, 12)).toEqual([
      { line: 0, left: 72, width: 18 },
      { line: 1, left: 0, width: 18 }
    ]);
  });

  it('硬换行：反斜杠 n 后行首即原值（abc/def，匹配 4..6）', () => {
    expect(segsOf('abc\ndef\n', 4, 6)).toEqual([
      { line: 1, left: 0, width: 18 }
    ]);
  });

  it('硬换行两行混合软折行：行号/行首各自正确（abcccccccc + defghijklmno）', () => {
    expect(segsOf('abcccccccc\ndefghijklmno', 11, 13)).toEqual([
      { line: 1, left: 0, width: 18 }
    ]);
  });

  it('连续软折行：逐行行段（a×25，匹配 8..23 跨三行）', () => {
    expect(segsOf('a'.repeat(25), 8, 23)).toEqual([
      { line: 0, left: 72, width: 18 },
      { line: 1, left: 0, width: 90 },
      { line: 2, left: 0, width: 27 }
    ]);
  });

  it('测量成本与文档总行数无关：单匹配深位置 ≤ 64 次 scrollHeight 读（旧全量走查 ~O(行数)）', () => {
    const api = window.editorApi;
    const m = api._srcEnsureMeasureTa();
    // 2000 字符（CAP=10 → 200 行），匹配在 1500..1502（深位置）
    const text = 'a'.repeat(2000);
    let reads = 0;
    const orig = api._srcLinesOf.bind(api);
    api._srcLinesOf = (mm, t, end) => { reads++; return orig(mm, t, end); };
    const saved = api._srcMeasureLineWidth;
    api._srcMeasureLineWidth = (s) => s.length * 9;
    try {
      const segs = api._srcMatchSegs(m, text, 1500, 1502);
      expect(segs.length).toBe(1);
      expect(reads).toBeLessThanOrEqual(64);   // 1 行号 + 1 末行号 + 窗口二分 ~8 次
    } finally {
      api._srcLinesOf = orig;
      api._srcMeasureLineWidth = saved;
    }
  });
});

describe('搜索启动全链路（overlay 几何以真实行首落位；首帧同步完成）', () => {
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
      const spans = await waitSpans(ov, 2);
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

  it('多匹配渐进渲染：全部就位，当前匹配橙、其余淡黄', async () => {
    const api = window.editorApi;
    const sv = document.getElementById('source-view');
    const ov = document.getElementById('source-overlay');
    sv.value = 'ZZ 甲\nZZ 乙\nZZ 丙\nZZ 丁\nZZ 戊';
    const saved = api._srcMeasureLineWidth;
    api._srcMeasureLineWidth = (s) => s.length * 9;
    try {
      api._srcStartSearch('ZZ', false, false, false);
      const spans = await waitSpans(ov, 5);
      expect(spans.length).toBe(5);
      expect(spans.filter(s => s.cls === 'src-mark-current').length).toBe(1);
      expect(spans.filter(s => s.cls === 'src-mark').length).toBe(4);
    } finally {
      api._srcMeasureLineWidth = saved;
      api._srcStop();
    }
  });
});

describe('两态过渡观感（症状②：激活类先行）', () => {
  it('启动搜索：src-search-active 同步生效且首帧即出色块；stop 后清除', async () => {
    const api = window.editorApi;
    const sv = document.getElementById('source-view');
    const ov = document.getElementById('source-overlay');
    sv.value = 'a'.repeat(10) + 'bc';
    const saved = api._srcMeasureLineWidth;
    api._srcMeasureLineWidth = (s) => s.length * 9;
    try {
      api._srcStartSearch('bc', false, false, false);
      // 同步断言（新机制首帧同步渲染——不再有「先激活无块」的测量窗）：
      // 激活类立即就位，底色块随首帧渲染出现
      expect(document.body.classList.contains('src-search-active')).toBe(true);
      const spans = await waitSpans(ov, 1);
      expect(spans.length).toBeGreaterThan(0);
      expect(document.body.classList.contains('src-search-active')).toBe(true);
      // 停止搜索：清块并移除激活类（恢复 textarea 正常显示）
      api._srcStop();
      expect(document.body.classList.contains('src-search-active')).toBe(false);
      expect(ov.children.length).toBe(0);
    } finally {
      api._srcMeasureLineWidth = saved;
    }
  });

  it('无匹配：不进入激活态', () => {
    const api = window.editorApi;
    document.getElementById('source-view').value = 'no hit here';
    api._srcStartSearch('围栏', false, false, false);
    expect(document.body.classList.contains('src-search-active')).toBe(false);
    api._srcStop();
  });
});

describe('编辑后高亮即时正确（2026-10-05/10-08 用户报告场景）', () => {
  it('同段内编辑（行结构不变）：left 即时正确', async () => {
    const api = window.editorApi;
    const sv = document.getElementById('source-view');
    const ov = document.getElementById('source-overlay');
    const text = 'aaaaZZbbbb\n尾巴';
    sv.value = text;
    const saved = api._srcMeasureLineWidth;
    api._srcMeasureLineWidth = (s) => s.length * 9;
    try {
      api._srcStartSearch('ZZ', false, false, false);
      await waitSpans(ov, 1);
      expect(parseFloat(ov.querySelector('.src-mark-current').style.left)).toBeCloseTo(PADH + 4 * 9, 5);

      // 段首插 1 字 → 11 字跨阈值 → 'b' 溢出行 2；ZZ 仍在行 1 列 5
      sv.value = 'xaaaaZZbbbb\n尾巴';
      api._srcOnSourceEdit();
      const spans = await waitSpans(ov, 1);
      expect(spans[0].top).toBeCloseTo(PADTOP, 5);              // 仍行 1（含 padTop）
      expect(spans[0].left).toBeCloseTo(PADH + 5 * 9, 5);       // 列 5（含 padLeft）
    } finally {
      api._srcMeasureLineWidth = saved;
    }
  });

  it('标题加 1 字跨折行阈值（1 行→2 行）：top 即时落正确行', async () => {
    const api = window.editorApi;
    const sv = document.getElementById('source-view');
    const ov = document.getElementById('source-overlay');
    const text1 = '0123456789\nZZ 正文测试';
    sv.value = text1;
    const saved = api._srcMeasureLineWidth;
    api._srcMeasureLineWidth = (s) => s.length * 9;
    try {
      api._srcStartSearch('ZZ', false, false, false);
      let spans = await waitSpans(ov, 1);
      expect(spans[0].top).toBeCloseTo(PADTOP + LH, 5);   // 行 2（含 padTop）

      // 标题加 'x' → 11 字跨阈值 → 标题 2 行 → ZZ 行 3
      sv.value = '0123456789x\nZZ 正文测试';
      api._srcOnSourceEdit();
      spans = await waitSpans(ov, 1);
      expect(spans[0].top).toBeCloseTo(PADTOP + 2 * LH, 5);   // 行 3
    } finally {
      api._srcMeasureLineWidth = saved;
    }
  });

  it('多点编辑（跨段落多处插入）：每次渲染即时正确（旧机制此时渲染陈旧全文表）', async () => {
    const api = window.editorApi;
    const sv = document.getElementById('source-view');
    const ov = document.getElementById('source-overlay');
    sv.value = '头段\n甲乙丙\nZZ 尾段';
    const saved = api._srcMeasureLineWidth;
    api._srcMeasureLineWidth = (s) => s.length * 9;
    try {
      api._srcStartSearch('ZZ', false, false, false);
      let spans = await waitSpans(ov, 1);
      expect(spans[0].top).toBeCloseTo(PADTOP + 2 * LH, 5);   // 行 3

      // 两处独立插入（第一段后 + 第二段内），使 ZZ 行下移
      sv.value = '头段xx\n甲乙丙y\nZZ 尾段';
      api._srcOnSourceEdit();
      spans = await waitSpans(ov, 1);
      expect(spans[0].top).toBeCloseTo(PADTOP + 2 * LH, 5);   // 行结构未变，仍行 3
      expect(spans[0].left).toBeCloseTo(PADH, 5);             // 列 0
    } finally {
      api._srcMeasureLineWidth = saved;
    }
  });
});

describe('窗口缩放后重绘（2026-10-08 用户报告：恢复/最大化窗口色块漂移）', () => {
  it('折行容量变化（CAP 10→5）：显式重绘后色块按新布局落位', async () => {
    const api = window.editorApi;
    const sv = document.getElementById('source-view');
    const ov = document.getElementById('source-overlay');
    sv.value = '0123456789\nZZ 尾';
    const saved = api._srcMeasureLineWidth;
    api._srcMeasureLineWidth = (s) => s.length * 9;
    try {
      api._srcStartSearch('ZZ', false, false, false);
      let spans = await waitSpans(ov, 1);
      expect(spans[0].top).toBeCloseTo(PADTOP + LH, 5);   // CAP=10：题行 1 行，ZZ 行 2

      // 模拟窗口缩放：折行容量变小（10→5），第一行变 2 行，ZZ 行下移至行 3
      CAP = 5;
      installScrollMock(document.getElementById('src-measure-ta'));   // 重装 mock（CAP 已变）
      api._srcRenderHighlights();                                     // resize 处理器防抖后的重绘
      spans = await waitSpans(ov, 1);
      expect(spans[0].top).toBeCloseTo(PADTOP + 2 * LH, 5);   // 新布局行 3
    } finally {
      api._srcMeasureLineWidth = saved;
    }
  });
});
