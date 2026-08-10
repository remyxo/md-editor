# HTML 标签在内联颜色 span 编辑器中的支持 — 设计 spec

> **状态**：Draft · **日期**：2026-08-10 · **父 spec**：内联颜色 span（v0.3.6）

## 1. 范围

本 spec 为 TipTap WYSIWYG 编辑器新增以下 HTML 标签的解析、渲染和往返支持：

| 标签 | 类型 | 优先级 |
|------|------|--------|
| `<kbd>` | 新增 — 键盘按键 inline Mark | P0 |
| `<small>` | 新增 — 小字 inline Mark | P0 |
| `<div>` | 新增 — 块级容器 Block Node（仅包裹内联内容） | P0 |
| `<mark>` | 已有（`==text==` Highlight），验证手写 HTML 解析 | P1 |
| `<sub>` / `<sup>` | 已有（`~text~` / `^text^`），验证手写 HTML 解析 | P1 |
| `<hr>` | 已有（`---` HorizontalRule），验证手写 HTML 解析 | P1 |
| `<br>` | 已有（HardBreak），验证手写 HTML 解析 | P1 |

### 非目标

- `<div>` 包裹块级元素（`<p>`、`<ul>` 等）——此版本不支持。嵌套块的 `<div>` 需要 schema 重构，留待后续考虑以后再实现。
- 工具栏按钮或编辑器命令（`applyKbd`、`applySmall` 等）——纯解析/渲染阶段。仅在通过手写 HTML 或外部粘贴使用时才可用。
- `<div align>` 对齐——已通过 TextAlign 扩展处理（CSS `style="text-align:…"`），不需要变回 `<div align>`。
- `<kbd>`、`<small>` 或 `<div>` 的 Markdown 语法快捷方式——无 Markdown 语法，仅支持 HTML 标签。
- Table/thead/tbody/tr/td/th 内联样式——此部分未做任何改动。

## 2. 架构

与 `colorSpan` 一样，每个标签通过一个 TipTap 自定义扩展实现（inline 标记使用 `Mark.create`，块级节点使用 `Node.create`），并通过 MarkdownHandler.cpp 中的占位符保护进行往返（使用 `ZZ*` 模式）。

```
手写 HTML 或粘贴
      ↓
TipTap parseHTML  →  识别为 Mark/Node  →  WYSIWYG 渲染
      ↓
editor.getHTML()  →  HTML 序列化（包含新标签）
      ↓
MarkdownHandler::fromHtml()  →  MD（保护/还原原始 HTML 标签）
MarkdownHandler::toHtml()    →  HTML（保护/还原原始 HTML 标签）
```

### 全局约束（继承自项目 / 之前的 spec）

- 所有注释和文档使用中文，代码标识符（类名、方法名、变量名）使用英文
- 提交规范：`feat:` / `fix:` / `chore:` 前缀，中文描述，尾注 `Co-Authored-By: Claude <noreply@anthropic.com>`
- 测试：新增标签的 MarkdownHandler.cpp 来回归测试 + TipTap 层的手动验证
- JS 层：完全位于 `editor.js` 中，使用 `window.TipTapMark` / `window.TipTapNode`（已在 tiptap-entry.js 中导出并打包为 tiptap-all.js）
- TipTap 版本：v3.29.2（`Mark.create` 中 `renderHTML` 签名为 `({mark, HTMLAttributes})`，已在之前的 bug 修复中确认）

## 3. 标签设计

### 3.1 `<kbd>` — 键盘按键

- **类型**：Inline Mark
- **实现**：`TipTapMark.create({ name: 'kbd', ... })`
- **HTML 输出**：`<kbd>text</kbd>`
- **Markdown 语法**：无（仅手写 HTML 标签）
- **parseHTML**：匹配 `<kbd>` 标签
- **renderHTML**：`['kbd', HTMLAttributes, 0]`
- **CSS 样式**（`editor.css`）：
  ```css
  kbd {
    font-family: monospace;
    font-size: 0.85em;
    border: 1px solid #999;
    border-radius: 3px;
    padding: 0.05em 0.3em;
    background: #f0f0f0;
  }
  ```
