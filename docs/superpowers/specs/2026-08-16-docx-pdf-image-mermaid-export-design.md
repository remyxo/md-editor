# DOCX/PDF 图片与 Mermaid 导出 —— 设计规格

**日期**: 2026-08-16
**状态**: 已确认
**版本目标**: 待定（并入 0.8.1 批量修复或另推进）

---

## 1. 背景与目标

当前 DOCX/PDF 导出存在两类缺陷：

1. **图片丢失**：PDF 导出把图片 src 以 `QUrl("qrc:/web/")` 为 baseUrl 交给 Chromium，绝对路径 `/home/.../x.jpg` 被解析为 `qrc:/home/...` 触发 `QResource ... not found` 报错；DOCX 导出把 `<img>` 一律降级为 `[图片: alt]` 占位文本。
2. **Mermaid 丢失**：PDF 导出未加载 `mermaid.min.js`、无渲染脚本，图表不渲染；DOCX 导出把 mermaid 当普通代码块只保留源码。

而 HTML 导出（`HtmlExporter`）已实现图片 base64 内联与 mermaid 渲染，是「能用的参照实现」。本规格将 PDF/DOCX 两条导出路径对齐到同一保真度。

| 模块 | 内容 |
|------|------|
| 公共 | `PdfExporter`/`DocxExporter` 增加 `docDir` 参数 |
| A. PDF 图片 | 复用 `HtmlExporter::inlineImages` 做 base64 内联 |
| B. PDF mermaid | 加载 mermaid.min.js + 渲染脚本（镜像 HtmlExporter） |
| C. DOCX 图片 | `<img>` 嵌入 OOXML media 部件 + `<w:drawing>` |
| D. DOCX mermaid | 离屏渲染 mermaid → SVG + PNG 兜底 → `<asvg>` 嵌入 |

---

## 2. 公共改动 —— `docDir` 参数

图片路径解析需要文档目录（相对路径 `images/x.png` 相对文档所在目录解析）。

- `PdfExporter::exportToPdf(html, docDir, outputPath, orientation, paperSize)` 新增 `docDir`。
- `DocxExporter::exportToDocx(html, docDir, outputPath)` 新增 `docDir`。
- `MainWindow::onExportPdf` / `onExportDocx` 传入 `QFileInfo(tab->webView()->property("filePath").toString()).absolutePath()`（与 `onExportHtml` 一致）。

路径解析规则与 `HtmlExporter::inlineImages` 一致：

```
path = src.startsWith('/') ? src : docDir + "/" + src
```

---

## 3. A 模块 —— PDF 图片

### 3.1 需求

PDF 导出图片正常显示，消除 `QResource ... not found` 报错。

### 3.2 方案

`PdfExporter::exportToPdf` 在构建 `fullHtml` 前先调用 `HtmlExporter::inlineImages(html, docDir)`：

- 把 `<img src="...">` 转 `data:<mime>;base64,...` 内联。
- 绝对路径 `/home/...` 原样读；相对路径 `docDir + "/" + src` 读。
- 读不到的文件保留原 src（不报错、不崩溃），浏览器侧显示破图占位。
- `data:`/`http(s):` 开头的 src 跳过。

`inlineImages` 已是 `HtmlExporter` 的 `static` 纯逻辑方法，直接复用，无需新增代码；PDF 侧只加一行调用。注意 `PdfExporter.cpp` 需 `#include "HtmlExporter.h"`。

---

## 4. B 模块 —— PDF mermaid

### 4.1 需求

PDF 导出渲染 mermaid 图为 SVG。

### 4.2 方案

镜像 `HtmlExporter` 的渲染脚本：

1. `fullHtml` 的 `<head>` 内，在 `<script src="katex.min.js"></script>` 旁追加 `<script src="mermaid.min.js"></script>`（qrc 相对路径，`qrc:/web/` baseUrl 可解析）。
2. `DOMContentLoaded` 脚本里，把 `pre code.language-mermaid` 逐个替换为渲染出的 SVG。**复用 `HtmlExporter::renderScript` 的 mermaid 逻辑**（含已修复的 holder/id 解耦 + IIFE 捕获迭代变量）。

