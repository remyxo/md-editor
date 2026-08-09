# Markdown 源码切换实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 将 HTML 源码视图替换为真正的 Markdown + HTML 混合源码视图，双向转换一致。

**Architecture:** C++ 侧通过 `MarkdownHandler` 编排 MD↔HTML 异步切换，JS 侧拆分为 enter/exit/getSourceContent 三个方法。新增 `<span style>`/`<u>` 占位符保护确保不可转换的 HTML 元素无损往返。

**Tech Stack:** Qt6 C++17, QRegularExpression, QTextDocument, JavaScript (ES5 兼容)

## Global Constraints

- 所有文档和注释使用中文，代码标识符使用英文
- 版本号全局唯一：定义在 `CMakeLists.txt` 的 `project(md-editor VERSION x.y.z)`
- 提交规范：`feat:` / `fix:` / `chore:` 前缀，中文描述
- `MarkdownHandler` 占位符格式：`ZZ{prefix}{n}ZZ`（大写双Z起止）
- JS 代码 ES5 兼容（无箭头函数、无模板字面量、无 const/let）
- `editor.js` 中的 HTML 文本转义使用 `_escapeText` 函数（行 26-29）

---

### Task 1: `MarkdownHandler::fromHtml()` — 保护不可转换的 HTML 内联元素

**Files:**
- Modify: `src/core/MarkdownHandler.cpp:1032-1036`（在 footnote defs 保护之后、`QTextDocument doc;` 之前插入新步骤）

**Interfaces:**
- Consumes: 现有的 `processed` 字符串（已完成所有其他占位符保护）
- Produces: `placeholders` / `backups` 列表新增 `ZZHS{n}ZZ`（span style）和 `ZZUL{n}ZZ`（underline）条目，由现有逆序还原逻辑统一还原

- [ ] **Step 1: 在 footnote defs 步骤 (7) 和 `QTextDocument doc;` 之间插入 HTML 内联元素保护代码**

在 `MarkdownHandler::fromHtml()` 中，找到 `// 7)` 脚注容器保护步骤结束的 `}` 之后、`QTextDocument doc;` 之前（约第 1032-1034 行），插入：

```cpp
    // N) 保护 Markdown 无法表达的 HTML 内联元素 → 占位符
    //    QTextDocument::toMarkdown() 会丢弃内联 CSS 样式和下划线标签，
    //    占位符保护后还原为原始 HTML，与 toHtml() 形成对称往返。
    {
        int n = 0;
        int offset = 0;
        // <span style="...">text</span>
        QRegularExpression spanStyleRe(
            R"(<span\b[^>]*\bstyle\s*=\s*"[^"]*"[^>]*>(.*?)</span>)",
            QRegularExpression::DotMatchesEverythingOption);
        while (true) {
            auto m = spanStyleRe.match(processed, offset);
            if (!m.hasMatch()) break;
            QString ph = QString("ZZHS%1ZZ").arg(n++);
            placeholders << ph;
            backups << m.captured();  // 保留完整原始 HTML
            processed.replace(m.capturedStart(), m.capturedLength(), ph);
            offset = m.capturedStart() + ph.length();
        }
        // <u>text</u>
        QRegularExpression uRe(
            R"(<u>(.*?)</u>)",
            QRegularExpression::DotMatchesEverythingOption);
        while (true) {
            auto m = uRe.match(processed, offset);
            if (!m.hasMatch()) break;
            QString ph = QString("ZZUL%1ZZ").arg(n++);
            placeholders << ph;
            backups << m.captured();  // 保留完整原始 HTML
            processed.replace(m.capturedStart(), m.capturedLength(), ph);
            offset = m.capturedStart() + ph.length();
        }
    }
```

**设计要点**：
- 使用 `DotMatchesEverythingOption`——`<span>` 内文本可能跨行（极少见，但安全）
- `backups` 存完整原始 HTML——逆序还原时直出，不丢失任何属性
- 不包含 `data-type` 等属性判断——`<span data-type="inline-math">` 已由步骤 0 保护为 `ZZIM`，`<span data-type="footnoteRef">` 的 `<sup>` 已由步骤 6 保护为 `ZZFR`
- 占位符 `ZZHS`/`ZZUL` 不在步骤 0 的 `placeholders` 使用范围（步骤 0 仅用 `ZZBM`/`ZZIM`），命名空间隔离

