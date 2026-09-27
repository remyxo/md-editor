# 导出 PDF/DOCX 代码块语法高亮 — 设计文档

日期：2026-09-27
状态：已与用户确认（样式范围=与编辑器完全一致；方案=A 单一来源预着色）

## 背景与目标

四个「面」（编辑器/HTML/PDF/DOCX，见 [`docs/export-visual-spec.md`](../../export-visual-spec.md)）中，
代码块语法高亮目前只有编辑器（lowlight）与 HTML 导出（内嵌 hljs.js 浏览器现场着色）有，
**PDF 与 DOCX 导出为纯色代码块**。本次补齐：PDF/DOCX 导出的代码块带语法高亮，
观感与编辑器亮色主题一致（颜色 + 粗体 + 斜体 + diff 增删背景，用户已确认全量携带）。

## 现状关键事实（已逐项验证）

1. 编辑器内高亮是 lowlight 插件的 **view 层 decoration**，不进 `getHTML()` 序列化
   ——导出链路拿到的都是无色 `<pre><code class="language-x">`。
2. PDF 两条分页路径（nativeHf 与 paged）**都**先跑 `__splitPreLines`（PdfExporter.cpp
   内嵌 JS）把 pre 按行拆成块级 `.pl` span；其对含 `\n` 的元素递归取子节点，
   **丢包裹元素样式**（多行 token 会掉色）。
3. DOCX 的 `parseCodeBlock`（DocxExporter.cpp）只收集 Characters 文本、显式跳过行内标签；
   但 `parseInline` 已有 `<span style="color:">` → `w:color` 的样式栈先例。
4. 源码模式导出走 `MarkdownHandler::toHtml`（C++）。Qt 原生 `QTextDocument::toHtml`
   **丢弃围栏语言**（实测无 `language-` class），但 MarkdownHandler 对围栏内容自绘
   `<pre><code class="language-x">`（含块引用内围栏），语言信息完好。
5. 编辑器页面内 `window.lowlight` 与编辑器高亮**同一实例**（同语言注册集），
   由 tiptap-all.js esbuild 打包暴露。
6. `hljs-light.css` 为 GitHub light 主题（约 25 条规则，含少量 font-weight/font-style/
   background 变体），editor.html 以 `<link id="hljs-light">` 引入。

## 方案抉择

- **A（选定）：editor.js 单一来源预着色**。编辑器页面新增纯函数，用同一 lowlight 实例
  高亮并解析主题 CSS 为 inline style span；PDF/DOCX/源码模式统一走它。
  单一来源无「规范抄两遍漂移」；PDF 渲染页零 CSS 改动；DOCX 直接拿到具体色值。
- B（否决）：PDF 渲染页注入 hljs.js+CSS 现场着色、DOCX 走 JS 预着色——两套机制，
  漂移温床，且 DOCX 侧机制反正要建，总工作量更大。
- C（否决）：C++ 侧实现高亮器——语法库庞大、与编辑器必然分叉。

## 架构与数据流

```
导出取内容（MainWindow::getEditorHtmlAsync 改造：bool forDocx → flavor 枚举）

WYSIWYG+PDF :  getContent() ──────────────┐
WYSIWYG+DOCX:  getExportHtml()（colgroup）─┤→ highlightExportHtml(html)（新）→ 各导出器
源码模式     :  getSourceContent() → C++ toHtml ─┘
HTML 导出    :  现状不动（浏览器现场 hljs 着色，支持暗色主题）
```

- 高亮步骤对 PDF/DOCX 统一在 C++ 侧追加一次
  `runJavaScript("window.editorApi.highlightExportHtml(<json>)")`；HTML 大字符串经
  JSON 编码传参，**必须转义 U+2028/U+2029**（合法 JSON 但破坏 JS 字符串字面量；
  计划期先查仓库有无现成编码工具，无则新写并单测）。
- **降级原则**：高亮函数任何异常都返回原文，导出不因高亮失败中断（代码块退化为
  现状纯色）。单块失败只跳过该块。
- 导出**恒用亮色主题**色板（PDF 固定亮色打印、DOCX 白底），与编辑器当前深浅色无关。

## editor.js `highlightExportHtml(html)`（纯函数，导出专用）