实现上把 mermaid 渲染片段从 `HtmlExporter::renderScript` 提为独立 static helper `HtmlExporter::mermaidRenderScript(theme)`（返回纯 mermaid JS 片段），HTML 路径与 PDF 路径共用该 helper；`renderScript` 内部改为「KaTeX 段 + `mermaidRenderScript(theme)` + hljs 段」组合。PDF 侧在 `DOMContentLoaded` 的 KaTeX 循环后追加 `mermaidRenderScript(theme)` 返回的片段即可，不重复渲染 KaTeX。

### 4.3 与现有 KaTeX 渲染的关系

PDF 现用 `<script>` 内联的 DOMContentLoaded 循环渲染 `[data-latex]`。mermaid 渲染需在同一 DOMContentLoaded 之后、`printToPdf` 之前完成；`printToPdf` 在 `loadFinished` 信号触发后调用，DOMContentLoaded 已执行完毕，时机安全。

---

## 5. C 模块 —— DOCX 图片

### 5.1 需求

DOCX 导出把 `<img>` 嵌入为真实图片（OOXML media 部件），不再降级为 `[图片]` 占位。

### 5.2 数据模型扩展

`ParseState` 新增图片部件收集：

```cpp
struct ImagePart {
    int rId;            // relationship id
    QString path;       // 源文件绝对路径
    QString ext;        // png/jpg/gif/...
    QByteArray bytes;   // 图片字节
    int cxEmu, cyEmu;   // 宽高（EMU）
};
QList<ImagePart> images;
```

`ParseState::makeRId` 已存在（用于超链接），图片复用同一 rId 序列（与超链接共享计数即可，二者不冲突）。

### 5.3 解析嵌入

`parseBody`（块级 `<img>`）与 `parseInline`（行内 `<img>`）不再输出 `[图片]` 文本，改为：

1. 解析 src → 按 §2 规则得绝对路径 → `QFile` 读字节。
2. `QImageReader(path).size()` 读像素宽高 → `px * 9525` 转 EMU；读不到或读失败用默认 300×200（约 2.86cm × 1.9cm）。
3. 追加 `ImagePart` 到 `ParseState`，分配 rId。
4. 输出 `<w:r><w:drawing><wp:inline distT="0" distB="0" distL="0" distR="0"><wp:extent cx=".." cy=".."/><wp:docPr id=".." name="图片N"/><a:graphic><a:graphicData uri=".../picture"><pic:pic><pic:nvPicPr>...<pic:blipFill><a:blip r:embed="rIdN"/></pic:blipFill><pic:spPr><a:xfrm><a:ext cx=".." cy=".."/></a:xfrm></pic:spPr></pic:pic></a:graphicData></a:graphic></wp:inline></w:drawing></w:r>`。

图片读取失败 → 保留现有 `[图片: alt]` 占位降级（不阻塞导出）。

### 5.4 部件写入

`writeDocx` 打包前：

- 为每个 `ImagePart` 写 `word/media/imageN.<ext>`。
- `[Content_Types].xml` 追加 `<Default Extension="<ext>" ContentType="image/<ext>"/>`（png/jpeg/gif；svg 走 D 模块）。
- `document.xml.rels` 追加 `<Relationship Id="rIdN" Type=".../image" Target="media/imageN.<ext>"/>`。
- `word/_rels/document.xml.rels` 已由 `ParseState::relsXml` 收集超链接，图片 relationship 一并写入。

### 5.5 命名空间

`document.xml` 根元素需补 `xmlns:pic`、`xmlns:a`、`xmlns:wp` 命名空间（当前 `buildDocumentXml` 已声明 `wp`，需补 `a`、`pic`）。

---

## 6. D 模块 —— DOCX mermaid

### 6.1 需求

DOCX 导出渲染 mermaid 图为可视图（用户选择「内嵌 SVG」，兼容 Word 2016+）。

### 6.2 关键约束

