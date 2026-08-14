# 搜索增强实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: 用 superpowers:subagent-driven-development（推荐）或 superpowers:executing-plans 逐任务实现。步骤用 `- [ ]` 复选框跟踪。

**Goal:** 用自写 TipTap 扩展 + ProseMirror plugin（`Decoration.inline` 高亮）替换 Chromium `findText`，实现正则 / 全词（Unicode）/ 大小写 toggle、匹配计数 `n/m`、上下导航（回绕）、替换 / 全部替换（正则反向引用 `$1/$2/$&`，修复现有全部替换位置漂移 bug）。

**Architecture:** 纯函数 `searchMatcher`（字符串级匹配）→ `searchPlugin`（ProseMirror `Plugin` + `StateField` + decorations + 位置映射）→ TipTap Extension 包装进 `_buildExtensions` → `editorApi` 方法（C++ 经 `runJavaScript` 调用）→ `EditorBridge.searchMatchChanged` slot 回传统计 → `SearchBar` 显示 `n/m`。所有新 JS 走现有 esbuild 管线（`tiptap-entry.js` 打包进 `tiptap-all.js`），不改 `editor.html`/qrc。

**Tech Stack:** TipTap 3.29.2 / @tiptap/pm（prosemirror-state 1.4.4 / view 1.42.2）/ vitest + jsdom（Node 下测纯逻辑与 plugin 状态）/ Qt6 C++（SearchBar、EditorBridge、MainWindow）/ GitHub Actions（ubuntu-24.04 / Qt 6.4.2 兼容基准）。

## Global Constraints

- **文档/注释中文，标识符英文**；提交前缀 `feat:`/`fix:`/`chore:` + 中文描述。
- **版本**：建议 `0.5.0 → 0.6.0`（`+0.1.0`，大增量），最终以用户确认为准；推进时同步 `CMakeLists.txt` 的 `project(md-editor VERSION x.y.z)`、`CHANGELOG.md`、打 tag（见 Task 7）。
- **Qt 6.4.2 兼容基准**：C++ 改动须在 6.4.2 下 `ctest` 通过（本功能 C++ 仅信号转发，风险低；改动 `EditorBridge`/`SearchBar` 后本地 `ctest` 必须绿）。
- **往返保真**：替换一律走 ProseMirror `transaction`（`tr.insertText`），不碰 `MarkdownHandler`，不破坏文档语义。
- **全词 = Unicode 词边界后置过滤**：`isWordChar = /[\p{L}\p{N}_]/u`，匹配 `[s,e)` 两侧均须非词字符；字面/正则模式统一此后置过滤（不包裹正则）。
- **非法正则** → `searchMatcher` 返回 `[]`（计数 `0/0`，按钮禁用）。
- **源码模式（`Ctrl+\`）本阶段不纳入**：`editorApi` 各搜索方法在 `isSourceMode===true` 时直接 `return`（由 C++ 在源码模式打开搜索栏时给状态栏提示）。
- **去抖 150ms**：C++ 侧 `SearchBar` 用 `QTimer`，JS plugin 保持同步。
- **重建 bundle**：改 `tiptap-entry.js` 或新 JS 模块后必须 `bash src/resources/web/build-tiptap.sh` 重新生成 `tiptap-all.js` 并提交。
- **位置映射**：扁平化 doc 文本时块边界插 `\u0000`（非词字符、不可匹配），记录「字符索引 → doc pos」；matcher 在字符串上跑，结果映射回 doc 区间。
- **新 JS 模块为 ESM**（`export`），由 `tiptap-entry.js` `import` 后挂 `window` 全局供 `editor.js` 使用；vitest 直接 `import` 同一 ESM 源测试。

## File Structure

| 文件 | 动作 | 职责 |
|------|------|------|
| `src/resources/web/searchMatcher.js` | 新建（ESM） | 纯函数：`escapeRegExp` / `isWordChar` / `searchMatcher` |
| `src/resources/web/searchMatcher.test.js` | 新建 | vitest，`environment: node` |
| `src/resources/web/searchPlugin.js` | 新建（ESM） | `@tiptap/pm` plugin：`flattenDoc`/`recompute`/`createSearchPlugin`/命令 + `searchKey` |
| `src/resources/web/searchPlugin.test.js` | 新建 | vitest，`// @vitest-environment jsdom`（EditorView 需 DOM） |
| `src/resources/web/tiptap-entry.js` | 修改 | `import` 两个新模块并挂 `window.searchMatcher`/`window.searchPlugin` |
| `src/resources/web/tiptap-all.js` | 重新生成 | `build-tiptap.sh` 产物 |
| `src/resources/web/editor.js` | 修改 | `buildSearchExtension()` 入 `_buildExtensions`；`editorApi` 加 6 个搜索方法 + `_notifySearchCount`；`onUpdate` 调 `_notifySearchCount` |
| `src/resources/web/editor.css` | 修改 | `.search-match` / `.search-match-current`（`body.dark` 变体） |
| `src/resources/web/package.json` | 修改 | devDependencies 加 `vitest`、`jsdom`；`scripts.test = "vitest run"`；加 `vitest.config.js` |
| `src/core/EditorBridge.h/.cpp` | 修改 | slot `searchMatchChanged(int,int)` + signal `searchMatchUpdated(int,int,int)` |
| `src/ui/SearchBar.h/.cpp` | 修改 | 两行折叠布局 / 三 toggle / 计数 / 导航 / 新信号 / `setCount` / `keyPressEvent` / 去抖 / objectName |
| `src/ui/MainWindow.cpp` | 修改 | SearchBar 新信号→editorApi；bridge `searchMatchUpdated`→`setCount`；`Ctrl+H`/`F3`/`Shift+F3` 快捷键；源码模式提示 |
| `tests/test_editorbridge.cpp` | 修改 | `searchMatchChanged` slot→signal 转发用例（`QSignalSpy`） |
| `.github/workflows/build-deb.yml` | 修改 | Qt 构建前 `cd src/resources/web && npm ci && npm test` |
| `CMakeLists.txt` / `CHANGELOG.md` | 修改 | 版本推进（Task 7） |

---

## Task 0：vitest 接入

**Files:**
- Modify: `src/resources/web/package.json`
- Create: `src/resources/web/vitest.config.js`, `src/resources/web/sanity.test.js`

**Interfaces:** 产出一个可跑的 `npm test`，供后续 Task 1/2 写测试；CI 在 Task 7 接入。

- [ ] **Step 1：写 package.json devDeps 与脚本**

把 `src/resources/web/package.json` 的 `scripts.test` 从占位改为 `vitest run`，并新增 `devDependencies`：

```json
  "scripts": {
    "test": "vitest run",
    "build:tiptap": "bash build-tiptap.sh"
  },
  "devDependencies": {
    "vitest": "^3.2.4",
    "jsdom": "^25.0.1"
  }
```

> `jsdom` 当前已在 node_modules（传递依赖），显式列入 devDeps 以保证 CI `npm ci` 可用。

