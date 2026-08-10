# 内联颜色样式支持 + 移除两端对齐按钮

> 状态：已确认，待写实现计划
> 日期：2026-08-10
> 范围：MVP（颜色 span 真正可渲染/编辑/往返 + 工具栏颜色入口 + 精简对齐按钮）

## 1. 目标与范围

### 背景

用户报告 `<span style="...">text</span>` 在 WYSIWYG 编辑器中不生效、保存后丢失。排查确认：`MarkdownHandler` 已有 `<span style>` 行内保护逻辑（`extractHtmlInlineElements`，原本服务于 0.3.4 的源码模式 HTML 视图往返），但 **TipTap 的 schema 没有任何扩展认识 `<span style>`**，`setContent` 时 ProseMirror 剥离标签、仅保留文本 → 渲染无样式；`getHTML` 取到的已是纯文本，保存时保护逻辑无米下炊。相比之下 `<u>` 因 StarterKit v3 内置 Underline 扩展（`parseHTML` 认识 `<u>`）而完整工作。

### MVP 目标

- **TipTap 认识带色 span**：新增 `colorSpan` mark（方案 B：attrs 存原始 style 字符串），`parseHTML`/`renderHTML` 让带 style 的 span 进出编辑器。
- **读写无损往返**：源码中手写的 `<span style="color:red">x</span>` 打开 → 编辑 → 保存，style 100% 不丢失。
- **工具栏颜色入口**：新增「文字色」「背景色」两个按钮，弹出调色板（~12 预设色 + 「无颜色」+ 「更多颜色…」调 `QColorDialog`）。
- **格式状态回显**：光标位于带色文本时，调色板高亮当前颜色。
- **底层保真、UI 限色**：UI 只产生颜色类 style，但 mark 层对任意 style 字符串往返保真（不静默丢弃手写的 `font-size` 等）。

### 附带改动

- **移除「两端对齐」按钮**：精简工具栏（按钮过多）。

### 非目标（独立后续 spec）

- **对齐（`text-align`）HTML 往返保真**：当前 `editor.js:619` 注释"对齐信息不在 .md 中保存，往返有损"。对齐作用在块级元素（`<p>`/`<h1-6>`），需新建块级 HTML 保护逻辑，且 Qt `QTextDocument` 对 `<p style="text-align">` 的往返行为未知，需先写探针测试。本期不做，留独立 spec。

## 2. 数据模型（colorSpan mark — 方案 B）

在 `editor.js` 内用 `TipTapMark.create()` 自建 mark。需先在 `tiptap-entry.js` 暴露 `Mark`（当前仅暴露 `Extension/InputRule/Node`）。

| 项 | 值 |
|----|----|
| `name` | `colorSpan` |
| `addAttributes` | `style: { default: '' }`（原始 style 字符串，原样存取） |
| `parseHTML` | `{ tag: 'span', getAttrs: el => { const s = el.getAttribute('style'); return s ? { style: s } : false; } }` |
| `renderHTML` | `['span', { style: attrs.style || null }, 0]`（空 style 输出裸 span，正常流程不出现） |
| `addCommands` | `setColorSpan(style)` → `setMark('colorSpan', { style })`；`unsetColorSpan()` → `resetMark('colorSpan')` |

### parseHTML 的 span 冲突处理（关键）

`getAttrs` 对**无 `style` 属性的 span 返回 `false`**，使 ProseMirror 跳过本规则、继续尝试其他规则。这保证脚注的 `<span class="footnote-marker">` / `<span class="footnote-content">`（均无 style）仍由 footnote 扩展匹配，不被 `colorSpan` 吞掉。参考 `tiptap-extension-underline.js` 中 `getAttrs: style => ... ? {} : false` 的同一范式。

## 3. 数据流（往返）

**读文件**：MD `<span style="color:red">x</span>` → `toHtml` `extractHtmlInlineElements` 保护为 `ZZHI{n}ZZ` 占位符 → 穿过 `QTextDocument::setMarkdown` → 后处理还原为原始 span → `setContent` → `colorSpan.parseHTML` 提取 style 存 attrs → 渲染为带色文本。

**写文件**：`getHTML` → `colorSpan.renderHTML` 输出 `<span style="...">x</span>` → `fromHtml` 保护正则匹配（`MarkdownHandler.cpp:1102`）→ 占位符穿过 `QTextDocument::toMarkdown` → 逆序还原（`MarkdownHandler.cpp:1160`）→ 写回 .md。

**多 mark 叠加**（如颜色 + 粗体）：`<strong><span style="color:red">x</span></strong>`，`fromHtml` 保护正则匹配内层 span，粗体走正常 `toMarkdown`，无嵌套往返问题。

## 4. 改动清单

