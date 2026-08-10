# Task 4 报告：已有 5 个 HTML 标签（mark/sub/sup/hr/br）parseHTML 验证

**状态：DONE（纯验证，无代码改动）**
**分支：** `feat/html-tags-support`

## 验证目标

TipTap 已注册 `<mark>`（Highlight）、`<sub>`（Subscript）、`<sup>`（Superscript）、`<hr>`（HorizontalRule，StarterKit 内置）、`<br>`（HardBreak，StarterKit 内置）五个扩展。本任务验证：在源码模式输入这些标签的原始 HTML、切换回 WYSIWYG 时，`parseHTML` 能否正确识别并保留。

## 验证方法（headless 自动注入）

1. 在 `editor.js` 的 `init()` 末尾（`_attachMathClickHandler` 之后）临时注入 `[VERIFY-TEMP]` 诊断：`setTimeout(2500)` 后对 5 个标签逐一执行 `window.editorApi.setContent('<p>…</p>')` → `window.editorApi.getContent()`，打印输入与输出 HTML。
   - 注：`editorApi` 无 `getHTML()` 方法（仅 `getContent()`，其内部即 `this.editor.getHTML()`），故诊断用 `getContent()`。
2. 重新构建（`cmake .. -DCMAKE_BUILD_TYPE=Debug && make -j$(nproc)`，`qrc_app.cpp` 重新生成将改动打进入二进制）。
3. `timeout 15 xvfb-run -a ./build/src/md-editor 2>&1` 捕获 `[JS]` 前缀的 `[VERIFY]` 输出（JS `console.log` 经 `EditorWebPage::javaScriptConsoleMessage` → `qDebug` → stderr）。
4. 验证完毕后删除诊断代码（`init()` 复原），`git status` 确认 `editor.js` 无残留改动。

诊断输出出现两次（MainWindow 初始化了多个 EditorTab 实例），两轮结果完全一致，互相印证。

## 验证结果

### ✅ Pass：`<mark>` 高亮（Highlight 扩展）

```
input=<mark>高亮</mark> -> <p><mark>高亮</mark></p>
```
`<mark>` 被 Highlight mark 的 `parseHTML({ tag: 'mark' })` 正确解析，getHTML 原样输出。**PASS**

### ✅ Pass：`<sub>` 下标（Subscript 扩展）

```
input=H<sub>2</sub>O -> <p>H<sub>2</sub>O</p>
```
`<sub>` 被 Subscript mark 正确解析并保留。**PASS**

### ✅ Pass：`<sup>` 上标（Superscript 扩展）

```
input=x<sup>2</sup> -> <p>x<sup>2</sup></p>
```
`<sup>` 被 Superscript mark 正确解析并保留。**PASS**

### ✅ Pass：`<hr>` 分隔线（HorizontalRule，StarterKit 内置）

```
input=<hr> -> <p></p><hr><p></p>
```
浏览器 DOMParser 将 `<p>` 内的 `<hr>` 弹出为块级（HTML 规范行为），ProseMirror 将其解析为 `horizontalRule` 节点，getHTML 输出 `<hr>`。标签存活。**PASS**（输出多出两个空段落包裹是 DOMParser 的标准重构，非解析失败）

### ✅ Pass：`<br>` 硬换行（HardBreak，StarterKit 内置）

```
input=行<br>行 -> <p>行<br>行</p>
```
`<br>` 被 HardBreak node 正确解析，原样保留在段落内。**PASS**

## 补充验证：Markdown 语法（==mark== / ~sub~ / ^sup^ / ---）

```
mark syntax: input===高亮== -> <p>==高亮==</p>
sub  syntax: input=~下标~  -> <p>~下标~</p>
sup  syntax: input=^上标^  -> <p>^上标^</p>
hr   syntax: input=---     -> <p>---</p>
```

四类 Markdown 语法经 `setContent` 注入后**保持字面文本**，未被转换为对应语义标签。**此现象符合预期、非回归**，原因有二：

1. `setContent` 走 ProseMirror `DOMParser`，把入参当 **HTML** 解析；TipTap 的 `==mark==`/`~sub~`/`^sup^`/`---` 等 Markdown 快捷语法由 **InputRule 仅在键入时**触发，程序化 setContent 不触发。
2. 本项目源码模式 → WYSIWYG 的真实路径是：C++ 侧 `MarkdownHandler::toHtml` 先把源码区 Markdown 转成 HTML，再经 `exitSourceMode(html)` 调 `setContent(html)` 注入。Markdown 语法的语义化转换由 MarkdownHandler 承担（本计划 Task 1–3 已验证其往返保真），TipTap 层只负责解析已转换的 HTML 标签。

因此 Markdown 语法直注 `setContent` 保持字面文本，属于诊断方法的天然限制，不影响 5 个 HTML 标签的 parseHTML 验证结论。

## 结论

| 标签 | 扩展 | 验证结果 |
|------|------|---------|
| `<mark>` | Highlight | ✅ PASS |
| `<sub>` | Subscript | ✅ PASS |
| `<sup>` | Superscript | ✅ PASS |
| `<hr>` | HorizontalRule（StarterKit） | ✅ PASS |
| `<br>` | HardBreak（StarterKit） | ✅ PASS |

5/5 全部 PASS。源码模式输入的原始 HTML 标签在切换回 WYSIWYG 时均能被既有 TipTap 扩展的 `parseHTML` 正确解析并保真输出，无代码改动。

## 清理确认

- 临时 `[VERIFY-TEMP]` 诊断已删除，`init()` 复原。
- `git status` 干净（`editor.js` 无残留改动）。
- 已重新构建，二进制内不含诊断代码。