- [ ] **Step 2：写 vitest 配置**

`src/resources/web/vitest.config.js`：

```js
import { defineConfig } from 'vitest/config';

export default defineConfig({
  test: {
    environment: 'node',            // 默认 node（纯逻辑）
    include: ['**/*.test.js']
  }
});
```

> 需要 DOM 的测试（Task 2 的 `searchPlugin.test.js`）用文件顶部 docblock `// @vitest-environment jsdom` 指定环境。
>
> **设计注记（Task 0 评审修订）：** 初稿曾用 `environmentMatchGlobs` 路由 `**/*.dom.test.js` 作「冗余保险」，但 vitest 3.2.7 已将其标记 `DEPRECATED`（每次 `npm test` 输出弃用警告，违反 pristine），且该 glob 根本匹配不到计划内的 `searchPlugin.test.js`（死配置）。用户裁定移除——docblock 是唯一选定机制，见 [[Task 0 修复轮次 1]]。

- [ ] **Step 3：写冒烟测试**

`src/resources/web/sanity.test.js`：

```js
import { describe, it, expect } from 'vitest';

describe('vitest 接入冒烟', () => {
  it('能跑断言', () => {
    expect(1 + 1).toBe(2);
  });
});
```

- [ ] **Step 4：安装依赖并跑测试（更新 lockfile）**

```bash
cd src/resources/web
npm install                 # 把 vitest/jsdom 写入 package-lock.json
npm test
```
Expected: 1 个测试通过；`package-lock.json` 更新。

- [ ] **Step 5：提交**

```bash
git add src/resources/web/package.json src/resources/web/package-lock.json \
        src/resources/web/vitest.config.js src/resources/web/sanity.test.js
git commit -m "chore: 接入 vitest（JS 测试基建）"
```

---

## Task 1：searchMatcher 纯函数（TDD）

**Files:**
- Create: `src/resources/web/searchMatcher.js`, `src/resources/web/searchMatcher.test.js`

**Interfaces:**
- Produces: `searchMatcher(text:String, query:String, {regex,wholeWord,caseSensitive}):Array<{start,end}>`（区间为扁平文本索引，左闭右开）；`escapeRegExp(s)`；`isWordChar(ch)`。非法正则/空 query → `[]`。

- [ ] **Step 1：先写失败测试**

`src/resources/web/searchMatcher.test.js`：

```js
import { describe, it, expect } from 'vitest';
import { searchMatcher, escapeRegExp, isWordChar } from './searchMatcher.js';

describe('searchMatcher', () => {
  it('字面匹配', () => {
    expect(searchMatcher('hello world', 'world', {})).toEqual([{ start: 6, end: 11 }]);
  });
  it('默认大小写不敏感', () => {
    expect(searchMatcher('Hello', 'hello', {})).toEqual([{ start: 0, end: 5 }]);
  });
  it('区分大小写开启时不匹配', () => {
    expect(searchMatcher('Hello', 'hello', { caseSensitive: true })).toEqual([]);
  });
  it('正则匹配多个', () => {
    expect(searchMatcher('a1b2c3', '\\d', { regex: true })).toEqual([
      { start: 1, end: 2 }, { start: 3, end: 4 }, { start: 5, end: 6 }
    ]);
  });
  it('非法正则返回空数组', () => {
    expect(searchMatcher('abc', '(', { regex: true })).toEqual([]);
  });
  it('全词 Unicode：中文逗号边界算全词', () => {
    expect(searchMatcher('你好，世界', '你好', { wholeWord: true })).toEqual([{ start: 0, end: 2 }]);
  });
  it('全词 Unicode：紧邻汉字不算全词', () => {
    expect(searchMatcher('你好世界', '你好', { wholeWord: true })).toEqual([]);
  });
  it('全词 ASCII：嵌入更长单词不算', () => {
    expect(searchMatcher('foo bar foobar', 'foo', { wholeWord: true })).toEqual([{ start: 0, end: 3 }]);
  });
  it('正则 + 全词组合', () => {
    expect(searchMatcher('cat dog', 'cat|dog', { regex: true, wholeWord: true })).toEqual([
      { start: 0, end: 3 }, { start: 4, end: 7 }
    ]);
  });
  it('跨块分隔符 \\u0000 不匹配（字面）', () => {
    expect(searchMatcher('ab\u0000cd', 'abc', {})).toEqual([]);
  });
  it('特殊字符按字面（转义）', () => {
    expect(searchMatcher('a.b.c', '.b', {})).toEqual([{ start: 1, end: 3 }]);
  });
  it('空 query 返回空', () => {
    expect(searchMatcher('abc', '', {})).toEqual([]);
  });
});

describe('escapeRegExp / isWordChar', () => {
  it('转义元字符', () => { expect(escapeRegExp('a.b*')).toBe('a\\.b\\*'); });
  it('汉字是词字符', () => { expect(isWordChar('你')).toBe(true); });
  it('标点不是词字符', () => { expect(isWordChar('，')).toBe(false); });
  it('\\u0000 不是词字符', () => { expect(isWordChar('\u0000')).toBe(false); });
});
```

- [ ] **Step 2：跑测试确认失败**

```bash
cd src/resources/web && npm test -- searchMatcher
```
Expected: FAIL（模块不存在 / 导出未定义）。

- [ ] **Step 3：实现 searchMatcher.js**

`src/resources/web/searchMatcher.js`：

```js
// 搜索匹配纯函数：在扁平化文本（块边界为 \u0000）上执行匹配。
// 无 ProseMirror 依赖，vitest 重点覆盖。

export function escapeRegExp(s) {
  return s.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
}

// Unicode 词字符判定：字母 / 数字 / 下划线。
export function isWordChar(ch) {
  return !!ch && /[\p{L}\p{N}_]/u.test(ch);
}

// 返回 [{ start, end }]（左闭右开，扁平文本索引）。
// options: { regex, wholeWord, caseSensitive }。非法正则 / 空 query → []。
export function searchMatcher(text, query, options) {
  var opts = options || {};
  var regex = !!opts.regex, wholeWord = !!opts.wholeWord, caseSensitive = !!opts.caseSensitive;
  if (!query) return [];

  var pattern;
  try {
    var src = regex ? query : escapeRegExp(query);
    pattern = new RegExp(src, 'g' + (caseSensitive ? '' : 'i'));
  } catch (e) {
    return []; // 非法正则
  }

  var result = [], m;
  while ((m = pattern.exec(text)) !== null) {
    if (m[0] === '') { pattern.lastIndex++; continue; } // 跳过零宽匹配，避免死循环
    var start = m.index, end = start + m[0].length;
    if (wholeWord) {
      var before = start > 0 ? text[start - 1] : '';
      var after = end < text.length ? text[end] : '';
      if (isWordChar(before) || isWordChar(after)) continue; // 非全词
    }
    result.push({ start: start, end: end });
  }
  return result;
}
```

