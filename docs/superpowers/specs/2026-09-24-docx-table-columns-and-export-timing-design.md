# DOCX 表格列宽对齐 PDF（隐藏渲染页重分配）+ 导出用时状态栏消息 设计

日期：2026-09-24
状态：设计已获用户批准（方案 B「隐藏渲染页，与 PDF 同构」经用户选定）
关系：修正 [2026-09-22 表格列宽智能布局设计](2026-09-22-table-column-layout-design.md) 的 DOCX 部分——当时的「live colgroup px 注入」方案在编辑器窗口宽 ≠ Word 页宽时错位，本设计替换该链路；编辑器/HTML/PDF 三面不动。

## 背景与问题

用户实测（`~/Desktop/复杂场景综合测试.md`，同 2026-09-22 报告的文档）：

1. **1.1 表（60 行）**：「编号」「状态」等短字段列（001、已完成）被拆成 3 行（一字一行）；
2. **1.2 表（12 列）**：列07（超长 JWT URL）占用过多，其余列拥挤；
3. **PDF 导出同文档效果良好**——诉求：能保持一致的保持一致。

另：导出 PDF/DOCX/HTML 后状态栏只报「已导出: XXX」，无耗时信息。

## 根因：DOCX 与 PDF 的列宽来源不同构

| | PDF（效果好） | DOCX（错位） |
|---|---|---|
| 列宽来源 | 导出时在隐藏渲染页**重新测量+重新分配** | 复用编辑器 live colgroup 的**比例** |
| 测量基准 | 打印基准：10pt 字号、4px 8px 内边距、avail=页内容宽（≈601px） | 编辑器基准：编辑器字体、**编辑器窗口宽**（常 1000px+） |
| 实现 | `__normalizeTableColumns`（`table-column-layout.js` 单一来源） | `editor.js getExportHtml()` 注入 live px colgroup → `DocxExporter.cpp:1196` 等比缩放到页内容宽 |

编辑器窗口比 Word 页面内容宽（如 1100px vs 601px），缩放系数 f≈0.55：短列在编辑器基准下的保底宽缩到 Word 后装不下 11pt 字（一字一行）；长列在宽窗口下按 max-content 吸收的过多比例缩放后依旧独占。PDF 在**目标宽度下用打印字号重新分配**，天然无此错位。

## 决策记录

1. **方案 B：隐藏渲染页，与 PDF 同构**（用户选定）。DocxExporter 经进程级单例隐藏页载入最终 HTML，跑同一份 `table-column-layout.js`，DOCX 基准参数重分配；否决轻量方案 A（编辑器页内重测）——源码模式无 DOM 不覆盖，且 DOCX 专属测量逻辑进 editor.js 制造第四份实现。
2. **用户拖宽（colwidth）的表：与 PDF 一致，导出时重新分配**。PDF 现状即无视拖宽按内容分配（`__normalizeTableColumns` 不读 colwidth）；「尊重用户不接管」仅是编辑器 live 布局原则。跟随 PDF 消除不一致。
3. **用时不计保存对话框浏览时间**：从内容取到、创建 exporter（进度对话框出现）到 `exportFinished`。

## 设计一：DOCX 表格列宽重分配

### 数据流（与 PdfExporter preparePrint 同构）

```
exportToDocx(html)
  └─ TableColumnNormalizer（进程级单例隐藏页，仿 MermaidRenderer 模式）
       ├─ setHtml(html, baseUrl=docDir)        ← 表内图片按文档目录解析，参与测量
       ├─ 运行 __normalizeTableColumns({px 模式}) ← 同一份 table-column-layout.js（qrc 内联）
       │    avail      = contentWidthTwips / 15   ← 与 writeDocx 同一 QSettings 公式（提取共享 helper，勿抄两遍）
       │    fontSize   = '11pt'                   ← DOCX Normal 22 半磅（PDF 10pt，各按各的基准）
       │    padding    = '0 7.2px'                ← 对应显式 tblCellMar 108 缇（Word 默认值，108/15=7.2）
       │    fontFamily = 'Noto Sans SC,DejaVu Sans,sans-serif'（与 PDF 同栈）
       └─ 回传 body.innerHTML（含 px colgroup）
  └─ 既有 writeDocx 路径不变（px→dxa ×15，f≈1.0 直通，末列吸收取整余量）
```

