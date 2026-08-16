# DOCX/PDF 图片与 Mermaid 导出 —— 实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 让 PDF/DOCX 导出能完整显示图片与 Mermaid 图表，消除 PDF 的 `QResource ... not found` 报错。

**Architecture:** 复用 `HtmlExporter::inlineImages` 给 PDF 图片做 base64 内联；把 mermaid 渲染片段提为共享 helper 注入 PDF；DOCX 侧新增 OOXML media 部件嵌入图片；mermaid 进 DOCX 走离屏 `MermaidRenderer`（QWebEngine）产出 SVG+PNG 双部件。所有导出器新增 `docDir` 参数以解析相对图片路径。

**Tech Stack:** C++/Qt6（WebEngineWidgets、Gui、Test），QXmlStreamReader 解析 HTML→OOXML，`zip`/`unzip` 打包解包。

## Global Constraints

- 兼容基准 **Qt 6.4.2**（CI ubuntu-24.04），本地 6.8.x——不得依赖 6.8 独有行为。
- **文档/注释中文，代码标识符英文**；提交前缀 `feat:`/`fix:`/`chore:`/`docs:`。
- 改 `src/resources/` 下 qrc 文件或任何 C++ 后必须 `cmake --build build` 重新编译。
- 不改 `editor.js`/`MarkdownHandler` 的往返逻辑（本计划只动导出路径）。
- 无头 CI 跑不了 QWebEngine：渲染器本体不写 ctest，核心字符串逻辑用纯函数单测。

---

### Task 1: 导出器 `docDir` 参数 + MainWindow 接线

**Files:**
- Modify: `src/core/PdfExporter.h`、`src/core/PdfExporter.cpp`
- Modify: `src/core/DocxExporter.h`、`src/core/DocxExporter.cpp`
- Modify: `src/ui/MainWindow.cpp`（`onExportPdf`/`onExportDocx`）
- Modify: `tests/test_docxexporter.cpp`（helper 适配新签名）

**Interfaces:**
- Consumes: 无（首任务）。
- Produces:
  - `void PdfExporter::exportToPdf(const QString &html, const QString &docDir, const QString &outputPath, QPageLayout::Orientation orientation = QPageLayout::Portrait, QPageSize::PageSizeId paperSize = QPageSize::A4)`
  - `void DocxExporter::exportToDocx(const QString &html, const QString &docDir, const QString &outputPath)`

- [ ] **Step 1: 改 PdfExporter 签名**

`src/core/PdfExporter.h` 中：

```cpp
    void exportToPdf(const QString &html, const QString &docDir, const QString &outputPath,
                     QPageLayout::Orientation orientation = QPageLayout::Portrait,
                     QPageSize::PageSizeId paperSize = QPageSize::A4);
```

`src/core/PdfExporter.cpp` 中 `exportToPdf` 定义改为：

```cpp
void PdfExporter::exportToPdf(const QString &html, const QString &docDir, const QString &outputPath,
                              QPageLayout::Orientation orientation,
                              QPageSize::PageSizeId paperSize)
{
    Q_UNUSED(docDir);   // Task 2 使用
    // ... 现有函数体不变
}
```

- [ ] **Step 2: 改 DocxExporter 签名**

`src/core/DocxExporter.h`：

```cpp
    void exportToDocx(const QString &html, const QString &docDir, const QString &outputPath);
```

`src/core/DocxExporter.cpp` 中 `exportToDocx` 与私有 `writeDocx` 均加 `docDir` 并透传：

```cpp
void DocxExporter::exportToDocx(const QString &html, const QString &docDir, const QString &outputPath)
{
    if (html.isEmpty()) { emit exportError("导出失败：编辑器内容为空"); return; }
    if (writeDocx(html, docDir, outputPath)) emit exportFinished(true, outputPath);
    else emit exportError("DOCX 生成失败");
}
```

`writeDocx` 定义改 `bool DocxExporter::writeDocx(const QString &html, const QString &docDir, const QString &outputPath)`，函数体暂加 `Q_UNUSED(docDir);`（Task 5 使用）。

- [ ] **Step 3: MainWindow 传入 docDir**

`src/ui/MainWindow.cpp` `onExportPdf` 中，`getSaveFileName` 后、`runJavaScript` 前新增 docDir 捕获：

```cpp
    QString docDir = QFileInfo(tab->webView()->property("filePath").toString()).absolutePath();
```

把 lambda 捕获列表 `[this, path]` 改为 `[this, path, docDir]`，调用改为 `exporter->exportToPdf(html, docDir, path);`。

`onExportDocx` 同理：新增 `docDir`，捕获 `[this, path, docDir]`，调用 `exporter->exportToDocx(html, docDir, path);`。

- [ ] **Step 4: 适配 test_docxexporter helper**

`tests/test_docxexporter.cpp` 的 `exportDocumentXml` 内，把 `ex.exportToDocx(html, out);` 改为：

```cpp
        ex.exportToDocx(html, dir.path(), out);
```

（`dir` 即函数内的 QTemporaryDir，`dir.path()` 作 docDir。）

- [ ] **Step 5: 构建并跑测试**

Run: `cmake --build build -j$(nproc) && cd build && ctest --output-on-failure`
Expected: 6 项 C++ 测试全绿（现有测试适配后不回归）。

- [ ] **Step 6: Commit**

```bash
git add src/core/PdfExporter.h src/core/PdfExporter.cpp src/core/DocxExporter.h src/core/DocxExporter.cpp src/ui/MainWindow.cpp tests/test_docxexporter.cpp
git commit -m "chore: 导出器增加 docDir 参数并接线 MainWindow"
```

---

### Task 2: PDF 图片内联（修 QResource 报错）

**Files:**
- Modify: `src/core/PdfExporter.cpp`

