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
8. **兼容基准为 Qt 6.10.2（最低运行时 6.8.2）**：CI/发布 DEB 在 `ubuntu-26.04`（Qt 6.10.2）构建，本地开发机为 6.8.x，DEB 运行时依赖 `>= 6.8`（最低 6.8.2，6.4.x 已停止支持）。新增任何依赖 Qt `toMarkdown()`/`setHtml()` 行为的逻辑，必须以**占位符保护实现版本无关转义**，覆盖 6.8.2 → 6.10.2 及未来版本，不得依赖单一版本的转义行为。详见 [`docs/qt-642-escaping-compat.md`](docs/qt-642-escaping-compat.md)
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
| `src/app/ThemeManager.cpp` | QSS 主题加载（light/dark 实际主题） |
| `src/app/SystemThemeWatcher.cpp` | 系统深浅色检测（QStyleHints::colorScheme，Qt 6.5+ 跨平台 API） |
| `src/resources/web/editor.js` | TipTap 初始化 + editorApi + 状态同步 |
| `src/resources/web/table-column-layout.js` | 表格列宽测量/分配共享算法（编辑器与 HTML/PDF 导出单一来源；DOCX 侧经 getExportHtml 注入 live colgroup） |
| `src/resources/web/tiptap-entry.js` | esbuild 打包入口，需 npm + esbuild 重建 |
| `src/resources/web/vendored-lowlight-plugin.js` | vendored 代码块高亮 plugin（异步分批 + 增量重算，见 docs/vendor-lowlight-plugin.md） |
| `src/resources/themes/light.qss` | 亮色主题 QSS |
| `src/resources/themes/dark.qss` | 暗色主题 QSS |
| `src/core/MarkdownHandler.cpp` | MD↔HTML 转换（基于 QTextDocument） |
| `src/core/ConversionQueue.{h,cpp}` | 每 tab 转换任务队列：QtConcurrent worker 执行 + GUI 线程完成回调（loadFile/保存/切源码模式共用，串行次序 + 加载代次护栏，W4-1） |
| `src/core/qt/QtMarkdownWriter.{h,cpp}` | vendored Qt 序列化器（去 80 列折行 + 版本固定，见 docs/vendor-qt-markdown-writer.md） |
| `src/core/FileManager.cpp` | 文件 I/O + 文件监控 |
| `src/core/DocxExporter.cpp` | DOCX 导出（基于 QTextDocument + zip） |
| `src/core/ExportCommon.{h,cpp}` | 三导出器共用：资源读取、属性转义、`</style>` 剥离、图片围栏+内容嗅探、页码文案单一来源（见 docs/export-visual-spec.md） |
| `src/core/HtmlExporter.cpp` | 自包含 HTML 导出（资源 base64 内联）；表格列宽归一脚本（`tableColumnNormalizeScript`）与 PDF 导出共用 |
| `src/core/PdfExporter.cpp` | PDF 导出：nativeHf 快路径（thead 页眉带 + 轻量分页 + 页脚覆盖层）与 paged.js 路径的版式对齐，见 docs/pdf-header-footer-layout.md |
| `src/resources/web/paged.polyfill.js` | vendored paged.js 分页引擎（PDF 页眉页脚/页码，见 docs/vendor-pagedjs.md） |

## 环境要求

- **开发环境**：Debian 13，Qt6 系统安装 (`apt install qt6-base-dev qt6-webengine-dev qt6-webchannel-dev qt6-tools-dev qt6-tools-dev-tools`)，本地为 Qt 6.8.x。`qt6-tools-dev` 提供 Qt6LinguistTools（`qt6_add_translations`），`qt6-tools-dev-tools` 提供 `lupdate`/`lrelease` 二进制
- **运行方式**：`./scripts/run-editor.sh` 或直接 `build/src/md-editor`
- **CI 构建策略（2026-09-18 起，500MB 制品配额）**：日常 CI 只构建 macOS DMG（`build-macos.yml`，手动 dispatch，制品保留 2 天）。DEB 本地构建测试（`./scripts/build-deb.sh`），EXE 本地 win11 VM 构建（`./scripts/build-windows-local.sh`）。`build-deb.yml` 转手动触发，**发版前手动跑一次**——`ubuntu-26.04`/Qt 6.10.2 兼容基准（本地 6.8.x 通过不等于出厂正确，差异见 `docs/qt-642-escaping-compat.md`）在 CI 自动构建取消后仅存此把关点；发版 Release 的 deb/exe 由本地构建后 `gh release upload` 手动挂载

