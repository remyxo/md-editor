# vendor paged.js 分页引擎说明

## 背景

PDF 导出需要页眉（导出文件基名）与页脚（`第 X 页 / 共 Y 页`）。三条原生路全断：`QWebEnginePage::printToPdf()` 不暴露 Chromium 的 headerTemplate；CSS `@page` margin boxes Blink 不支持；固定定位元素做不到每页变化的页码。故引入 [paged.js](https://pagedjs.org) 分页引擎（CSS Paged Media polyfill）接管分页，页眉页脚成为 CSS Paged Media 原生能力。

详见 [`docs/superpowers/specs/2026-09-06-pdf-header-footer-footnote-design.md`](superpowers/specs/2026-09-06-pdf-header-footer-footnote-design.md) §1。

## 来源与版本

| 项 | 值 |
|----|----|
| 上游包 | `pagedjs`，来源 <https://www.npmjs.com/package/pagedjs> |
| 上游版本 | **0.4.3**（2026-09-06 vendoring 时 npm 最新） |
| vendored 位置 | `src/resources/web/paged.polyfill.js`（dist 原样落盘，非压缩版） |
| 加载方式 | `app.qrc` 已登记，qrc 基 URL 加载（离线，无 CDN 依赖） |
| 全局入口 | `window.PagedPolyfill`（UMD 包装 `globalThis.PagedPolyfill = factory()`），`PagedPolyfill.preview()` 返回 Promise |

**落盘方式**：`npm pack pagedjs@<版本>` 解出 `package/dist/paged.polyfill.js` 原样拷入，**不做任何本地修改**。

## 用途（本项目接线约定）

PDF 导出管线（Task 4/5 接入；#29-#33/R1 健壮化后的现状，机制以 `src/core/PdfExporter.cpp` 实装为准）：

- polyfill 以**手动模式**运行：`<script>window.PagedConfig={auto:false}</script>` 须先于 polyfill 加载，禁止其自动分页；
- **rAF 垫片，且必须先于 polyfill 注入**：PDF 渲染视图是永不可见的隐藏 QWebEngineView，Chromium 不为其派发合成帧，`requestAnimationFrame` 永不触发——paged.js 渲染队列（`Queue.tick = requestAnimationFrame`）会饿死，`preview()` 永不 settle。模板用 setTimeout 等价垫片替换 rAF（一次性打印页、无动画）；垫片脚本须排在 polyfill 之前（`Queue` 构造时捕获全局 `requestAnimationFrame`，顺序颠倒即失效）；
- **打印前置状态机**（`window.__preparePrint`，完成态经 `window.__printState` 通报：idle → working → 终态 paged/legacy/fallback）：Qt 的 `runJavaScript` 不等待 Promise（Qt 6.8 实测回调立即触发），不能在其回调里直接 `printToPdf`——分页进行中 body 可能已被 paged.js `wrapContent` 掏成空 `<template>`，产出单页空白 PDF。实际链路：C++ 在 `loadFinished` 后触发 `window.__preparePrint()`（内部依次 `__renderAllMermaid()` → `__fixTableBreaks()` → `__fixTableColumns()` → `__splitPreLines()` → 双指纹快照 → `PagedPolyfill.preview()` → `__verifyContent()`），随后以 100ms×300 轮询 `__printState`（30s 超时兜底同样还原 body 走回退版式，宁可无页眉页脚也不交白纸），拿到终态再按对应版式打印（paged=微量边距版式，页边距由 `@page` 承载、Qt 侧取 0.1mm 的缘由见 `pagedPrintMargins()` 注释；legacy/fallback=用户边距版式）。**顺序铁律：mermaid 先渲染、分页后跑**，SVG 定型后才切块；
- **内容指纹保底**（`__verifyContent` 双指纹）：分页前快照 `__preText`（body 文本）与 `__preImgs`（img `src` 排序多重集，同 src 合法重复保留次数语义），分页后汇总 `.pagedjs_page_content` 与 `.pagedjs_footnote_content` 比对——任何静默丢失/重复即 `__restoreBody()` 还原 body 置 fallback 走原生打印，绝不把被掏空的 body 交给 `printToPdf`（保底零丢失优先于页眉页脚）；
- **取证注意**：往 body 里 append `<script>` 做探针会改变 `document.body.textContent`，污染 `__preText` 快照造成指纹比对假红、误回退原生分页——调试/取证钩子必须经 C++ `runJavaScript` 注入（不进 DOM、不污染指纹）；
- `@page` 规则（显式 mm 纸张尺寸/方向/四边距 + `@top-center`/`@bottom-right` 页眉页脚 + mermaid/img 容纳上限）由 `PdfExporter::buildRenderHtml` 按 `export/*` QSettings 动态生成，且置于 `@media print` 之外——paged.js 在屏幕媒体上分页测量；
- 页眉页脚全关时不引入 paged.js，走原管线（回归零风险）。

## 同步 / 升级流程

```bash
cd src/resources/web
VER=$(npm view pagedjs version)
npm pack "pagedjs@$VER"
tar xf "pagedjs-$VER.tgz"
cp package/dist/paged.polyfill.js .
rm -rf package "pagedjs-$VER.tgz"
```

随后：更新本表中的版本号 → 重编译（qrc 打包进二进制）→ 跑 PDF 分页 E2E 清单（见 spec §1.6：多页页码连续、KaTeX 不碎、mermaid 不切断、表格跨页、横版/A5/自定义边距、单页与空文档、开关组合）。升级后若页眉页脚整批消失（页码也不在），先查 rAF 垫片是否仍先于 polyfill 注入、`__preparePrint` 状态机与指纹回退是否被破坏——机制契约见上节。

## 与上游的差异

**零差异**。纯 dist 文件原样落盘，无本地补丁——升级时直接整文件替换即可，无需 diff 合并。

## 协议

MIT（见 dist 文件头 `@license Paged.js v0.4.3 | MIT`）。vendored 后保留原许可证声明，与本项目 GPL-3.0 不冲突。

## 已知限制

spec §1.7 为设计时记录，其中第二条已随后续修复过期；本节为落地后现状（2026-09-07 更正）。

- 用户自定义 CSS 若含 `@page` 规则，可与本项目生成的 `@page` 规则冲突（级联后行为不确定，用户自担，文档不展开）；
- 超一页高的不可断元素三类均已可容纳/可断：mermaid SVG 有容纳上限 `.mermaid-diagram svg{max-height:capH!important}`（capH = 纵向纸高/横向纸宽 − 上下边距 − 2mm 安全间隙；只约高度，宽度归 mermaid 自身 `width="100%"` + 内联 `max-width` 机制）；普通 `<img>` 有双规则上限（全量 `max-width:100%` 只收缩不放大 + `:not([width]):not([style])` 施加 `max-height` 上限并配 `width:auto` 超高时等比缩）；代码块 pre-wrap + `__splitPreLines()` 按源码行拆块级 span，分页只落在行边界，超一页高的代码块逐行分页；
- 残余缺口：带显式 `width`/`height`（或 `style`）属性的用户字面 HTML 图片不受高度上限约束——`:not` 刻意排除（`width:auto!important` 会覆盖属性，把用户显式缩小的图放大回自然尺寸）。此类图超高时**回退保底仍在**：原子图无法分页 → paged.js 重复铺排被 img 多重集指纹拦截 → 回退原生分页（代价丢页眉页脚、不丢内容）；指纹真正拦不住的是纯几何裁切（超宽溢出 sheet 静默右裁——已由全量 `max-width:100%` 封住该面，未来若放宽该规则此面重开）。防放大陷阱优先，已记录的已知取舍。