**Interfaces:**
- Consumes: Task 1 的 `docDir` 参数。
- Produces: `PdfExporter` 内联图片后的 PDF 渲染 HTML（供 Task 4 复用同一构建路径）。

- [ ] **Step 1: 提取可测的 HTML 构建函数**

把 `exportToPdf` 中「读 katexCss + 拼 fullHtml」抽为 static 纯函数（声明加在 `PdfExporter.h` public 区，与 HtmlExporter 风格一致）：

```cpp
    // 构建 PDF 渲染 HTML（纯字符串逻辑，供测试）
    static QString buildRenderHtml(const QString &html, const QString &docDir,
                                   const QString &katexCss, const QString &customCss);
```

`src/core/PdfExporter.cpp` 顶部加 `#include "HtmlExporter.h"`，实现：

```cpp
QString PdfExporter::buildRenderHtml(const QString &html, const QString &docDir,
                                     const QString &katexCss, const QString &customCss)
{
    // 图片 base64 内联（相对路径相对 docDir 解析；绝对路径原样；读不到保留 src）
    QString body = HtmlExporter::inlineImages(html, docDir);

    QString fullHtml = QString(
        "<!DOCTYPE html><html><head><meta charset=\"UTF-8\"><style>"
        "@media print{"
        "@page{margin:0;}"
        "body{font-family:\"Noto Sans SC\",\"DejaVu Sans\",sans-serif;"
        "font-size:11pt;line-height:1.6;color:#333;}"
        "h1{font-size:1.7em;margin-top:1em;margin-bottom:0.3em;}"
        "h2{font-size:1.3em;margin-top:0.8em;margin-bottom:0.2em;}"
        "h3{font-size:1.1em;margin-top:0.6em;margin-bottom:0.2em;}"
        "p{margin-top:0;margin-bottom:0.3em;}"
        "pre{background:#f4f5f7;padding:10px 14px;border-radius:4px;"
        "font-family:monospace;font-size:9.5pt;line-height:1.35;overflow-x:auto;margin:0.5em 0;}"
        "code{background:#f0f0f0;padding:1px 3px;border-radius:2px;font-size:0.9em;}"
        "table{border-collapse:collapse;width:100%%;margin:0.5em 0;}"
        "th,td{border:1px solid #ccc;padding:4px 8px;font-size:10pt;}"
        "th{background:#f5f5f5;}"
        "img{max-width:100%%;}"
        "blockquote{border-left:3px solid #0078d4;padding:4px 12px;"
        "color:#555;margin:0.5em 0;}"
        "[data-type=\"block-math\"]{display:block;text-align:center;margin:1em 0;}"
        "}"
        "%2"
        "</style>"
        "<script src=\"katex.min.js\"></script>"
        "<script>"
        "window.addEventListener('DOMContentLoaded',function(){"
        "var els=document.querySelectorAll('[data-latex]');"
        "for(var i=0;i<els.length;i++){"
        "var el=els[i];"
        "var latex=el.getAttribute('data-latex');"
        "if(!latex)continue;"
        "try{"
        "katex.render(latex,el,{throwOnError:false,displayMode:el.dataset.type==='block-math'});"
        "}catch(e){el.textContent=latex;}"
        "}"
        "});"
        "</script>"
        "</head><body>%1</body></html>"
    ).arg(body, katexCss);

    if (!customCss.isEmpty()) {
        fullHtml.replace("</head>", QString("<style>%1</style></head>").arg(customCss));
    }
    return fullHtml;
}
```

- [ ] **Step 2: exportToPdf 改用 buildRenderHtml**

把 `exportToPdf` 中 `fullHtml` 的构建代码替换为对 `buildRenderHtml` 的调用（katexCss 读取逻辑保留在 exportToPdf，customCss 读取保留在 exportToPdf）：

```cpp
    QString body = html;
    // （删除原 fullHtml 大段拼接）
    QString fullHtml = buildRenderHtml(html, docDir, katexCss, customCss);
```

并移除函数体开头的 `Q_UNUSED(docDir);`（现在用到了）。

- [ ] **Step 3: 写测试**

`tests/test_docxexporter.cpp` 不动；新建 `tests/test_pdfexporter.cpp`（只测纯函数 `buildRenderHtml`，不触发 QWebEngine）：

```cpp
#include <QTest>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include "../src/core/PdfExporter.h"
#include "../src/core/HtmlExporter.h"

class TestPdfExporter : public QObject {
    Q_OBJECT
private slots:
    void testBuildRenderHtmlInlinesImages() {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        QFile f(dir.path() + "/a.png");
        QVERIFY(f.open(QIODevice::WriteOnly)); f.write("PNGDATA"); f.close();

        QString html = "<p><img src=\"a.png\" alt=\"x\"></p>";
        QString out = PdfExporter::buildRenderHtml(html, dir.path(), "", "");
        QVERIFY(out.contains("data:image/png;base64,"));
        QVERIFY(!out.contains("src=\"a.png\""));
    }

    void testBuildRenderHtmlKeepsAbsoluteImageAsIsWhenMissing() {
        // 读不到的文件保留原 src（不抛异常、不报错）
        QString html = "<p><img src=\"/nonexistent/zzz.jpg\"></p>";
        QString out = PdfExporter::buildRenderHtml(html, "/tmp", "", "");
        QVERIFY(out.contains("src=\"/nonexistent/zzz.jpg\""));
    }
};

QTEST_APPLESS_MAIN(TestPdfExporter)
#include "test_pdfexporter.moc"
```

- [ ] **Step 4: 注册并跑测试**

`tests/CMakeLists.txt` 追加：

```cmake
add_executable(test_pdfexporter test_pdfexporter.cpp ../src/core/PdfExporter.cpp ../src/core/HtmlExporter.cpp)
target_include_directories(test_pdfexporter PRIVATE ../src)
target_link_libraries(test_pdfexporter PRIVATE Qt6::Test Qt6::Gui)
add_test(NAME test_pdfexporter COMMAND test_pdfexporter)
```