## 构建命令

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug
make -j$(nproc)
ctest --output-on-failure
```

## 推送前预检（pre-push 钩子）

- 一次性启用（本机生效，换机需重跑）：`git config core.hooksPath .githooks`
- 改动含 C++（src/构建文件）时 push 前自动：容器 clang 语法检查（`scripts/verify-clang.sh`，镜像 `mdeditor-clang:trixie`）+ win11 VM MSVC 编译（`scripts/build-windows-local.sh --compile-only`）；无 C++ 改动秒过
- 编译错误硬阻断；环境故障（docker/镜像/VM/ssh）警告放行
- 逃生门：`git push --no-verify` 或 `MD_EDITOR_SKIP_PRECHECK=1`
- clang 镜像构建：`docker build -t mdeditor-clang:trixie -f scripts/docker/Dockerfile.mdeditor-clang scripts/docker/`

## 已知问题 & 待完善

- `tiptap-all.js` 更新需在 `src/resources/web/` 下运行 `npx esbuild tiptap-entry.js --bundle --outfile=tiptap-all.js --format=iife`
- **colorSpan 单 mark 无法嵌套继承（已知边界，2026-09-13 记录）**：TipTap/ProseMirror 的 mark 是集合语义，同型 mark 不能嵌套——嵌套 `<span style="color:red">外<span style="background-color:yellow">内</span>尾</span>` 载入编辑器时，内层 colorSpan **替换**外层（非叠加）：「内」= 默认色 + 黄底（CSS 规范应为继承的红字），且 getHTML 扁平化为兄弟 span。文字内容无损，视觉语义一轮往返改变。根治需把 colorSpan 拆为 color / background-color 两个独立 mark（牺牲「整串 style 原样保真」设计），属 schema 级重构，待独立需求评估。DOCX 导出的颜色栈（DocxExporter RunFormat）对嵌套/扁平两种输入各按其语义正确渲染
- **源码搜索高亮（2026-10-08 机制重构）**：旧「全文档折行点预测量 + 跨会话缓存」冷启动 318 行文档 13-20s、4k 分钟级，且编辑/窗口缩放使整表失效后长时间渲染陈旧坐标（色块漂移，用户实测）。已重构为**按需局部测量**：行号 = `linesOf(slice(0,p+1))−1`（一次 scrollHeight 读）、行首在 [p−256, p+1] 窗口内二分（T8 修正语义）——单匹配 ~15ms 与文档规模无关，渐进分片渲染（当前匹配优先）、resize 防抖自动重绘、每次渲染按当前文本现算（无缓存无失效态）。见 editor.js「匹配定位测量」节与 srcsearch-highlight.test.js（含限定读次数断言）
- **Linux/Windows 无单实例（待完善，2026-09-19 记录）**：双击文件各开独立实例——两平台文件管理器只按关联再拉进程，无系统级单实例服务；macOS 由 Launch Services 天然单实例，再开文件经 QFileOpenEvent 进既有实例新标签。对齐方案预留：QLocalServer 每用户 socket，新进程发现既有实例 → 发送文件路径后自退出；注意 Wayland 下窗口置前受限（或只能任务栏提醒）

## 专题文档

关键问题的深度分析与解决方案，修改相关代码前**务必先阅读**：

- **[导出视觉规范对照（编辑器/HTML/PDF/DOCX）](docs/export-visual-spec.md)** — 四个面的逐项数值对照与判定（媒介差异 vs 漂移）；改任何导出样式前先在此定调
- **[PDF 页眉页脚版式（Chromium 打印约束与两路对齐）](docs/pdf-header-footer-layout.md)** — 可打印区=纸−Qt边距 / 越界即幽灵文本 / 无逐页底部保留机制 ⇒ 轻量分页 + 版心模拟；**分页前测量几何**（视图宽=页盒内容宽、不包 `@media print`、滚动条压制、脚注浮动先于切块）与**表格预分页容量模型**（实测脚注区高、位移累加、余量 ≥20px）同文；改 `PdfExporter` 版式常量、`__paginateNativeHf` 或 `__prepaginateTables` 前必读
- **[Mermaid SVG 渲染问题](docs/mermaid-svg-rendering.md)** — 负宽度 rect 报错 / 字体缩放 / 甘特图缩块 / `_sanitizeMermaidSvg` 设计规范
- **[模态对话框 ESC 关闭机制](docs/modal-dialog-esc-handling.md)** — Qt QShortcut 拦截 Esc → Bridge 通知 → 分流处理
- **[\<br\> 与软换行往返机制](docs/br-and-softbreak-roundtrip.md)** — BRMARKER/data-brk 标记 / 双 TipTap 节点 / toHtml/fromHtml 三层分离架构
- **[Qt 转义兼容（6.8.2→6.10.2）](docs/qt-642-escaping-compat.md)** — 各版本 toMarkdown 转义差异 / 占位符保护范式 / 容器验证法
- **[HTML 属性转义与实体解码](docs/html-attr-escape-and-entity-decode.md)** — 属性值 escapeHtmlAttr/unescapeHtmlEntities 对称规则 / TipTap innerHTML 恒转义 / tiptap_serialize 真实链路夹具
- **[跨平台等宽字体与行内代码往返](docs/cross-platform-monospace-font.md)** — FixedFont 族名平台相关（monospace/Courier New/Menlo）/ Linux 测不出 Windows bug / monoSpanFamilyPattern 动态匹配
- **[代码块围栏往返机制](docs/code-fence-roundtrip.md)** — 嵌套围栏三坑：解析层围栏长度 / 序列化层反引号数推断 / 占位符段落重排粘连
- **[vendor QtMarkdownWriter 说明](docs/vendor-qt-markdown-writer.md)** — 80 列折行根治 / 与上游等价性验证 / 上游同步 diff 流程 / LGPL 义务与 GPL-3.0 决策
- **[vendor lowlight plugin 说明](docs/vendor-lowlight-plugin.md)** — 代码块高亮异步化（init 零高亮 / 空闲分批 / step 相交增量）/ 高亮缓存与 PluginKey 单例 / 上游同步与保底降级
- **[vendor paged.js 说明](docs/vendor-pagedjs.md)** — PDF 分页引擎来源/版本/同步流程