- [ ] **Step 2: 验证占位符在逆序还原链中的位置**

现有的逆序还原循环：
```cpp
for (int i = placeholders.size() - 1; i >= 0; --i)
    md.replace(placeholders[i], backups[i]);
```

由于新步骤在末尾追加 `placeholders`/`backups`，逆序还原时 **最先还原** `ZZHS`/`ZZUL`。这符合设计——这些占位符位于 `processed` 中的最外层（它们包裹的文本已由早期步骤保护），逆序还原时先还原最外层。

- [ ] **Step 3: 提交**

```bash
git add src/core/MarkdownHandler.cpp
git commit -m "feat: fromHtml 保护 <span style>/<u> 为占位符，toMarkdown 后还原原始 HTML"
```

---

### Task 2: `MarkdownHandler::toHtml()` — 保护 markdown 中的原始 HTML 标签

**Files:**
- Modify: `src/core/MarkdownHandler.cpp:345-648`（toHtml 函数）

**Interfaces:**
- Consumes: markdown 源码行（`line` 变量）
- Produces: 新增 `htmlInlines` 占位符列表，与现有 `subContents`/`taskLists`/`footnoteRefs` 等并行管理

- [ ] **Step 1: 在 toHtml() 顶部声明新增占位符列表**

在 `toHtml()` 函数的局部变量声明区域（约第 352-361 行，`subContents` 等变量附近），新增：

```cpp
    QStringList htmlInlineBackups; // 原始 HTML 内联标签（<span style> / <u>）
```

- [ ] **Step 2: 新增 `extractHtmlInlineElements` 辅助函数**

在 `toHtml()` 函数之前（或 `fromHtml()` 之后）的静态函数区域，新增：

```cpp
// 提取 markdown 行中的原始 HTML 内联标签（<span style>/<u>），
// 替换为占位符 ZZHI{n}ZZ。这些标签无法用 Markdown 语法表达，
// 需保护后直通 Qt setMarkdown，后处理时还原为原始 HTML。
static QString extractHtmlInlineElements(const QString &line,
                                          QStringList &backups)
{
    QString r = line;
    int n = backups.size();
    // <span style="...">text</span>
    {
        QRegularExpression spanRe(
            R"(<span\b[^>]*\bstyle\s*=\s*"[^"]*"[^>]*>(.*?)</span>)");
        int offset = 0;
        while (true) {
            auto m = spanRe.match(r, offset);
            if (!m.hasMatch()) break;
            backups << m.captured();
            QString ph = QString("ZZHI%1ZZ").arg(n++);
            r.replace(m.capturedStart(), m.capturedLength(), ph);
            offset = m.capturedStart() + ph.length();
        }
    }
    // <u>text</u>
    {
        QRegularExpression uRe(R"(<u>(.*?)</u>)");
        int offset = 0;
        while (true) {
            auto m = uRe.match(r, offset);
            if (!m.hasMatch()) break;
            backups << m.captured();
            QString ph = QString("ZZHI%1ZZ").arg(n++);
            r.replace(m.capturedStart(), m.capturedLength(), ph);
            offset = m.capturedStart() + ph.length();
        }
    }
    return r;
}
```

**注意**：不用 `DotMatchesEverythingOption`——markdown 源码逐行处理，`<span>`/`<u>` 作为内联元素不会跨行（与 `fromHtml()` 侧不同，那里处理的是完整 HTML 文档）。

- [ ] **Step 3: 在行处理循环中调用 extractHtmlInlineElements**

在 `toHtml()` 的行处理循环中（约第 372-464 行 `for (int i = 0; i < lines.size(); ++i)` 内部），每行在进入现有转换链之前先提取 HTML 内联元素。

找到 `isFootnoteDef` 判断后的正文处理分支（约第 445-463 行），修改其中两处正文行处理，将 `extractSubscripts(line, subContents)` 包裹为：

```cpp
QString cleanedLine = extractHtmlInlineElements(line, htmlInlineBackups);

// 然后在所有使用 line 的地方改用 cleanedLine：
// 原: extractFootnoteRefs(extractSubscripts(line, subContents), footnoteRefs)
// 改: extractFootnoteRefs(extractSubscripts(cleanedLine, subContents), footnoteRefs)
```