Run: `cmake --build build -j$(nproc) && cd build && ctest -R test_pdfexporter --output-on-failure`
Expected: 2 项通过。

- [ ] **Step 5: Commit**

```bash
git add src/core/PdfExporter.h src/core/PdfExporter.cpp tests/test_pdfexporter.cpp tests/CMakeLists.txt
git commit -m "feat: PDF 导出图片 base64 内联，消除 QResource 报错"
```

---

### Task 3: 提取 mermaid 渲染脚本共享 helper

**Files:**
- Modify: `src/core/HtmlExporter.h`、`src/core/HtmlExporter.cpp`
- Modify: `tests/test_htmlexporter.cpp`

**Interfaces:**
- Consumes: 无（独立 helper）。
- Produces:
  - `static QString HtmlExporter::mermaidRenderScript(const QString &theme)` —— 返回纯 mermaid 渲染 JS 片段（不含 `<script>` 标签）。

- [ ] **Step 1: 提取 helper（Promise 版）**

`src/core/HtmlExporter.h` public 区加：

```cpp
    static QString mermaidRenderScript(const QString &theme);
```

`src/core/HtmlExporter.cpp` 把 `renderScript` 内 mermaid 段提为独立函数。**helper 定义 `window.__renderAllMermaid()`（返回 Promise，全部渲染完成才 resolve）**，HTML 与 PDF 共用：

```cpp
QString HtmlExporter::mermaidRenderScript(const QString &theme)
{
    const QString mermaidTheme = (theme == "dark") ? "dark" : "default";
    return QStringLiteral(
        "window.__renderAllMermaid=function(){"
        "return new Promise(function(resolve){"
        "if(!window.mermaid){resolve();return;}"
        "mermaid.initialize({startOnLoad:false,securityLevel:'loose',theme:'%1',"
        "themeVariables:{fontSize:'16px'}});"
        "var pres=document.querySelectorAll('pre code.language-mermaid');"
        "if(!pres.length){resolve();return;}"
        "var seq=0,jobs=[];"
        "for(var j=0;j<pres.length;j++){(function(code){"
        "var src=code.textContent;var pre=code.parentNode;"
        "var holder=document.createElement('div');"
        "pre.parentNode.replaceChild(holder,pre);"
        "jobs.push(mermaid.render('mermaid-export-'+(++seq),src).then(function(res){holder.innerHTML=res.svg;})"
        ".catch(function(err){holder.textContent=src;}));"
        "})(pres[j]);}"
        "Promise.all(jobs).then(function(){resolve();});"
        "});};").arg(mermaidTheme);
}
```

再把 `renderScript` 改为组合式：KaTeX 段 + `<script>` 包裹的 `mermaidRenderScript(theme) + "window.__renderAllMermaid();"`（HTML 导出自调用，浏览器常驻无需等 Promise）+ hljs 段，保持对外行为不变。

- [ ] **Step 2: 补测试**

`tests/test_htmlexporter.cpp` 加：

```cpp
    void testMermaidRenderScriptIsolated() {
        QString s = HtmlExporter::mermaidRenderScript("dark");
        QVERIFY(s.contains("'dark'"));
        QVERIFY(s.contains("pre code.language-mermaid"));
        QVERIFY(s.contains("__renderAllMermaid"));
        QVERIFY(s.contains("mermaid.render('mermaid-export-'+(++seq),src)"));
        QVERIFY(!s.contains("<script"));   // 片段不含 script 标签，调用方自行包裹
    }
```

- [ ] **Step 3: 跑测试**

Run: `cmake --build build -j$(nproc) && cd build && ctest -R "test_htmlexporter|test_pdfexporter" --output-on-failure`
Expected: 全绿（含 renderScript 既有 2 项回归 + 新 1 项）。

- [ ] **Step 4: Commit**

```bash
git add src/core/HtmlExporter.h src/core/HtmlExporter.cpp tests/test_htmlexporter.cpp
git commit -m "refactor: 提取 mermaidRenderScript 共享 helper"
```

---

### Task 4: PDF mermaid 渲染 + 异步 print

**Files:**
- Modify: `src/core/PdfExporter.cpp`（`buildRenderHtml` + `onLoadFinished`）

**Interfaces:**
- Consumes: Task 3 的 `HtmlExporter::mermaidRenderScript(theme)`。
- Produces: PDF 导出渲染 mermaid 图后打印。

- [ ] **Step 1: buildRenderHtml 注入 mermaid 脚本**

`buildRenderHtml` 里，`<script src="katex.min.js"></script>` 后追加 mermaid.min.js 与渲染片段。把 `<script src="katex.min.js"></script>` 替换为：

```cpp
        "<script src=\"katex.min.js\"></script>"
        "<script src=\"mermaid.min.js\"></script>"
```

并在 `buildRenderHtml` 返回前，注入 Task 3 的 helper（定义 `window.__renderAllMermaid`，PDF 用亮色）：

```cpp
    fullHtml.replace("</head>",
        QString("<script>%1</script></head>").arg(HtmlExporter::mermaidRenderScript("default")));
```

（helper 只定义 `__renderAllMermaid`，不自调用——PDF 在 `onLoadFinished` 里显式调用并等待 Promise。）

- [ ] **Step 2: onLoadFinished 改为「先渲染 mermaid 再 print」**

`src/core/PdfExporter.cpp` 中：

```cpp
void PdfExporter::onLoadFinished(bool ok)
{
    if (!ok) { emit exportError("HTML 渲染失败"); return; }
    // 先渲染 mermaid（异步），全部完成后再打印，否则图表空白
    m_renderView->page()->runJavaScript(
        "window.__renderAllMermaid().then(function(){return true;})",
        [this](const QVariant &) {
            m_renderView->page()->printToPdf(m_pendingOutputPath, m_pendingLayout);
        });
}
```