1. **主题规则表**（惰性构建一次并缓存）：从 `document.styleSheets` 中 id 为
   `hljs-light` 的 link 读规则；解析为 `选择器 → {color, font-weight, font-style,
   background}`，含 `.hljs` 基础色。**不硬编码色值**——色板唯一来源是主题 CSS 文件。
   规则解析器独立导出、可单测；函数接受可选规则表参数（缺省读页面 stylesheet，
   vitest 环境无 `document.styleSheets`）。
2. DOMParser 包裹解析（`<div>` 包一层取回 inner，避免 document/body 包装差异），
   找 `pre > code[class*="language-"]`：
   - `language-mermaid` 跳过（导出链路另有 SVG 渲染）；
   - `lowlight.registered(lang)` 为假跳过（与编辑器语言集天然一致）。
3. `lowlight.highlight(lang, text)` 产出 hljs class span → 逐 token 按类名匹配规则表，
   **复合选择器（如 `.hljs-title.function_`）按最长匹配优先**；重写为 inline
   `style` span（仅含有值属性），无匹配类的 span 剥壳保文本。
   **嵌套 token 按 CSS 继承语义合成有效样式**（内层缺省的属性继承外层，如
   `.hljs-meta` 包 `.hljs-string` 时内层继承外层背景），避免拆行/解析两侧语义分叉。
4. 无已注册语言代码块时快速路径原样返回（零 DOM 操作开销）。
5. 单块高亮抛异常只跳过该块，整篇继续。

## PDF 侧改造（仅 `__splitPreLines` 一处）

- 递归分支（元素含 `\n`）改为：对该元素逐行拆分其子节点，**每行片段用
  `cloneNode(false)` 的同元素包裹**（保 color/bold/italic/bg）；不含 `\n` 的元素
  分支维持现状整块入行。
- 行内 span 不改几何（颜色/字重不影响折行测量与分页容量模型），`.pl` 块级结构
  不变；指纹校验基于文本内容，不受影响。
- token 背景色（diff 增删行）作为 inline `background-color` 由 Chromium 绘制在
  `.pl` 灰底上，无需 CSS 改动。
- nativeHf 与 paged 两路径共用此函数，一次修复两路受益。

## DOCX 侧改造（`parseCodeBlock` 重写，最大工作量块）

- 从「只收 Characters、跳过行内标签」改为：pre 内维护**样式栈**（`<span style>` 的
  color / font-weight / font-style / background-color；嵌套 span 就近覆盖、缺省属性
  继承外层——与 JS 侧合成的有效样式同一语义）；文本按 Characters 流收集为
  `(文本, 有效样式)` 片段。
- 片段流按 `\n` 切分成行 → 每行一个 `<w:p>`（pPr 灰底/缩进/行距**不变**），
  行内按片段生成多个 `<w:r>`。
- run 的 rPr 叠加序：`rFonts(Consolas) → w:b → w:i → w:color → w:sz(22) → w:shd`，
  严格按 CT_RPr schema 序列（顺序违规触发 Word 修复框）。
- 代码块字号维持 22 半磅现状（export-visual-spec 的疑似漂移项不在本次范围）。
- **验证要求**：按既定流程做 macOS Word 双击验证（WPS/LibreOffice 宽容，
  测不出 schema 违规）。

## 测试与验证

| 层 | 内容 |
|---|---|
| JS（vitest） | 规则解析器；高亮函数夹具：颜色断言 / mermaid 跳过 / 未注册语言跳过 / 嵌套 token 继承 / 单块异常保原样 / 实体转义保真（`<` `&`） |
| C++（ctest） | test_docxexporter 增用例：高亮 span → `w:color/w:b/w:i/w:shd` run、多行 token 跨行拆段颜色保持、schema 顺序、空行段落 |
| 端到端 | 无头导出自证工具实测 PDF/DOCX 样本；PDF 目视代码块着色 + 页数/分页不回归；DOCX 走 macOS Word 验证 |
| 早期验证（计划首个任务） | 禁用 link 的 `cssRules` 可读性（不可读走 `fetch('hljs-light.css')` 回退）；U+2028/2029 编码工具现状盘点 |

## 范围外

- HTML 导出（保留浏览器现场着色与暗色主题支持）
- DOCX 代码块字号漂移（export-visual-spec 疑似漂移项）
- 版本推进与 CHANGELOG（待用户明示）
