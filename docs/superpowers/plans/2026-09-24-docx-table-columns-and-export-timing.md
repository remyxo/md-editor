# DOCX 表格列宽对齐 PDF + 导出用时状态栏消息 实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** DOCX 导出表格列宽改为隐藏渲染页按页宽重分配（与 PDF 同构），并在 PDF/DOCX/HTML 导出完成消息中显示用时。

**Architecture:** `table-column-layout.js` 的写回壳（`HtmlExporter::tableColumnNormalizeScript` 内嵌字符串）加 `cfg.px` 分支；新增 `TableColumnNormalizer` 进程级单例隐藏页（仿 `MermaidRenderer`）载入待导出 HTML 跑归一脚本，回传含 px colgroup 的 `body.innerHTML`；`MainWindow::onExportDocx` 编排（归一 → `exportToDocx`，失败降级原 html）；`DocxExporter` 仅加显式 `tblCellMar` 并把页内容宽计算提取到 `ExportCommon`。用时消息经 `QElapsedTimer` + 文件内静态 helper 三处共用。

**Tech Stack:** Qt 6.8.x（兼容基准 6.10.2）/ QtWebEngine / qrc 内联 JS / QTest。

**设计文档:** `docs/superpowers/specs/2026-09-24-docx-table-columns-and-export-timing-design.md`

## Global Constraints

- 所有注释/文档用中文，代码标识符用英文。
- **不 commit**：用户既定规则——目视验证通过前不提交（Task 7 统一提交）。
- **不推进版本号 / CHANGELOG / tag**（用户未要求）。
- 改 C++ 后必须 `cmake --build build` 重编译并确认二进制时间戳新于源文件。
- 代码须过容器 clang 与 MSVC（pre-push 钩子）：**不得 lambda 捕获 static 局部变量**（GCC 宽容掩盖 clang/MSVC 硬错的既训，见 MermaidRenderer.cpp:41-42 注释的规避写法）。
- 测试套件哲学：**ctest 不构造 live QWebEngineView**（test_pdfexporter 注释「从不构造 PdfExporter，不会触发无头渲染」）。归一器逻辑全部以静态函数 + 字符串断言测试；真机链路由用户目检验收。
- 共享脚本的既有子串锚定断言（test_htmlexporter.cpp:258-292）必须逐字保留——`js.contains(...)` 是子串匹配，改动行只要原文子串仍在即通过，但**不得重排锚定行**。

## 与设计文档的三处实现偏差（已论证）

1. **归一编排在 MainWindow 而非 DocxExporter 内部**：若 `exportToDocx` 内部构造隐藏页，test_docxexporter 的全部表格用例将触发无头渲染（违反上一条全局约束）。移到 MainWindow 编排后，`DocxExporter` 输入契约不变（px colgroup html → tblGrid），测试保持同步且无 WebEngine。
2. **无表格快速路径在 C++（MainWindow）而非 JS**：`html.contains("<table")` 门控直接免页面加载，比 spec 写的 JS 侧检测更强（JS 侧 querySelectorAll 空循环本就无害）。
3. **`cfg.px` 分支位置修正**：spec 写「table-column-layout.js 的壳」——壳实际内嵌在 `HtmlExporter::tableColumnNormalizeScript()` 的 C++ 字符串里（该函数运行时读 qrc 内联 js 文件 + 追加壳）。`table-column-layout.js` 本体（测量/分配）不改。

## 文件清单

| 文件 | 动作 | 职责 |
|---|---|---|
| `src/core/HtmlExporter.cpp` | 改 | 壳 `__normalizeTableColumns` 加 `cfg.px` 分支 |
| `tests/test_htmlexporter.cpp` | 改 | px 分支锚定断言 |
| `src/core/ExportCommon.h/.cpp` | 改 | 新增 `pageContentWidthTwips()` |
| `src/core/DocxExporter.cpp` | 改 | 改用共享 helper；tblPr 写显式 `tblCellMar` |
| `src/core/TableColumnNormalizer.h/.cpp` | 新建 | 隐藏页单例归一器 |
| `src/CMakeLists.txt`、`tests/CMakeLists.txt` | 改 | 源清单/测试目标 |
| `tests/test_docxexporter.cpp` | 改 | 三个新用例 |
| `src/ui/MainWindow.cpp` | 改 | DOCX 编排 + 三处用时消息 |

---

### Task 1: 归一壳加 `cfg.px` 分支（colgroup 写 px、跳过单元格内联写回）

**Files:**
- Modify: `src/core/HtmlExporter.cpp:246-261`（壳字符串）与 `:211-214`（cfg 注释块）
- Test: `tests/test_htmlexporter.cpp`（testTableColumnNormalizeScriptShape，约 :267-270 附近）

**Interfaces:**
- Consumes: 无（既有 `tableColumnNormalizeScript()`）
- Produces: 壳支持 `cfg.px`（truthy 时 colgroup 写 `Math.round(w[c2])+'px'`、跳过每行单元格内联百分比写回）；`{px:true, avail, fontSize, padding, fontFamily}` 的调用形态（Task 4 的 `buildNormalizeScript` 依赖）

- [ ] **Step 1: 写失败测试**（testTableColumnNormalizeScriptShape 末尾、`QVERIFY(js.contains("oldCg.remove()"));` 之后追加）