- [ ] **Step 3: 补测试**

`tests/test_pdfexporter.cpp` 加：

```cpp
    void testBuildRenderHtmlInjectsMermaid() {
        QString out = PdfExporter::buildRenderHtml("<p>x</p>", "/tmp", "", "");
        QVERIFY(out.contains("mermaid.min.js"));
        QVERIFY(out.contains("__renderAllMermaid"));
    }
```

- [ ] **Step 4: 跑测试**

Run: `cmake --build build -j$(nproc) && cd build && ctest -R test_pdfexporter --output-on-failure`
Expected: 3 项通过。

- [ ] **Step 5: Commit**

```bash
git add src/core/PdfExporter.cpp tests/test_pdfexporter.cpp
git commit -m "feat: PDF 导出渲染 mermaid 图并异步打印"
```

---

### Task 5: DOCX 图片嵌入 OOXML media 部件

**Files:**
- Modify: `src/core/DocxExporter.cpp`
- Modify: `tests/test_docxexporter.cpp`

**Interfaces:**
- Consumes: Task 1 的 `docDir` 参数。
- Produces:
  - `struct MediaPart { int rId; QString fileName; QString contentType; QByteArray bytes; };`（ParseState 内 `QList<MediaPart> media`）
  - `static bool readImageFile(const QString &src, const QString &docDir, QByteArray *bytes, QString *mime, int *cxEmu, int *cyEmu)`
  - `static QString buildImageDrawingXml(int docPrId, int rId, int cxEmu, int cyEmu)`（rId=-1 表示无 PNG，纯 SVG 时传 svgRId）

- [ ] **Step 1: 加头文件 + MediaPart 结构**

`src/core/DocxExporter.cpp` 顶部 `#include` 区加：

```cpp
#include <QImageReader>
#include <QMimeDatabase>
```

`ParseState` 结构体内（`QList<int> usedAbstractNums;` 附近）加：

```cpp
    struct MediaPart {
        int rId;
        QString fileName;     // "imageN.png" / "diagramN.svg"
        QString contentType;  // "image/png" 等
        QByteArray bytes;
    };
    QList<MediaPart> media;
    int nextDocPrId = 1;
```

- [ ] **Step 2: 加 readImageFile 与 drawing XML helper**

`makeXmlSafe` 函数之后加：

```cpp
// 解析图片路径 → 字节 + MIME + 尺寸（EMU）。失败返回 false。
static bool readImageFile(const QString &src, const QString &docDir,
                          QByteArray *bytes, QString *mime, int *cxEmu, int *cyEmu)
{
    if (src.startsWith("data:")) {
        // data URI：解析 base64（mime 取 data: 前缀）
        int comma = src.indexOf(',');
        if (comma < 0) return false;
        QString header = src.left(comma);
        QRegularExpression mre("data:([^;]+);base64");
        auto mm = mre.match(header);
        if (!mm.hasMatch()) return false;
        *mime = mm.captured(1);
        *bytes = QByteArray::fromBase64(src.mid(comma + 1).toLatin1());
    } else {
        QString path = src.startsWith('/') ? src : docDir + "/" + src;
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) return false;
        *bytes = f.readAll();
        f.close();
        QMimeDatabase db;
        *mime = db.mimeTypeForFile(path).name();
        if (!mime->startsWith("image/")) return false;
    }
    // 尺寸：PNG/JPEG 用 QImageReader；SVG 不读尺寸用默认
    QImageReader ir;
    QBuffer buf(bytes);
    buf.open(QIODevice::ReadOnly);
    ir.setDevice(&buf);
    QSize sz = ir.size();
    if (sz.isValid() && sz.width() > 0 && sz.height() > 0) {
        *cxEmu = sz.width() * 9525;
        *cyEmu = sz.height() * 9525;
    } else {
        *cxEmu = 300 * 9525;   // 默认 300×200 px
        *cyEmu = 200 * 9525;
    }
    return true;
}

// 生成 <w:drawing> 内联图片 XML。svgRId > 0 时挂 <asvg:svgBlip>（Word 2016+ SVG）。
static QString buildImageDrawingXml(int docPrId, int pngRId, int svgRId,
                                    int cxEmu, int cyEmu)
{
    QString blip;
    if (svgRId > 0) {
        blip = QString("<a:blip r:embed=\"rId%1\"><a:extLst>"
                       "<a:ext uri=\"{96DAC541-7B7A-43D3-8B79-37D633B846F1}\">"
                       "<asvg:svgBlip xmlns:asvg=\"http://schemas.microsoft.com/office/drawing/2016/SVG/main\" r:embed=\"rId%2\"/>"
                       "</a:ext></a:extLst></a:blip>").arg(pngRId).arg(svgRId);
    } else {
        blip = QString("<a:blip r:embed=\"rId%1\"/>").arg(pngRId);
    }
    return QString(
        "<w:drawing><wp:inline distT=\"0\" distB=\"0\" distL=\"0\" distR=\"0\">"
        "<wp:extent cx=\"%1\" cy=\"%2\"/><wp:effectExtent l=\"0\" t=\"0\" r=\"0\" b=\"0\"/>"
        "<wp:docPr id=\"%3\" name=\"图片%3\"/>"
        "<wp:cNvGraphicFramePr><a:graphicFrameLocks noChangeAspect=\"1\"/></wp:cNvGraphicFramePr>"
        "<a:graphic><a:graphicData uri=\"http://schemas.openxmlformats.org/drawingml/2006/picture\">"
        "<pic:pic><pic:nvPicPr><pic:cNvPr id=\"%3\" name=\"图片%3\"/><pic:cNvPicPr/></pic:nvPicPr>"
        "<pic:blipFill>%4<a:stretch><a:fillRect/></a:stretch></pic:blipFill>"
        "<pic:spPr><a:xfrm><a:off x=\"0\" y=\"0\"/><a:ext cx=\"%1\" cy=\"%2\"/></a:xfrm>"
        "<a:prstGeom prst=\"rect\"><a:avLst/></a:prstGeom></pic:spPr></pic:pic>"
        "</a:graphicData></a:graphic></wp:inline></w:drawing>")
        .arg(cxEmu).arg(cyEmu).arg(docPrId).arg(blip);
}

// 新增图片 media 部件，返回 rId
static int addMedia(ParseState &s, const QByteArray &bytes, const QString &contentType,
                    const QString &ext)
{
    int rId = s.nextRId++;
    QString fileName = QString("image%1.%2").arg(s.media.size() + 1).arg(ext);
    s.media.append({rId, fileName, contentType, bytes});
    s.relsXml += QString("  <Relationship Id=\"rId%1\""
        " Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/image\""
        " Target=\"media/%2\"/>\n").arg(rId).arg(fileName);
    return rId;
}
```