| 层 | 文件 | 改动 |
|----|------|------|
| 打包入口 | `tiptap-entry.js` | `import { Mark } from '@tiptap/core'` + `window.TipTapMark = Mark`，运行 `build-tiptap.sh` 重建 `tiptap-all.js` |
| TipTap 扩展 | `editor.js` | 新增 `buildColorSpanExtension()`（`TipTapMark.create`），在 `_buildExtensions` 注册 |
| 命令 | `editor.js` | `setColorSpan(style)` / `unsetColorSpan()` |
| 格式状态 | `editor.js` `notifyFormat` | 增加 `colorSpanStyle = getAttributes('colorSpan').style \|\| ''` |
| C++/JS 编排 | `MainWindow.cpp` | 连接两个新工具栏信号 → `runEditorCommand("window.editorApi.setColorSpan('...')")` |
| 工具栏 | `ToolBar.cpp/.h` | 新增文字色/背景色按钮 + 调色板弹出（`QMenu` + 颜色网格 `QWidgetAction`）+ 「更多颜色」调 `QColorDialog` |
| MarkdownHandler | `MarkdownHandler.cpp` | **预期无需改**（现有 `<span style>` 行内保护正则已对称）—— 实现时用往返测试验证 |
| 工具栏（附带） | `ToolBar.cpp/.h`、`editor.js` | 移除「两端对齐」按钮 + `TextAlign.configure` 限定 `alignments:['left','center','right']` |

## 5. 工具栏 UI 设计

### 颜色按钮

- **「文字色」按钮**：图标为带色条下划线的 `A`；**「背景色」按钮**：图标为高亮块。
- 点击 → 弹出 `QMenu`：
  - 顶部「无颜色」项 → `unsetColorSpan()`
  - 中部颜色网格（~12 预设色，如 黑/灰/红/橙/黄/绿/青/蓝/紫/粉/棕/白）
  - 底部「更多颜色…」项 → `QColorDialog::getColor()`
- 选定颜色 → 构造 style 字符串（文字色 `color:#rrggbb`，背景色 `background-color:#rrggbb`）→ `setColorSpan(style)`

### 格式状态回显

`notifyFormat` 携带 `colorSpanStyle`，C++ 侧（`ToolBar::setFormat`）解析出当前 `color` / `background-color`，在调色板中高亮对应色块；无颜色不高亮。按钮图标色条可选更新为当前色。

### 接入链路（参考 underline）

`ToolBar` 信号 `textColorSelected(QString)` / `bgColorSelected(QString)` → `MainWindow.cpp` `connect` → `runEditorCommand("window.editorApi.setColorSpan('color:#ff0000');")`。

## 6. 移除「两端对齐」按钮

| 文件 | 删除内容 |
|------|----------|
| `ToolBar.h` | `m_alignJustifyAction` 声明 |
| `ToolBar.cpp` | 初始化（`m_alignJustifyAction(nullptr)`）、`makeAction` 创建、`m_alignGroup->addAction`、`connect` → `alignSelected("justify")`、`setFormat` 中 justify 的 `setChecked` |
| `editor.js` | `TextAlign.configure({ types:['heading','paragraph'], alignments:['left','center','right'] })`（防御性禁用 justify） |

**回归评估**：现有 .md 不含持久化的 justify 对齐（对齐本就往返有损、从未保存），移除按钮无数据影响。

## 7. 边界与错误处理

- **JS 注入**：`setColorSpan` 的 style 字符串用单引号包裹传入；颜色值（`#rrggbb` 或命名色）无特殊字符，`MainWindow` 拼接时对 `'` 做转义防御。
- **`QColorDialog` 取消**：不触发命令，保持当前选区不变。
- **方案 B 保真语义**：mark 层保留任意 style 字符串往返；UI 只产生颜色类。手写的 `font-size` 等非颜色 style 也会原样保留（符合用户选择的"UI 限色、底层保真"）。
- **空选区**：`setColorSpan` 在无选区时光标处无效果（`setMark` 需选区）；UI 可在无选区时禁用按钮或提示，实现时定。

## 8. 测试策略

### C++ 层（`tests/test_markdownhandler.cpp`，QTest）

新增用例：
- `<span style="color:red">x</span>` 的 `toHtml` → 含还原后的 span
- `<span style="color:red">x</span>` 的 `fromHtml`（往返）→ 保留原始 span
- span 内含其他 markdown 语法（如 `<span style="color:red">**粗**</span>`）的提取/还原顺序

### TipTap 层（手动验证，无 QTest 覆盖）

以下需在运行的应用中人工验证（**测试边界限制**）：
- 手写 `<span style="color:red">` 打开后渲染为红色
- 工具栏设文字色/背景色/清除颜色
- 编辑带色文本后保存，重新打开颜色仍在
- 带色 span 与脚注共存，脚注 span 不被吞
- 颜色 + 粗体叠加渲染与往返
- 移除 justify 后，左/居中/右对齐按钮仍工作

## 9. 版本与 CHANGELOG

- **版本**：`0.3.6`（默认 +0.0.1）
- **CHANGELOG 规划**（打 tag 时写入，见记忆 `update-changelog-on-tag`）：
  - **新增**：内联颜色样式支持（`<span style>` WYSIWYG 渲染 + 工具栏文字色/背景色按钮 + 调色板）
  - **变更**：移除「两端对齐」按钮精简工具栏