```cpp
        // px 模式（DOCX 经 TableColumnNormalizer）：colgroup 写 px（DocxExporter
        // 解析 width:Npx 生成 tblGrid）、跳过单元格内联百分比写回（DOCX 列宽走
        // tcW，无 paged.js 跨页碎片）；百分比原路径两处锚定子串仍逐字保留
        QVERIFY(js.contains("if(cfg.px){col.style.width=Math.round(w[c2])+'px';}"));
        QVERIFY(js.contains("else col.style.width=(w[c2]/total*100).toFixed(3)+'%';"));
        QVERIFY(js.contains("if(!cfg.px){var rows=table.querySelectorAll('tr');"));
```

- [ ] **Step 2: 跑测试确认失败**

```bash
cmake --build build --target test_htmlexporter -j$(nproc) && ./build/tests/test_htmlexporter testTableColumnNormalizeScriptShape
```
预期：FAIL（新三处 contains 均不命中）。

- [ ] **Step 3: 实现**——`src/core/HtmlExporter.cpp` 两处编辑。

（a）函数头注释块（现 :211-214 `// cfg（可省）：avail 可用宽 px…` 段末尾）追加一行：

```cpp
    // px 模式（DOCX 经 TableColumnNormalizer 传）：colgroup 写 px 而非百分比、
    // 跳过单元格内联百分比写回——DocxExporter 解析 width:Npx 生成 tblGrid
```

（b）壳字符串：**精确替换**（锚定行原文保留，不得重排）——

旧：
```cpp
        "var old=table.querySelector('colgroup');if(old)old.remove();"
        "var cg=document.createElement('colgroup');"
        "for(var c2=0;c2<n;c2++){"
        "var col=document.createElement('col');"
        "col.style.width=(w[c2]/total*100).toFixed(3)+'%';"
        "cg.appendChild(col);"
        "}"
        "table.insertBefore(cg,table.firstChild);"
        "var rows=table.querySelectorAll('tr');"
```
新：
```cpp
        "var old=table.querySelector('colgroup');if(old)old.remove();"
        "var cg=document.createElement('colgroup');"
        "for(var c2=0;c2<n;c2++){"
        "var col=document.createElement('col');"
        "if(cfg.px){col.style.width=Math.round(w[c2])+'px';}"
        "else col.style.width=(w[c2]/total*100).toFixed(3)+'%';"
        "cg.appendChild(col);"
        "}"
        "table.insertBefore(cg,table.firstChild);"
        "if(!cfg.px){var rows=table.querySelectorAll('tr');"
```
随后保持行内循环原文不动，在循环的两行收尾 `"}"` `"}"`（分别闭合内层 for 与 rows for）之后、`"table.style.width='100%';"` 之前**再补一行** `"}"`（闭合 `if(!cfg.px){`）：

```cpp
        "idx+=sp;"
        "}"
        "}"
        "}"
        "table.style.width='100%';"
```

- [ ] **Step 4: 跑测试确认通过**

```bash
cmake --build build --target test_htmlexporter -j$(nproc) && ./build/tests/test_htmlexporter
```
预期：整二进制 PASS（QJSEngine 语法检查随行——壳语法错会在此暴露；既有锚定 :269/:270 因子串仍逐字存在而通过）。

---

### Task 2: `ExportCommon::pageContentWidthTwips()` 共享 helper + DocxExporter 改用

**Files:**
- Modify: `src/core/ExportCommon.h`（kMmToPt 常量块后）、`src/core/ExportCommon.cpp`（文件尾）
- Modify: `src/core/DocxExporter.cpp:1660`（改调 helper）
- Test: `tests/test_docxexporter.cpp`（新用例 + 顶部 include）

**Interfaces:**
- Consumes: 无
- Produces: `int ExportCommon::pageContentWidthTwips()`——读 QSettings（键与 DocxExporter writeDocx 的 sectPr 同源：`export/paperSize`、`export/orientation`、`export/marginLeft`、`export/marginRight`，默认 A4 竖版 1.8cm），返回页内容宽 twips（A4 竖版 1.8cm = 9865）。Task 5 的 MainWindow 调用它传给归一器。

- [ ] **Step 1: 写失败测试**（test_docxexporter.cpp，include 区 `#include "../src/core/DocxExporter.h"` 后补 `#include "../src/core/ExportCommon.h"`；用例放在 testTableColgroupWidthsScaleToPage 之后）

```cpp
    void testPageContentWidthTwipsMatchesSectPr() {
        // 列宽归一 avail 与 writeDocx 的 tblW/tblGrid 基准同源：A4 竖版 1.8cm
        // 边距 = (210−36)mm×56.6929 = 9865 缇；横版取纸高作宽 = (297−36)mm = 14797。
        // QSettings 已重定向临时目录（initTestCase）；用毕清理防污染后续用例
        QSettings settings("md-editor", "md-editor");
        settings.setValue("export/paperSize", static_cast<int>(QPageSize::A4));
        settings.setValue("export/orientation", static_cast<int>(QPageLayout::Portrait));
        settings.setValue("export/marginLeft", 1.8);
        settings.setValue("export/marginRight", 1.8);
        QCOMPARE(ExportCommon::pageContentWidthTwips(), 9865);
        settings.setValue("export/orientation", static_cast<int>(QPageLayout::Landscape));
        QCOMPARE(ExportCommon::pageContentWidthTwips(), 14797);
        settings.remove("export/paperSize");
        settings.remove("export/orientation");
        settings.remove("export/marginLeft");
        settings.remove("export/marginRight");
    }
```