- [ ] **Step 3: parseInline / parseBody 的 img 分支改为嵌入**

`parseInline` 的 `img` 分支（现「图片占位」）与 `parseBody` 的 `img` 分支，改成：先 `readImageFile`，成功则 `addMedia` 并输出 drawing run；失败保留占位。parseInline 需要 `docDir` 才能解析相对路径——把 `docDir` 加入 `ParseState`（`QString docDir;`，`writeDocx` 里 `state.docDir = docDir;`）。

`parseInline` img 分支改为：

```cpp
            else if (tag == "img") {
                flushRun();
                QString alt = r.attributes().value("alt").toString();
                QByteArray bytes; QString mime; int cx = 0, cy = 0;
                if (readImageFile(r.attributes().value("src").toString(), s.docDir, &bytes, &mime, &cx, &cy)) {
                    QString ext = (mime == "image/png") ? "png" : (mime == "image/gif" ? "gif" : "jpg");
                    int rId = addMedia(s, bytes, mime, ext);
                    out += "<w:r>" + buildImageDrawingXml(s.nextDocPrId++, rId, 0, cx, cy) + "</w:r>";
                } else {
                    out += "<w:r><w:t>[图片" + (alt.isEmpty() ? "" : ": " + xmlEscape(alt)) + "]</w:t></w:r>";
                }
            }
```

`parseBody` img 分支同样处理，外层包 `<w:p>`：

```cpp
        } else if (tag == "img") {
            QString alt = r.attributes().value("alt").toString();
            QByteArray bytes; QString mime; int cx = 0, cy = 0;
            if (readImageFile(r.attributes().value("src").toString(), s.docDir, &bytes, &mime, &cx, &cy)) {
                QString ext = (mime == "image/png") ? "png" : (mime == "image/gif" ? "gif" : "jpg");
                int rId = addMedia(s, bytes, mime, ext);
                s.bodyXml += "<w:p><w:r>" + buildImageDrawingXml(s.nextDocPrId++, rId, 0, cx, cy) + "</w:r></w:p>\n";
            } else {
                s.bodyXml += "<w:p><w:r><w:t>[图片" + (alt.isEmpty() ? "" : ": " + xmlEscape(alt)) + "]</w:t></w:r></w:p>\n";
            }
        }
```

（注意 `addMedia` 依赖 `s.nextRId`，但 `makeRId` 已用 `nextRId`；二者共享 `nextRId`，超链接与图片不冲突。）

- [ ] **Step 4: buildDocumentXml 补命名空间**

`buildDocumentXml` 根元素 `xmlns:w="..."` 之前加：

```cpp
        " xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\""
        " xmlns:pic=\"http://schemas.openxmlformats.org/drawingml/2006/picture\""
```

- [ ] **Step 5: writeDocx 写 media 部件 + content types**

`writeDocx` 中，`word/document.xml` 写入之后、ZIP 打包之前，写入 media 部件与扩展名声明：

```cpp
    // word/media/ 部件
    if (!state.media.isEmpty()) {
        QDir().mkpath(base + "/word/media");
        int idx = 1;
        for (const auto &mp : state.media) {
            if (!writeFile(base + "/word/" + mp.fileName, mp.bytes)) return false;
            (void)idx;
        }
    }
```

`[Content_Types].xml` 的 `contentTypes` 构建处，追加图片/ svg 的 Default（按实际出现的扩展名去重追加）：

```cpp
    if (!state.media.isEmpty()) {
        QSet<QString> exts;
        for (const auto &mp : state.media)
            exts.insert(QFileInfo(mp.fileName).suffix());
        for (const QString &e : exts) {
            QString ct = (e == "png") ? "image/png" : (e == "gif") ? "image/gif"
                       : (e == "svg") ? "image/svg+xml" : "image/jpeg";
            contentTypes += "  <Default Extension=\"" + e + "\" ContentType=\"" + ct + "\"/>\n";
        }
    }
```

（需 `#include <QSet>`。）

- [ ] **Step 6: 写测试**

`tests/test_docxexporter.cpp` 加：