- [ ] **Step 4：跑测试确认通过**

```bash
cd src/resources/web && npm test -- searchMatcher
```
Expected: PASS（全部用例）。

- [ ] **Step 5：提交**

```bash
git add src/resources/web/searchMatcher.js src/resources/web/searchMatcher.test.js
git commit -m "feat: searchMatcher 纯函数（正则/Unicode 全词/大小写）"
```

---

## Task 2：searchPlugin（位置映射 + StateField + decorations + 命令，TDD）

**Files:**
- Create: `src/resources/web/searchPlugin.js`, `src/resources/web/searchPlugin.test.js`

**Interfaces:**
- Consumes: `searchMatcher`（Task 1）；`@tiptap/pm/state` 的 `Plugin/PluginKey/StateField/TextSelection`；`@tiptap/pm/view` 的 `Decoration/DecorationSet`。
- Produces:
  - `searchKey`（PluginKey）
  - `createSearchPlugin(): Plugin`
  - `requestSearch(view, query, flags)`、`searchNext(view)`、`searchPrev(view)`
  - `replaceOne(view, rep)`、`replaceAll(view, rep)`、`clearSearch(view)`
  - `searchCurrent(state): { total, current }`（current 为 1 基，无匹配为 0）
  - StateField 值：`{ query, flags:{regex,wholeWord,caseSensitive}, matches:[{from,to}], current, text, mapping }`

- [ ] **Step 1：先写失败测试（jsdom 环境）**

`src/resources/web/searchPlugin.test.js`（首行 docblock 指定 jsdom；EditorView 需 DOM）：

```js
// @vitest-environment jsdom
import { describe, it, expect, beforeEach } from 'vitest';
import { Schema } from '@tiptap/pm/model';
import { EditorState, TextSelection } from '@tiptap/pm/state';
import { EditorView } from '@tiptap/pm/view';
import {
  createSearchPlugin, searchKey, requestSearch,
  searchNext, searchPrev, replaceOne, replaceAll, clearSearch, searchCurrent
} from './searchPlugin.js';

// 最小 schema：doc / paragraph / text
const schema = new Schema({
  nodes: {
    doc: { content: 'block+' },
    paragraph: { group: 'block', content: 'text*', toDOM: () => ['p', 0] },
    text: { group: 'inline' }
  }
});

function makeDoc(lines) {
  const paras = lines.map(l => schema.node('paragraph', null, l ? schema.text(l) : []));
  return schema.node('doc', null, paras);
}

let view, plugin;
beforeEach(() => {
  plugin = createSearchPlugin();
  view = new EditorView(document.createElement('div'), {
    state: EditorState.create({ doc: makeDoc(['hello hello']), plugins: [plugin] })
  });
});

describe('searchPlugin', () => {
  it('requestSearch 计算匹配区间', () => {
    requestSearch(view, 'hello', {});
    const st = searchKey.getState(view.state);
    expect(st.matches.length).toBe(2);
    expect(st.current).toBe(0);
  });

  it('decorations 数量 == 匹配数，当前匹配高亮', () => {
    requestSearch(view, 'hello', {});
    const decos = plugin.props.decorations(view.state).find();
    expect(decos.length).toBe(2);
    const classes = decos.map(d => d.type.attrs.class).sort();
    expect(classes).toEqual(['search-match', 'search-match-current']);
  });

  it('searchNext/searchPrev 回绕', () => {
    requestSearch(view, 'hello', {});
    searchNext(view); expect(searchKey.getState(view.state).current).toBe(1);
    searchNext(view); expect(searchKey.getState(view.state).current).toBe(0); // 回绕
    searchPrev(view); expect(searchKey.getState(view.state).current).toBe(1); // 回绕
  });

  it('导航后选中匹配区间', () => {
    requestSearch(view, 'hello', {});
    searchNext(view);
    const sel = view.state.selection;
    expect(sel.from).toBeLessThan(sel.to);
  });

  it('replaceOne 字面替换并前进', () => {
    requestSearch(view, 'hello', {});
    replaceOne(view, 'HI');
    expect(view.state.doc.textContent).toBe('HI hello');
    expect(searchKey.getState(view.state).matches.length).toBe(1);
  });

  it('replaceAll 不同长度替换正确（无位置漂移）', () => {
    view.destroy();
    view = new EditorView(document.createElement('div'), {
      state: EditorState.create({ doc: makeDoc(['foo foo foo']), plugins: [plugin = createSearchPlugin()] })
    });
    requestSearch(view, 'foo', {});
    replaceAll(view, 'barbar');
    expect(view.state.doc.textContent).toBe('barbar barbar barbar');
  });

  it('replaceAll 正则反向引用 $1', () => {
    view.destroy();
    view = new EditorView(document.createElement('div'), {
      state: EditorState.create({ doc: makeDoc(['a1b']), plugins: [plugin = createSearchPlugin()] })
    });
    requestSearch(view, '(\\d)', { regex: true });
    replaceAll(view, '[$1]');
    expect(view.state.doc.textContent).toBe('a[1]b');
  });

  it('doc 编辑后自动重算计数', () => {
    requestSearch(view, 'hello', {});
    view.dispatch(view.state.tr.insertText(' hello', view.state.doc.content.size - 1));
    const st = searchKey.getState(view.state);
    expect(st.matches.length).toBe(3);
  });

  it('clearSearch 清空', () => {
    requestSearch(view, 'hello', {});
    clearSearch(view);
    expect(searchKey.getState(view.state).matches.length).toBe(0);
    expect(searchCurrent(view.state)).toEqual({ total: 0, current: 0 });
  });

  it('searchCurrent 1 基计数', () => {
    requestSearch(view, 'hello', {});
    expect(searchCurrent(view.state)).toEqual({ total: 2, current: 1 });
    searchNext(view);
    expect(searchCurrent(view.state)).toEqual({ total: 2, current: 2 });
  });

  it('跨块不匹配', () => {
    view.destroy();
    view = new EditorView(document.createElement('div'), {
      state: EditorState.create({ doc: makeDoc(['hel', 'lo']), plugins: [plugin = createSearchPlugin()] })
    });
    requestSearch(view, 'hello', {});
    expect(searchKey.getState(view.state).matches.length).toBe(0);
  });
});
```

- [ ] **Step 2：跑测试确认失败**

```bash
cd src/resources/web && npm test -- searchPlugin
```
Expected: FAIL（模块不存在）。

- [ ] **Step 3：实现 searchPlugin.js**

`src/resources/web/searchPlugin.js`：