具体修改点（行 451 和 461）：
```cpp
// 行 451 原:
processed += extractFootnoteRefs(extractSubscripts(line, subContents), footnoteRefs) + " " + MARKER + "\n";
// 改为:
QString cl = extractHtmlInlineElements(line, htmlInlineBackups);
processed += extractFootnoteRefs(extractSubscripts(cl, subContents), footnoteRefs) + " " + MARKER + "\n";

// 行 461 原:
processed += extractFootnoteRefs(extractSubscripts(line, subContents), footnoteRefs) + '\n';
// 改为:
QString cl = extractHtmlInlineElements(line, htmlInlineBackups);
processed += extractFootnoteRefs(extractSubscripts(cl, subContents), footnoteRefs) + '\n';
```

- [ ] **Step 4: 在 `doc.toHtml()` 后处理中还原占位符**

在 `toHtml()` 的还原步骤区域末尾（约第 645 行 `return html;` 之前，所有现有点位符还原之后），新增：

```cpp
    // 还原 HTML 内联标签占位符 ZZHI → 原始 <span style> / <u>
    for (int i = 0; i < htmlInlineBackups.size(); ++i)
        html.replace("ZZHI" + QString::number(i) + "ZZ", htmlInlineBackups[i]);
```

- [ ] **Step 5: 提交**

```bash
git add src/core/MarkdownHandler.cpp
git commit -m "feat: toHtml 保护 markdown 中 <span style>/<u> 为占位符，直通 setMarkdown"
```

---

### Task 3: `editor.js` — 拆分 toggleSourceMode

**Files:**
- Modify: `src/resources/web/editor.js:879-903`（移除 `toggleSourceMode`，新增三个方法）
- Modify: `src/resources/web/editor.js:570-571`（保留 `isSourceMode` / `_sourceValue` 成员不变）

**Interfaces:**
- Consumes: 现有的 `#editor` div、`#source-view` textarea、`_loading`/`_dirty`/`_readOnly` 状态
- Produces: `window.editorApi.enterSourceMode(md)`、`window.editorApi.exitSourceMode(html)`、`window.editorApi.getSourceContent()`
- 被 C++ 侧 `MainWindow::onToggleSourceMode()` 的回调调用

- [ ] **Step 1: 删除 `toggleSourceMode` 方法**

删除 `editor.js` 第 879-904 行的 `toggleSourceMode: function() { ... },` 整个方法体。

- [ ] **Step 2: 在原地新增三个方法**

在删除位置（即 `insertHorizontalRule` 之后、`scrollToPos` 之前，约第 878 行）插入：

```javascript
    // 进入源码模式（C++ 侧传入已转换好的 Markdown）
    enterSourceMode: function(md) {
        if (!this.editor) return;
        this._sourceValue = md;
        var sourceView = document.getElementById('source-view');
        var editorDiv = document.getElementById('editor');
        sourceView.value = md;
        editorDiv.style.display = 'none';
        sourceView.style.display = '';
        if (this._readOnly) sourceView.setAttribute('readonly', '');
        else sourceView.removeAttribute('readonly');
        this.isSourceMode = true;
    },

    // 退出源码模式（C++ 侧传入已转换好的 HTML）
    exitSourceMode: function(html) {
        if (!this.editor) return;
        var sourceView = document.getElementById('source-view');
        var editorDiv = document.getElementById('editor');
        var changed = sourceView.value !== this._sourceValue;
        this._loading = true;
        this.editor.commands.setContent(html);
        this._loading = false;
        sourceView.style.display = 'none';
        editorDiv.style.display = '';
        this.isSourceMode = false;
        if (changed && !this._dirty) this._markDirty();
        this.notifyFormat();
        this.notifyHeadings();
        this.notifyWordCount();
        this.notifyHistory();
    },

    // 获取源码区当前内容（C++ 侧用于 MD→HTML 转换）
    getSourceContent: function() {
        var sv = document.getElementById('source-view');
        return sv ? sv.value : '';
    },
```

- [ ] **Step 3: 验证 setEditable 对源码模式的处理**