- **与其它标记共存**：可以嵌套在 `bold`/`italic`/`colorSpan` 等标记内部/外部（ProseMirror 默认行为）

### 3.2 `<small>` — 小字

- **类型**：Inline Mark
- **实现**：`TipTapMark.create({ name: 'small', ... })`
- **HTML 输出**：`<small>text</small>`
- **Markdown 语法**：无
- **parseHTML**：匹配 `<small>` 标签
- **renderHTML**：`['small', HTMLAttributes, 0]`
- **CSS 样式**（`editor.css`）：`small { font-size: smaller; }`（浏览器默认，低优先级）

### 3.3 `<div>` — 块级容器

- **类型**：Block Node（只能包裹内联内容）
- **实现**：`TipTapNode.create({ name: 'divBlock', group: 'block', content: 'inline*', ... })`
- **HTML 输出**：`<div>content</div>`（可带有 `style`/`align`/`class` 属性）
- **Markdown 语法**：无
- **parseHTML**：匹配 `<div>` 标签（返回 `true` 代表匹配；无 style 属性的 `<div>` **不会被** `colorSpan` 拦截，因为 `colorSpan.parseHTML.getAttrs` 只有在存在 style 属性时才返回 `true`）。注意：**冲突风险** —— 用于内部结构的 `<div data-type="footnoteDef">` / `<div data-type="block-math">` 等需要被排除。在 `parseHTML` 中使用 `getAttrs: (el) => !el.hasAttribute('data-type')` 来排除内部节点。
- **renderHTML**：`['div', HTMLAttributes, 0]`（属性级 `renderHTML` 用于 `style`/`align`/`class`）
- **content**：`inline*`（和内联标记混排的文本内容）
- **CSS**：`div { display: block; }`（浏览器默认）
- **schema group**：将 `divBlock` 添加到 `block` group，这样就可以出现在文档根部。

### 3.4 `<mark>` — 高亮（已有，需验证/增强 parseHTML）

- TipTap Highlight 扩展（已注册为 `TipTapHighlight`）表现为 `<mark>` 标签
- Markdown 语法：`==text==`（`MarkdownHandler.cpp:612-615`）
- MarkdownHandler 往返：`toHtml`: `==x==` → `<mark>x</mark>`；`fromHtml`：`<mark>x</mark>` → `==x==`
- **parseHTML 验证**：确认原始的 `<mark>text</mark>` HTML 能够进入编辑器时被正确识别并保留往返能力。检查 `@tiptap/extension-highlight` 是否已包含 `<mark>` 作为 parseHTML 中的一个标签。
- 如果缺失或部分匹配：使用自定义的 `addParseHTML`（可通过 TipTap v3 中的 `renderHTML` 或配置选项进行配置）来扩展 Highlight 扩展的 `parseHTML`。

### 3.5 `<sub>` / `<sup>` — 上下标（已有，需验证 parseHTML）

- 已通过 `TipTapExtensionSubscript` / `TipTapExtensionSuperscript` 注册
- Markdown 语法：`~text~` → `<sub>`，`^text^` → `<sup>`（`MarkdownHandler.cpp:655-691`）
- TipTap 命令：`editorApi.toggleSubscript()` / `toggleSuperscript()`
- **parseHTML 验证**：确认原始的 `<sub>` / `<sup>` 标签进入编辑器时能被识别
- MarkdownHandler 已经处理了这些标签的往返（使用 ZZSB 占位符保护 `<sub>`，使用 ZZFR 占位符处理 `<sup data-type="footnoteRef">`）

### 3.6 `<hr>` — 水平线（已有，需验证 parseHTML）

- TipTap StarterKit 包含 HorizontalRule 扩展
- Markdown 语法：`---` 或 `***`（QTextDocument::setMarkdown 能够识别）
- 命令：`editorApi.insertHorizontalRule()`
- **parseHTML 验证**：确认原始的 `<hr>` 标签进入编辑器时能被识别
- MarkdownHandler：QTextDocument 原生就能处理 `<hr>` ↔ `---`

