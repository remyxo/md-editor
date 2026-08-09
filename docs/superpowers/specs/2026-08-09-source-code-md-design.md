# Markdown 源码切换设计文档

**日期**：2026-08-09
**状态**：已确认
**分支**：feat/emoji-picker

## 1. 目标

将当前"HTML 源码视图"替换为真正的 **Markdown + HTML 混合源码视图**：
- 能转换成 Markdown 语法的元素输出为 MD（粗体 → `**text**`、下标 → `~text~` 等）
- 无法用 MD 表达的元素保留原始 HTML（`<span style="color:...">`、`<u>` 等）
- 双向转换一致：`编辑区 HTML → fromHtml → MD → toHtml → 编辑区 HTML` 无损往返

## 2. 架构

### 2.1 切换流程（C++ 编排）

```
进入源码模式:
  MainWindow::onToggleSourceMode()
  → page()->runJavaScript("getContent()", callback)
  → callback: MarkdownHandler::fromHtml(html) → markdown
  → escapeForJsTemplate(md) → runJs("enterSourceMode(`{md}`)")

退出源码模式:
  MainWindow::onToggleSourceMode()
  → page()->runJavaScript("getSourceContent()", callback)
  → callback: MarkdownHandler::toHtml(md) → html
  → escapeForJsTemplate(html) → runJs("exitSourceMode(`{html}`)")
```

与旧实现的区别：旧实现切换在 JS 侧同步完成（`getHTML()` → `<textarea>` → `setContent()`），
新实现需要 C++ 侧的 `MarkdownHandler` 参与转换，因此用异步回调编排。

### 2.2 组件职责

| 组件 | 新增/修改 | 职责 |
|------|----------|------|
| `MainWindow` | 修改 | `onToggleSourceMode()` 异步编排、`m_sourceMode`/`m_sourceModeToggling` 状态管理 |
| `ToolBar` | 修改 | `setSourceMode(bool)` 禁用/恢复编辑按钮 |
| `editor.js` | 修改 | 拆分 `toggleSourceMode` 为 `enterSourceMode`/`exitSourceMode`/`getSourceContent` |
| `MarkdownHandler` | 修改 | `fromHtml()`/`toHtml()` 新增 HTML 内联元素保护层 |

### 2.3 不变的部分

- `EditorBridge` — 无需新增槽或信号
- `MenuBar` — 快捷键 `Ctrl+\` 连接不变
- `editor.html` / `editor.css` — textarea 和样式不变
- `EditorTab` / `TabWidget` — 已有模式不变

## 3. MarkdownHandler 改造

### 3.1 `fromHtml()` 新增保护步骤

在所有现有占位符提取之后、`doc.setHtml(processed)` 之前，新增：

**保护元素**：

| 原始 HTML | 占位符 | 还原输出 |
|----------|--------|---------|
| `<span style="...">text</span>` | `ZZHS{n}ZZ` | 原始 `<span style="...">text</span>` |
| `<u>text</u>` | `ZZUL{n}ZZ` | 原始 `<u>text</u>` |

**处理逻辑**：
1. 正则匹配 `<span\b[^>]*style="[^"]*"[^>]*>(.*?)</span>` → 替换为 `ZZHS{n}ZZ`
2. 正则匹配 `<u>(.*?)</u>` → 替换为 `ZZUL{n}ZZ`
3. `doc.setHtml(processed)` → `doc.toMarkdown()` 后，逆序还原占位符

**设计理由**：`QTextDocument::toMarkdown()` 不保留 CSS 内联样式和下划线标签，
导致 `<span style="color:...">` 和 `<u>` 样式信息丢失。占位符保护后直出原始 HTML，
与 `toHtml()` 侧形成对称。

**注意**：不含 `style` 属性的 `<span>`（如 `data-type="inline-math"`）由各自保护步骤处理，
不重复提取。嵌套 span（如 `<span style="color:red"><strong>text</strong></span>`）中的 `strong`
由 QTextDocument 正常转换为 `**text**`，外层 span 保护确保 `<span style="color:red">**text**</span>`
整体还原。

### 3.2 `toHtml()` 新增保护步骤

在行处理循环中（`setMarkdown` 之前），新增：

**处理逻辑**：
1. 从 markdown 源码行中提取 `<span\b[^>]*style=..."[^>]*>(.*?)</span>` → 占位符
2. 提取 `<u>(.*?)</u>` → 占位符
3. 后处理阶段（现有逆序还原之后）还原占位符