```js
// ProseMirror 搜索插件：位置映射 + StateField + Decoration.inline 高亮 + 命令。
import { Plugin, PluginKey, TextSelection } from '@tiptap/pm/state';
import { Decoration, DecorationSet } from '@tiptap/pm/view';
import { searchMatcher } from './searchMatcher.js';

export const searchKey = new PluginKey('search');

// 扁平化 doc：文本节点逐字记录 doc pos，块边界插 \u0000（非词、不可匹配）。
function flattenDoc(doc) {
  var text = '', mapping = [];
  doc.descendants(function (node, pos) {
    if (node.isText) {
      var s = node.text;
      for (var i = 0; i < s.length; i++) { mapping.push(pos + i); text += s[i]; }
    } else if (node.isBlock) {
      if (text.length && text[text.length - 1] !== '\u0000') { mapping.push(-1); text += '\u0000'; }
    }
    return true;
  });
  return { text: text, mapping: mapping };
}

// 扁平区间 [{start,end}] → doc 区间 [{from,to}]，跳过跨分隔符的（理论上不会）。
function toDocRanges(matches, mapping) {
  var out = [];
  for (var i = 0; i < matches.length; i++) {
    var s = matches[i].start, e = matches[i].end;
    if (mapping[s] === -1 || mapping[e - 1] === -1) continue;
    out.push({ from: mapping[s], to: mapping[e - 1] + 1 });
  }
  return out;
}

function recompute(doc, query, flags) {
  if (!query) return { query: '', flags: flags, matches: [], current: -1, text: '', mapping: [] };
  var flat = flattenDoc(doc);
  var matches = toDocRanges(searchMatcher(flat.text, query, flags), flat.mapping);
  return { query: query, flags: flags, matches: matches, current: matches.length ? 0 : -1, text: flat.text, mapping: flat.mapping };
}

// 取 doc 区间内文本（匹配不跨块，故为同段连续文本）。
function rangeText(doc, from, to) { return doc.textBetween(from, to, ''); }

// 解析替换串：正则模式解析 $1/$2/$&，字面模式原样。
function resolveReplacement(matchedText, rep, flags, query) {
  if (flags.regex) {
    try {
      var single = new RegExp(query, flags.caseSensitive ? '' : 'i'); // 非 g，单次
      return matchedText.replace(single, rep);
    } catch (e) { return rep; }
  }
  return rep;
}

// 在 apply 之后把 current 设为 idx，并选中 + 滚动到该匹配。
function setCurrentAndSelect(view, idx) {
  var st = searchKey.getState(view.state);
  if (idx < 0 || idx >= st.matches.length) return;
  var m = st.matches[idx];
  var tr = view.state.tr.setMeta(searchKey, { type: 'setCurrent', idx: idx });
  tr.setSelection(TextSelection.create(view.state.doc, m.from, m.to));
  tr.scrollIntoView();
  view.dispatch(tr);
}

export function createSearchPlugin() {
  return new Plugin({
    key: searchKey,
    state: {
      init: function () {
        return { query: '', flags: { regex: false, wholeWord: false, caseSensitive: false }, matches: [], current: -1, text: '', mapping: [] };
      },
      apply: function (tr, value, oldState, newState) {
        var meta = tr.getMeta(searchKey);
        if (meta && meta.type === 'search') {
          return recompute(newState.doc, meta.query, meta.flags);
        }
        if (meta && meta.type === 'setCurrent') {
          return Object.assign({}, value, { current: meta.idx });
        }
        if (tr.docChanged && value.query) {
          var oldFrom = (value.matches[value.current] || { from: 0 }).from;
          var next = recompute(newState.doc, value.query, value.flags);
          var idx = 0;
          for (var i = 0; i < next.matches.length; i++) { if (next.matches[i].from >= oldFrom) { idx = i; break; } }
          next.current = next.matches.length ? Math.min(idx, next.matches.length - 1) : -1;
          return next;
        }
        return value;
      }
    },
    props: {
      decorations: function (state) {
        var v = searchKey.getState(state);
        if (!v || !v.matches.length) return Decoration.empty;
        var decos = v.matches.map(function (m, i) {
          return Decoration.inline(m.from, m.to, { class: i === v.current ? 'search-match-current' : 'search-match' });
        });
        return DecorationSet.create(state.doc, decos);
      }
    }
  });
}

export function requestSearch(view, query, flags) {
  view.dispatch(view.state.tr.setMeta(searchKey, { type: 'search', query: query || '', flags: flags }));
}

export function searchNext(view) {
  var st = searchKey.getState(view.state);
  if (!st || !st.matches.length) return;
  setCurrentAndSelect(view, (st.current + 1) % st.matches.length);
}

export function searchPrev(view) {
  var st = searchKey.getState(view.state);
  if (!st || !st.matches.length) return;
  setCurrentAndSelect(view, (st.current - 1 + st.matches.length) % st.matches.length);
}

export function replaceOne(view, rep) {
  var st = searchKey.getState(view.state);
  if (!st || !st.query || !st.matches.length) return;
  var m = st.matches[st.current];
  var resolved = resolveReplacement(rangeText(view.state.doc, m.from, m.to), rep, st.flags, st.query);
  view.dispatch(view.state.tr.insertText(resolved, m.from, m.to)); // docChanged → 重算
  var st2 = searchKey.getState(view.state);
  if (st2.matches.length) {
    var idx = 0;
    for (var i = 0; i < st2.matches.length; i++) { if (st2.matches[i].from >= m.from) { idx = i; break; } }
    setCurrentAndSelect(view, Math.min(idx, st2.matches.length - 1));
  }
}

export function replaceAll(view, rep) {
  var st = searchKey.getState(view.state);
  if (!st || !st.query || !st.matches.length) return 0;
  // 从尾向前替换，避免位置漂移（高位置先换，低位置不变）。
  var sorted = st.matches.slice().sort(function (a, b) { return b.from - a.from; });
  var tr = view.state.tr, count = 0;
  for (var i = 0; i < sorted.length; i++) {
    var m = sorted[i];
    var orig = rangeText(tr.doc, m.from, m.to); // 该区域尚未被（更高的）替换触及
    tr.insertText(resolveReplacement(orig, rep, st.flags, st.query), m.from, m.to);
    count++;
  }
  view.dispatch(tr); // docChanged → 重算（通常为 0）
  return count;
}

export function clearSearch(view) {
  view.dispatch(view.state.tr.setMeta(searchKey, { type: 'search', query: '', flags: {} }));
}

export function searchCurrent(state) {
  var v = searchKey.getState(state);
  if (!v) return { total: 0, current: 0 };
  return { total: v.matches.length, current: v.matches.length ? (v.current < 0 ? 0 : v.current) + 1 : 0 };
}
```

- [ ] **Step 4：跑测试确认通过**

```bash
cd src/resources/web && npm test -- searchPlugin
```
Expected: PASS（全部用例）。若有用例对 `deco.type.attrs.class` 断言失败，按 prosemirror-view 实际属性名微调断言（不变实现逻辑）。

- [ ] **Step 5：提交**

```bash
git add src/resources/web/searchPlugin.js src/resources/web/searchPlugin.test.js
git commit -m "feat: searchPlugin（位置映射/StateField/decorations/替换）"
```

---

## Task 3：接入 bundle + editor.js

