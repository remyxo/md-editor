# DOCX 导出页眉页脚实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** DOCX 导出支持页眉（文件名基名居中）与页脚（「第 X 页 / 共 Y 页」右对齐，PAGE/NUMPAGES 活字段），独立开关默认开，全关时产物与现状语义等价。

**Architecture:** 延续 `DocxExporter` 手工组装 OOXML 的既有范式——新增 `word/header1.xml`/`word/footer1.xml` 两个手写字符串部件，在 `writeDocx()` 组装阶段完成 sectPr 引用、rels、Content_Types、zip 条目四处接线。Word 原生渲染页眉页脚，无需分页引擎。规格见 `docs/superpowers/specs/2026-09-12-docx-header-footer-design.md`。

**Tech Stack:** Qt 6.8+（QSettings/QXmlStreamReader/QProcess）、miniz（既有 zip 组装）、Qt Test。

## Global Constraints

- 所有注释与文档中文；代码标识符英文。
- Qt 兼容基准 6.10.2（最低 6.8.2）。本特性纯自有 OOXML 字符串生成，不涉 Qt 序列化行为，无占位符保护需求。
- 改 C++ 源码后必须 `cmake --build build` 重编译再验证（qrc/二进制时戳规则）。
- 提交规范：`feat:`/`chore:` 前缀 + 中文描述。**不推版本号**——收口（CMakeLists/CHANGELOG/tag）留待用户决定（feat 增量建议 +0.1.0）。
- DOCX schema 违规只在真 Word 显形（WPS/LibreOffice 宽容）：放行门为 macOS Word 双击 A/B 取证。
- 测试内写 QSettings 必须 save/restore（initTestCase 已重定向 NativeFormat 到临时目录；restore 时 `setValue(key, null QVariant)` 即移除键回落默认，沿用 `testLargeImageShrinksToPageWidth` 既有模式）。
- 测试解包 **不得用 `unzip -p out [Content_Types].xml`**——unzip 参数是 glob 模式，`[...]` 是字符类；必须 `unzip -o out -d 目录` 整包解压后按文件读。

---

### Task 1: 页眉页脚部件生成 + 开关组合单测

**Files:**
- Modify: `src/core/DocxExporter.cpp`（新增 `makeHeaderXml`/`makeFooterXml`，改 `writeDocx()` 四处接线）
- Test: `tests/test_docxexporter.cpp`

**Interfaces:**
- Consumes: `writeDocx()` 既有管线（settings 读取块、`docRelsXml`/`contentTypes` 组装、zip `add` lambda、`xmlEscape`/`xmlSanitize` 既有静态函数）。
- Produces: 导出行为变化（无新对外 API）。测试经 `exportToDocx()` + 解包断言；后续 Task 依赖的设置键：`export/docxHeaderEnabled` / `export/docxFooterEnabled`（默认 true）。

- [ ] **Step 1: 写失败测试——测试基建 + 四种开关组合 + 转义 + 良构**

`tests/test_docxexporter.cpp` 顶部 include 区（`#include <QSettings>` 之后）补：

```cpp
#include <QMap>
#include <QXmlStreamReader>
```

`private slots:` 之前（`exportDocumentXml` helper 之后）加 helper：

```cpp
    // 导出 HTML 为 docx 并整包解压，返回 {部件路径 → 内容}；不存在的部件不在 map 中。
    // 注：不得用 `unzip -p out [Content_Types].xml`——unzip 参数是 glob 模式，
    // `[...]` 是字符类匹配不到字面名，必须 -o 解压后按文件读。
    static QMap<QString, QString> exportParts(const QString &html,
                                               const QStringList &partNames,
                                               const QString &outFileName = QStringLiteral("out.docx")) {
        QMap<QString, QString> out;
        QTemporaryDir dir;
        if (!dir.isValid()) return out;
        const QString outPath = dir.path() + "/" + outFileName;
        DocxExporter ex;
        ex.exportToDocx(html, dir.path(), outPath);
        if (!QFile::exists(outPath)) return out;
        QProcess unzip;
        unzip.start("unzip", QStringList() << "-o" << outPath << "-d" << dir.path() + "/x");
        if (!unzip.waitForFinished(10000)) return out;
        for (const QString &p : partNames) {
            QFile f(dir.path() + "/x/" + p);
            if (f.open(QIODevice::ReadOnly))
                out.insert(p, QString::fromUtf8(f.readAll()));
        }
        return out;
    }
```