- [ ] **Step 2: 跑测试确认失败**

```bash
cmake --build build --target test_docxexporter -j$(nproc) && ./build/tests/test_docxexporter testPageContentWidthTwipsMatchesSectPr
```
预期：编译失败（pageContentWidthTwips 未定义）。

- [ ] **Step 3: 实现**

`src/core/ExportCommon.h`——`constexpr qreal kMmToPt` 之后、`} // namespace ExportCommon` 之前：

```cpp
// ── DOCX 页面几何（表格列宽归一基准）──────────────────────────────
// 页内容宽（twips）= 纸张宽 − 左右边距。QSettings 键与 DocxExporter writeDocx
// 的 sectPr 声明同源（export/paperSize、export/orientation、export/marginLeft、
// export/marginRight，默认 A4 竖版 1.8cm）。TableColumnNormalizer 的 avail 与
// DocxExporter 的 tblW/tblGrid 共用本值——改导出设置两侧同步变
int pageContentWidthTwips();
```

`src/core/ExportCommon.cpp` 文件尾（include 区按需补 `<QSettings>` `<QPageSize>` `<QPageLayout>`）：

```cpp
int pageContentWidthTwips()
{
    QSettings settings("md-editor", "md-editor");
    const int paper = settings.value("export/paperSize",
                                     static_cast<int>(QPageSize::A4)).toInt();
    const int orient = settings.value("export/orientation",
                                      static_cast<int>(QPageLayout::Portrait)).toInt();
    const double ml = settings.value("export/marginLeft", 1.8).toDouble();
    const double mr = settings.value("export/marginRight", 1.8).toDouble();
    const QSizeF ps = QPageSize(static_cast<QPageSize::PageSizeId>(paper))
                          .size(QPageSize::Millimeter);
    const bool landscape = (orient == static_cast<int>(QPageLayout::Landscape));
    const double pageW = landscape ? ps.height() : ps.width();
    const double contentMm = pageW - (ml + mr) * 10.0;
    // 与 DocxExporter::writeDocx 的 mm2twips 同式（1mm = 56.6929134 缇）
    return contentMm > 0 ? static_cast<int>(qRound(contentMm * 56.6929134)) : 0;
}
```

`src/core/DocxExporter.cpp:1660`——旧：
```cpp
        state.contentWidthTwips = contentMm > 0 ? mm2twips(contentMm) : 0;
```
新：
```cpp
        // 单一来源（ExportCommon）：与 TableColumnNormalizer 的 avail 基准同值
        state.contentWidthTwips = ExportCommon::pageContentWidthTwips();
```
（`contentMm` 局部变量仍被 `maxImageWidthEmu` 使用，保留不删。）

- [ ] **Step 4: 跑测试确认通过**

```bash
cmake --build build --target test_docxexporter -j$(nproc) && ./build/tests/test_docxexporter
```
预期：整二进制 PASS（含既有 testTableColgroupWidthsScaleToPage 的 2466/7399——默认设置路径数值不变）。

---

### Task 3: DocxExporter 写显式 `tblCellMar`（fixed 分支）

**Files:**
- Modify: `src/core/DocxExporter.cpp:1289-1294`（tblPr 收尾）
- Test: `tests/test_docxexporter.cpp`（新用例）

**Interfaces:**
- Consumes: 无
- Produces: 产物 XML 的 tblPr（fixed 分支）含 `<w:tblCellMar><w:left w:w="108" w:type="dxa"/><w:right w:w="108" w:type="dxa"/></w:tblCellMar>`；与 Task 4 归一脚本测量基准 `padding:'0 7.2px'`（108 缇 ÷ 15）自洽。降级分支（无 colgroup）产物 byte 级不变。

- [ ] **Step 1: 写失败测试**（test_docxexporter.cpp，testPageContentWidthTwipsMatchesSectPr 之后）

```cpp
    void testTableFixedLayoutWritesExplicitCellMargins() {
        // 带 colgroup（fixed 分支）：显式 tblCellMar 左右 108 缇（Word 默认值，
        // 纯确定性声明，防主题/版本差异），与归一器测量 padding 7.2px 自洽
        QString xml = exportDocumentXml(
            "<table><colgroup><col style=\"width:300px\"><col style=\"width:358px\">"
            "</colgroup><tbody><tr><th>A</th><th>B</th></tr></tbody></table>");
        QVERIFY(xml.contains("<w:tblLayout w:type=\"fixed\"/>"));
        QVERIFY(xml.contains("<w:tblCellMar><w:left w:w=\"108\" w:type=\"dxa\"/>"
                             "<w:right w:w=\"108\" w:type=\"dxa\"/></w:tblCellMar>"));
        // 无 colgroup（降级路径）：保持现状，不写 fixed/tblCellMar
        QString xml2 = exportDocumentXml(
            "<table><tbody><tr><th>A</th><th>B</th></tr></tbody></table>");
        QVERIFY(!xml2.contains("<w:tblCellMar>"));
        QVERIFY(!xml2.contains("<w:tblLayout w:type=\"fixed\"/>"));
    }
```

