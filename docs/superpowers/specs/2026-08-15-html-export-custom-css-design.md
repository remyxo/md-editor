# HTML 导出 + 自定义 CSS + 文档记账清理 —— 设计规格

**日期**: 2026-08-15
**状态**: 已确认
**版本目标**: 0.8.0

---

## 1. 背景与目标

回顾设计之初（`docs/superpowers/specs/2026-08-02-md-editor-design.md` §7）的规划，二阶段「HTML + 更丰富展现」中尚有 **HTML 导出** 与 **自定义 CSS 样式** 两项未落地（行内 HTML、DOCX 导出已实现）。同时项目文档存在记账漂移（CLAUDE.md「已知问题」过时、README 仍列已删除的自动保存、DOCX 导出未入 CHANGELOG）。

本规格覆盖三块工作：

| 模块 | 内容 |
|------|------|
| A. HTML 导出 | 新增 `HtmlExporter`，自包含单文件、完整渲染、资源全内联（base64） |
| B. 自定义 CSS | 设置新增自定义 CSS 编辑区，注入编辑器渲染 + 应用于 HTML/PDF 导出 |
| C. 文档记账清理 | 更新 CLAUDE.md / README / CHANGELOG 三处过时或遗漏 |

版本推进：合计属「新增内容多」→ 0.7.0 → **0.8.0**。

---

## 2. A 模块 —— HTML 导出

### 2.1 需求

将当前文档导出为**单个自包含 HTML 文件**，双击即可在浏览器中离线打开，数学公式、Mermaid 图表、代码高亮、图片全部完整渲染，与编辑器所见一致。

### 2.2 组件

新增 `src/core/HtmlExporter.{h,cpp}`，复刻 `PdfExporter`/`DocxExporter` 的 QObject + 信号模式：

```cpp
class HtmlExporter : public QObject {
    Q_OBJECT
public:
    explicit HtmlExporter(QObject *parent = nullptr);
    // html: 编辑器 getContent() 返回的 WYSIWYG HTML
    // docDir: 当前文档所在目录（解析相对图片路径）
    // theme: "light" / "dark"（决定 mermaid/hljs/背景主题）
    void exportToHtml(const QString &html, const QString &docDir,
                      const QString &outputPath, const QString &theme);
signals:
    void exportFinished(bool success, const QString &outputPath);
    void exportError(const QString &error);
private:
    QString inlineCss(const QString &qrcPath) const;      // 读 qrc CSS 文本
    QString inlineCssFonts(const QString &css) const;     // url(fonts/x) → base64 data URI
    QString inlineJs(const QString &qrcPath) const;       // 读 qrc JS 文本，内联为 <script>
    QString inlineImages(const QString &bodyHtml, const QString &docDir) const; // <img src> → base64
    QString bodyCss(const QString &theme) const;          // 正文排版 CSS
};
```

### 2.3 数据流

与 `onExportPdf` / `onExportDocx` 完全一致：

```
MainWindow::onExportHtml()
  → QFileDialog 选 .html
  → runJavaScript("window.editorApi.getContent()")   // 拿 WYSIWYG HTML
  → HtmlExporter::exportToHtml(html, docDir, path, theme)
  → 写盘 → exportFinished → 状态栏提示
```

### 2.4 自包含 HTML 拼装步骤

产出单文件，按以下顺序内联：

1. **正文排版 CSS**：从 PdfExporter 现有 print CSS 提炼复用（标题/段落/表格/引用/代码块底色、块级公式居中、`img{max-width:100%}` 等），去掉 `@media print` 与 `@page`，改为屏幕渲染样式。
2. **KaTeX**：`:/web/katex.min.css` 内联，其 `url(fonts/KaTeX_*.woff2|woff|ttf)` 逐一替换为 `data:font/<mime>;base64,...`；`:/web/katex.min.js` 内联为 `<script>`。
3. **Mermaid**：`:/web/mermaid.min.js` 内联为 `<script>`。
4. **highlight.js**：`:/web/highlight.min.js` 内联为 `<script>`，配主题 CSS（亮 `github` / 暗 `github-dark`）。
5. **图片**：见 §2.6。
6. **渲染脚本**：`DOMContentLoaded` 后（i）`katex.render` 渲染所有 `[data-latex]`（`displayMode` 依 `data-type`）；(ii) 初始化 mermaid（`startOnLoad` + 扫描 `.language-mermaid` / mermaid 节点）；(iii) `hljs.highlightAll()`。
7. 组装 `<!DOCTYPE html><html><head>…</head><body>…</body></html>` 写盘。