`private slots:` 内追加五个用例（放在 `testBasicInlineFormatting` 之后即可）：

```cpp
    // === DOCX 页眉页脚（export/docxHeaderEnabled / docxFooterEnabled，默认开）===

    // 双开：两部件齐全 + sectPr 引用先于 pgSz（CT_SectPr schema 顺序）+ rels/Content_Types 接线 + 良构
    void testDocxHeaderFooterBothEnabled() {
        QSettings s("md-editor", "md-editor");
        const QVariant oldH = s.value("export/docxHeaderEnabled");
        const QVariant oldF = s.value("export/docxFooterEnabled");
        s.setValue("export/docxHeaderEnabled", true);
        s.setValue("export/docxFooterEnabled", true);
        s.sync();

        auto parts = exportParts("<p>hello</p>",
            {"word/document.xml", "word/header1.xml", "word/footer1.xml",
             "word/_rels/document.xml.rels", "[Content_Types].xml"},
            QStringLiteral("测试文档.docx"));
        QVERIFY(parts.contains("word/document.xml"));
        QVERIFY(parts.contains("word/header1.xml"));
        QVERIFY(parts.contains("word/footer1.xml"));

        // sectPr 引用存在且位于 pgSz 之前
        const QString doc = parts["word/document.xml"];
        const int hdrPos = doc.indexOf("<w:headerReference w:type=\"default\" r:id=\"rIdHeader1\"/>");
        const int ftrPos = doc.indexOf("<w:footerReference w:type=\"default\" r:id=\"rIdFooter1\"/>");
        const int pgSzPos = doc.indexOf("<w:pgSz");
        QVERIFY(hdrPos >= 0 && ftrPos >= 0);
        QVERIFY(pgSzPos > hdrPos && pgSzPos > ftrPos);

        // 页眉：居中 + 标题 = 导出文件名基名 + 9pt 灰
        const QString hdr = parts["word/header1.xml"];
        QVERIFY(hdr.contains("<w:jc w:val=\"center\"/>"));
        QVERIFY(hdr.contains("测试文档"));
        QVERIFY(hdr.contains("<w:color w:val=\"808080\"/><w:sz w:val=\"18\"/>"));

        // 页脚：右对齐 + PAGE/NUMPAGES 简单字段 + 「第 X 页 / 共 Y 页」文案
        const QString ftr = parts["word/footer1.xml"];
        QVERIFY(ftr.contains("<w:jc w:val=\"right\"/>"));
        QVERIFY(ftr.contains("w:instr=\" PAGE   \\* MERGEFORMAT \""));
        QVERIFY(ftr.contains("w:instr=\" NUMPAGES   \\* MERGEFORMAT \""));
        QVERIFY(ftr.contains("第 ") && ftr.contains(" 页 / 共 ") && ftr.contains(" 页"));

        // 两部件 XML 良构
        for (const QString &x : {hdr, ftr}) {
            QXmlStreamReader r(x);
            while (!r.atEnd()) r.readNext();
            QVERIFY2(!r.hasError(), qPrintable(r.errorString()));
        }

        // rels 与 Content_Types 接线
        QVERIFY(parts["word/_rels/document.xml.rels"]
                    .contains("relationships/header\" Target=\"header1.xml\""));
        QVERIFY(parts["word/_rels/document.xml.rels"]
                    .contains("relationships/footer\" Target=\"footer1.xml\""));
        QVERIFY(parts["[Content_Types].xml"].contains(
            "/word/header1.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.header+xml\""));
        QVERIFY(parts["[Content_Types].xml"].contains(
            "/word/footer1.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.footer+xml\""));

        s.setValue("export/docxHeaderEnabled", oldH);   // null QVariant → 移除键，回落默认
        s.setValue("export/docxFooterEnabled", oldF);
    }

    // 只开页眉：footer 部件/引用/字段全无
    void testDocxHeaderOnly() {
        QSettings s("md-editor", "md-editor");
        const QVariant oldH = s.value("export/docxHeaderEnabled");
        const QVariant oldF = s.value("export/docxFooterEnabled");
        s.setValue("export/docxHeaderEnabled", true);
        s.setValue("export/docxFooterEnabled", false);
        s.sync();

        auto parts = exportParts("<p>x</p>",
            {"word/document.xml", "word/header1.xml", "word/footer1.xml"});
        QVERIFY(parts.contains("word/header1.xml"));
        QVERIFY(!parts.contains("word/footer1.xml"));
        QVERIFY(parts["word/document.xml"].contains("r:id=\"rIdHeader1\""));
        QVERIFY(!parts["word/document.xml"].contains("footerReference"));
        QVERIFY(!parts["word/document.xml"].contains("NUMPAGES"));

        s.setValue("export/docxHeaderEnabled", oldH);
        s.setValue("export/docxFooterEnabled", oldF);
    }

    // 只开页脚：header 部件/引用全无
    void testDocxFooterOnly() {
        QSettings s("md-editor", "md-editor");
        const QVariant oldH = s.value("export/docxHeaderEnabled");
        const QVariant oldF = s.value("export/docxFooterEnabled");
        s.setValue("export/docxHeaderEnabled", false);
        s.setValue("export/docxFooterEnabled", true);
        s.sync();

        auto parts = exportParts("<p>x</p>",
            {"word/document.xml", "word/header1.xml", "word/footer1.xml"});
        QVERIFY(!parts.contains("word/header1.xml"));
        QVERIFY(parts.contains("word/footer1.xml"));
        QVERIFY(!parts["word/document.xml"].contains("headerReference"));
        QVERIFY(parts["word/document.xml"].contains("r:id=\"rIdFooter1\""));

        s.setValue("export/docxHeaderEnabled", oldH);
        s.setValue("export/docxFooterEnabled", oldF);
    }

    // 全关：零注入，sectPr 直接跟 pgSz（与改造前语义等价，回归锁定）
    void testDocxHeaderFooterAllDisabled() {
        QSettings s("md-editor", "md-editor");
        const QVariant oldH = s.value("export/docxHeaderEnabled");
        const QVariant oldF = s.value("export/docxFooterEnabled");
        s.setValue("export/docxHeaderEnabled", false);
        s.setValue("export/docxFooterEnabled", false);
        s.sync();

        auto parts = exportParts("<p>x</p>",
            {"word/document.xml", "word/header1.xml", "word/footer1.xml",
             "word/_rels/document.xml.rels", "[Content_Types].xml"});
        QVERIFY(!parts.contains("word/header1.xml"));
        QVERIFY(!parts.contains("word/footer1.xml"));
        QVERIFY(parts["word/document.xml"].contains("<w:sectPr><w:pgSz"));
        QVERIFY(!parts["word/_rels/document.xml.rels"].contains("header1.xml"));
        QVERIFY(!parts["[Content_Types].xml"].contains("header+xml"));

        s.setValue("export/docxHeaderEnabled", oldH);
        s.setValue("export/docxFooterEnabled", oldF);
    }

    // 标题含 XML 特殊字符：页眉文本经 xmlEscape，无注入
    void testDocxHeaderTitleEscaped() {
        QSettings s("md-editor", "md-editor");
        const QVariant oldH = s.value("export/docxHeaderEnabled");
        const QVariant oldF = s.value("export/docxFooterEnabled");
        s.setValue("export/docxHeaderEnabled", true);
        s.setValue("export/docxFooterEnabled", false);
        s.sync();

        auto parts = exportParts("<p>x</p>", {"word/header1.xml"},
                                 QStringLiteral("a&b<c.docx"));
        const QString hdr = parts.value("word/header1.xml");
        QVERIFY(hdr.contains("a&amp;b&lt;c"));
        QVERIFY(!hdr.contains("a&b<c"));

        s.setValue("export/docxHeaderEnabled", oldH);
        s.setValue("export/docxFooterEnabled", oldF);
    }
```

