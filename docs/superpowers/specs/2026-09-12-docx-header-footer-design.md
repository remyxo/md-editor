# DOCX 导出页眉页脚设计

> 状态：已确认，待写实现计划
> 日期：2026-09-12
> 范围：DOCX 导出增加页眉（文档标题）与页脚（页码），独立开关，版式对齐 PDF 导出

## 1. 目标与非目标

### 目标

- **DOCX 导出支持页眉页脚**：页眉 = 导出文件名基名（居中），页脚 = 「第 X 页 / 共 Y 页」（右对齐），与 PDF 导出版式一致。
- **独立开关、默认开**：`export/docxHeaderEnabled` / `export/docxFooterEnabled`，与 PDF 的 `pdfHeaderEnabled`/`pdfFooterEnabled` 互不影响。
- **活页码**：页码用 OOXML 字段（`PAGE`/`NUMPAGES`），用户在 Word 里继续编辑时自动重算，非导出时快照。
- **全关零注入**：两开关全关时不生成任何部件、不写任何引用，产物与现状语义等价（同 PDF「全关走原管线」哲学）。

### 非目标（YAGNI）

- 首页不同（`titlePg`）、奇偶页不同（`evenAndOddHeaders`）
- 富文本/图片页眉、自定义页眉文本（只用文件名基名，与 PDF 同源）
- 页码起始值、页码格式选项

## 2. 技术背景

- DOCX（OOXML）的页眉页脚是 **Word 原生概念**：包内加 `word/header1.xml`/`word/footer1.xml` 部件，`sectPr` 挂引用即生效，**无需任何分页引擎**（PDF 侧需 vendored paged.js 的原因在此不存在）。
- `DocxExporter` 手工组装整个 OOXML 包（miniz zip），现有范式即手写字符串部件（`styles.xml`、`numbering.xml` 同模式）。
- 现有 `sectPr` 已声明 `w:header="720" w:footer="720"`（页眉/页脚内容距页边 0.5"），只差部件与引用。
- 页码字段两形态：简单字段 `fldSimple`（schema 合法、WPS/LibreOffice 均支持）与 complex field（`fldChar` 三段式）。选 `fldSimple`（部件字符串更短）；Word 实测若有问题再降级。

## 3. OOXML 部件规格

### 3.1 `word/header1.xml`

单段居中纯文本，9pt（`w:sz="18"` 半点）灰色（`#808080`），段距紧凑（覆盖 `styles.xml` docDefaults 的 1.15 行距为单倍、段后距 0）：

```xml
<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<w:hdr xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main">
  <w:p>
    <w:pPr><w:spacing w:after="0" w:line="240" w:lineRule="auto"/><w:jc w:val="center"/></w:pPr>
    <w:r><w:rPr><w:color w:val="808080"/><w:sz w:val="18"/></w:rPr>
      <w:t xml:space="preserve">{标题}</w:t></w:r>
  </w:p>
</w:hdr>
```

标题取 `QFileInfo(outputPath).completeBaseName()`，经现有 `xmlEscape` + `xmlSanitize` 处理（XML 转义 + 控制字符剥离，与正文同防线）。基名为空（理论不可能，防御）：跳过页眉部件，页脚照常。

### 3.2 `word/footer1.xml`

右对齐，「第 X 页 / 共 Y 页」，X/Y 为简单字段：

```xml
<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<w:ftr xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main">
  <w:p>
    <w:pPr><w:spacing w:after="0" w:line="240" w:lineRule="auto"/><w:jc w:val="right"/></w:pPr>
    <w:r><w:rPr><w:color w:val="808080"/><w:sz w:val="18"/></w:rPr><w:t xml:space="preserve">第 </w:t></w:r>
    <w:fldSimple w:instr=" PAGE   \* MERGEFORMAT ">
      <w:r><w:rPr><w:color w:val="808080"/><w:sz w:val="18"/></w:rPr><w:t>1</w:t></w:r>
    </w:fldSimple>
    <w:r><w:rPr><w:color w:val="808080"/><w:sz w:val="18"/></w:rPr><w:t xml:space="preserve"> 页 / 共 </w:t></w:r>
    <w:fldSimple w:instr=" NUMPAGES   \* MERGEFORMAT ">
      <w:r><w:rPr><w:color w:val="808080"/><w:sz w:val="18"/></w:rPr><w:t>1</w:t></w:r>
    </w:fldSimple>
    <w:r><w:rPr><w:color w:val="808080"/><w:sz w:val="18"/></w:rPr><w:t xml:space="preserve"> 页</w:t></w:r>
  </w:p>
</w:ftr>
```