**Files:**
- Modify: `src/resources/web/tiptap-entry.js`, `src/resources/web/editor.js`
- Regenerate: `src/resources/web/tiptap-all.js`

**Interfaces:**
- Consumes: `searchMatcher`/`searchPlugin`（Task 1/2）；`TipTapExtension` 全局。
- Produces（`window.editorApi`）：`startSearch(q,case,regex,ww)`、`searchNext()`、`searchPrev()`、`replaceOne(rep)`、`replaceAll(rep)`、`stopSearch()`，供 C++ `runJavaScript` 调用。

- [ ] **Step 1：tiptap-entry.js 暴露全局**

在 `src/resources/web/tiptap-entry.js` 既有 import 块（line 22 后）追加，并在全局赋值区（line 48 前）追加暴露：

```js
// 搜索（纯函数 matcher + ProseMirror plugin）
import { searchMatcher, isWordChar, escapeRegExp } from './searchMatcher.js';
import {
  createSearchPlugin, searchKey, requestSearch,
  searchNext, searchPrev, replaceOne, replaceAll, clearSearch, searchCurrent
} from './searchPlugin.js';
```

全局赋值区追加：

```js
window.searchMatcher = searchMatcher;
window.searchPlugin = {
  createSearchPlugin: createSearchPlugin,
  searchKey: searchKey,
  requestSearch: requestSearch,
  searchNext: searchNext,
  searchPrev: searchPrev,
  replaceOne: replaceOne,
  replaceAll: replaceAll,
  clearSearch: clearSearch,
  searchCurrent: searchCurrent
};
```

- [ ] **Step 2：重建 bundle**

```bash
bash src/resources/web/build-tiptap.sh
```
Expected: 生成 `src/resources/web/tiptap-all.js`，体积增大；终端打印字节数。

- [ ] **Step 3：editor.js 加 buildSearchExtension 并注册**

在 `editor.js` `_buildExtensions` 的 `return exts;`（约 line 1011）前追加：

```js
        // 搜索高亮（自写 ProseMirror plugin，TipTap Extension 包装）
        if (TipTapExtension && window.searchPlugin)
            exts.push(buildSearchExtension());
```

在 `editor.js` 顶层函数区（与 `buildMermaidNode` 等同级，约 line 500 附近）加：

```js
function buildSearchExtension() {
    return TipTapExtension.create({
        name: 'search',
        addProseMirrorPlugins: function () {
            return [window.searchPlugin.createSearchPlugin()];
        }
    });
}
```

- [ ] **Step 4：editor.js 加 editorApi 搜索方法**

在 `window.editorApi = { ... }` 内（与 `scrollToPos` 等同级）追加：

```js
    // === 搜索 ===
    startSearch: function (query, caseSensitive, regex, wholeWord) {
        if (!this.editor || this.isSourceMode) return;
        var sp = window.searchPlugin; if (!sp) return;
        sp.requestSearch(this.editor.view, query || '', {
            regex: !!regex, wholeWord: !!wholeWord, caseSensitive: !!caseSensitive
        });
        this._notifySearchCount();
    },
    searchNext: function () {
        if (!this.editor || this.isSourceMode) return;
        window.searchPlugin.searchNext(this.editor.view);
        this._notifySearchCount();
    },
    searchPrev: function () {
        if (!this.editor || this.isSourceMode) return;
        window.searchPlugin.searchPrev(this.editor.view);
        this._notifySearchCount();
    },
    replaceOne: function (rep) {
        if (!this.editor || this.isSourceMode) return;
        window.searchPlugin.replaceOne(this.editor.view, rep || '');
        this._notifySearchCount();
    },
    replaceAll: function (rep) {
        if (!this.editor || this.isSourceMode) return;
        window.searchPlugin.replaceAll(this.editor.view, rep || '');
        this._notifySearchCount();
    },
    stopSearch: function () {
        if (!this.editor) return;
        if (window.searchPlugin) window.searchPlugin.clearSearch(this.editor.view);
        this._notifySearchCount();
    },
    _notifySearchCount: function () {
        if (!this.editor || !this.bridge) return;
        var sp = window.searchPlugin;
        var st = sp ? sp.searchCurrent(this.editor.state) : { total: 0, current: 0 };
        this.bridge.searchMatchChanged(st.total, st.current);
    },
```

- [ ] **Step 5：onUpdate 触发计数刷新（边输入边更新计数）**

在 `_createEditor` 的 `onUpdate`（约 line 1032-1041）里，`self.notifyHistory();` 之后追加一行：

```js
                self._notifySearchCount();
```

- [ ] **Step 6：手动冒烟（无浏览器自动化，记录结果）**

```bash
bash scripts/run-editor.sh
```
手动验证：打开含多次重复词的 .md → Ctrl+F → 输入词 → 观察是否高亮（`editor.css` 样式 Task 6 才加，此步先确认控制台无报错、`window.searchPlugin` 存在；高亮可见性 Task 6 验证）。退出。

- [ ] **Step 7：提交**

```bash
git add src/resources/web/tiptap-entry.js src/resources/web/tiptap-all.js src/resources/web/editor.js
git commit -m "feat: editor.js 接入搜索扩展与 editorApi"
```

---

## Task 4：EditorBridge slot/signal

**Files:**
- Modify: `src/core/EditorBridge.h`, `src/core/EditorBridge.cpp`, `tests/test_editorbridge.cpp`

**Interfaces:**
- Produces: slot `searchMatchChanged(int total, int current)` → signal `searchMatchUpdated(int total, int current, int tabId)`。

- [ ] **Step 1：EditorBridge.h 增 slot 与 signal**

`public slots:` 末尾（`requestImageSave` 后）加：

```cpp
    // JS → C++：搜索匹配计数（total 总数，current 当前第几个，1 基；0 表示无匹配）
    void searchMatchChanged(int total, int current);
```

`signals:` 末尾（`imageSaveRequested` 后）加：

```cpp
    void searchMatchUpdated(int total, int current, int tabId);
```

- [ ] **Step 2：EditorBridge.cpp 实现 slot**

在文件末尾追加：

```cpp
void EditorBridge::searchMatchChanged(int total, int current)
{
    emit searchMatchUpdated(total, current, m_tabId);
}
```

- [ ] **Step 3：先写失败测试**

`tests/test_editorbridge.cpp` 加用例（仿现有 `QSignalSpy` 用法）：

```cpp
void testSearchMatchForwarding()
{
    EditorBridge bridge(7);
    QSignalSpy spy(&bridge, &EditorBridge::searchMatchUpdated);
    bridge.searchMatchChanged(12, 3);
    QCOMPARE(spy.count(), 1);
    QList<QVariant> args = spy.takeFirst();
    QCOMPARE(args.at(0).toInt(), 12);   // total
    QCOMPARE(args.at(1).toInt(), 3);    // current
    QCOMPARE(args.at(2).toInt(), 7);    // tabId
}
```
并在该文件的 `private slots:` 测试列表与 `main()` 的注册处登记 `testSearchMatchForwarding`（按文件现有模式）。