- [ ] **Step 2: 跑测试确认失败**

```bash
cmake --build build --target test_docxexporter -j$(nproc) && ./build/tests/test_docxexporter testTableFixedLayoutWritesExplicitCellMargins
```
预期：FAIL（产物无 tblCellMar）。

- [ ] **Step 3: 实现**——`src/core/DocxExporter.cpp`，旧：

```cpp
    if (colDxa.isEmpty()) {
        tblPr = tblPr.arg(0).arg(QStringLiteral("auto"), QString());
    } else {
        tblPr = tblPr.arg(s.contentWidthTwips).arg(QStringLiteral("dxa"),
                      QStringLiteral("<w:tblLayout w:type=\"fixed\"/>"));
    }
```
新：

```cpp
    if (colDxa.isEmpty()) {
        tblPr = tblPr.arg(0).arg(QStringLiteral("auto"), QString());
    } else {
        // tblCellMar 显式声明 Word 默认格边距（左右 108 缇 = 0.19cm；CT_TblPrBase
        // 顺序在 tblLayout 之后）：与 TableColumnNormalizer 测量基准
        // padding '0 7.2px'（108 缇 ÷ 15）自洽，防 Word/WPS 主题差异
        tblPr = tblPr.arg(s.contentWidthTwips).arg(QStringLiteral("dxa"),
                      QStringLiteral("<w:tblLayout w:type=\"fixed\"/>"
                                     "<w:tblCellMar>"
                                     "<w:left w:w=\"108\" w:type=\"dxa\"/>"
                                     "<w:right w:w=\"108\" w:type=\"dxa\"/>"
                                     "</w:tblCellMar>"));
    }
```

- [ ] **Step 4: 跑测试确认通过**

```bash
cmake --build build --target test_docxexporter -j$(nproc) && ./build/tests/test_docxexporter
```
预期：整二进制 PASS（既有 2466/7399 等数值断言不受影响——tblCellMar 不改列宽计算）。

---

### Task 4: TableColumnNormalizer 隐藏页单例（新文件）

> **执行中变更（2026-09-24 用户裁决）**：归一器并发竞态以 request-id 协议修复——`normalize()` 返回 `quint64` 请求序号（busy 返 0 且**不发信号**，降级决策上移调用方）；`finished(quint64 requestId, const QString&)` 调用方按 id 匹配消费；看门狗携代 id（`id != m_seq` 挡旧代定时器误触发）。本节下方代码块为初版形态，以代码为准。

**Files:**
- Create: `src/core/TableColumnNormalizer.h`、`src/core/TableColumnNormalizer.cpp`
- Modify: `src/CMakeLists.txt`（core 源清单，`core/HtmlExporter.cpp` 之后）、`tests/CMakeLists.txt`（test_docxexporter 目标）
- Test: `tests/test_docxexporter.cpp`（新用例 + include）

**Interfaces:**
- Consumes: `HtmlExporter::tableColumnNormalizeScript()`（public static，单一来源）；Task 1 的 `cfg.px`
- Produces:
  - `TableColumnNormalizer::instance()`——进程级单例
  - `void normalize(const QString &html, const QString &docDir, int availTwips)`——异步
  - `signal finished(const QString &normalizedHtml)`——空串 = 失败/降级信号
  - `static QString buildNormalizeScript(int availTwips)`——纯函数供测试
  - Task 5 的 MainWindow 消费上述全部

- [ ] **Step 1: 写失败测试**（test_docxexporter.cpp：include 区补 `#include "../src/core/TableColumnNormalizer.h"`；用例放 testTableFixedLayoutWritesExplicitCellMargins 之后）

```cpp
    void testBuildNormalizeScriptShape() {
        // DOCX 基准（单一来源脚本在前，壳调用携 px:true）：
        // avail = 9865 缇 ÷ 15 = 657.7px；11pt = Normal 22 半磅；
        // '0 7.2px' = tblCellMar 108 缇；字族与 PDF 同栈（CJK 等宽 1em 主导，
        // ASCII 的 DejaVu 略宽于 Arial——测量偏保守方向）；
        // IIFE 同步返回 body.innerHTML（非 Promise，runJavaScript 可直接取回字符串）
        const QString js = TableColumnNormalizer::buildNormalizeScript(9865);
        QVERIFY(js.contains("__measureTableColumns"));
        QVERIFY(js.contains("__allocateTableColumns"));
        QVERIFY(js.contains("window.__normalizeTableColumns({px:true,avail:657.7,"));
        QVERIFY(js.contains("fontSize:'11pt'"));
        QVERIFY(js.contains("padding:'0 7.2px'"));
        QVERIFY(js.contains("fontFamily:'Noto Sans SC,DejaVu Sans,sans-serif'});"));
        QVERIFY(js.endsWith("return document.body?document.body.innerHTML:'';"
                            "})()"));
    }
```

- [ ] **Step 2: 跑测试确认失败**

```bash
cmake --build build --target test_docxexporter -j$(nproc) && ./build/tests/test_docxexporter testBuildNormalizeScriptShape
```
预期：编译失败（头文件不存在）。

- [ ] **Step 3: 实现新文件**

`src/core/TableColumnNormalizer.h`：

