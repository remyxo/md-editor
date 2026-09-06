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

PDF 导出管线（Task 4/5 接入）：

- polyfill 以**手动模式**运行：`<script>window.PagedConfig={auto:false}</script>` 须先于 polyfill 加载，禁止其自动分页；
- C++ 侧在 mermaid 渲染 Promise settle 后调 `runJavaScript("PagedPolyfill.preview()")`，等待其 Promise resolve 再 `printToPdf`（**顺序铁律：mermaid 先渲染、分页后跑**，SVG 定型后才切块）;
- `@page` 规则（纸张/方向/四边距 + `@top-center`/`@bottom-right` 页眉页脚）由 `PdfExporter::buildRenderHtml` 按 `export/*` QSettings 动态生成；
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

随后：更新本表中的版本号 → 重编译（qrc 打包进二进制）→ 跑 PDF 分页 E2E 清单（见 spec §1.6：多页页码连续、KaTeX 不碎、mermaid 不切断、表格跨页、横版/A5/自定义边距、单页与空文档、开关组合）。

## 与上游的差异

**零差异**。纯 dist 文件原样落盘，无本地补丁——升级时直接整文件替换即可，无需 diff 合并。

## 协议

MIT（见 dist 文件头 `@license Paged.js v0.4.3 | MIT`）。vendored 后保留原许可证声明，与本项目 GPL-3.0 不冲突。

## 已知限制（spec §1.7）

- 用户自定义 CSS 若含 `@page` 规则，可与本项目生成的 `@page` 规则冲突（级联后行为不确定，用户自担，文档不展开）；
- 超一页高的不可断元素（长图、长代码块）在分页引擎下溢出裁切——与不启用分页时的现状行为一致。