- [ ] **Step 4：构建并跑测试**

```bash
cd build && cmake --build . --target test_editorbridge -j$(nproc)
QT_QPA_PLATFORM=offscreen ./tests/test_editorbridge
```
Expected: PASS（含新用例）。

- [ ] **Step 5：提交**

```bash
git add src/core/EditorBridge.h src/core/EditorBridge.cpp tests/test_editorbridge.cpp
git commit -m "feat: EditorBridge 增 searchMatchChanged 信号转发"
```

---

## Task 5：SearchBar UI 重构

**Files:**
- Rewrite: `src/ui/SearchBar.h`, `src/ui/SearchBar.cpp`

**Interfaces:**
- Produces signals（替换旧 4 个）：`searchParamsChanged(QString text, bool caseSensitive, bool regex, bool wholeWord)`、`findNextRequested()`、`findPrevRequested()`、`replaceRequested(QString rep)`、`replaceAllRequested(QString rep)`、`replaceModeToggleRequested(bool on)`、`closeRequested()`。
- Produces 槽：`setCount(int total, int current)`、`setReplaceMode(bool on)`。
- 约束：去抖 150ms（`QTimer`）合并 text+三 toggle 变化 → 单次 `searchParamsChanged`；`objectName` 设为 `"searchBar"`。

- [ ] **Step 1：重写 SearchBar.h**

```cpp
#ifndef MD_EDITOR_SEARCHBAR_H
#define MD_EDITOR_SEARCHBAR_H

#include <QWidget>

class QLineEdit;
class QPushButton;
class QLabel;
class QTimer;

class SearchBar : public QWidget {
    Q_OBJECT
public:
    explicit SearchBar(QWidget *parent = nullptr);

    void focusFindField();
    void setCount(int total, int current);      // bridge 回传：显示 n/m、禁用态
    void setReplaceMode(bool on);                // Ctrl+H 展开/收起替换行
    void emitParamsNow();                        // 立即触发一次（去抖 flush）

signals:
    void searchParamsChanged(const QString &text, bool caseSensitive, bool regex, bool wholeWord);
    void findNextRequested();
    void findPrevRequested();
    void replaceRequested(const QString &rep);
    void replaceAllRequested(const QString &rep);
    void replaceModeToggleRequested(bool on);
    void closeRequested();

protected:
    void keyPressEvent(QKeyEvent *event) override;   // Enter→next / Shift+Enter→prev

private:
    void scheduleEmitParams();   // 启动 150ms 去抖

    QLineEdit *m_findEdit;
    QLineEdit *m_replaceEdit;
    QPushButton *m_caseBtn;      // Aa 大小写
    QPushButton *m_regexBtn;     // .* 正则
    QPushButton *m_wordBtn;      // W| 全词
    QPushButton *m_nextBtn;
    QPushButton *m_prevBtn;
    QPushButton *m_replaceBtn;
    QPushButton *m_replaceAllBtn;
    QPushButton *m_closeBtn;
    QLabel *m_countLabel;
    QWidget *m_replaceRow;       // 第二行容器（可显隐）
    QTimer *m_debounce;
};

#endif // MD_EDITOR_SEARCHBAR_H
```

- [ ] **Step 2：重写 SearchBar.cpp**

```cpp
#include "SearchBar.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QTimer>
#include <QKeyEvent>

SearchBar::SearchBar(QWidget *parent)
    : QWidget(parent)
{
    setObjectName("searchBar");

    m_debounce = new QTimer(this);
    m_debounce->setSingleShot(true);
    m_debounce->setInterval(150);
    connect(m_debounce, &QTimer::timeout, this, &SearchBar::emitParamsNow);

    // 第一行：查找 + toggles + 导航 + 计数 + 关闭
    auto *row1 = new QHBoxLayout;
    row1->setContentsMargins(4, 2, 4, 2);

    m_findEdit = new QLineEdit(this);
    m_findEdit->setPlaceholderText("查找...");
    m_findEdit->setMaximumWidth(280);
    row1->addWidget(m_findEdit);

    auto mkToggle = [this](const QString &text, const QString &tip) {
        auto *b = new QPushButton(text, this);
        b->setCheckable(true);
        b->setFixedWidth(40);
        b->setToolTip(tip);
        connect(b, &QPushButton::toggled, this, &SearchBar::scheduleEmitParams);
        row1->addWidget(b);
        return b;
    };
    m_caseBtn = mkToggle("Aa", "区分大小写");
    m_regexBtn = mkToggle(".*", "正则表达式");
    m_wordBtn = mkToggle("W|", "全词匹配");

    m_prevBtn = new QPushButton("↑", this);
    m_nextBtn = new QPushButton("↓", this);
    m_prevBtn->setToolTip("上一个 (Shift+F3)");
    m_nextBtn->setToolTip("下一个 (F3)");
    connect(m_nextBtn, &QPushButton::clicked, this, &SearchBar::findNextRequested);
    connect(m_prevBtn, &QPushButton::clicked, this, &SearchBar::findPrevRequested);
    row1->addWidget(m_prevBtn);
    row1->addWidget(m_nextBtn);

    m_countLabel = new QLabel("0/0", this);
    m_countLabel->setFixedWidth(60);
    m_countLabel->setAlignment(Qt::AlignCenter);
    row1->addWidget(m_countLabel);

    m_closeBtn = new QPushButton("✕", this);
    m_closeBtn->setFixedWidth(32);
    m_closeBtn->setToolTip("关闭 (Esc)");
    connect(m_closeBtn, &QPushButton::clicked, this, &SearchBar::closeRequested);
    row1->addWidget(m_closeBtn);

    // 第二行：替换（默认隐藏）
    m_replaceRow = new QWidget(this);
    auto *row2 = new QHBoxLayout(m_replaceRow);
    row2->setContentsMargins(4, 2, 4, 2);
    m_replaceEdit = new QLineEdit(this);
    m_replaceEdit->setPlaceholderText("替换为...");
    m_replaceEdit->setMaximumWidth(280);
    row2->addWidget(m_replaceEdit);
    m_replaceBtn = new QPushButton("替换", this);
    m_replaceAllBtn = new QPushButton("全部替换", this);
    connect(m_replaceBtn, &QPushButton::clicked, this, [this]() {
        emit replaceRequested(m_replaceEdit->text());
    });
    connect(m_replaceAllBtn, &QPushButton::clicked, this, [this]() {
        emit replaceAllRequested(m_replaceEdit->text());
    });
    row2->addWidget(m_replaceBtn);
    row2->addWidget(m_replaceAllBtn);
    row2->addStretch();
    m_replaceRow->setVisible(false);

    connect(m_findEdit, &QLineEdit::textChanged, this, &SearchBar::scheduleEmitParams);

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);
    outer->addLayout(row1);
    outer->addWidget(m_replaceRow);

    setVisible(false);
}

void SearchBar::scheduleEmitParams() { m_debounce->start(); }

void SearchBar::emitParamsNow()
{
    emit searchParamsChanged(m_findEdit->text(), m_caseBtn->isChecked(),
                             m_regexBtn->isChecked(), m_wordBtn->isChecked());
}

void SearchBar::focusFindField()
{
    m_findEdit->setFocus();
    m_findEdit->selectAll();
}

void SearchBar::setCount(int total, int current)
{
    m_countLabel->setText(QStringLiteral("%1/%2").arg(total > 0 ? current : 0).arg(total));
    const bool has = total > 0;
    m_nextBtn->setEnabled(has);
    m_prevBtn->setEnabled(has);
    m_replaceBtn->setEnabled(has);
    m_replaceAllBtn->setEnabled(has);
}

void SearchBar::setReplaceMode(bool on) { m_replaceRow->setVisible(on); }

void SearchBar::keyPressEvent(QKeyEvent *event)
{
    // 查找框：Enter 下一个 / Shift+Enter 上一个；替换框：Enter 替换
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        if (m_replaceEdit->hasFocus()) {
            emit replaceRequested(m_replaceEdit->text());
        } else {
            if (event->modifiers() & Qt::ShiftModifier) emit findPrevRequested();
            else emit findNextRequested();
        }
        return;
    }
    QWidget::keyPressEvent(event);
}
```