确认 `setEditable` 方法（第 937-968 行）中对 `#source-view` readonly 的处理与新模式兼容：
- 第 943-944 行 `if (sv) sv.setAttribute('readonly', '');` ——进入只读模式时 textarea 不可编辑 ✓
- 第 963 行 `if (sv) sv.removeAttribute('readonly');` ——退出只读模式时恢复 ✓
- `enterSourceMode` 中重复检查 `_readOnly` 确保切换时 textarea 状态正确 ✓

无需修改，只做验证。

- [ ] **Step 4: 提交**

```bash
git add src/resources/web/editor.js
git commit -m "feat: 拆分 toggleSourceMode 为 enterSourceMode/exitSourceMode/getSourceContent"
```

---

### Task 4: `ToolBar` — 新增源码模式禁用

**Files:**
- Modify: `src/ui/ToolBar.h:15`（新增 `setSourceMode` 声明）
- Modify: `src/ui/ToolBar.cpp:247-262`（在 `setReadMode` 之后新增实现）

**Interfaces:**
- Consumes: `m_allActions`（已在 `setupUi` 末尾收集）
- Produces: `void setSourceMode(bool on)` — 灰化/恢复编辑按钮

- [ ] **Step 1: 在 ToolBar.h 新增方法声明**

在 `void setReadMode(bool readMode);` 之后新增：

```cpp
    void setSourceMode(bool on);
```

- [ ] **Step 2: 在 ToolBar.cpp 新增实现**

在 `setReadMode` 方法之后（约第 262 行之后）新增：

```cpp
void ToolBar::setSourceMode(bool on)
{
    // 源码模式下灰化所有编辑/格式化按钮，保留源码切换和阅读模式可用
    for (QAction *a : m_allActions) {
        if (a->objectName() != QStringLiteral("sourceAction")
            && a->objectName() != QStringLiteral("readModeAction")
            && a->objectName() != QStringLiteral("pdfAction")) {
            a->setEnabled(!on);
        }
    }
    // 恢复时根据格式状态重新设置按钮可用性（disabled 可能残留）
    if (!on) {
        for (QAction *a : m_allActions) {
            a->setEnabled(true);
        }
    }
    m_headingCombo->setEnabled(!on);
}
```

**注意**：退出源码模式时无条件恢复所有 action 为 enabled，后续 `updateFormatState` 会精确设置各按钮的 checked 状态。

- [ ] **Step 3: 提交**

```bash
git add src/ui/ToolBar.h src/ui/ToolBar.cpp
git commit -m "feat: ToolBar 新增 setSourceMode 方法，源码模式下灰化编辑按钮"
```

---

### Task 5: `MainWindow` — 重写 onToggleSourceMode 异步编排

**Files:**
- Modify: `src/ui/MainWindow.h:79-80`（新增 `m_sourceMode` / `m_sourceModeToggling` 成员）
- Modify: `src/ui/MainWindow.cpp:164-169`（MenuBar lambda 简化）
- Modify: `src/ui/MainWindow.cpp:522-525`（`onToggleSourceMode` 完全重写）
- Modify: `src/ui/MainWindow.cpp:452-458`（新增 `escapeForJsTemplate` 辅助）

**Interfaces:**
- Consumes: `editorApi.enterSourceMode()` / `editorApi.exitSourceMode()` / `editorApi.getSourceContent()`（Task 3）
- Consumes: `MarkdownHandler::fromHtml()` / `MarkdownHandler::toHtml()`（Tasks 1-2）
- Consumes: `ToolBar::setSourceMode()`（Task 4）
- Produces: 完整的异步切换流程

- [ ] **Step 1: 在 MainWindow.h 新增成员变量**

在 `bool m_modalDialogOpen = false;` 之后新增：

```cpp
    bool m_sourceMode = false;           // 当前是否处于源码模式
    bool m_sourceModeToggling = false;   // 异步切换进行中（防重入）
```

- [ ] **Step 2: 简化 MenuBar 连接 lambda**

将 `connectSignals()` 中第 164-169 行的 MenuBar 连接：

```cpp
    connect(m_menuBar, &MenuBar::toggleSourceMode, this, [this]() {
        static bool srcMode = false;
        srcMode = !srcMode;
        m_menuBar->findChild<QAction *>("sourceModeAction")->setChecked(srcMode);
        onToggleSourceMode();
    });
```