```cpp
#ifndef MD_EDITOR_TABLECOLUMNNORMALIZER_H
#define MD_EDITOR_TABLECOLUMNNORMALIZER_H

#include <QObject>
#include <QString>
#include <QUrl>

class QWebEngineView;

// 离屏 QWebEngine 载入待导出 HTML，按 DOCX 基准跑表格列宽归一（与 PDF 导出
// 同一份 table-column-layout.js，经 HtmlExporter::tableColumnNormalizeScript
// 内联），回传含 px colgroup 的 body.innerHTML 供 DocxExporter 生成 tblGrid。
// 编辑器 live colgroup 的比例是编辑器窗口基准，缩放到 Word 页宽后短列装不下
// 11pt 字（一字一行）、长列独占——归一在目标页宽下重测重分配，与 PDF 同构。
// 异步，结果经 finished 回传；空串 = 失败/降级信号（调用侧用原 html 走旧路径）
class TableColumnNormalizer : public QObject {
    Q_OBJECT
public:
    explicit TableColumnNormalizer(QObject *parent = nullptr);
    ~TableColumnNormalizer() override;

    // 进程级单例（同 MermaidRenderer：隐藏视图常驻免冷启动，aboutToQuit 销毁）
    static TableColumnNormalizer *instance();

    // 归一 HTML 中的表格列宽。availTwips：DOCX 页内容宽（ExportCommon::
    // pageContentWidthTwips，JS 侧 ÷15 转 px）；docDir：相对图片解析基准
    //（表内图片参与 min/max 测量；空 = 未保存文档，无相对图片）
    void normalize(const QString &html, const QString &docDir, int availTwips);

    // 归一脚本（共享脚本 + DOCX 基准 cfg 拼装），拆静态便于单测断言 cfg 参数
    static QString buildNormalizeScript(int availTwips);

signals:
    void finished(const QString &normalizedHtml);   // 空串 = 降级

private slots:
    void onLoadFinished(bool ok);

private:
    void settle(const QString &html);   // 结算（与 30s 看门狗互斥，防 finished 双发）

    QWebEngineView *m_view = nullptr;
    QString m_pending;          // 待归一 HTML（loadFinished 后消费）
    QUrl m_baseUrl;             // 相对图片解析基准
    int m_availTwips = 0;
    bool m_finished = false;    // 看门狗与正常结算的互斥
    bool m_busy = false;        // 单例并发防御（busy 时新请求立即降级）
};

#endif // MD_EDITOR_TABLECOLUMNNORMALIZER_H
```

`src/core/TableColumnNormalizer.cpp`：

```cpp
#include "TableColumnNormalizer.h"
#include "ExportCommon.h"
#include "HtmlExporter.h"

#include <QPointer>
#include <QTimer>
#include <QCoreApplication>
#include <QWebEngineView>
#include <QWebEnginePage>
#include <QWebEngineSettings>

TableColumnNormalizer::TableColumnNormalizer(QObject *parent)
    : QObject(parent), m_view(new QWebEngineView())
{
    m_view->setVisible(false);
    m_view->settings()->setAttribute(QWebEngineSettings::LocalContentCanAccessFileUrls, true);
    connect(m_view, &QWebEngineView::loadFinished, this, &TableColumnNormalizer::onLoadFinished);
}

TableColumnNormalizer::~TableColumnNormalizer() { delete m_view; }

TableColumnNormalizer *TableColumnNormalizer::instance()
{
    static QPointer<TableColumnNormalizer> s_inst;
    if (s_inst.isNull()) {
        s_inst = new TableColumnNormalizer();
        // aboutToQuit 主动销毁：QWebEngineView 须先于 WebEngine 全局回收删除
        //（同 MermaidRenderer；static 局部变量免捕获——lambda 捕获 static 存储
        // 对象是 clang 硬错、GCC 宽容的既训）
        QObject::connect(qApp, &QCoreApplication::aboutToQuit, qApp, []() {
            delete s_inst;   // QPointer 随之置空
        });
    }
    return s_inst;
}

QString TableColumnNormalizer::buildNormalizeScript(int availTwips)
{
    // 与 PDF __fixTableColumns 同构：单一来源脚本在前，随后壳调用。
    // px:true——colgroup 写 px（DocxExporter 解析 width:Npx 生成 tblGrid）、
    // 跳过单元格内联百分比写回（DOCX 列宽走 tcW，无 paged.js 跨页碎片）。
    // 11pt = DOCX Normal 22 半磅（PDF 是 10pt，各按各的基准）；
    // '0 7.2px' 对应显式 tblCellMar 108 缇（108 ÷ 15 = 7.2）；
    // avail 单位换算 twips → px（96dpi：1px = 15 缇）。
    // IIFE 同步返回 body.innerHTML（非 Promise，runJavaScript 可直接取回字符串；
    // Promise 的 resolve 值会被序列化成空 QVariantMap——MermaidRenderer 既训）
    return HtmlExporter::tableColumnNormalizeScript()
         + QStringLiteral(
               "(function(){"
               "window.__normalizeTableColumns({px:true,avail:%1,fontSize:'11pt',"
               "padding:'0 7.2px',fontFamily:'Noto Sans SC,DejaVu Sans,sans-serif'});"
               "return document.body?document.body.innerHTML:'';"
               "})()").arg(QString::number(availTwips / 15.0, 'f', 1));
}

void TableColumnNormalizer::normalize(const QString &html, const QString &docDir,
                                      int availTwips)
{
    // 单例并发防御：上一轮未结算时本轮直接降级（同 MermaidRenderer）
    if (m_busy) { emit finished(QString()); return; }
    m_busy = true;
    m_pending = html;
    m_availTwips = availTwips;
    m_baseUrl = docDir.isEmpty()
                    ? QUrl()
                    : QUrl::fromLocalFile(docDir + QStringLiteral("/"));
    m_finished = false;
    // 30s 看门狗：loadFinished 永不触发（渲染进程僵死）时导出无声失联——
    // 超时按降级结算（调用侧用原 html 走旧路径），对齐 MermaidRenderer 范式
    QTimer::singleShot(30000, this, [this]() {
        if (m_finished) return;
        settle(QString());
    });
    m_view->setHtml(m_pending, m_baseUrl);
}

void TableColumnNormalizer::onLoadFinished(bool ok)
{
    if (!ok) { settle(QString()); return; }
    if (m_pending.isEmpty()) return;   // 单例视图初建时的首次加载，无任务
    m_view->page()->runJavaScript(buildNormalizeScript(m_availTwips),
                                  [this](const QVariant &res) {
                                      // JS 异常 → res 无效 → toString 空 → 降级
                                      settle(res.toString());
                                  });
}

void TableColumnNormalizer::settle(const QString &html)
{
    if (m_finished) return;
    m_finished = true;
    m_busy = false;
    m_pending.clear();
    emit finished(html);
}
```