- [ ] **Step 3：构建确认编译通过**

```bash
cd build && cmake --build . --target md-editor -j$(nproc)
```
Expected: 编译成功（此时 MainWindow 旧信号连接会编译失败——这是预期的；Task 6 同步修 MainWindow。若要本任务独立编译绿，可在 Task 5 暂时把 MainWindow 的旧 connect 注释掉，或与 Task 6 合并提交。**建议 Task 5 与 Task 6 一起编译验证、分开提交**）。

> 说明：`SearchBar` 信号集变化会令 `MainWindow.cpp` 现有 `connect(m_searchBar, &SearchBar::findRequested, ...)` 等编译失败。故 Task 5 编辑后必须紧接着做 Task 6 才能 `make` 通过。两任务代码上紧耦合，提交可拆分但编译验证需合并。

- [ ] **Step 4：提交（与 Task 6 一起验证后）**

```bash
git add src/ui/SearchBar.h src/ui/SearchBar.cpp
git commit -m "feat: SearchBar 重构（toggles/计数/导航/折叠/新信号/去抖）"
```

---

## Task 6：MainWindow 接线 + 快捷键 + 主题 CSS

**Files:**
- Modify: `src/ui/MainWindow.cpp`, `src/resources/web/editor.css`

**Interfaces:**
- Consumes: SearchBar 新信号、`EditorBridge::searchMatchUpdated`、`editorApi` 搜索方法。

- [ ] **Step 1：替换 connectSignals 中的搜索栏连接块**

在 `MainWindow.cpp::connectSignals()`（约 line 325-381）删除旧的 `findRequested/replaceRequested/replaceAllRequested/closeRequested` 四个 connect，替换为：

```cpp
    // === 搜索栏 ===
    // JS 字符串字面量转义（query / rep 嵌入 runJavaScript）
    static auto jsStr = [](const QString &s) {
        QString t(s);
        t.replace("\\", "\\\\").replace("'", "\\'").replace("\n", "\\n");
        return t;
    };
    auto runSearchJs = [this](const QString &jsBody) {
        EditorTab *tab = m_tabWidget->currentEditorTab();
        if (tab) tab->runJsCommand("(function(){var ed=window.editorApi;if(!ed||!ed.editor)return;" + jsBody + "})();");
    };

    connect(m_searchBar, &SearchBar::searchParamsChanged, this,
            [this, jsStr](const QString &text, bool cs, bool rx, bool ww) {
        runSearchJs(QStringLiteral("ed.startSearch('%1',%2,%3,%4);")
                    .arg(jsStr(text)).arg(cs).arg(rx).arg(ww));
    });
    connect(m_searchBar, &SearchBar::findNextRequested, this, [this]() { runSearchJs("ed.searchNext();"); });
    connect(m_searchBar, &SearchBar::findPrevRequested, this, [this]() { runSearchJs("ed.searchPrev();"); });
    connect(m_searchBar, &SearchBar::replaceRequested, this, [this, jsStr](const QString &rep) {
        runSearchJs(QStringLiteral("ed.replaceOne('%1');").arg(jsStr(rep)));
    });
    connect(m_searchBar, &SearchBar::replaceAllRequested, this, [this, jsStr](const QString &rep) {
        runSearchJs(QStringLiteral("ed.replaceAll('%1');").arg(jsStr(rep)));
    });
    connect(m_searchBar, &SearchBar::closeRequested, this, [this]() {
        m_searchBar->setVisible(false);
        runSearchJs("ed.stopSearch();");
    });
```

> `jsStr` 把 query/rep 安全嵌入单引号 JS 字面量（与现有替换逻辑同款转义）。`.arg(cs)` 等 bool→"0/1"，JS `!!regex` 在 `editorApi` 内部再归一化，安全。

- [ ] **Step 2：接 bridge 计数回传**

在 MainWindow 现有 per-tab bridge 信号连接处（与 `headingsUpdated`/`formatStateChanged` 同处，按现有模式连接），加：

```cpp
    // 搜索计数回传 → 仅更新当前 tab 的搜索栏
    connect(tab->bridge(), &EditorBridge::searchMatchUpdated, this,
            [this, tab](int total, int current, int tabId) {
        if (m_tabWidget->currentEditorTab() == tab)
            m_searchBar->setCount(total, current);
    });
```

> 若现有 bridge 连接是在 `onTabCreated`/`EditorTab` 构造后集中连接，按其位置放入；保证切 tab 后只反映当前 tab（切 tab 的处理见 Step 4）。

- [ ] **Step 3：Ctrl+F 改为「仅查找」，新增 Ctrl+H / F3 / Shift+F3**

在 `MainWindow::setupUi()` 快捷键区（约 line 96-118），把现有 `Ctrl+F` lambda 改为显示搜索栏 + `setReplaceMode(false)`；新增：

```cpp
    // Ctrl+H：显示搜索栏 + 展开替换行
    QShortcut *replaceShortcut = new QShortcut(QKeySequence("Ctrl+H"), this);
    connect(replaceShortcut, &QShortcut::activated, this, [this]() {
        m_searchBar->setVisible(true);
        m_searchBar->setReplaceMode(true);
        m_searchBar->focusFindField();
    });

    // F3 / Shift+F3：下一个 / 上一个（仅搜索栏可见时生效）
    QShortcut *nextSc = new QShortcut(QKeySequence("F3"), this);
    connect(nextSc, &QShortcut::activated, this, [this]() {
        if (m_searchBar->isVisible()) runEditorCommand("window.editorApi.searchNext();");
    });
    QShortcut *prevSc = new QShortcut(QKeySequence("Shift+F3"), this);
    connect(prevSc, &QShortcut::activated, this, [this]() {
        if (m_searchBar->isVisible()) runEditorCommand("window.editorApi.searchPrev();");
    });
```