```cpp
    void testImageEmbeddedAsMedia() {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        QFile f(dir.path() + "/a.png");
        QVERIFY(f.open(QIODevice::WriteOnly));
        // 1×1 透明 PNG
        const unsigned char png[] = {0x89,0x50,0x4e,0x47,0x0d,0x0a,0x1a,0x0a,0,0,0,0x0d,0x49,0x48,0x44,0x52,
                                     0,0,0,1,0,0,0,1,0x08,0x06,0,0,0,0x1f,0x15,0xc4,0x89,0,0,0,0x0a,0x49,0x44,0x41,0x54,
                                     0x78,0x9c,0x63,0,1,0,0,0x05,0,1,0x0d,0x0a,0x2d,0xb4,0,0,0,0,0x49,0x45,0x4e,0x44,0xae,0x42,0x60,0x82};
        f.write(reinterpret_cast<const char*>(png), sizeof(png)); f.close();

        QString out = dir.path() + "/o.docx";
        DocxExporter ex;
        ex.exportToDocx("<p><img src=\"a.png\" alt=\"图\"></p>", dir.path(), out);
        QVERIFY(QFile::exists(out));

        // document.xml 含 drawing + blip
        QProcess unzip; unzip.start("unzip", QStringList() << "-p" << out << "word/document.xml");
        QVERIFY(unzip.waitForFinished(10000));
        QString docXml = QString::fromUtf8(unzip.readAllStandardOutput());
        QVERIFY(docXml.contains("<w:drawing>"));
        QVERIFY(docXml.contains("<a:blip r:embed=\"rId"));
        QVERIFY(docXml.contains("<pic:pic>"));

        // media 部件存在
        unzip.start("unzip", QStringList() << "-l" << out);
        QVERIFY(unzip.waitForFinished(10000));
        QString listing = QString::fromUtf8(unzip.readAllStandardOutput());
        QVERIFY(listing.contains("word/media/image1.png"));
    }
```

- [ ] **Step 7: 跑测试**

Run: `cmake --build build -j$(nproc) && cd build && ctest -R test_docxexporter --output-on-failure`
Expected: 全部通过（含新 image 测试 + 既有 12 项）。

- [ ] **Step 8: Commit**

```bash
git add src/core/DocxExporter.cpp tests/test_docxexporter.cpp
git commit -m "feat: DOCX 导出图片嵌入 OOXML media 部件"
```

---

### Task 6: DOCX mermaid 离屏渲染 + SVG/PNG 嵌入

**Files:**
- Create: `src/core/MermaidRenderer.h`、`src/core/MermaidRenderer.cpp`
- Modify: `src/CMakeLists.txt`（SOURCE/HEADER 各加一行）
- Modify: `src/core/DocxExporter.h`、`src/core/DocxExporter.cpp`
- Modify: `tests/test_docxexporter.cpp`

**Interfaces:**
- Consumes: Task 5 的 `MediaPart`/`buildImageDrawingXml`/`addMedia`。
- Produces:
  - `class MermaidRenderer : QObject`（`renderAll(sources)` → `finished(QVector<Result>)`，`Result{svg,png,cxEmu,cyEmu}`）
  - `static QStringList DocxExporter::extractMermaidSources(const QString &html)`（纯函数，可测）

- [ ] **Step 1: 新增 MermaidRenderer.h**

```cpp
#ifndef MD_EDITOR_MERMAIDRENDERER_H
#define MD_EDITOR_MERMAIDRENDERER_H

#include <QObject>
#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>

class QWebEngineView;

// 离屏 QWebEngine 渲染 mermaid 源码 → SVG + PNG（canvas 栅格化），供 DOCX 导出嵌入。
// 渲染异步，结果经 finished 信号回传；渲染失败该项回退为空（调用侧降级为源码代码块）。
class MermaidRenderer : public QObject {
    Q_OBJECT
public:
    struct Result {
        QString svg;        // 完整 SVG 字符串
        QByteArray png;     // PNG 字节
        int cxEmu = 0;      // 宽（EMU）
        int cyEmu = 0;      // 高（EMU）
    };

    explicit MermaidRenderer(QObject *parent = nullptr);
    ~MermaidRenderer() override;

    void renderAll(const QStringList &sources);   // 异步

signals:
    void finished(const QVector<MermaidRenderer::Result> &results);

private slots:
    void onLoadFinished(bool ok);

private:
    QWebEngineView *m_view = nullptr;
    QStringList m_pending;
    bool m_loaded = false;
};

#endif // MD_EDITOR_MERMAIDRENDERER_H
```

- [ ] **Step 2: 实现 MermaidRenderer.cpp**