替换为简洁的直接连接：

```cpp
    connect(m_menuBar, &MenuBar::toggleSourceMode, this, &MainWindow::onToggleSourceMode);
```

**注意**：`sourceModeAction` 的 check 状态现在在 `onToggleSourceMode` 的异步回调中管理，不再需要在 connect lambda 中提前设置。

- [ ] **Step 3: 新增 escapeForJsTemplate 辅助函数**

在 `runEditorCommand` 方法之后（约第 458 行）新增：

```cpp
// 转义字符串以安全嵌入 JS 模板字面量 `...`（与 EditorTab::loadFile 一致）
static QString escapeForJsTemplate(const QString &s)
{
    QString r = s;
    r.replace("\\", "\\\\");
    r.replace("`", "\\`");
    r.replace("$", "\\$");
    return r;
}
```

- [ ] **Step 4: 完全重写 onToggleSourceMode**

将现有的 `onToggleSourceMode()`（第 522-525 行）：

```cpp
void MainWindow::onToggleSourceMode()
{
    runEditorCommand("window.editorApi.toggleSourceMode();");
}
```

替换为：

```cpp
void MainWindow::onToggleSourceMode()
{
    EditorTab *tab = m_tabWidget->currentEditorTab();
    if (!tab || m_sourceModeToggling) return;
    m_sourceModeToggling = true;

    if (!m_sourceMode) {
        // → 进入源码模式：获取编辑器 HTML → 转换为 MD → 显示到 textarea
        tab->webView()->page()->runJavaScript(
            QStringLiteral("window.editorApi.getContent()"),
            [this, tab](const QVariant &result) {
                QString html = result.toString();
                QString md = MarkdownHandler::fromHtml(html);
                QString escaped = escapeForJsTemplate(md);
                tab->runJsCommand(
                    QString("window.editorApi.enterSourceMode(`%1`);").arg(escaped));
                m_sourceMode = true;
                m_toolBar->setSourceMode(true);
                if (QAction *act = m_menuBar->findChild<QAction *>(
                        QStringLiteral("sourceModeAction")))
                    act->setChecked(true);
                m_sourceModeToggling = false;
            });
    } else {
        // → 退出源码模式：获取 textarea MD → 转换为 HTML → 加载到编辑器
        tab->webView()->page()->runJavaScript(
            QStringLiteral("window.editorApi.getSourceContent()"),
            [this, tab](const QVariant &result) {
                QString md = result.toString();
                QString html = MarkdownHandler::toHtml(md);
                QString escaped = escapeForJsTemplate(html);
                tab->runJsCommand(
                    QString("window.editorApi.exitSourceMode(`%1`);").arg(escaped));
                m_sourceMode = false;
                m_toolBar->setSourceMode(false);
                if (QAction *act = m_menuBar->findChild<QAction *>(
                        QStringLiteral("sourceModeAction")))
                    act->setChecked(false);
                m_sourceModeToggling = false;
            });
    }
}
```

- [ ] **Step 5: 验证阅读模式与源码模式的交互**

确认：
- 阅读模式下点源码切换：`toolBar::sourceToggleClicked` → `onToggleSourceMode()` → `enterSourceMode` 中 textarea 会检查 `_readOnly` 并加 `readonly` ✓
- 源码模式下点阅读切换：`MenuBar::toggleReadMode` → `toggleReadMode` lambda 中调用 `setEditable(false)` → `editor.js` 会同步 `sourceView.setAttribute('readonly', '')` ✓
- Esc 在源码模式下：`m_modalDialogOpen` 为 false → 隐藏搜索栏。不影响。✓

- [ ] **Step 6: 提交**

```bash
git add src/ui/MainWindow.h src/ui/MainWindow.cpp
git commit -m "feat: 重写 onToggleSourceMode 为 C++ 异步编排 MD 源码切换"
```

---

### Task 6: 添加 MarkdownHandler 往返测试

**Files:**
- Modify: `tests/test_markdownhandler.cpp`（末尾新增测试用例）