把原 `Ctrl+F` 的 lambda 内补 `m_searchBar->setReplaceMode(false);`（仅查找模式）。

- [ ] **Step 4：切 tab 重置搜索栏计数 + 源码模式提示**

在 `onTabChanged`（或现有切换 tab 的槽）里：

```cpp
    // 切 tab：搜索栏计数重置为新当前 tab 的状态（触发一次空查询刷新由 focus/可见性决定）
    if (m_searchBar->isVisible()) {
        m_searchBar->setCount(0, 0);
        runEditorCommand("window.editorApi._notifySearchCount();");
    }
```

源码模式提示：在源码模式开启处（搜索 `Ctrl+\`/源码模式切换逻辑），若搜索栏可见，向状态栏（`StatusBar`）显示「源码模式暂不支持搜索」（沿用现有状态栏临时消息 API；若无，则在 `setCount(0,0)` 旁置计数 label 文本为「源码模式」）。**最小实现**：源码模式下各 editorApi 方法已直接 return（Task 3），故 C++ 侧仅给提示即可。

- [ ] **Step 5：editor.css 加高亮样式**

在 `src/resources/web/editor.css` 末尾追加（亮/暗两套；`body.dark` 变体对齐现有主题机制）：

```css
/* 搜索高亮 */
.ProseMirror .search-match {
    background-color: rgba(255, 213, 79, 0.45);   /* 淡黄 */
    border-radius: 2px;
}
.ProseMirror .search-match-current {
    background-color: rgba(255, 152, 0, 0.7);     /* 橙：当前匹配 */
    border-radius: 2px;
}
body.dark .ProseMirror .search-match {
    background-color: rgba(255, 213, 79, 0.35);
}
body.dark .ProseMirror .search-match-current {
    background-color: rgba(255, 152, 0, 0.8);
}
```

- [ ] **Step 6：编译 + 跑测试 + 手动验证**

```bash
cd build && cmake --build . -j$(nproc) && QT_QPA_PLATFORM=offscreen ctest --output-on-failure
```
Expected: 编译通过，全 ctest 绿。

手动（`bash scripts/run-editor.sh`）逐项验证：
1. 打开含 `foo foo foo` 的文档 → Ctrl+F 输入 `foo` → 3 处淡黄高亮、第 1 处橙、计数 `1/3`。
2. F3/Enter → 跳到第 2、第 3、回绕第 1，计数随之变 `2/3`、`3/3`、`1/3`。
3. Shift+F3 → 反向。
4. `Aa` toggle：大写 `Foo` 在小写文档不匹配（计数 0/0，按钮禁用）。
5. `.*` 正则：输入 `\d` 匹配所有数字；非法 `(回` → 0/0 不崩。
6. `W|` 全词：`foo` 不匹配 `foobar` 内部。
7. Ctrl+H → 展开替换行；替换 `foo`→`bar`：替换单个前进；全部替换 `foo`→`barbar`：`barbar barbar barbar`（无漂移）；正则 `(\d)`→`[$1]` 全部替换生效。
8. Esc → 关闭并清高亮。
9. 切 tab → 计数重置；源码模式（Ctrl+\）下搜索无操作/有提示。

- [ ] **Step 7：提交**

```bash
git add src/ui/MainWindow.cpp src/resources/web/editor.css
git commit -m "feat: MainWindow 接线搜索 + 快捷键 + 高亮样式"
```

---

## Task 7：CI 接入 + 版本推进

**Files:**
- Modify: `.github/workflows/build-deb.yml`, `CMakeLists.txt`, `CHANGELOG.md`

- [ ] **Step 1：CI 跑 npm test**

`.github/workflows/build-deb.yml` 在 Qt 构建（cmake 之前）插入 JS 测试步骤：

```yaml
      - name: JS 测试 (vitest)
        working-directory: src/resources/web
        run: |
          npm ci
          npm test
```

> ubuntu-24.04 自带 node；`package-lock.json` 已存在（Task 0 更新），`npm ci` 可用。

- [ ] **Step 2：本地全量验证**

```bash
cd src/resources/web && npm test && cd ../../..
cd build && cmake --build . -j$(nproc) && QT_QPA_PLATFORM=offscreen ctest --output-on-failure
```
Expected: vitest 全绿 + ctest 全绿。

- [ ] **Step 3：版本推进（建议 0.6.0，以用户确认为准）**

- `CMakeLists.txt`：`project(md-editor VERSION 0.6.0)`
- `CHANGELOG.md` 顶部新增：

```markdown
## [0.6.0] — 2026-08-14

### 新增
- **搜索增强** — 正则 / 全词（Unicode 词边界）/ 大小写 toggle、匹配计数 `n/m`、上下导航（回绕）、替换与全部替换（正则反向引用 `$1/$2/$&`），统一走自写 ProseMirror `Decoration.inline` 高亮（替换 Chromium `findText`）
- **修复全部替换位置漂移** — 改为从尾向前替换
- **JS 测试基建** — 接入 vitest，CI 运行 `npm test`

### 修复
- 替换与查找的大小写/正则脱节（现共用同一 matcher）
```

- [ ] **Step 4：提交 + 打 tag（确认版本后）**

```bash
git add .github/workflows/build-deb.yml CMakeLists.txt CHANGELOG.md
git commit -m "chore: CI 接入 npm test + 版本推进至 0.6.0"
git tag v0.6.0
```

> 推送/tag 以用户指示为准（远端推送可能需 `gh api` 绕行，见记忆 [[qt-642-compatibility-baseline]] 同期经验）。

---

## 自审清单（写计划后自检）

- [x] **Spec 覆盖**：设计 §4.1–4.7 全部落到 Task 1/2/3/5/6；§5 测试基建落 Task 0/7。
- [x] **无占位符**：每个 code step 含完整可写代码；无 TODO/「适当处理」。
- [x] **类型/命名一致**：`searchMatcher` 返回 `{start,end}`（Task1）↔ `searchPlugin.toDocRanges` 消费；`editorApi.startSearch(q,case,regex,ww)`（Task3）↔ MainWindow `.arg` 顺序（Task6）↔ SearchBar `searchParamsChanged(text,case,regex,ww)`（Task5）四参顺序全程一致。
- [x] **Qt 6.4.2 基准**：C++ 改动仅信号转发 + UI，无 Qt 版本相关转义；Task 4/6 `ctest` 在 6.4.2 容器复跑（如需）。
- [x] **往返保真**：替换走 `tr.insertText`，不碰 `MarkdownHandler`。
- [x] **边界**：源码模式 `return`（Task3）+ 提示（Task6）；非法正则 `[]`（Task1）；去抖 150ms（Task5）。