```cpp
#include "MermaidRenderer.h"

#include <QWebEngineView>
#include <QWebEnginePage>
#include <QWebEngineSettings>
#include <QUrl>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>

MermaidRenderer::MermaidRenderer(QObject *parent)
    : QObject(parent), m_view(new QWebEngineView())
{
    m_view->setVisible(false);
    m_view->settings()->setAttribute(QWebEngineSettings::LocalContentCanAccessFileUrls, true);

    // 内联 mermaid.min.js 的 helper 页
    QFile f(":/web/mermaid.min.js");
    QString mermaidJs;
    if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        mermaidJs = QString::fromUtf8(f.readAll());
        f.close();
    }
    QString html = QStringLiteral(
        "<!DOCTYPE html><html><head><meta charset=\"UTF-8\"><script>%1</script></head>"
        "<body></body></html>").arg(mermaidJs);

    connect(m_view, &QWebEngineView::loadFinished, this, &MermaidRenderer::onLoadFinished);
    m_view->setHtml(html);
}

MermaidRenderer::~MermaidRenderer() { delete m_view; }

void MermaidRenderer::renderAll(const QStringList &sources)
{
    m_pending = sources;
    if (m_loaded) onLoadFinished(true);
}

void MermaidRenderer::onLoadFinished(bool ok)
{
    if (!ok) { emit finished({}); return; }
    if (m_pending.isEmpty()) return;
    m_loaded = true;

    // JS：对每个 source 渲染 SVG，canvas 栅格化 PNG，返回 JSON 数组
    QString script = QStringLiteral(
        "(function(){"
        "mermaid.initialize({startOnLoad:false,securityLevel:'loose',theme:'default',"
        "themeVariables:{fontSize:'16px'}});"
        "var sources=%1;"
        "function svgDims(svg){var d=document.createElement('div');d.style.cssText='position:absolute;left:-9999px';"
        "d.innerHTML=svg;document.body.appendChild(d);var s=d.querySelector('svg');"
        "var w=s&&s.viewBox&&s.viewBox.baseVal? s.viewBox.baseVal.width : (s? s.getBoundingClientRect().width : 0);"
        "var h=s&&s.viewBox&&s.viewBox.baseVal? s.viewBox.baseVal.height : (s? s.getBoundingClientRect().height : 0);"
        "document.body.removeChild(d);return {w:w||400,h:h||200};}"
        "function toPng(svg,w,h){return new Promise(function(res){"
        "var svgBlob=new Blob([svg],{type:'image/svg+xml;charset=utf-8'});"
        "var url=URL.createObjectURL(svgBlob);var img=new Image();"
        "img.onload=function(){var c=document.createElement('canvas');c.width=Math.ceil(w*2);c.height=Math.ceil(h*2);"
        "c.getContext('2d').drawImage(img,0,0,c.width,c.height);"
        "res(c.toDataURL('image/png'));URL.revokeObjectURL(url);};"
        "img.onerror=function(){res('');URL.revokeObjectURL(url);};"
        "img.src=url;});}"
        "Promise.all(sources.map(function(src,i){"
        "return mermaid.render('mmd'+i,src).then(function(r){"
        "var dim=svgDims(r.svg);return toPng(r.svg,dim.w,dim.h).then(function(png){"
        "return {svg:r.svg,png:png,w:Math.round(dim.w),h:Math.round(dim.h)};});"
        "}).catch(function(e){return null;});"
        "})).then(function(arr){return JSON.stringify(arr);});"
        "})()").arg(QString::fromUtf8(QJsonDocument(
            QJsonArray::fromStringList(m_pending)).toJson(QJsonDocument::Compact)));

    int n = m_pending.size();   // clear 前捕获数量，供解析失败回退
    m_pending.clear();
    m_view->page()->runJavaScript(script, [this, n](const QVariant &res) {
        QVector<Result> out;
        QString json = res.toString();
        if (json.startsWith('"')) json = QJsonDocument::fromJson(json.toUtf8()).toString(); // 解一层字符串
        QJsonParseError err;
        QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &err);
        if (err.error == QJsonParseError::NoError && doc.isArray()) {
            const QJsonArray arr = doc.array();
            for (const QJsonValue &v : arr) {
                if (!v.isObject()) { out.append(Result()); continue; }
                QJsonObject o = v.toObject();
                Result r;
                r.svg = o.value("svg").toString();
                QString png = o.value("png").toString();
                if (png.startsWith("data:image/png;base64,"))
                    r.png = QByteArray::fromBase64(png.mid(22).toLatin1());
                r.cxEmu = o.value("w").toInt() * 9525;
                r.cyEmu = o.value("h").toInt() * 9525;
                out.append(r);
            }
        } else {
            for (int i = 0; i < n; ++i) out.append(Result());
        }
        emit finished(out);
    });
}
```

- [ ] **Step 3: 注册到 CMake**

`src/CMakeLists.txt` 的 SOURCES 加 `core/MermaidRenderer.cpp`，HEADERS 加 `core/MermaidRenderer.h`。

- [ ] **Step 4: DocxExporter 提取 mermaid 源码 + 占位替换（纯函数）**

`src/core/DocxExporter.h` 加 static 声明：

```cpp
    // 提取 <pre><code class="language-mermaid"> 的源码（纯函数，供测试）
    static QStringList extractMermaidSources(const QString &html);
```

`src/core/DocxExporter.cpp` 实现（在 `makeXmlSafe` 后；`extractMermaidSources` 与 `substituteMermaidPlaceholders` 共用同一正则，保证序号对齐）：

```cpp
static const QRegularExpression &mermaidBlockRe()
{
    static const QRegularExpression re(
        "<pre[^>]*><code[^>]*class=\"[^\"]*language-mermaid[^\"]*\"[^>]*>([\\s\\S]*?)</code></pre>",
        QRegularExpression::CaseInsensitiveOption);
    return re;
}

static QString htmlUnescape(QString s)   // mermaid 源码在 getContent() 中被 HTML 转义
{
    return s.replace("&lt;", "<").replace("&gt;", ">").replace("&amp;", "&").replace("&quot;", "\"");
}

QStringList DocxExporter::extractMermaidSources(const QString &html)
{
    QStringList out;
    auto it = mermaidBlockRe().globalMatch(html);
    while (it.hasNext()) { auto m = it.next(); out << htmlUnescape(m.captured(1)); }
    return out;
}

// 每个 mermaid 块替换为 <p data-mermaid-idx="N"></p> 占位（N 与 extractMermaidSources 顺序一致）
static QString substituteMermaidPlaceholders(const QString &html)
{
    QString out = html; int offset = 0, idx = 0;
    auto it = mermaidBlockRe().globalMatch(html);
    while (it.hasNext()) {
        auto m = it.next();
        QString ph = QString("<p data-mermaid-idx=\"%1\"></p>").arg(idx++);
        out.replace(m.capturedStart(0) + offset, m.capturedLength(0), ph);
        offset += ph.length() - m.capturedLength(0);
    }
    return out;
}
```

- [ ] **Step 5: DocxExporter 两阶段流程**

`src/core/DocxExporter.h` 增加成员与私有槽：

```cpp
    QString m_docDir;
    QString m_outputPath;
    QString m_lastHtml;
    QStringList m_mermaidSources;
    QVector<MermaidRenderer::Result> m_mermaidResults;
    MermaidRenderer *m_renderer = nullptr;
private slots:
    void onMermaidRendered(const QVector<MermaidRenderer::Result> &results);
```

`src/core/DocxExporter.cpp` 中 `exportToDocx` 改为：