- DocxExporter 是纯 C++（`QXmlStreamReader`），无 JS 引擎，mermaid 源码必须经**离屏浏览器**渲染。
- OOXML 的 `<a:blip r:embed>` 必填且指向栅格图；SVG 通过 `<a:extLst><a:ext uri="{96DAC541-...}"><asvg:svgBlip r:embed="svgRId"/></a:ext></a:extLst>` 额外挂载。故 SVG 嵌入仍需一张 **PNG 兜底**。
- 栅格化在 Chromium 内用 canvas 完成（`drawImage(svg) → canvas.toDataURL('image/png')`），规避 QSvgRenderer 不吃 mermaid `foreignObject` 的问题，且不加 QtSvg 依赖。

### 6.3 新增组件 `MermaidRenderer`

```cpp
class MermaidRenderer : public QObject {
    Q_OBJECT
public:
    struct Result { QString svg; QByteArray png; int cxEmu; int cyEmu; };
    void renderAll(const QStringList &sources);   // 异步
signals:
    void finished(const QVector<Result> &results);
};
```

实现：离屏 `QWebEngineView` 加载内嵌 mermaid.min.js 的 helper 页，`runJavaScript` 对每个 source 执行 `mermaid.render(id, source)` 得 SVG，再在 canvas 上 `drawImage` 得 PNG dataURL 与宽高，回传 C++。

### 6.4 DocxExporter 流程改造

`exportToDocx` 改为两阶段：

1. **同步解析**：`parseBody` 收集 mermaid 源码块（`pre code.language-mermaid`）为占位（如 `ZZMD{n}ZZ`），其余按现状生成 bodyXml。
2. **异步渲染**：若有 mermaid → `MermaidRenderer::renderAll`；无则直接 `writeDocx`（现有同步路径不变）。
3. **嵌入**：渲染完成后，把每个 `ZZMD{n}ZZ` 占位替换为 `ImagePart`（svg + png 双部件）+ 与 §5.4 类似的 media 写入；mermaid 的 `r:embed` 指向 PNG，`<asvg:svgBlip>` 指向 SVG。

渲染失败 → 该 mermaid 块回退为现有「源码代码块」。

### 6.5 部件写入

- `word/media/diagramN.svg` + `word/media/diagramN.png` 各一份。
- `[Content_Types].xml` 追加 svg/png 的 `<Default>`（svg 用 `image/svg+xml`）。
- `document.xml.rels` 追加两条 relationship（png 与 svg）。

---

## 7. 测试

| 测试 | 断言 |
|------|------|
| `test_pdfexporter`（新，纯字符串层） | 若 PDF 逻辑可测部分覆盖：inlineImages 后无相对/绝对 img src、含 data URI；mermaid 脚本注入 |
| `test_docxexporter`（扩） | 图片：`document.xml` 含 `<w:drawing>`/`<a:blip r:embed>`、`word/media/` 有部件、`[Content_Types].xml` 有 override；mermaid：含 `<asvg:svgBlip>`、svg/png 部件 |
| 现有 6 项 C++ / 31 项 JS | 全绿不回归 |

**CI 无头限制**：`MermaidRenderer` 依赖 QWebEngine 渲染，`ctest` 无头环境跑不了（需 Xvfb + 软件 GL，ubuntu-24.04 不稳定）。故 D 的渲染器本体**不写 ctest**，核心「占位符 → 部件」字符串逻辑用可测的纯函数覆盖；渲染器留待手动/端到端验证。

---

## 8. 依赖与风险

- **无新增 Qt 模块**：PNG 栅格化在 Chromium canvas 内完成，不引 QtSvg。
- **风险 1（D 渲染时机）**：离屏 QWebEngine 需事件循环跑完 JS 才回传结果；`MermaidRenderer` 内部用 `loadFinished` + `runJavaScript` 回调 + 结果信号串行化，避免竞态。
- **风险 2（D 在 CI 不可测）**：如上，渲染器本体不进 ctest，靠手动验证 + 纯字符串单测兜底。
- **风险 3（图片路径解析）**：绝对路径/相对路径/缺失文件三分支已在 `HtmlExporter::inlineImages` 验证，PDF 复用零新增风险；DOCX 侧用同一规则实现并加缺失文件占位降级。

---

## 9. 版本推进

合计为「修复 + 新增内容」——图片/mermaid 进 DOCX/PDF 属功能补全。是否推进版本号、目标版本（0.8.1 或 0.9.0）由用户届时指示；本规格不预设。