注意：`buildNormalizeScript` 归一结果为空串还有一种正常情况——文档本身 body 为空；调用侧（MainWindow）把空串一律按降级处理（原 html 导出），语义一致。

- [ ] **Step 4: 挂构建**——`src/CMakeLists.txt` core 清单 `core/HtmlExporter.cpp` 之后加一行 `core/TableColumnNormalizer.cpp`；`tests/CMakeLists.txt` test_docxexporter 目标源列表改为：

```cmake
add_executable(test_docxexporter test_docxexporter.cpp ../src/core/DocxExporter.cpp ../src/core/miniz/miniz.c ../src/core/MermaidRenderer.cpp ../src/core/MathMlRenderer.cpp ../src/core/MathMlConverter.cpp ../src/core/ExportCommon.cpp ../src/core/TableColumnNormalizer.cpp ../src/core/HtmlExporter.cpp ../src/resources/app.qrc)
```
并把其 target_link_libraries 注释补一句：`# TableColumnNormalizer/HtmlExporter：前者链 WebEngine（仅静态函数入测），后者 readResource 需 app.qrc`。

- [ ] **Step 5: 跑测试确认通过**

```bash
cmake --build build --target test_docxexporter -j$(nproc) && ./build/tests/test_docxexporter
```
预期：整二进制 PASS（不构造归一器实例——仅断言 buildNormalizeScript 字符串，无头渲染零触发）。

---

### Task 5: MainWindow 编排——DOCX 接归一器 + 三处导出用时消息

> **执行中变更（2026-09-24 用户裁决）**：归一段改为「先取 id 再连接」——`normId == 0`（busy）就地 `exportToDocx(html, …)` 一次并 return；连接 `finished` 后按 `requestId != normId → return` 只消费本单结果（lambda 捕获取消未用 `this`）。本节下方代码块为初版形态，以代码为准。

**Files:**
- Modify: `src/ui/MainWindow.cpp`（include 区、getEditorHtmlAsync 附近 helper、onExportPdf/onExportDocx/onExportHtml 三函数）

**Interfaces:**
- Consumes: `TableColumnNormalizer::instance()/normalize()/finished`（Task 4）、`ExportCommon::pageContentWidthTwips()`（Task 2）、`getEditorHtmlAsync(tab, cb, /*forDocx=*/true)`（既有，含降级路径依赖的 live colgroup）
- Produces: 无下游依赖（UI 终点）；状态栏消息格式 `已导出: <文件>, 用时 <N 毫秒|X.Y 秒>`

- [ ] **Step 1: include 与 helper**

include 区（`#include "../core/HtmlExporter.h"` 之后）补：

```cpp
#include "../core/ExportCommon.h"
#include "../core/TableColumnNormalizer.h"
```
以及 Qt 区补 `#include <memory>`（shared_ptr 计时器跨异步层存活）。

`getEditorHtmlAsync`（MainWindow.cpp:908）之前加文件内静态 helper：

```cpp
// 导出用时后缀：<1s 整数毫秒；≥1s 秒（四舍五入 1 位小数）。
// 用 translate 而非 tr：文件内静态自由函数无类 tr 上下文，显式挂 MainWindow
static QString exportTimeSuffix(qint64 elapsedMs)
{
    if (elapsedMs < 1000)
        return QCoreApplication::translate("MainWindow", "%1 毫秒").arg(elapsedMs);
    return QCoreApplication::translate("MainWindow", "%1 秒")
        .arg(QString::number(elapsedMs / 1000.0, 'f', 1));
}
```

- [ ] **Step 2: onExportDocx 重写**（MainWindow.cpp:1002-1034 整函数体替换）