### 3.7 `<br>` — 硬换行（已有，需验证 parseHTML）

- TipTap StarterKit 包含 HardBreak 扩展；`Shift+Enter` 触发
- MarkdownHandler 使用 BRMARKER 模式进行换行保护（`MarkdownHandler.cpp:388-389,585-587`）
- **parseHTML 验证**：确认原始的 `<br>` HTML 进入编辑器时能被识别
- MarkdownHandler 往返：应已正常工作

## 4. MarkdownHandler 改动

### 4.1 toHtml 方向（MD → HTML）

新增 `_protectInlineElements` 风格的 `<kbd>`、`<small>`、`<div>` 正则保护，将它们替换为占位符，从而在 Markdown 转换时不受影响：

- `<kbd>`：`<kbd[^>]*>.*?</kbd>` → `ZZKD + index + ZZ`
- `<small>`：`<small[^>]*>.*?</small>` → `ZZSM + index + ZZ`
- `<div>`：`<div\b[^>]*>.*?</div>` → `ZZDV + index + ZZ`（需处理 data-type 冲突 —— 只有**没有** `data-type` 属性的 `<div>` 才应被保护返回到手写用户 `<div>`；带有 `data-type` 属性的内部 div 由现有的内部逻辑处理）

### 4.2 fromHtml 方向（HTML → MD）

对称还原：post-processing step to restore ZZ* placeholders back to original HTML tags.

### 4.3 测试

在第 3 号任务的 test_markdownhandler.cpp 中新增回归测试用例，覆盖内容与现有颜色 span 测试模型类似：

- `testKbdRoundTrip`：toHtml 保留 `<kbd>` → fromHtml 还原 → 二次往返无 ZZ* 残留
- `testSmallRoundTrip`：同上
- `testDivBlockRoundTrip`：toHtml/fromHtml 往返保留 `<div>`（无 data-type）→ ZZ 占位符干净
- `testMarkParseVerification` / `testSubSupParseVerification` / `testHrBrParseVerification`：验证手写 HTML 标签能在 toHtml 流程中原封不动幸存，不被 QTextDocument 吃掉。

## 5. CSS 样式

在 `editor.css` 中添加：

```css
/* kbd 键盘按键 */
kbd {
  font-family: ui-monospace, SFMono-Regular, monospace;
  font-size: 0.85em;
  border: 1px solid #aaa;
  border-radius: 3px;
  padding: 0.05em 0.35em;
  background: #f5f5f5;
}
/* small — 浏览器默认已经处理了 smaller */
```

在 `light.qss` / `dark.qss` 中也添加 `kbd` 样式（因为 QWebEngineView 需要显式的 QSS 规则来适应浅色/深色主题）。

## 6. self-review checklist

- [ ] `<kbd>`、`<small>`、`<div>` 的 `parseHTML` → `getAttrs` 正确排除数据内部 div（`data-type` 检查）
- [ ] `<div>` node group 正确，不会破坏现有文档 schema（paragraph/heading/list 等依然可以嵌套在文档根部）
- [ ] 属性级 `renderHTML` 用于 `style`/`align`/`class`（和 `colorSpan` 使用相同的模式 —— 已在 TipTap v3 中通过之前的 bug 修复验证）
- [ ] MarkdownHandler 占位符与现有的 `ZZHI/ZZHS/ZZUL/ZZFR/ZZSR…` 占位符无冲突
- [ ] 新增 MarkdownHandler 测试用例断言的是有意义的子字符串，而不是脆弱的精确匹配
- [ ] 代码注释使用中文，变量名/函数名使用英文

## 7. 开放问题（计划编写前解决）

- [ ] P0 标签（`kbd`/`small`/`div`）应该在一个计划中一起实现，还是分拆到多个？
- [ ] P1 的 parseHTML 验证 —— 如果 `<mark>`/`<sub>`/`<sup>` 等已经正常解析原始 HTML（可能性较高），是否还需要额外的步骤？还是在 spec 中记录“已通过验证，无需改动”即可？