**设计理由**：Qt6 `setMarkdown` 的 CommonMark 模式理论支持内联 HTML，
但为保持与 `fromHtml()` 一致的确定性行为，统一用占位符保护。
避免 Qt 版本差异导致 `<span>` 属性被丢弃或格式异常。

## 4. JS 侧改造

### 4.1 方法拆分

移除 `toggleSourceMode()`，新增三个方法：

```javascript
// 进入源码模式（由 C++ 调用，md 已转换好）
enterSourceMode: function(md) {
    this._sourceValue = md;
    var sourceView = document.getElementById('source-view');
    sourceView.value = md;
    document.getElementById('editor').style.display = 'none';
    sourceView.style.display = '';
    if (this._readOnly) sourceView.setAttribute('readonly', '');
    this.isSourceMode = true;
},

// 退出源码模式（由 C++ 调用，html 已转换好）
exitSourceMode: function(html) {
    var sourceView = document.getElementById('source-view');
    var changed = sourceView.value !== this._sourceValue;
    this._loading = true;
    this.editor.commands.setContent(html);
    this._loading = false;
    sourceView.style.display = 'none';
    document.getElementById('editor').style.display = '';
    this.isSourceMode = false;
    if (changed && !this._dirty) this._markDirty();
    this.notifyFormat();
    this.notifyHeadings();
    this.notifyWordCount();
    this.notifyHistory();
},

// 获取源码区当前内容（C++ 侧获取 textarea 值用于 MD→HTML 转换）
getSourceContent: function() {
    var sv = document.getElementById('source-view');
    return sv ? sv.value : '';
},
```

### 4.2 脏状态机制

- `enterSourceMode` 将传入的 MD 存入 `_sourceValue`
- `exitSourceMode` 比较 `textarea.value !== this._sourceValue`（MD 级别差异判断）
- MD 变化 → 标记脏；未变 → 不标记
- `_loading` 锁抑制 `commands.setContent` 触发的 `onUpdate`

### 4.3 移除的代码

- `toggleSourceMode()` 方法
- `toggleSourceMode` 内部对 `editor.getHTML()` 的调用（移到 C++ 侧）

`setFont`、`setWidth`、`setEditable`、`applySettings` 中对 `#source-view` 的引用保持不变。

## 5. C++ 侧改造

### 5.1 MainWindow

**新增成员**（`MainWindow.h`）：
```cpp
bool m_sourceMode = false;         // 当前视图模式
bool m_sourceModeToggling = false; // 异步切换进行中，防重入
```

**修改方法** — `onToggleSourceMode()` 完全重写：
```cpp
void MainWindow::onToggleSourceMode()
{
    EditorTab *tab = m_tabWidget->currentEditorTab();
    if (!tab || m_sourceModeToggling) return;
    m_sourceModeToggling = true;

    if (!m_sourceMode) {
        tab->webView()->page()->runJavaScript(
            "window.editorApi.getContent()",
            [this, tab](const QVariant &result) {
                QString md = MarkdownHandler::fromHtml(result.toString());
                QString escaped = escapeForJsTemplate(md);
                tab->runJsCommand(
                    QString("window.editorApi.enterSourceMode(`%1`);").arg(escaped));
                m_sourceMode = true;
                m_toolBar->setSourceMode(true);
                m_sourceModeToggling = false;
            });
    } else {
        tab->webView()->page()->runJavaScript(
            "window.editorApi.getSourceContent()",
            [this, tab](const QVariant &result) {
                QString html = MarkdownHandler::toHtml(result.toString());
                QString escaped = escapeForJsTemplate(html);
                tab->runJsCommand(
                    QString("window.editorApi.exitSourceMode(`%1`);").arg(escaped));
                m_sourceMode = false;
                m_toolBar->setSourceMode(false);
                m_sourceModeToggling = false;
            });
    }
}
```

**移除**：旧的 lambda 中 `static bool srcMode` 变量和 `sourceModeAction->setChecked` 调用。`connectSignals()` 中 MenuBar 连接简化为直接调用 `onToggleSourceMode()`。

**回调内 MenuBar 状态同步**：在进入/退出源码模式的回调中通过
`m_menuBar->findChild<QAction *>("sourceModeAction")->setChecked(m_sourceMode)` 同步菜单勾选状态。