字段内 run 的文本是占位值（Word 打开时按字段重算显示）。

### 3.3 接线四处（均在 `writeDocx()` 组装阶段）

| 位置 | 内容 |
|------|------|
| `sectPr` | `<w:headerReference w:type="default" r:id="rIdHdrX"/>` / `<w:footerReference w:type="default" r:id="rIdFtrX"/>` 插在 `sectPr` **最前**——CT_SectPr schema 顺序要求 `headerReference`/`footerReference`（EG_HdrFtrReferences）先于 `pgSz`/`pgMar`。`document.xml` 根元素已声明 `xmlns:r`，无需改 |
| `word/_rels/document.xml.rels` | 两条 Relationship：Type `…/relationships/header` → `header1.xml`；`…/relationships/footer` → `footer1.xml`。rId 编号沿用现有 `rIdStyles`/`rIdNumbering` 命名风格（如 `rIdHeader1`/`rIdFooter1`），不与图片/超链接的 `rIdN` 数字段冲突 |
| `[Content_Types].xml` | 两个 Override：`…wordprocessingml.header+xml` / `…wordprocessingml.footer+xml` |
| zip | 新增 `word/header1.xml`、`word/footer1.xml` 条目（`MZ_BEST_COMPRESSION`） |

### 3.4 开关语义

| header | footer | 产物 |
|--------|--------|------|
| 开 | 开 | 两部件 + 全部接线 |
| 开 | 关 | 仅 header1.xml + 对应接线 |
| 关 | 开 | 仅 footer1.xml + 对应接线 |
| 关 | 关 | 零部件零引用（与现状语义等价，回归锁定） |

## 4. 边界与既有交互

- **mermaid/公式异步路径正交**：页眉页脚在 `writeDocx()` 最终组装阶段注入，与占位替换（`substituteMermaidPlaceholders` 等）无交集。
- **边距交互**：`pgMar` 已有 `w:header="720" w:footer="720"`；用户边距（cm 设置）小于 720 twips 时 Word 自动把正文下推到页眉之下，无重叠风险，不需处理。
- **导出流程零改动**：`DocxExporter` 与 `PdfExporter` 同模式自查 QSettings，`MainWindow`/`FileManager` 不动。

## 5. 设置 UI

`SettingsDialog` 导出页在 PDF 两个勾选框（`m_pdfHeaderCheck`/`m_pdfFooterCheck`）下方新增：

- `m_docxHeaderCheck`：「DOCX 页眉（文档标题）」
- `m_docxFooterCheck`：「DOCX 页码（第 X 页 / 共 Y 页）」

读写 `export/docxHeaderEnabled` / `export/docxFooterEnabled`（默认 true），`loadSettings()`/`saveSettings()`/`retranslateUi()` 三处接线，模式与 PDF 勾选框完全一致。

## 6. 测试策略

### 6.1 单元测试（`tests/test_docxexporter.cpp` 扩展）

四种开关组合 × 以下断言（QSettings 重定向 NativeFormat 到临时目录，沿用现有测试模式）：

- zip 内部件存在性（开→有 / 关→无）
- `[Content_Types].xml` 含对应 Override
- `document.xml.rels` 含对应 Relationship
- `sectPr` 引用存在且位于 `<w:pgSz` **之前**（schema 顺序回归锁）
- 标题文本正确（含 XML 转义用例：标题带 `&`/`<`）
- `PAGE`/`NUMPAGES` instr 字符串正确
- 各部件 XML 良构（`QXmlStreamReader` 解析无错）
- 全关产物与改造前语义等价（无 header/footer 痕迹）

### 6.2 macOS Word 双击验证（放行门）

A/B 取证（老教训：WPS/LibreOffice 宽容测不出 schema 违规，macOS Word 双击才能复现修复框）：

1. 开页眉页脚导出多页文档 → Word 双击打开**无修复框**
2. 页眉居中显示标题、9pt 灰色；页脚右对齐「第 X 页 / 共 Y 页」
3. 翻页页码递增；`NUMPAGES` 等于实际页数
4. 在 Word 中增删内容后页码/总页数自动重算（活字段）
5. WPS/LibreOffice 交叉打开确认渲染正常（仅参考，不放行依据）

## 7. 涉及文件

| 文件 | 改动 |
|------|------|
| `src/core/DocxExporter.cpp` | 读开关 → 生成两部件 + 四处接线（~80 行） |
| `src/ui/SettingsDialog.{h,cpp}` | 两个勾选框 + 持久化 + retranslate |
| `tests/test_docxexporter.cpp` | 开关组合用例 |
| `CHANGELOG.md` / `README.md` | 版本收口时更新 |