```cpp
void MainWindow::onExportDocx()
{
    EditorTab *tab = m_tabWidget->currentEditorTab();
    if (!tab) return;

    QString filePath = tab->webView()->property("filePath").toString();
    QString path = QFileDialog::getSaveFileName(this, tr("导出 DOCX"),
                                                exportDefaultName(filePath, "docx"),
                                                tr("Word 文档 (*.docx)"));
    if (path.isEmpty()) return;

    // 文档目录（解析相对图片路径）；未保存文档无相对图片（插入图片须先保存），空 filePath 回退 CWD 亦无影响
    QString docDir = QFileInfo(filePath).absolutePath();

    // 异步获取编辑器 HTML 内容（源码模式下取 source-view 当前内容经 toHtml，
    // 否则导出的是进入源码前的旧快照）
    getEditorHtmlAsync(tab, [this, path, docDir](const QString &html) {
            // 用时从内容就绪（进度起点）起算：含列宽归一 + mermaid/公式 + 写盘
            auto start = std::make_shared<QElapsedTimer>();
            start->start();
            auto *exporter = new DocxExporter(this);
            // 失败路径（空内容/生成失败/mermaid 挂死）须有反馈且回收导出器
            connect(exporter, &DocxExporter::exportError, this,
                    [this, exporter](const QString &err) {
                        m_statusBar->showMessage(tr("导出失败: %1").arg(err), 5000);
                        exporter->deleteLater();
                    });
            connect(exporter, &DocxExporter::exportFinished, this,
                    [this, exporter, start](bool ok, const QString &out) {
                        m_statusBar->showMessage(
                            ok ? tr("已导出: %1, 用时 %2").arg(out, exportTimeSuffix(start->elapsed()))
                               : tr("导出失败"), 3000);
                        exporter->deleteLater();
                    });
            // 表格列宽归一（与 PDF 同构）：编辑器 live colgroup 是窗口宽基准，
            // 直接缩放到 Word 页宽会短列一字一行/长列独占——经隐藏页按 DOCX
            // 基准（页宽 avail/11pt/tblCellMar 边距）重测重分配。无表格直通
            // （免页面加载）；归一失败（空串）降级原 html（live colgroup 缩放
            // 路径仍在 DocxExporter 内，行为同改造前）
            if (!html.contains(QLatin1String("<table"))) {
                exporter->exportToDocx(html, docDir, path);
                return;
            }
            auto *norm = TableColumnNormalizer::instance();
            auto conn = std::make_shared<QMetaObject::Connection>();
            *conn = connect(norm, &TableColumnNormalizer::finished, this,
                    [this, conn, exporter, html, docDir, path](const QString &normalized) {
                        QObject::disconnect(*conn);   // 单发：只消费一次结果
                        exporter->exportToDocx(normalized.isEmpty() ? html : normalized,
                                               docDir, path);
                    });
            norm->normalize(html, docDir, ExportCommon::pageContentWidthTwips());
        },
        /*forDocx=*/true);
}
```

- [ ] **Step 3: onExportPdf 计时**——lambda 体首行（`getEditorHtmlAsync(tab, [this, path, docDir](const QString &html) {` 之后、`QPointer<ExportProgressDialog> dlg…` 之前）加：

```cpp
            // 用时从内容就绪（进度对话框出现）起算，到 exportFinished
            auto start = std::make_shared<QElapsedTimer>();
            start->start();
```
exportFinished 连接（MainWindow.cpp:961）改为捕获 `start` 并改两处成功消息：

```cpp
            connect(exporter, &PdfExporter::exportFinished, this,
                    [this, exporter, dlg, start](bool ok, const QString &out) {
                // 本条是状态栏最后一条消息：走回退时并入回退说明并延长展示——
                // pagedFallbackUsed 的独立提示早于 printToPdf 触发，1-3s 后即被
                // 本条覆盖，用户须从这里得知产物无页眉页脚
                if (ok && exporter->fallbackUsed())
                    m_statusBar->showMessage(
                        tr("已导出: %1（分页引擎回退，本次无页眉页脚）, 用时 %2")
                            .arg(out, exportTimeSuffix(start->elapsed())), 6000);
                else
                    m_statusBar->showMessage(
                        ok ? tr("已导出: %1, 用时 %2").arg(out, exportTimeSuffix(start->elapsed()))
                           : tr("导出失败"), 3000);
                if (dlg) {
                    if (ok)
                        dlg->showFinished();
                    else
                        dlg->showError(tr("PDF 生成失败"));
                }
                exporter->deleteLater();
            });
```

- [ ] **Step 4: onExportHtml 计时**——同型：lambda 体首行加 `start`；exportFinished 连接改为：

```cpp
            connect(exporter, &HtmlExporter::exportFinished, this,
                    [this, exporter, start](bool ok, const QString &out) {
                        m_statusBar->showMessage(
                            ok ? tr("已导出: %1, 用时 %2").arg(out, exportTimeSuffix(start->elapsed()))
                               : tr("导出失败"), 3000);
                        exporter->deleteLater();
                    });
```

- [ ] **Step 5: 编译验证**（MainWindow 无单测二进制，编译 + 全量测试代把关）

```bash
cmake --build build -j$(nproc)
```
预期：编译零警告通过（GCC 本地；clang/MSVC 由 pre-push 钩子把关——本任务无 lambda 捕获 static、无窄化转换风险点）。

---

### Task 6: 全量构建 + 全量测试 + 二进制新鲜度

- [ ] **Step 1: 全量构建与测试**

