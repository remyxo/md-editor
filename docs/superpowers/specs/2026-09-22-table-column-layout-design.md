# 表格列宽智能布局设计（编辑器 + DOCX，算法单一来源）

日期：2026-09-22
状态：设计已获用户批准（范围与方案均经确认）

## 背景与问题

源文档 `~/Desktop/复杂场景综合测试.md` 实测（用户 2026-09-22 报告）：

- **1.1 表（60 行 5 列）**：「状态」列的「已完成」被压成一字一行（3 行），「备注」列却留得很宽——浏览器 auto 布局在总宽不足时按 max-content 比例分配，长文本列尽吞宽度、短列被挤爆；
- **1.2 表（12 列）**：列07 含一个无断点的超长 JWT URL，auto 布局下该列 min-content = 整串宽度，导致该列异常宽。

诉求：**宽度够时小列尽量不换行，宽列该换行的要换行**。

## 现状（四个面）

| 面 | 现状 | 结论 |
|----|------|------|
| 编辑器 | `editor.css:134` 纯 auto 布局，无 overflow-wrap；`resizable:true`（editor.js:1704）已激活 TableView，colgroup 恒在 DOM（无 colwidth 时每列仅 `min-width:25px`） | **问题所在，无智能分配** |
| HTML 导出 | `HtmlExporter::tableColumnNormalizeScript`（HtmlExporter.cpp:181）：两趟 intrinsic 测量 + floor/cap 三分支分配 + colgroup 百分比 + fixed | ✅ 已解决（注释记录的同症状同文档实测） |
| PDF 导出 | 经 `__fixTableColumns` 兼容层共用上述脚本 | ✅ 已解决 |
| DOCX 导出 | DocxExporter.cpp:1176 解析 colgroup `width:px`→gridCol（×15 dxa，不按页宽缩放），否则每列 4680 等宽；编辑器从不产生 colwidth（除手动拖拽），实际等宽 | 等宽，不智能 |

## 决策记录

1. **范围：编辑器 + DOCX**（用户选定）。HTML/PDF 已有智能列宽，本设计不动其行为。
2. **方案 B：算法抽共享 qrc JS 单一来源**（用户选定）。编辑器与导出共用同一份测量/分配实现，杜绝漂移（KaTeX 段「合并回一份」同款品味）；代价是重构刚发版的 v0.19.0 导出脚本构成，以「数学不变 ⇒ 分页不变」的回归锁兜底。
3. **否决方案 C（colwidth 文档属性）**：每次自动布局都是文档事务，脏标记/撤销历史/resize 重算/用户拖宽区分四条管道都要额外处理，复杂且行为劣于 DOM colgroup 方案。

## 设计

### 1. 共享算法模块 `src/resources/web/table-column-layout.js`（新文件，入 app.qrc）

从 `tableColumnNormalizeScript` 抽出两个函数：

```
window.__measureTableColumns(tableEl, cfg) → { minW:[], maxW:[] }
    两趟 intrinsic 测量：临时应用测量样式（cfg.fontSize/padding/fontFamily 可选、
    overflowWrap:normal + wordBreak:keep-all 强制——拉丁整词与 CJK 逐字才是自然
    断行单元）、table 宽 min-content / max-content 各展开一次首行（含 colspan
    展开），完成后恢复原样式。
    相比现状的增强：测量前先剥掉表格已有 colgroup（编辑器二次测量时克隆表带着
    上轮的百分比 col 会污染 auto 测量；导出侧旧 colgroup 本来就在写回前被移除，
    行为不变）。

window.__allocateTableColumns(minW, maxW, avail, slack) → w[]
    原有分配数学原样搬入：maxW=min(maxW, 3×minW) 封顶、+slack 余量（小数布局
    与边框折叠吃掉零点几像素，正好卡在 max-content 边界的短列仍折行——HTML 导出
    实测 0.3px 即一字一行）、floor=min(max, 3×min, 等份额)、
    sumMax≤avail / sumFloor≤avail / 兜底 三分支。
```

`tableColumnNormalizeScript` 重构为：**C++ 运行时读 qrc 内联共享文件 + 保留 DOM 写回壳**（调上述两函数，写回部分——colgroup 百分比 + 逐格内联百分比 + `table-layout:fixed`——逐字不变）。`window.__normalizeTableColumns` 函数签名不变，PdfExporter 的 `__fixTableColumns` 兼容层零改动。

### 2. 编辑器接入（editor.js + editor.css）

- **TipTap 插件**（view update 钩子）：
  - 首次布局**同步**执行（避免加载时闪一下坏布局），此后文档变更**去抖 300ms**；
  - **跳过条件**：首行任一 cell 带 `colwidth` 属性 = 用户手动拖过列宽 → 尊重用户，不接管该表（本方案从不写 colwidth，故用户拖宽与自动布局天然可区分）；
  - 克隆表格到 body 下离屏容器 `<div class="ProseMirror" style="position:absolute;left:-99999px;visibility:hidden;width:{容器宽}px">`，容器宽取 live 表的 `.tableWrapper` 客户宽（即表格实际可占宽；`body.dark` 由真实 body 带着，暗色单元格样式自动生效；不进 PM 根节点，避开 DOMObserver）；
  - `__measureTableColumns(克隆, {})`（编辑器真实样式测量，无导出侧字体覆盖）→ `__allocateTableColumns(min, max, 容器宽, 2)`；
  - 把百分比写入 live 表现有 colgroup 的 `<col>`，`table.style.tableLayout='fixed'; width='100%'; minWidth=0`。**百分比 ⇒ 窗口缩放/侧栏开合免重算**；
  - 写回安全依据：TableView 的 `ignoreMutation` 已忽略 table/colgroup 变更（vendored tiptap-extension-table.js:66-67）。
