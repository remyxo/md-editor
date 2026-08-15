# CLAUDE.md — Markdown Editor 开发指南

## 项目概述

基于 Qt6 + TipTap/ProseMirror 的桌面端 Markdown WYSIWYG 编辑器。C++ 负责桌面外壳，QWebEngineView 内嵌 TipTap 编辑器页面，C++ ↔ JS 通过 QWebChannel 双向通信。

## 关键原则

1. **所有文档和注释使用中文**，代码标识符（类名、方法名、变量名）使用英文
2. **版本号全局唯一**：定义在 `CMakeLists.txt` 的 `project(md-editor VERSION x.y.z)`，C++ 侧通过 `MD_EDITOR_VERSION` 宏引用
3. **提交规范**：`feat:` / `fix:` / `chore:` 前缀，中文描述
4. **版本推进**：增量较大（新增内容多、代码增删改量大）→ `+0.1.0`；增量较小（主要是 bug 修复、代码量小）→ `+0.0.1`。用户明确指定目标版本时以用户为准。每次推进需同步更新 `CMakeLists.txt` 中 `project(md-editor VERSION x.y.z)`、`CHANGELOG.md`、以及打 tag
5. **编辑区内部存储 HTML**：读文件时 MD→HTML，写文件时 HTML→MD，通过 `MarkdownHandler` 转换
6. **先讨论设计再编码**：不确定的地方先和用户确认
7. **往返保真（Round-Trip Fidelity）**：MD→HTML→MD 转换必须保证信息不丢失。详见下方「往返保真规则」
8. **兼容基准为 Qt 6.4.2**：CI/发布 DEB 在 `ubuntu-24.04`（Qt 6.4.2）构建，本地开发机为 6.8.x，二者行为有差异。新增任何依赖 Qt `toMarkdown()`/`setHtml()` 行为的逻辑，必须以 **6.4.2 的行为为基准设计与验证，不得依赖 6.8.x 独有的更激进转义**。详见 [`docs/qt-642-escaping-compat.md`](docs/qt-642-escaping-compat.md)
9. **改资源/源码后必须重新编译**：`src/resources/` 下打进 qrc 的文件（`web/` 的 .js/.css/.md、`themes/` 的 .qss）与任何 C++ 源码，改动后必须 `cmake --build build`——qrc 打包进二进制，不重编译用户跑旧版。告知用户「已修复」前，先确认二进制时间戳新于所有改动源文件

## 架构

```
MainWindow (Qt)
├── MenuBar     →    信号 → MainWindow 编排
├── ToolBar     →    信号 → runJsCommand() → QWebChannel → editor.js
├── SideBar
│   ├── FileTree      (QFileSystemModel, *.md 过滤)
│   └── OutlineTree   (QStandardItemModel，标题层级)
├── TabWidget
│   └── EditorTab     每个标签页 = EditorWebView (QWebEngineView 子类)
├── StatusBar         行号/列号/字数
└── SearchBar         (隐藏面板)
```

## 往返保真规则

**核心原则：MD→HTML→MD 完整转换链路中，不得丢失用户写入的任何语义信息。**

### HTML 标签转换规则

| 规则 | 适用标签 | 说明 |
|------|---------|------|
| **有 MD 等价语法的→转为 MD 语法** | `<sub>`→`~x~`、`<sup>`→`^x^`、`<mark>`→`==x==`、`<hr>`→`---` | 输出可移植的标准 Markdown |
| **无 MD 等价语法的→保留字面 HTML** | `<br>`、`<kbd>`、`<small>`、`<span style>`、`<div>` | `\n` 是字符不是语法，不得用于替代 `<br>` |

### 实现机制

```
toHtml (MD→HTML)                   fromHtml (HTML→MD)
─────────────────                   ─────────────────
手写 HTML 标签 → ZZ* 占位符保护      HTML 标签 → ZZ* 占位符保护
  → setMarkdown（标签在占位符中安全）   → setHtml → toMarkdown
  → 还原占位符 → 完整 HTML              → 还原占位符 → 完整 MD
```

### 相关文档

- **[`<br>` 与软换行往返机制](docs/br-and-softbreak-roundtrip.md)** — BRMARKER/data-brk 三层分离架构、占位符速查表

## 通信协议