- [ ] **Step 2: 跑测试确认全红**

```bash
cmake --build build --target test_docxexporter -j$(nproc) && ./build/tests/test_docxexporter testDocxHeader -platform offscreen 2>&1 | tail -20
```

预期：5 个新用例 FAIL（`exportParts` 返回的 map 无 header/footer 部件、document.xml 无引用），既有用例 PASS。
（注：测试二进制名以 `build/tests/` 下实际为准；`-platform offscreen` 防 headless 环境弹窗。）

- [ ] **Step 3: 实现——两部件生成函数**

`src/core/DocxExporter.cpp` 的 `makeStylesXml()` 之前加两个文件内静态函数：

```cpp
// 页眉部件：单段居中文档标题（9pt 灰 #808080），段距紧凑（覆盖 docDefaults 1.15 行距）
static QString makeHeaderXml(const QString &title)
{
    const QString rpr = "<w:rPr><w:color w:val=\"808080\"/><w:sz w:val=\"18\"/></w:rPr>";
    return QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<w:hdr xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\">\n"
        "  <w:p>"
        "<w:pPr><w:spacing w:after=\"0\" w:line=\"240\" w:lineRule=\"auto\"/>"
        "<w:jc w:val=\"center\"/></w:pPr>"
        "<w:r>%1<w:t xml:space=\"preserve\">%2</w:t></w:r>"
        "</w:p>\n"
        "</w:hdr>\n").arg(rpr, xmlEscape(xmlSanitize(title)));
}

// 页脚部件：右对齐「第 X 页 / 共 Y 页」，X/Y 为 PAGE/NUMPAGES 简单字段——
// Word 打开时按字段重算（活页码，用户续编辑自动更新）；字段内 run 文本是占位值
static QString makeFooterXml()
{
    const QString rpr = "<w:rPr><w:color w:val=\"808080\"/><w:sz w:val=\"18\"/></w:rPr>";
    const QString pPr = "<w:pPr><w:spacing w:after=\"0\" w:line=\"240\" w:lineRule=\"auto\"/>"
                        "<w:jc w:val=\"right\"/></w:pPr>";
    return QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<w:ftr xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\">\n"
        "  <w:p>%1"
        "<w:r>%2<w:t xml:space=\"preserve\">第 </w:t></w:r>"
        "<w:fldSimple w:instr=\" PAGE   \\* MERGEFORMAT \"><w:r>%2<w:t>1</w:t></w:r></w:fldSimple>"
        "<w:r>%2<w:t xml:space=\"preserve\"> 页 / 共 </w:t></w:r>"
        "<w:fldSimple w:instr=\" NUMPAGES   \\* MERGEFORMAT \"><w:r>%2<w:t>1</w:t></w:r></w:fldSimple>"
        "<w:r>%2<w:t xml:space=\"preserve\"> 页</w:t></w:r>"
        "</w:p>\n"
        "</w:ftr>\n").arg(pPr, rpr);
}
```