### 关键改动点

1. **`table-column-layout.js` 壳 `__normalizeTableColumns` 加 `cfg.px` 分支**：colgroup 写 px（分配所得 w[c2] 即 px）而非百分比；px 模式跳过单元格内联百分比写回（DOCX 只读 colgroup，PDF 的写回是为 paged.js 跨页碎片保列，DOCX 无此需求）。HTML/PDF 不传 `px`，行为零变化。**改动前先核对 test_htmlexporter / test_pdfexporter 对该脚本的子串锚定断言**。
2. **DocxExporter tblPr 写显式 `w:tblCellMar`**（左右 108 缇，同 Word 默认）：纯确定性声明（防 Word/WPS 主题差异），并与测量 padding 7.2px 自洽。CT_TblPrBase 顺序：tblLayout 之后插 tblCellMar。
3. **降级保护**：隐藏页 loadFinished 失败 / JS 异常 / 超时 → 用原始 html 走现有路径（live colgroup 缩放），状态栏不额外提示，不阻断导出。须有载入超时（防挂死，参照 MermaidRenderer 的 30s QTimer 兜底，MermaidRenderer.cpp:115）。**MainWindow 的 forDocx 分支保留 `getExportHtml()` 调用**——正常路径 normalize 会剥旧 colgroup 重写（无害），降级路径则依赖其中的 live colgroup。
4. **无表格快速路径**：JS 侧 `querySelectorAll('table')` 为空直接回传原文，避免无表文档白付页面加载。
5. **全链路异步化**：exportToDocx 先过隐藏页（含无 mermaid/math 的文档），完成后再走既有 mermaid/math → writeDocx 流程；`m_emitDone` 防重复唤醒模式沿用。
6. **源码模式顺带修复**：toHtml 产物无 colgroup，经同一隐藏页获得一致列宽，无需额外分支。

### 已知边界（接受）

- DOCX 测量字体（Noto Sans SC/DejaVu）≠ Word 实际字体（黑体/Arial）：CJK 等宽 1em 主导差异，ASCII 部分 DejaVu 略宽于 Arial、测量偏保守方向；与 PDF 的近似同性质。
- 回传 body.innerHTML 的重序列化差异由既有 makeXmlSafe/QXmlStreamReader 容错消化，测试夹具（tiptap_serialize.h）覆盖。

## 设计二：导出用时状态栏消息

- **计时**：lambda 内 `QElapsedTimer` 起表（内容就绪、创建 exporter 处），`exportFinished` 回调取 `elapsed()`。
- **格式**（helper：MainWindow.cpp 静态函数，三条导出路径共用）：
  - < 1000ms：`已导出: report.pdf, 用时 423 毫秒`
  - ≥ 1000ms：`已导出: report.pdf, 用时 3.2 秒`（四舍五入 1 位小数）
- PDF 回退组合消息同样追加：`已导出: xxx（分页引擎回退，本次无页眉页脚）, 用时 3.2 秒`；失败消息不加用时。

## 验证方案

1. **ctest**：`test_docxexporter` 新增隐藏页全链路用例（tblGrid 总宽 = 页内容宽、跨列 tcW 与所跨 gridCol 之和一致、无表格文档快速路径）；`test_htmlexporter` / `test_pdfexporter` 共享脚本改动回归（含子串锚定）。
2. **真机目检**：用户以 `复杂场景综合测试.md` 导出 DOCX，对照 PDF 检查 1.1 表短列不折行、1.2 表列07 让宽。
3. **macOS Word 双击验证**：DOCX 改动按既定流程（WPS/LibreOffice 宽容测不出 schema 违规）。
4. **用时消息**：三种导出各一次目检（大 PDF 走秒格式、小 HTML 走毫秒格式）。

## 不做（范围外）

- 表头底色 `D9E2F3`、行距 1.15 等其他视觉漂移项（见 export-visual-spec.md 待裁决清单）；
- 版本号 / CHANGELOG / tag（用户未要求）。