**新增辅助**：`escapeForJsTemplate(const QString &s)` — 转义 `\` → `\\`、`` ` `` → `` \` ``、`$` → `\$`，与 `EditorTab::loadFile` 一致

### 5.2 ToolBar

**新增方法**（`ToolBar.h`）：
```cpp
void setSourceMode(bool on);
```

**实现**（`ToolBar.cpp`）：
```cpp
void ToolBar::setSourceMode(bool on)
{
    // 源码模式下禁用所有编辑/格式化按钮
    // 保留 sourceAction（切换按钮自身）和 readModeAction 可用
    for (QAction *a : m_allActions) {
        if (a->objectName() != "sourceAction"
            && a->objectName() != "readModeAction"
            && a->objectName() != "pdfAction") {
            a->setEnabled(!on);
        }
    }
    m_headingCombo->setEnabled(!on);
}
```

注意：使用 `setEnabled` 而非 `setVisible`（阅读模式用隐藏，源码模式用灰化），
让用户看到工具栏仍然存在但当前不可用。

### 5.3 阅读模式交互

- 阅读模式（`m_readMode`）和源码模式（`m_sourceMode`）互斥但独立管理
- 源码模式下点阅读模式按钮：不做特殊处理，调用方自行判断
- 阅读模式下点源码模式按钮：源码 textarea 已有 `readonly` 属性保护
- `modalDialogChanged` 处理保持不变

## 6. 边界情况

### 6.1 快速双击切换

`m_sourceModeToggling` 防重入锁：前一个异步操作未完成时忽略新请求。
正常切换延迟 < 100ms，用户感知不到。

### 6.2 模态对话框打开时切换

数学公式/Mermaid/Emoji 选择器打开时按 Esc 应优先关闭对话框，
不触发源码切换。此逻辑由现有的 `m_modalDialogOpen` 分流处理，不变。

### 6.3 标签页切换

`onTabChanged` 中调用 `syncFormatState()`，若当前标签页处于源码模式，
`notifyFormat()` 中的 `editor.isActive()` 调用仍然有效（editor 未销毁，仅隐藏）。
源码模式本身就是标签页级别状态，切换标签页后应保持。

### 6.4 源码模式下保存文件

**已知局限**：源码模式下 Ctrl+S 保存的是隐藏编辑区的 HTML 转换结果，**源码区未提交的修改不会被保存**。用户应先 `Ctrl+\` 切回 WYSIWYG 再保存。

自动退出源码模式后再保存的方案涉及异步回调嵌套（exitSourceMode → 等待 toHtml → 加载 → 再 getContent → fromHtml → 写文件），复杂度高且影响保存路径可靠性。后续版本重构保存流程时统一处理。

### 6.5 大文件性能

`escapeForJsTemplate` 生成的 JS 模板字面量可能非常大（>1MB 的 MD 文本）。
与现有 `EditorTab::loadFile` 行为一致（同样用模板字面量传递 HTML），均受
Chromium 的 `runJavaScript` 参数大小限制。大文件需后续优化（如 `QWebChannel` 传输），
不在本次范围内。

## 7. 测试要点

### 7.1 双向转换一致性

| 测试项 | 输入 | 期望 |
|--------|------|------|
| 粗体 | `<strong>text</strong>` | `**text**` ↔ `<strong>text</strong>` |
| 下标 | `<sub>x</sub>` | `~x~` ↔ `<sub>x</sub>` |
| 上标 | `<sup>n</sup>` | `^n^` ↔ `<sup>n</sup>` |
| 高亮 | `<mark>text</mark>` | `==text==` ↔ `<mark>text</mark>` |
| 文字颜色 | `<span style="color: rgb(186,55,42);">红</span>` | 保留原始 `<span style="color:...">红</span>` |
| 下划线 | `<u>text</u>` | 保留原始 `<u>text</u>` |
| 混合格式 | `<strong>粗</strong> <span style="color:red">红</span>` | `**粗** <span style="color:red">红</span>` |

### 7.2 UI 状态

- 源码模式下工具栏按钮灰化
- 源码模式下 `Ctrl+\` 切回 WYSIWYG
- 阅读模式下源码 textarea 不可编辑
- 快速双击不产生异常状态

### 7.3 脏状态

- 进入源码模式不改脏状态
- 源码区编辑后切回 → 标记脏
- 源码区无改动切回 → 不标记脏
- 保存后脏状态清除，源码区重新打开 `_sourceValue` 更新

## 8. 已知局限

1. **深层嵌套 HTML**：`<span style="..."><span style="...">text</span></span>` 不被正则匹配（实际中 TipTap 不产生此类输出）
2. **块级样式**：`<div style="...">` 和 `<p style="text-align:...">` 暂不保护（文本对齐等块级属性在后续需求中处理）
3. **大文件**：模板字面量传输有 Chromium 字符串大小限制，极端大文件需后续用 QWebChannel 传输数据
4. **源码模式下保存**：源码区未提交的修改在保存时丢失，需先切回 WYSIWYG。后续版本统一处理