```cpp
void DocxExporter::exportToDocx(const QString &html, const QString &docDir, const QString &outputPath)
{
    if (html.isEmpty()) { emit exportError("导出失败：编辑器内容为空"); return; }
    m_docDir = docDir;
    m_outputPath = outputPath;
    m_lastHtml = html;
    m_mermaidSources = extractMermaidSources(html);
    if (m_mermaidSources.isEmpty()) {
        if (writeDocx(html, docDir, outputPath)) emit exportFinished(true, outputPath);
        else emit exportError("DOCX 生成失败");
        return;
    }
    if (!m_renderer) {
        m_renderer = new MermaidRenderer(this);
        connect(m_renderer, &MermaidRenderer::finished, this, &DocxExporter::onMermaidRendered);
    }
    m_renderer->renderAll(m_mermaidSources);
}

void DocxExporter::onMermaidRendered(const QVector<MermaidRenderer::Result> &results)
{
    m_mermaidResults = results;
    QString html = substituteMermaidPlaceholders(m_lastHtml);   // mermaid 块 → <p data-mermaid-idx>
    if (writeDocx(html, m_docDir, m_outputPath)) emit exportFinished(true, m_outputPath);
    else emit exportError("DOCX 生成失败");
}
```

- [ ] **Step 6: ParseState 挂结果 + parseBody 处理占位**

`ParseState`（`usedAbstractNums` 附近）加：

```cpp
    QString docDir;                                       // 图片/占位路径解析用
    QVector<MermaidRenderer::Result> mermaidResults;      // 渲染结果（按 idx）
    QStringList mermaidSources;                           // 源码（渲染失败回退用）
```

`writeDocx` 里 `parseBody(reader, state)` 之前，注入：

```cpp
    state.docDir = docDir;
    state.mermaidResults = m_mermaidResults;
    state.mermaidSources = m_mermaidSources;
```

`parseBody` 的 `p` 分支改为先识别占位：

```cpp
        if (tag == "p") {
            if (r.attributes().hasAttribute("data-mermaid-idx")) {
                int idx = r.attributes().value("data-mermaid-idx").toInt();
                while (!r.atEnd()) {                     // 消费空 <p> 到 </p>
                    auto t = r.readNext();
                    if (t == QXmlStreamReader::EndElement
                        && r.name().toString().compare(QLatin1String("p"), Qt::CaseInsensitive) == 0)
                        break;
                }
                if (idx >= 0 && idx < s.mermaidResults.size()) {
                    const auto &res = s.mermaidResults[idx];
                    if (!res.svg.isEmpty() && !res.png.isEmpty()) {
                        int pngRId = addMedia(s, res.png, "image/png", "png");
                        int svgRId = addMedia(s, res.svg.toUtf8(), "image/svg+xml", "svg");
                        s.bodyXml += "<w:p><w:r>" + buildImageDrawingXml(s.nextDocPrId++, pngRId, svgRId, res.cxEmu, res.cyEmu) + "</w:r></w:p>\n";
                    } else if (idx < s.mermaidSources.size()) {
                        // 渲染失败：回退源码文本
                        s.bodyXml += "<w:p><w:r><w:t xml:space=\"preserve\">" + xmlEscape(s.mermaidSources[idx]) + "</w:t></w:r></w:p>\n";
                    }
                }
                continue;
            }
            s.bodyXml += parseParagraph(r, "p", s) + "\n";
        }
```

- [ ] **Step 7: addMedia 支持 SVG 扩展名**

Task 5 的 `addMedia` 已按 `ext` 拼文件名、contentType 由调用方传入，无需改逻辑；仅确认 SVG 调用传 `contentType="image/svg+xml"`、`ext="svg"`，relationship Type 仍为 `.../relationships/image`（OOXML 的 SVG 走 image 关系，正确）。`writeDocx` 的 content-types 追加已按实际扩展名去重生成，SVG 会输出 `<Default Extension="svg" ContentType="image/svg+xml"/>`。

- [ ] **Step 8: 写测试（纯函数层）**

`tests/test_docxexporter.cpp` 加：

```cpp
    void testExtractMermaidSources() {
        QString html = "<pre><code class=\"language-mermaid\">graph TD;A--&gt;B;</code></pre>";
        QStringList srcs = DocxExporter::extractMermaidSources(html);
        QCOMPARE(srcs.size(), 1);
        QVERIFY(srcs[0].contains("graph TD"));
    }
```

（渲染器本体 QWebEngine 依赖，不写 ctest；端到端手动验证。）

- [ ] **Step 9: 跑测试 + 编译主程序**

Run: `cmake --build build -j$(nproc) && cd build && ctest --output-on-failure`
Expected: 7 项 C++ 全绿（含新 test_pdfexporter/test_docxexporter 新用例），主程序 md-editor 编译通过。

- [ ] **Step 10: Commit**

```bash
git add src/core/MermaidRenderer.h src/core/MermaidRenderer.cpp src/CMakeLists.txt src/core/DocxExporter.h src/core/DocxExporter.cpp tests/test_docxexporter.cpp
git commit -m "feat: DOCX 导出 mermaid 图（离屏渲染 SVG+PNG 嵌入）"
```

---

## 自检清单

- **Spec 覆盖**：§2 docDir → Task 1；§3 PDF 图片 → Task 2；§4 PDF mermaid → Task 3+4；§5 DOCX 图片 → Task 5；§6 DOCX mermaid → Task 6；§7 测试 → 各 Task 内嵌；§8 风险（QWebEngine 无头不可测）→ Task 6 Step 8 说明 + 纯函数兜底。
- **可回退性**：Task 1~5 完成后已交付「PDF/DOCX 图片 + PDF mermaid」；Task 6 单独交付「DOCX mermaid」，若渲染器风险过大可独立取舍，不影响前五任务。
- **手动端到端验证点**（提交前务必跑真实程序）：`./scripts/run-editor.sh` → 打开含图片+ mermaid 的文档 → 分别导出 PDF 与 DOCX → 确认图片与图表可见、PDF 无 QResource 报错、DOCX 在 Word/WPS 打开图表正常。