```bash
cmake --build build -j$(nproc) && ctest --test-dir build --output-on-failure
```
预期：全部测试 PASS。重点关注 test_htmlexporter / test_docxexporter / test_pdfexporter（共享脚本三链路回归）。

- [ ] **Step 2: 二进制新鲜度确认**（CLAUDE.md 规则 9：告知「已修复」前必须确认）

```bash
ls -la build/src/md-editor && find src -newer build/src/md-editor -name "*.cpp" -o -newer build/src/md-editor -name "*.js" -o -newer build/src/md-editor -name "*.h" | grep -v TableColumnNormalizer || echo "二进制新鲜"
```
预期：输出「二进制新鲜」（无源文件新于二进制）。注意：`table-column-layout.js` 未改（改动在 C++ 内嵌字符串），qrc 无需单独处理。

---

### Task 7: 用户验证 + 提交（验证通过后才执行）

- [x] **Step 1: 用户真机目检**（`./scripts/run-editor.sh`）——2026-10-14 用户验证通过

1. `~/Desktop/复杂场景综合测试.md` 导出 DOCX：1.1 表「001」「已完成」等短列不折行；1.2 表列07 让宽、其余列不拥挤；与 PDF 导出目测一致。
2. 同文档导出 PDF/HTML：状态栏显示「已导出: XXX, 用时 N 毫秒」（小文档）或「X.Y 秒」（大 PDF 文档）；PDF 回退路径（如触发）组合消息同样带用时。
3. 源码模式导出 DOCX：表格列宽与所见即所得模式一致。
4. 无表格文档导出 DOCX：秒回（快速路径）。

- [x] **Step 2: macOS Word 双击验证**（既定流程：WPS/LibreOffice 宽容测不出 schema 违规；重点看 tblCellMar/tblGrid/tcW 的 Word 兼容性）——2026-10-14 用户确认通过

- [x] **Step 3: 用户确认后分两个 commit 提交**（已执行：`45fbc37` 列宽重分配、`b44c0ee` 用时消息；2026-10-14 验收收口）

```bash
git add src/core/HtmlExporter.cpp src/core/ExportCommon.h src/core/ExportCommon.cpp \
        src/core/DocxExporter.cpp src/core/TableColumnNormalizer.h src/core/TableColumnNormalizer.cpp \
        src/CMakeLists.txt tests/CMakeLists.txt tests/test_htmlexporter.cpp tests/test_docxexporter.cpp \
        src/ui/MainWindow.cpp \
        docs/superpowers/specs/2026-09-24-docx-table-columns-and-export-timing-design.md \
        docs/superpowers/plans/2026-09-24-docx-table-columns-and-export-timing.md
```

提交一（表格列宽，含编排与用时消息所在文件按 hunk 归属说明——MainWindow 同文件两逻辑，先整体提交列宽主题亦可）：

```bash
git commit -m "fix: DOCX 表格列宽经隐藏渲染页按页宽重分配（与 PDF 同构）

编辑器 live colgroup 是窗口宽基准，等比缩放到 Word 页宽后短列一字一行、
长列独占。新增 TableColumnNormalizer 单例隐藏页，按 DOCX 基准（页宽
avail/11pt/tblCellMar 108 缇）跑共享 table-column-layout.js 重测重分配；
归一脚本加 cfg.px 分支（HTML/PDF 不传零变化）；DocxExporter 写显式
tblCellMar 并改用 ExportCommon::pageContentWidthTwips 单一来源。

Co-Authored-By: Claude Code <noreply@anthropic.com>"
```

提交二：若用时消息改动与列宽改动在 MainWindow.cpp 中难以按 hunk 拆分（同函数体交织），并入提交一并在标题体现；否则：

```bash
git commit -m "feat: 导出完成状态栏显示用时（毫秒/秒两档）

PDF/DOCX/HTML 导出 exportFinished 消息追加耗时：<1s 整数毫秒，
≥1s 秒（四舍五入 1 位）；PDF 分页回退组合消息同带用时。

Co-Authored-By: Claude Code <noreply@anthropic.com>"
```

（版本号/CHANGELOG/tag：用户未要求，不推进。）

---

## Self-Review 记录

- **Spec 覆盖**：设计一（数据流/px 分支/tblCellMar/降级/快速路径/源码模式）→ Task 1-5；设计二（计时/格式/回退消息）→ Task 5；验证方案 → Task 6-7。✓
- **占位符**：无 TBD/TODO；全部代码步骤带完整代码块。✓
- **类型一致性**：`pageContentWidthTwips()` 返回 int，Task 5 以 `int` 传入 `normalize(html, docDir, int)`；`buildNormalizeScript(int)`；`finished(const QString&)` 三处一致；测试锚定字符串与实现字符串逐字对照（`if(cfg.px){col.style.width=Math.round(w[c2])+'px';}` / `else col.style.width=…toFixed(3)+'%';` / `if(!cfg.px){var rows=…` / `window.__normalizeTableColumns({px:true,avail:657.7,`）。✓
- **数值核算**：A4 竖版 1.8cm → (210−36)×56.6929134 = 9864.57 → 9865 ✓；横版 (297−36)×56.6929134 = 14796.83 → 14797 ✓；9865÷15 = 657.67 → 'f',1 = "657.7" ✓；108 缇 ÷ 15 = 7.2px ✓。