- [ ] **Step 4: 实现——writeDocx() 四处接线**

① `writeDocx()` 的 settings 读取块（`state.maxImageWidthEmu = ...` 与 `state.sectPrXml = ...` 之间）读开关并把引用前置进 sectPr：

```cpp
    // DOCX 页眉页脚（独立开关默认开，与 PDF 的 pdfHeader/FooterEnabled 互不影响；
    // 标题取导出文件名基名，与 PDF 同源。CT_SectPr schema 要求 headerReference/
    // footerReference（EG_HdrFtrReferences）位于 pgSz 之前）
    bool headerEnabled = settings.value("export/docxHeaderEnabled", true).toBool();
    const bool footerEnabled = settings.value("export/docxFooterEnabled", true).toBool();
    const QString headerTitle = QFileInfo(outputPath).completeBaseName();
    if (headerTitle.isEmpty()) headerEnabled = false;   // 防御：无标题不产页眉部件
    QString hdrFtrRefs;
    if (headerEnabled)
        hdrFtrRefs += "<w:headerReference w:type=\"default\" r:id=\"rIdHeader1\"/>";
    if (footerEnabled)
        hdrFtrRefs += "<w:footerReference w:type=\"default\" r:id=\"rIdFooter1\"/>";
```

同块内把 `state.sectPrXml = QStringLiteral("<w:sectPr><w:pgSz ...")` 改为引用前置：

```cpp
    state.sectPrXml = QStringLiteral(
        "<w:sectPr>%1<w:pgSz w:w=\"%2\" w:h=\"%3\"/><w:pgMar w:top=\"%4\" w:right=\"%5\" "
        "w:bottom=\"%6\" w:left=\"%7\" w:header=\"720\" w:footer=\"720\" w:gutter=\"0\"/></w:sectPr>")
        .arg(hdrFtrRefs)
        .arg(mm2twips(pageW)).arg(mm2twips(pageH))
        .arg(mm2twips(mt * 10.0)).arg(mm2twips(mr * 10.0))
        .arg(mm2twips(mb * 10.0)).arg(mm2twips(ml * 10.0));
```

（`.arg(hdrFtrRefs)` 排在最前；全关时 `hdrFtrRefs` 为空串，产物与改造前语义等价。）

② `docRelsXml` 组装段（`rIdNumbering` 条目之后、`state.relsXml` 合并之前）：