```
C++ → JS:  QWebEnginePage::runJavaScript("window.editorApi.xxx()")
JS → C++:  editor.js → QWebChannel → EditorBridge 槽 → 信号 → UI 组件
```

### EditorBridge 关键槽方法（JS 可调用）

| 槽 | 参数 | 说明 |
|----|------|------|
| `cursorChanged` | (int line, int col) | 光标位置 |
| `contentChanged` | (bool dirty) | 脏标记 |
| `headingsChanged` | (QString json) | 标题结构 JSON |
| `formatChanged` | (QString json) | 格式状态 JSON（bold/italic/heading/table…） |

### editor.js API（C++ 可调用）

所有编辑命令通过 `window.editorApi.xxx()` 调用，详见 `src/resources/web/editor.js`。

## 脏状态机制

`editor.js` 通过 `_dirty` 标志手动跟踪（TipTap/ProseMirror **没有** `isDirty` 属性）：
- `init()` 后和 `setContent()` 加载文件时重置 `_dirty = false`
- `onUpdate` 首次触发时设置 `_dirty = true`，之后不重复通知
- `markClean()` 保存后清除并通知 C++
- `_loading` 锁抑制 `setContent` 内部触发的 `onUpdate`

## 关键文件

| 文件 | 职责 |
|------|------|
| `src/ui/MainWindow.cpp` | 信号编排中枢，所有组件在此连接 |
| `src/core/EditorBridge.cpp` | C++/JS 通信核心 |
| `src/ui/EditorTab.cpp` | QWebEngineView 封装 + 中文右键菜单 |
| `src/resources/web/editor.js` | TipTap 初始化 + editorApi + 状态同步 |
| `src/resources/web/tiptap-entry.js` | esbuild 打包入口，需 npm + esbuild 重建 |
| `src/resources/themes/light.qss` | 亮色主题 QSS |
| `src/resources/themes/dark.qss` | 暗色主题 QSS |
| `src/core/MarkdownHandler.cpp` | MD↔HTML 转换（基于 QTextDocument） |
| `src/core/FileManager.cpp` | 文件 I/O + 自动保存 + 文件监控 |

## 环境要求

- **开发环境**：Debian 13，Qt6 系统安装 (`apt install qt6-base-dev qt6-webengine-dev qt6-webchannel-dev`)，本地为 Qt 6.8.x
- **运行方式**：`./scripts/run-editor.sh` 或直接 `build/src/md-editor`
- **GitHub Actions**：`ubuntu-24.04`，Qt 6.4.2（**兼容基准**，本地 6.8.x 通过不等于出厂正确，差异见 `docs/qt-642-escaping-compat.md`）

## 构建命令

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug
make -j$(nproc)
ctest --output-on-failure
```

## 已知问题 & 待完善

- PDF 导出尚未完整集成到 MainWindow（`PdfExporter` 已实现，需接线）
- 搜索栏的查找/替换功能需对接编辑器
- 数学公式（LaTeX） → 二阶段
- `tiptap-all.js` 更新需在 `src/resources/web/` 下运行 `npx esbuild tiptap-entry.js --bundle --outfile=tiptap-all.js --format=iife`

## 专题文档

关键问题的深度分析与解决方案，修改相关代码前**务必先阅读**：

- **[Mermaid SVG 渲染问题](docs/mermaid-svg-rendering.md)** — 负宽度 rect 报错 / 字体缩放 / 甘特图缩块 / `_sanitizeMermaidSvg` 设计规范
- **[模态对话框 ESC 关闭机制](docs/modal-dialog-esc-handling.md)** — Qt QShortcut 拦截 Esc → Bridge 通知 → 分流处理
- **[\<br\> 与软换行往返机制](docs/br-and-softbreak-roundtrip.md)** — BRMARKER/data-brk 标记 / 双 TipTap 节点 / toHtml/fromHtml 三层分离架构
- **[Qt 6.4.2 转义兼容](docs/qt-642-escaping-compat.md)** — 6.4.2 与 6.8.x 的 toMarkdown 转义差异 / 占位符保护范式 / Docker 容器验证法
- **[代码块围栏往返机制](docs/code-fence-roundtrip.md)** — 嵌套围栏三坑：解析层围栏长度 / 序列化层反引号数推断 / 占位符段落重排粘连