**Interfaces:**
- Consumes: `MarkdownHandler::fromHtml()` / `MarkdownHandler::toHtml()`
- Produces: 验证 `<span style>` / `<u>` 的双向无损往返

- [ ] **Step 1: 新增 span style 往返测试**

在 `test_markdownhandler.cpp` 末尾（`QTEST_MAIN` 之前）新增：

```cpp
    void testSpanStyleRoundTrip()
    {
        // <span style="color:..."> 应在 fromHtml → toHtml 往返后保持
        QString html = QStringLiteral(
            "<p>正常文字<span style=\"color: rgb(186, 55, 42);\">红色文字</span>结束</p>");
        QString md = MarkdownHandler::fromHtml(html);
        // MD 中应保留原始 <span style> HTML
        QVERIFY(md.contains("<span style=\"color: rgb(186, 55, 42);\">红色文字</span>"));
        // 不应出现 ZZHS 占位符泄漏
        QVERIFY(!md.contains("ZZHS"));
        // 往返
        QString html2 = MarkdownHandler::toHtml(md);
        QVERIFY(html2.contains("span style=\"color: rgb(186, 55, 42);\""));
        QVERIFY(html2.contains("红色文字"));
    }

    void testUnderlineRoundTrip()
    {
        // <u> 应在往返后保持
        QString html = QStringLiteral("<p>普通<u>下划线</u>文字</p>");
        QString md = MarkdownHandler::fromHtml(html);
        QVERIFY(md.contains("<u>下划线</u>"));
        QVERIFY(!md.contains("ZZUL"));
        QString html2 = MarkdownHandler::toHtml(md);
        QVERIFY(html2.contains("<u>下划线</u>"));
    }

    void testMixedFormatRoundTrip()
    {
        // 粗体转 MD + 颜色保持 HTML
        QString html = QStringLiteral(
            "<p><strong>粗体</strong> <span style=\"color: red;\">红色</span></p>");
        QString md = MarkdownHandler::fromHtml(html);
        // 粗体→ MD
        QVERIFY(md.contains("**粗体**"));
        // 颜色→ 原始 HTML
        QVERIFY(md.contains("<span style=\"color: red;\">红色</span>"));
        // 往返
        QString html2 = MarkdownHandler::toHtml(md);
        QVERIFY(html2.contains("<strong>粗体</strong>") || html2.contains("<b>粗体</b>"));
        QVERIFY(html2.contains("span style=\"color: red;\""));
    }
```

- [ ] **Step 2: 编译运行测试**

```bash
cd build && cmake .. -DCMAKE_BUILD_TYPE=Debug && make -j$(nproc) && ctest --output-on-failure
```

期望：三个新测试 PASS，全部已有测试仍然 PASS。

- [ ] **Step 3: 提交**

```bash
git add tests/test_markdownhandler.cpp
git commit -m "test: 新增 span style/<u> 往返测试，验证 MD 源码切换一致性"
```

---

### Task 7: 版本推进与终验

**Files:**
- Modify: `CMakeLists.txt`（版本号 +0.0.1）

- [ ] **Step 1: 推进版本号**

在 `CMakeLists.txt` 中将 `project(md-editor VERSION x.y.z)` 推进 0.0.1。

- [ ] **Step 2: 完整构建与测试**

```bash
cd build && cmake .. -DCMAKE_BUILD_TYPE=Debug && make -j$(nproc) && ctest --output-on-failure
```

期望：所有测试 PASS，无编译警告。

- [ ] **Step 3: 手动冒烟测试**

启动编辑器，执行以下手动测试：
1. 新建文档，输入粗体、斜体文字 → `Ctrl+\` 切换源码模式 → 看到 `**粗体**` `*斜体*`
2. 源码区修改 MD 文本 → `Ctrl+\` 切回 → 内容更新，标题栏显示脏标记 ●
3. 源码区不修改 → `Ctrl+\` 切回 → 不标记脏
4. (若已支持) 文字颜色 → 源码中保留 `<span style="color:...">`
5. 工具栏在源码模式下灰化
6. 阅读模式下源码 textarea 不可编辑
7. 快速双击 `Ctrl+\` 不发生异常

- [ ] **Step 4: 提交**

```bash
git add CMakeLists.txt
git commit -m "chore: 版本推进至 x.y.z"
```