```cpp
    if (headerEnabled)
        docRelsXml += "  <Relationship Id=\"rIdHeader1\""
            " Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/header\""
            " Target=\"header1.xml\"/>\n";
    if (footerEnabled)
        docRelsXml += "  <Relationship Id=\"rIdFooter1\""
            " Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/footer\""
            " Target=\"footer1.xml\"/>\n";
```

③ `contentTypes` 组装段（numbering Override 之后、media Default 之前）：

```cpp
    if (headerEnabled)
        contentTypes += "  <Override PartName=\"/word/header1.xml\""
            " ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.header+xml\"/>\n";
    if (footerEnabled)
        contentTypes += "  <Override PartName=\"/word/footer1.xml\""
            " ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.footer+xml\"/>\n";
```

④ zip 条目段（`word/styles.xml` 条目之后、media 循环之前）：

```cpp
    if (headerEnabled)
        add("word/header1.xml", makeHeaderXml(headerTitle).toUtf8(), MZ_BEST_COMPRESSION);
    if (footerEnabled)
        add("word/footer1.xml", makeFooterXml().toUtf8(), MZ_BEST_COMPRESSION);
```

（`headerEnabled` 声明在 settings 块内、需贯穿整个 `writeDocx()`——settings 块是用 `{}` 包裹的局部作用域，`headerEnabled`/`footerEnabled` 须声明在块外或提升声明；按现有代码结构，把上面 Step 4① 的两个 bool 声明放在 `{ QSettings settings... }` 块之前即可读到 `outputPath`（函数参数，全程可见）。）

- [ ] **Step 5: 跑新用例确认全绿**

```bash
cmake --build build --target test_docxexporter -j$(nproc) && ./build/tests/test_docxexporter testDocxHeader -platform offscreen 2>&1 | tail -8
```

预期：5 个新用例 PASS。

- [ ] **Step 6: 全量回归**

```bash
cmake --build build -j$(nproc) && (cd build && ctest --output-on-failure)
```

预期：13/13 全绿（既有用例默认开路径会带页眉页脚部件，但断言均为 `contains`，不受影响；`testDocxIsOpcNotOdf` 的 zip 首条目断言不受影响——新条目在 `[Content_Types].xml` 之后追加）。

- [ ] **Step 7: 提交**

```bash
git add src/core/DocxExporter.cpp tests/test_docxexporter.cpp
git commit -m "feat: DOCX 导出页眉页脚——Word 原生部件 + PAGE/NUMPAGES 活字段

word/header1.xml（文件名基名居中 9pt 灰）+ footer1.xml（第 X 页/共 Y 页右对齐），
sectPr 引用前置/schema 顺序、rels/Content_Types 接线；export/docxHeaderEnabled
与 docxFooterEnabled 独立开关默认开，全关零注入等价现状（回归锁定）。"
```

---

### Task 2: SettingsDialog 开关 UI

**Files:**
- Modify: `src/ui/SettingsDialog.h:74-75`（成员声明区）
- Modify: `src/ui/SettingsDialog.cpp`（5 处：构造/文案/载入/保存，均在 `m_pdfHeaderCheck`/`m_pdfFooterCheck` 对应位置之后）

**Interfaces:**
- Consumes: Task 1 的设置键 `export/docxHeaderEnabled` / `export/docxFooterEnabled`（默认 true）。
- Produces: 无代码接口；UI 可配置性（手工目检验收）。

- [ ] **Step 1: 头文件加成员**

`src/ui/SettingsDialog.h` 的 `m_pdfFooterCheck` 声明之后：

```cpp
    QCheckBox *m_docxHeaderCheck = nullptr;
    QCheckBox *m_docxFooterCheck = nullptr;
```

- [ ] **Step 2: cpp 五处接线（全部模仿 PDF 勾选框，紧随其后）**

① 构造区（`exportLayout->addWidget(m_pdfFooterCheck);` 之后）：

```cpp
    m_docxHeaderCheck = new QCheckBox(exportTab);
    m_docxFooterCheck = new QCheckBox(exportTab);
    exportLayout->addWidget(m_docxHeaderCheck);
    exportLayout->addWidget(m_docxFooterCheck);
```

② `retranslateUi`（`m_pdfFooterCheck->setText(...)` 之后）：

```cpp
    m_docxHeaderCheck->setText(tr("DOCX 页眉（文档标题）"));
    m_docxFooterCheck->setText(tr("DOCX 页码（第 X 页 / 共 Y 页）"));
```

③ `loadSettings`（PDF 两行之后）：