- **CSS 两条**（editor.css）：
  - `.ProseMirror th, .ProseMirror td { overflow-wrap: anywhere }`——1.2 列07 JWT 在封顶列宽内断行，根治异常宽列；
  - `.tableWrapper { overflow-x: auto }`——手动拖宽超出视口时横滚（现状溢出裁切）。
- 覆盖时机：加载文件、源码模式切换回来、insertTable 全部经 view update 钩子自然覆盖，无需单独挂点。
- editor.html 在 editor.js 之前引入共享模块（`<script src="table-column-layout.js">`）。

### 3. DOCX 链路

- editorApi 新增 `getExportHtml()`：`getHTML()` 后按**文档序**把序列化出的空 colgroup 逐个替换为 live colgroup 的 **px 宽**版本（百分比 × live 表宽换算取整；用户手动拖的表本来就是 px，原样）。保存路径 `getContent()` 不动。
- MainWindow 导出 DOCX 的取 HTML 处换调 `getExportHtml()`；HTML/PDF 导出仍走 `getContent()`（其归一脚本会重写 colgroup，不受影响）。
- DocxExporter：
  - 解析 colgroup px → dxa（现有逻辑）；
  - 新增**按页内容宽比例缩放**：页宽 − 左右边距（与 sectPr 同源 QSettings 计算），gridCol 写缩放值；
  - 每格 `tcW` = 所跨列缩放宽之和（替换现在写死的 2340），`tblPr` 加 `<w:tblLayout w:type="fixed"/>`；
  - 超页宽的表（用户拖太宽）一律钳到页宽；
  - 无 colgroup 的表退回现有等宽行为（兼容旧输入）。
- 验证：macOS Word 双击验证（既定 A/B 样本法流程，见项目记忆）。

### 4. 测试

| 层 | 内容 |
|----|------|
| vitest（web/） | `__allocateTableColumns` 纯数学三分支/封顶/slack 边界；`getExportHtml` 文档序 colgroup 替换逻辑 |
| C++ test_htmlexporter | 断言更新：脚本构成改为「内联共享函数 + 调用」（`__normalizeTableColumns` 签名断言保留） |
| C++ test_pdfexporter | 回归锁：分页页数与 0.19.0 基线一致（数学不变 ⇒ 产物不变） |
| C++ test_docxexporter | 新增：colgroup px → 按页宽缩放的 gridCol/tcW/tblLayout 断言；无 colgroup 表退回等宽 |
| 真机端到端 | 复杂场景综合测试.md：编辑器 1.1「已完成」不折行、1.2 列07 封顶断行、1.3 均衡；DOCX → Word 验证；HTML/PDF 产物与 0.19.0 对照无漂移 |

### 5. 风险与边界

- **测量成本**：每表克隆 + 2 次强制布局；60 行表导出侧实测毫秒级，去抖挡住键入风暴；
- **只读模式**（`setEditable(false)`，无 TableView）：表格走 renderHTML，colgroup 同样存在，插件照常写 DOM（此路径 PM 无 node view 干扰，风险更低）；
- **空表/colspan**：首行展开逻辑导出侧已验证，直接复用；
- **colgroup 剥离对导出的影响**：无——导出写回前本就移除旧 colgroup，测量前剥只剩时序提前；
- **改 qrc 后必须重编译**（项目铁律）：新 JS 文件与 editor.html 改动都在 qrc 内。

## 关键代码锚点

| 文件 | 位置 | 内容 |
|------|------|------|
| `src/core/HtmlExporter.cpp` | :181 | `tableColumnNormalizeScript`（重构为内联共享文件 + 写回壳） |
| `src/resources/web/editor.css` | :134 | `.ProseMirror table` 样式（补 anywhere / tableWrapper 横滚） |
| `src/resources/web/editor.js` | :1704 | `resizable:true`（TableView 激活点）；:2004 `getContent`（保存/导出共用出口） |
| `src/resources/web/tiptap-extension-table.js` | :66-67 | `ignoreMutation`（colgroup 写回安全依据） |
| `src/core/DocxExporter.cpp` | :1162-1256 | colgroup 解析 / tblGrid / tcW（缩放改造点） |
| `src/resources/app.qrc` | :137-143 | web 资源注册（新增共享 JS） |
| `tests/test_htmlexporter.cpp` | :251-298 | 脚本构成断言（更新） |
| `tests/test_pdfexporter.cpp` | :301+ | `__fixTableColumns` 断言（回归锁） |