### 2.5 主题

导出跟随当前编辑器主题：
- 亮色：mermaid `theme: 'default'`、hljs `github`、body 白底深字
- 暗色：mermaid `theme: 'dark'`、hljs `github-dark`、body 深底浅字

`theme` 由 MainWindow 传入（复用现有主题状态）。

### 2.6 图片内联

`getContent()` 输出的 `<img src>` 为**原始路径**（相对路径或绝对路径，非 `localimg://`，因 `localimg://` 仅存在于 NodeView 的 DOM 层，ProseMirror 序列化 attrs.src 输出原始值）。

处理：正则扫描 `<img ... src="...">`，对每个 src：
- 若已是 `data:` / `http(s):` → 跳过
- 若以 `/` 开头（绝对路径）→ 直接读该文件
- 否则相对路径 → 相对 `docDir` 解析 → 读该文件
- 读盘成功 → 替换为 `data:image/<ext>;base64,...`
- 读盘失败 → 保留原 src（不阻断导出）

实现注意：`__pending_` 占位图、缺文件时静默保留，不报错中断。

---

## 3. B 模块 —— 自定义 CSS

### 3.1 需求

用户可在设置中定义自定义 CSS，覆盖编辑器渲染，并在 HTML/PDF 导出时一并应用（对应设计规格 §7 二阶段「自定义 CSS 样式」）。

### 3.2 设置 UI

`SettingsDialog`「外观」页新增分组「自定义 CSS」：`QPlainTextEdit` 多行编辑框 + 提示文案（「高级：覆盖编辑器与导出的默认样式」）。

### 3.3 存储

`QSettings("md-editor", "md-editor")` 键 `editor/customCss`（QString）。

### 3.4 注入编辑器

- `editor.js` 新增 `window.editorApi.applyCustomCss(css)`：创建/更新页面内 `<style id="custom-css">`（空串则移除该 style）。
- C++ 侧镜像现有 `setEditorTheme` 接线：
  - 设置保存后：遍历所有标签页调用 `applyCustomCss`
  - 新标签页加载完成后：补推一次
  - 应用启动时：加载设置后应用

### 3.5 导出应用

- `HtmlExporter`：构建 HTML 时读 `editor/customCss`，内联在默认样式**之后**（后写覆盖）。
- `PdfExporter`：同法，在 `fullHtml` 的 `<style>` 末尾追加 customCss（覆盖 print CSS 中同名规则）。

---

## 4. C 模块 —— 文档记账清理

### 4.1 CLAUDE.md

- 「已知问题 & 待完善」：
  - 移除「PDF 导出尚未完整集成」（已接线）、「搜索栏查找/替换需对接」（0.6.0 已完成）、「数学公式 → 二阶段」（0.2.0 已实现）
  - 保留 tiptap-all.js esbuild 重建说明（构建操作，非功能缺口）
  - 补充当前状态：DOCX 导出、HTML 导出、自定义 CSS
- 「关键文件」表：补 `DocxExporter`、`HtmlExporter`

### 4.2 README.md

- 功能清单移除「自动保存」（0.2.1 已删除）
- 补 DOCX 导出、HTML 导出（本轮后）、搜索增强等

### 4.3 CHANGELOG.md

- 补录 DOCX 导出（历史遗漏）
- 本轮 HTML 导出 + 自定义 CSS 记入 0.8.0

---

## 5. 测试策略

| 层级 | 范围 | 工具 |
|------|------|------|
| C++ 单元测试 | `HtmlExporter`：自包含结构完整、KaTeX 字体 url 已 base64、图片已内联、mermaid/hljs 脚本存在；customCss 读/写/注入 | Qt Test |
| 手工验收 | 导出含公式/图表/代码/图片的文档，浏览器离线打开验证 | 手工 |

---

## 6. 非目标（本轮不做）

- 插件系统（设计 §1.2 明确「暂不包含」）
- 界面语言 English 国际化、主题「跟随系统」（低优先级，另行立项）
- 标签页交互补全、大纲光标高亮（体验优化，另行立项）
- 自动保存恢复（一阶段遗留，需另行确认是否恢复）
- PDF 页眉页脚/页码（一阶段遗留，另行立项）