```cpp
    m_docxHeaderCheck->setChecked(
        settings.value("export/docxHeaderEnabled", true).toBool());
    m_docxFooterCheck->setChecked(
        settings.value("export/docxFooterEnabled", true).toBool());
```

④ `saveSettings`（PDF 两行之后）：

```cpp
    settings.setValue("export/docxHeaderEnabled", m_docxHeaderCheck->isChecked());
    settings.setValue("export/docxFooterEnabled", m_docxFooterCheck->isChecked());
```

- [ ] **Step 3: 重编译 + 手工目检（用户确认后提交）**

```bash
cmake --build build -j$(nproc)
```

启动 `./scripts/run-editor.sh`，目检清单（请用户确认）：

1. 设置 → 导出页：PDF 两项下方出现「DOCX 页眉（文档标题）」「DOCX 页码（第 X 页 / 共 Y 页）」，默认勾选
2. 全开导出 → WPS/LibreOffice 打开：页眉居中标题、页脚右对齐页码（Linux 侧参考性检查）
3. 双关导出 → 打开：无页眉页脚（与旧版观感一致）
4. 重开设置对话框：勾选状态保持

- [ ] **Step 4: 提交（目检通过后）**

```bash
git add src/ui/SettingsDialog.h src/ui/SettingsDialog.cpp
git commit -m "feat: 设置页 DOCX 页眉页脚独立开关（默认开）"
```

---

### Task 3: 收口——E2E 手工清单 + macOS Word 放行门

**Files:**
- 无代码改动（验证任务；文档同步收口留用户版本收口时一并处理）

**Interfaces:**
- Consumes: Task 1/2 完成的导出与设置。

- [ ] **Step 1: 二进制时戳自检**

```bash
B=build/src/md-editor && stat -c "%y %n" $B && find src tests -newer $B \( -name "*.cpp" -o -name "*.h" \)
```

预期：`find` 无输出（二进制新于全部源文件；有输出则重编译后再验）。

- [ ] **Step 2: E2E 手工清单（多页文档，用户确认）**

用 ≥3 页的真实文档（含标题/表格/图片/公式）：

1. 默认开导出 → 页眉每页居中标题、页脚「第 1 页 / 共 N 页」递增正确
2. 横向纸导出 → 页眉页脚位置正确（sectPr 换向后仍由 Word 定位）
3. 双关导出 → 与旧版产物一致无页眉页脚
4. 文件名含中文/特殊字符（如 `评审&纪要.docx`）→ 页眉文本正确显示无转义残留

- [ ] **Step 3: macOS Word 双击 A/B 取证（放行门，用户操作）**

A/B 样本法：同一文档分别「双开」「双关」导出两个 docx，macOS Word 双击打开：

- **A（双开）**：无「发现无法读取的内容」修复框；页眉页脚渲染如上；进入编辑后增删段落，页码与总页数自动重算（活字段）
- **B（双关）**：无修复框，无页眉页脚
- WPS/LibreOffice 交叉打开仅作参考，不作放行依据

任一弹修复框 → 回到 Task 1 排查 schema（优先嫌疑：`fldSimple` 字段形态，降级为 `fldChar` 三段式 complex field 再验证）。

- [ ] **Step 4: 汇报与版本收口建议**

向用户汇报：① 单测/回归/E2E/Word 取证各自结果；② 版本收口建议（feat 增量 → +0.1.0，含 CHANGELOG 词条草稿：「DOCX 导出页眉页脚（文档标题 + 第 X 页/共 Y 页 活页码，独立开关默认开）」）；③ 版本推进与 tag 由用户决定后执行。

---

## 自审记录

- **Spec 覆盖**：spec §3（部件+四接线）→ Task 1；§4 边界（空标题跳过、全关等价、异步正交）→ Task 1 代码与 `testDocxHeaderFooterAllDisabled`；§5 设置 UI → Task 2；§6.1 单测（四组合/顺序/转义/良构/等价）→ Task 1 五用例；§6.2 Word 放行门 → Task 3 Step 3；§7 文档 → Task 3 Step 4。无缺口。
- **占位符扫描**：无 TBD/「适当处理」；所有代码步骤给全量代码。
- **类型一致性**：`exportParts(html, partNames, outFileName)` 三处调用签名一致；设置键名四处（Task 1 实现 ×2、Task 2 load/save）与 spec 一致；rId `rIdHeader1`/`rIdFooter1` 在实现与断言中一致。
