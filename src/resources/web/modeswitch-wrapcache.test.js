// @vitest-environment jsdom
// 折行点缓存跨源码会话保留（Wave 2 切换体验 Task 4）：
// 痛点②「看一眼源码再切回也要全文重测」——旧实现 _srcStop 退出源码模式即清空
// _srcWrapCache，重进必重测（实测 guide 档 ~23s、4k 档 O(行²) >10min）。改为
// 度量指纹 key 化 + LRU 的跨会话缓存后：
//   ① _srcStop 后缓存条目仍在（不再清空）
//   ② 度量变化（同文本不同 fontFamily/布局宽度）→ key 不匹配 miss
//   ③ 同 key 二次进入源码模式命中
// jsdom 无布局，不测真实折行测量（_srcEnsureAsyncWrapPoints 依赖 scrollHeight，
// 恒 0 会悬挂）；缓存构造走 _srcWrapCacheSet 直接回填，真实收益由 Task 1 基线
// 脚本的 wrap_cache_hit 判定。
import { describe, it, expect, beforeAll } from 'vitest';
import { readFileSync } from 'fs';
import { fileURLToPath } from 'url';
import path from 'path';

const W = path.dirname(fileURLToPath(import.meta.url));

function loadEditor() {
  window.eval(readFileSync(path.join(W, 'tiptap-all.js'), 'utf-8'));
  window.eval(readFileSync(path.join(W, 'editor.js'), 'utf-8'));
}

// #source-view 的度量现值（getComputedStyle mock 的可变后端）——度量指纹
// （fontFamily/fontSize/宽度）由此构造，模拟主题字号/窗口缩放变化。
const svStyle = { fontFamily: 'TestMono', fontSize: '14px', width: '800px' };

beforeAll(() => {
  // jsdom 未实现 window.scrollTo 与 Element.scrollIntoView，桩掉避免报错
  window.scrollTo = () => {};
  window.HTMLElement.prototype.scrollIntoView = () => {};
  document.body.innerHTML =
    '<div id="editor"></div>' +
    '<div id="source-wrap" style="display:none">' +
    '<pre id="source-overlay"></pre><textarea id="source-view"></textarea></div>';
  // 只对 #source-view 拦截 fontFamily/fontSize/width 三个度量属性，其余元素与
  // 属性透传原生 getComputedStyle（不影响 init/TipTap 的样式读取）
  const origGcs = window.getComputedStyle.bind(window);
  window.getComputedStyle = (el, pseudo) => {
    const base = origGcs(el, pseudo);
    if (el && el.id === 'source-view') {
      return new Proxy(base, {
        get(target, prop) {
          if (prop === 'fontFamily') return svStyle.fontFamily;
          if (prop === 'fontSize') return svStyle.fontSize;
          if (prop === 'width') return svStyle.width;
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

describe('折行点缓存跨源码会话保留', () => {
  it('_srcStop 后条目仍在；度量变化 key 不匹配 miss；同 key 二次进入命中', () => {
    const api = window.editorApi;
    api.setContent('<p>hello wrap cache</p>');
    api.enterSourceMode('hello wrap cache\n');
    const sv = document.getElementById('source-view');
    // 模拟一次已完成的折行测量回填（jsdom 无布局，不走异步测量链路）
    api._srcWrapCacheSet(sv.value, [0, sv.value.length]);
    expect(window.__wrapCacheState()).toEqual({ entries: 1, hit: true });

    // ① _srcStop（stopSearch 与 exitSourceMode 共用的清理入口）不再清空折行缓存
    api._srcStop();
    expect(window.__wrapCacheState().entries).toBe(1);

    // 完整往返（未编辑快速路径）：退出 → 再进入，条目仍在且精确命中
    api.exitSourceMode(null);
    api.enterSourceMode('hello wrap cache\n');
    expect(window.__wrapCacheState()).toEqual({ entries: 1, hit: true });

    // ② 度量变化：同文本不同 fontFamily → key 不匹配 miss
    svStyle.fontFamily = 'OtherMono';
    expect(window.__wrapCacheState().hit).toBe(false);
    // 布局宽度变化（视口宽改变的映射，测量层宽度来源即该计算值）→ miss
    svStyle.fontFamily = 'TestMono';
    svStyle.width = '640px';
    expect(window.__wrapCacheState().hit).toBe(false);

    // ③ 度量还原 → 同 key 再次命中（二次进入免全文重测）
    svStyle.width = '800px';
    expect(window.__wrapCacheState().hit).toBe(true);

    // FNV-1a 32 位已知向量（自实现纯函数的锚点自证）
    expect(api._srcFnv1a32('')).toBe(0x811c9dc5);
    expect(api._srcFnv1a32('a')).toBe(0xe40c292c);
    expect(api._srcFnv1a32('foobar')).toBe(0xbf9cf968);
  });

  it('测量窗内度量变化：起点锁存指纹与完成时不一致 → 丢弃不入缓存', () => {
    const api = window.editorApi;
    api._srcWrapCacheMap = null;   // 用例间隔离
    // 模拟测量启动时刻锁存的度量指纹（_srcEnsureAsyncWrapPoints 起点锁存）
    const mfp0 = api._srcWrapCacheMfp();
    // 测量窗内度量变化（前后两次 getComputedStyle 返回不同值，如窗口缩放/字号变化）
    svStyle.fontSize = '16px';
    expect(api._srcWrapCacheCommit('mid-measure changed\n', [0, 21], mfp0)).toBe(false);
    expect(window.__wrapCacheState().entries).toBe(0);   // 不收旧度量下的结果
    // 度量一致的提交正常入缓存（防丢弃逻辑误伤正常路径）
    const mfpNow = api._srcWrapCacheMfp();
    expect(api._srcWrapCacheCommit('stable metrics\n', [0, 15], mfpNow)).toBe(true);
    expect(window.__wrapCacheState().entries).toBe(1);
    svStyle.fontSize = '14px';    // 还原，不影响后续用例
  });

  it('度量维度失效：fontSize 变化（同文本）→ key 不匹配 miss', () => {
    const api = window.editorApi;
    api._srcWrapCacheMap = null;   // 用例间隔离
    svStyle.fontSize = '14px';     // 度量基线显式归位（不依赖前序用例还原）
    api.setContent('<p>font size drift</p>');
    api.enterSourceMode('font size drift\n');
    const sv = document.getElementById('source-view');
    api._srcWrapCacheSet(sv.value, [0, sv.value.length]);
    expect(window.__wrapCacheState().hit).toBe(true);
    svStyle.fontSize = '16px';
    expect(window.__wrapCacheState().hit).toBe(false);
    svStyle.fontSize = '14px';
    expect(window.__wrapCacheState().hit).toBe(true);
  });
});
