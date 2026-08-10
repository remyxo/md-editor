# HTML 标签支持（kbd / small / div）实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 为编辑器新增 `<kbd>`、`<small>`（inline Marks）和 `<div>`（block Node）的 TipTap 自定义扩展，在 MarkdownHandler 中添加占位符保护实现 MD↔HTML 往返，并验证已有标签（`<mark>`/`<sub>`/`<sup>`/`<hr>`/`<br>`）的 parseHTML 能力。

**Architecture:** 每个新标签 = 一个 TipTap 自定义扩展（Mark.create 或 Node.create），注册到 `_buildExtensions()`。MarkdownHandler 用 `ZZ*` 占位符模式（`ZZKD`/`ZZSM`/`ZZDV`）在 `toHtml` 和 `fromHtml` 两端保护原始 HTML 标签，确保 QTextDocument 转换不丢失它们。CSS 在 `editor.css` 和 `light.qss`/`dark.qss` 中添加。

**Tech Stack:** TipTap v3.29.2 (`Mark.create` / `Node.create`)、ProseMirror schema、Qt6 QTextDocument（md4c）、esbuild 打包（tiptap-all.js 无需重建——新增扩展全在 editor.js 中）

## Global Constraints

- 所有注释和提交信息使用中文；代码标识符（类名/方法名/变量名）使用英文
- 版本号：当前 0.3.6，推进至 0.3.7（Task 5）
- 提交规范：`feat:` / `fix:` / `test:` / `chore:` 前缀，中文描述，尾注 `Co-Authored-By: Claude <noreply@anthropic.com>`
- JS 层使用 `window.TipTapMark` / `window.TipTapNode`（已在 tiptap-entry.js 中导出并打包为 tiptap-all.js），无需重建 bundle
- TipTap v3.29.2：Mark 的 `renderHTML` 签名为 `({mark, HTMLAttributes})`，Node 同理
- MarkdownHandler 占位符模式：`ZZKD{n}ZZ` / `ZZSM{n}ZZ` / `ZZDV{n}ZZ`，不与已有 `ZZHI`/`ZZHS`/`ZZUL`/`ZZFR`/`ZZSR` 冲突
- `<div>` 必须排除 `data-type` 属性（内部结构的 div 由脚注/math 扩展处理），`getAttrs: el => el.hasAttribute('data-type') ? false : {}`
- `<div>` content 为 `inline*`（零或多个内联节点），group 为 `block`
- 构建命令：`cd build && cmake .. -DCMAKE_BUILD_TYPE=Debug && make -j$(nproc)`

---

### Task 1: `<kbd>` + `<small>` inline Marks + CSS 样式 + editor.js 注册

**Files:**
- Modify: `src/resources/web/editor.js`（新增 `buildKbdExtension` / `buildSmallExtension` 工厂函数 + 在 `_buildExtensions` 中注册）
- Modify: `src/resources/web/editor.css`（新增 `kbd` 样式）
- Modify: `src/resources/themes/light.qss`（新增 `kbd` QSS 规则）
- Modify: `src/resources/themes/dark.qss`（新增 `kbd` QSS 规则）

**Interfaces:**
- Consumes: `window.TipTapMark`（Task 1 时必定存在——colorSpan 已验证）
- Produces: `buildKbdExtension()` / `buildSmallExtension()`（模块级函数），注册到 `_buildExtensions()`
- 注意：这两个扩展不提供 toolbar 按钮或 editorApi 命令，仅支持 parseHTML 解析 + renderHTML 序列化

- [ ] **Step 1: 在 editor.js 中新增 `buildKbdExtension` 函数**

在 `buildColorSpanExtension()` 定义之后（约第 347 行 `}` 之后）、`buildEmojiExtension()` 之前新增：

```js
// 构造 kbd mark：键盘按键内联样式，渲染为 <kbd> 标签（等宽字体 + 边框圆角）。
// 无 Markdown 语法；仅用于手写 HTML <kbd> 的解析与往返保真。
function buildKbdExtension() {
    return TipTapMark.create({
        name: 'kbd',
        parseHTML: function() {
            return [{ tag: 'kbd' }];
        },
        renderHTML: function(props) {
            return ['kbd', (props && props.HTMLAttributes) || {}, 0];
        }
    });
}

// 构造 small mark：小字内联样式，渲染为 <small> 标签（浏览器默认 font-size: smaller）。
// 无 Markdown 语法；仅用于手写 HTML <small> 的解析与往返保真。
function buildSmallExtension() {
    return TipTapMark.create({
        name: 'small',
        parseHTML: function() {
            return [{ tag: 'small' }];
        },
        renderHTML: function(props) {
            return ['small', (props && props.HTMLAttributes) || {}, 0];
        }
    });
}
```

- [ ] **Step 2: 在 `_buildExtensions` 中注册 kbd 和 small**

在 `editor.js` 的 `_buildExtensions` 函数中，`colorSpan` 注册之后（约第 683 行）、`// Mermaid 块节点` 注释之前新增：

```js
        // 颜色 span（<span style> 内联样式 mark，parseHTML 仅认带 style 的 span）
        if (TipTapMark)
            exts.push(buildColorSpanExtension());
        // kbd / small 内联标签（手写 HTML 解析 + 往返保真，无 Markdown 语法）
        if (TipTapMark) {
            exts.push(buildKbdExtension());
            exts.push(buildSmallExtension());
        }
```

- [ ] **Step 3: 在 editor.css 中新增 kbd 样式**

在 `src/resources/web/editor.css` 末尾新增：

```css
/* kbd 键盘按键 */
kbd {
    font-family: ui-monospace, SFMono-Regular, "Cascadia Code", Consolas, monospace;
    font-size: 0.85em;
    border: 1px solid #aaa;
    border-radius: 3px;
    padding: 0.05em 0.35em;
    background: #f0f0f0;
}
/* small 小字（浏览器默认行为即可，不额外声明） */
```

- [ ] **Step 4: 在 light.qss / dark.qss 中新增 kbd QSS 规则**

QSS 文件控制 Qt 组件样式，但 QWebEngineView 中渲染的 web 页面的样式由 `editor.css` 控制。QSS 中的 kbd 规则仅用于编辑器内置预览场景（如果有）。此处为可选项，但需确保 light/dark 主题下 kbd 背景色与主题协调。

在 `src/resources/themes/light.qss` 末尾新增：
```css
/* kbd 键盘按键（web 编辑器内样式见 editor.css） */
```

在 `src/resources/themes/dark.qss` 末尾新增：
```css
/* kbd 键盘按键（web 编辑器内样式见 editor.css） */
```

实际上 QSS 文件不控制 web 页面内容。但为了一致性，在 `editor.css` 中额外添加媒体查询支持暗色主题：

在 `editor.css` 的 kbd 规则块追加：
```css
/* 暗色主题 kbd */
@media (prefers-color-scheme: dark) {
    kbd {
        border-color: #777;
        background: #333;
        color: #eee;
    }
}
```

- [ ] **Step 5: 构建并启动，手动验证 kbd 和 small 渲染**

```bash
cd build && cmake .. -DCMAKE_BUILD_TYPE=Debug && make -j$(nproc) && cd ..
./scripts/run-editor.sh
```

验证清单：
1. 源码模式（Ctrl+\）输入 `按<kbd>Esc</kbd>键退出`，切回 WYSIWYG → "Esc" 显示键盘按键样式（等宽、边框、浅灰底色）。
2. 源码模式输入 `这是<small>小字注释</small>文字`，切回 WYSIWYG → "小字注释" 为较小字号。
3. `getHTML()` 输出包含 `<kbd>Esc</kbd>` 和 `<small>小字注释</small>` → 标记正确序列化。
4. 保存 → 关闭 → 重开文件 → 标签仍在（端到端往返）。

- [ ] **Step 6: 提交**

```bash
git add src/resources/web/editor.js src/resources/web/editor.css src/resources/themes/light.qss src/resources/themes/dark.qss
git commit -m "feat: 新增 kbd/small 内联标签扩展支持键盘按键与小字样式"
```

---

### Task 2: `<div>` block Node 扩展 + editor.js 注册

**Files:**
- Modify: `src/resources/web/editor.js`（新增 `buildDivBlockExtension` 工厂函数 + 在 `_buildExtensions` 中注册）

**Interfaces:**
- Consumes: `window.TipTapNode`（Task 1 已验证环境）
- Produces: `buildDivBlockExtension()` → `divBlock` Node（group: `block`, content: `inline*`），注册到 `_buildExtensions()`
- 冲突处理：`divBlock.parseHTML.getAttrs` 排除 `data-type` 属性——`el.hasAttribute('data-type') ? false : {}`，让脚注/math 的 div 不被误解

- [ ] **Step 1: 在 editor.js 中新增 `buildDivBlockExtension` 函数**

在 `buildSmallExtension()` 定义之后（Task 1 新增区域末尾）、`buildEmojiExtension()` 之前新增：

```js
// 构造 divBlock 节点：块级容器（仅包裹对内联），渲染为 <div> 标签。
// - 排除 data-type 属性的 div（内部结构：脚注容器/数学块等由专有扩展处理）
// - content: inline* 表示只包裹文本与内联标记，不包裹段落/列表
// - group: block 使 divBlock 能直接出现在文档根层级（与 paragraph/heading 同级）
function buildDivBlockExtension() {
    return TipTapNode.create({
        name: 'divBlock',
        group: 'block',
        content: 'inline*',
        addAttributes: function() {
            return {
                style: {
                    default: '',
                    parseHTML: function(el) { return el.getAttribute('style') || ''; },
                    renderHTML: function(attrs) {
                        return attrs.style ? { style: attrs.style } : {};
                    }
                },
                align: {
                    default: '',
                    parseHTML: function(el) { return el.getAttribute('align') || ''; },
                    renderHTML: function(attrs) {
                        return attrs.align ? { align: attrs.align } : {};
                    }
                },
                class: {
                    default: '',
                    parseHTML: function(el) { return el.getAttribute('class') || ''; },
                    renderHTML: function(attrs) {
                        return attrs.class ? { class: attrs.class } : {};
                    }
                }
            };
        },
        parseHTML: function() {
            return [{
                tag: 'div',
                getAttrs: function(el) {
                    // 排除内部结构的 div（脚注/数学块等已有专属扩展）
                    return el.hasAttribute('data-type') ? false : {};
                }
            }];
        },
        renderHTML: function(props) {
            return ['div', (props && props.HTMLAttributes) || {}, 0];
        }
    });
}
```

- [ ] **Step 2: 在 `_buildExtensions` 中注册 divBlock**

在 `_buildExtensions` 中，`kbd`/`small` 注册之后、`// Mermaid 块节点` 注释之前新增：

```js
        // kbd / small 内联标签（手写 HTML 解析 + 往返保真，无 Markdown 语法）
        if (TipTapMark) {
            exts.push(buildKbdExtension());
            exts.push(buildSmallExtension());
        }
        // div 块级容器（仅包裹内联内容，排除 data-type div）
        if (TipTapNode)
            exts.push(buildDivBlockExtension());
```

- [ ] **Step 3: 构建并启动，手动验证 divBlock 渲染/冲突**

```bash
cd build && cmake .. -DCMAKE_BUILD_TYPE=Debug && make -j$(nproc) && cd ..
./scripts/run-editor.sh
```

验证清单：
1. 源码模式输入 `<div>这是 div 容器</div>`，切回 WYSIWYG → div 渲染为块级容器。
2. 源码模式输入 `<div style="text-align:right">右对齐文本</div>` → 右对齐生效。
3. 脚注功能正常：插入脚注后切换到源码模式，`<div data-type="footnoteContainer">` 和 `<div data-type="footnoteDef">` 不受 divBlock 影响。
4. 数学公式正常：`$$x=y$$` → `<div data-type="block-math">` 正常渲染。
5. `getHTML()` 输出中用户手写的 `<div>` 保留完整，内部结构的 `<div data-type>` 不受干扰。

- [ ] **Step 4: 提交**

```bash
git add src/resources/web/editor.js
git commit -m "feat: 新增 divBlock 块级容器节点仅包裹对内联内容"
```

---

### Task 3: MarkdownHandler 占位符保护 + 往返测试

**Files:**
- Modify: `src/core/MarkdownHandler.cpp`（`toHtml` 方向新增 `extractHtmlInlineElements` 处理 `<kbd>`/`<small>`/`<div>` 提取 + `fromHtml` 方向新增 ZZ* 保护）
- Test: `tests/test_markdownhandler.cpp`（新增 `testKbdRoundTrip` / `testSmallRoundTrip` / `testDivBlockRoundTrip` 三个用例）

**Interfaces:**
- Consumes: Task 1+2 的 JS 扩展输出 `<kbd>`/`<small>`/`<div>` 标签；`extractHtmlInlineElements` 静态函数（`MarkdownHandler.cpp:64`）、`ZZHS`/`ZZUL` 保护块（`:1093-1129`）
- Produces: 三个新 ZZ* 占位符类型的保护/还原逻辑；`test_markdownhandler.cpp` 中三个新的 QTest 用例

**注意**：`<div>` 在 toHtml 方向需要与 `<span style>`/`<u>` 区分——`<div>` 可能跨行（`DotMatchesEverythingOption`），但 `extractHtmlInlineElements` 是按行调用的。对于块级 `<div>`，需要在**按行处理之前**先提取（类似代码块/数学块预扫描），或者在**整段 HTML 构建完成后**再保护/还原。

阅读 `extractHtmlInlineElements` 的调用上下文（`toHtml` 中 `:495-513`）：该函数按行处理内联元素。`<div>` 是块级元素，**不应**在此函数中处理。应在 `toHtml()` 的**后处理阶段**（`setMarkdown` 之后）直接处理 `<div>` 的 ZZ* 保护，或在**预处理阶段**（`setMarkdown` 之前）提取 `<div>` 块。

实际实现策略：
- **toHtml 方向**：在 `setMarkdown()` 之后、后处理还原 `ZZHI` 之前，扫描完整 HTML 输出，将不带 `data-type` 的 `<div>` 替换为 `ZZDV{n}ZZ` 占位符，然后还原 ZZHI，最后在末尾还原 `ZZDV` → 原始 `<div>`。
- **fromHtml 方向**：在保护 `ZZHS`/`ZZUL` 之后、`setHtml()` 之前（即第 1129 行之后），用相同逻辑保护不带 `data-type` 的 `<div>`。

- [ ] **Step 1: 在 `toHtml` 方向新增 `<div>` 保护**

在 `MarkdownHandler.cpp` 的 `toHtml()` 函数中，找到还原 `ZZHI` 占位符的代码块（`// 还原 HTML 内联标签占位符 ZZHI → ...`，约 `:667-672`），**在该还原步骤之前**新增：

```cpp
    // 保护手写 <div> 块级容器（排除 data-type 内部结构）
    // 置于 ZZHI 还原之前：ZZHI 占位符内容可能含 <div>，若先还原 ZZHI 再匹配
    // <div> 会把内联 span/style 一起卷进去，造成嵌套错乱。
    // 先用占位符保护不带 data-type 的 <div>，还原 ZZHI 后再还原 ZZDV。
    QStringList divBackups;
    {
        QRegularExpression divRe(
            R"(<div\b(?![^>]*\bdata-type\s*=)[^>]*>(.*?)</div>)",
            QRegularExpression::DotMatchesEverythingOption);
        int n = 0;
        int offset = 0;
        while (true) {
            auto m = divRe.match(html, offset);
            if (!m.hasMatch()) break;
            divBackups << m.captured();
            QString ph = QString("ZZDV%1ZZ").arg(n++);
            html.replace(m.capturedStart(), m.capturedLength(), ph);
            offset = m.capturedStart() + ph.length();
        }
    }
```

在还原 ZZHI 之后新增 ZZDV 还原：
```cpp
    // 还原 <div> 占位符 ZZDV → 原始 <div>（在 ZZHI 之后：ZZDV 内部可能含 ZZ* 占位符的字面文本）
    for (int i = 0; i < divBackups.size(); ++i)
        html.replace("ZZDV" + QString::number(i) + "ZZ", divBackups[i]);
```

- [ ] **Step 2: 在 `toHtml` 方向为 `<kbd>` 和 `<small>` 扩展现有的 `extractHtmlInlineElements`**

在 `extractHtmlInlineElements` 函数中（`MarkdownHandler.cpp:64-97`），`<u>` 处理块之后新增两段：

```cpp
    // <kbd>text</kbd>
    {
        QRegularExpression kbdRe(R"(<kbd>(.*?)</kbd>)");
        int offset = 0;
        while (true) {
            auto m = kbdRe.match(r, offset);
            if (!m.hasMatch()) break;
            backups << m.captured();
            QString ph = QString("ZZHI%1ZZ").arg(n++);
            r.replace(m.capturedStart(), m.capturedLength(), ph);
            offset = m.capturedStart() + ph.length();
        }
    }
    // <small>text</small>
    {
        QRegularExpression smallRe(R"(<small>(.*?)</small>)");
        int offset = 0;
        while (true) {
            auto m = smallRe.match(r, offset);
            if (!m.hasMatch()) break;
            backups << m.captured();
            QString ph = QString("ZZHI%1ZZ").arg(n++);
            r.replace(m.capturedStart(), m.capturedLength(), ph);
            offset = m.capturedStart() + ph.length();
        }
    }
```

注意：`<kbd>` 和 `<small>` 是 inline 元素（和 `<span style>`/`<u>` 一样），复用已有的 ZZHI 占位符体系（不需要单独的 ZZKD/ZZSM）。已有还原逻辑 `html.replace("ZZHI" + QString::number(i) + "ZZ", htmlInlineBackups[i])` 自动处理。这样可以最小化改动。

- [ ] **Step 3: 在 `fromHtml` 方向新增三个标签的 ZZ* 保护**

在 `fromHtml()` 函数中，`<u>` 保护块之后（约第 1129 行 `}` 之后）、`QTextDocument doc;` 之前（约第 1131 行）新增三段：

```cpp
        // <kbd>text</kbd>（内联键盘元素，QTextDocument::toMarkdown 会丢弃标签）
        offset = 0;
        QRegularExpression kbdRe(
            R"(<kbd>(.*?)</kbd>)",
            QRegularExpression::DotMatchesEverythingOption);
        while (true) {
            auto m = kbdRe.match(processed, offset);
            if (!m.hasMatch()) break;
            QString ph = QString("ZZKD%1ZZ").arg(n++);
            placeholders << ph;
            backups << m.captured();
            processed.replace(m.capturedStart(), m.capturedLength(), ph);
            offset = m.capturedStart() + ph.length();
        }
        // <small>text</small>（内联小字元素）
        offset = 0;
        QRegularExpression smallRe(
            R"(<small>(.*?)</small>)",
            QRegularExpression::DotMatchesEverythingOption);
        while (true) {
            auto m = smallRe.match(processed, offset);
            if (!m.hasMatch()) break;
            QString ph = QString("ZZSM%1ZZ").arg(n++);
            placeholders << ph;
            backups << m.captured();
            processed.replace(m.capturedStart(), m.capturedLength(), ph);
            offset = m.capturedStart() + ph.length();
        }
        // <div>（块级容器，排除 data-type 内部结构）
        offset = 0;
        QRegularExpression divRe(
            R"(<div\b(?![^>]*\bdata-type\s*=)[^>]*>(.*?)</div>)",
            QRegularExpression::DotMatchesEverythingOption);
        while (true) {
            auto m = divRe.match(processed, offset);
            if (!m.hasMatch()) break;
            QString ph = QString("ZZDV%1ZZ").arg(n++);
            placeholders << ph;
            backups << m.captured();
            processed.replace(m.capturedStart(), m.capturedLength(), ph);
            offset = m.capturedStart() + ph.length();
        }
```

- [ ] **Step 4: 新增 C++ 测试用例**

在 `tests/test_markdownhandler.cpp` 的 `testSpanInnerFootnote` 之后（约第 678 行）新增三个用例：

```cpp
    void testKbdRoundTrip()
    {
        // kbd 往返：toHtml 保护 → fromHtml 还原 → 二次往返无占位符残留
        QString md = QStringLiteral("按<kbd>Esc</kbd>键退出");
        QString html = MarkdownHandler::toHtml(md);
        QVERIFY2(html.contains("<kbd>Esc</kbd>"),
                 qPrintable("toHtml 未保留 <kbd>，实际: " + html));

        QString md2 = MarkdownHandler::fromHtml(
            QStringLiteral("<p>按<kbd>Esc</kbd>键退出</p>"));
        QVERIFY2(md2.contains("<kbd>Esc</kbd>"),
                 qPrintable("fromHtml 未保留 <kbd>，实际: " + md2));

        // 二次往返，无占位符残留
        QString html2 = MarkdownHandler::toHtml(md2);
        QVERIFY2(html2.contains("<kbd>Esc</kbd>"),
                 qPrintable("二次往返 kbd 丢失，实际: " + html2));
        QVERIFY2(!html2.contains("ZZKD") && !html2.contains("ZZHI"),
                 qPrintable("残留占位符，实际: " + html2));
    }

    void testSmallRoundTrip()
    {
        QString md = QStringLiteral("这是<small>小字注释</small>文字");
        QString html = MarkdownHandler::toHtml(md);
        QVERIFY2(html.contains("<small>小字注释</small>"),
                 qPrintable("toHtml 未保留 <small>，实际: " + html));

        QString md2 = MarkdownHandler::fromHtml(
            QStringLiteral("<p>这是<small>小字注释</small>文字</p>"));
        QVERIFY2(md2.contains("<small>小字注释</small>"),
                 qPrintable("fromHtml 未保留 <small>，实际: " + md2));

        QString html2 = MarkdownHandler::toHtml(md2);
        QVERIFY2(html2.contains("<small>小字注释</small>"),
                 qPrintable("二次往返 small 丢失，实际: " + html2));
        QVERIFY2(!html2.contains("ZZSM") && !html2.contains("ZZHI"),
                 qPrintable("残留占位符，实际: " + html2));
    }

    void testDivBlockRoundTrip()
    {
        // 无 data-type 的手写 <div> 应被保护往返
        QString md = QStringLiteral("<div>容器内容</div>");
        QString html = MarkdownHandler::toHtml(md);
        QVERIFY2(html.contains("<div>容器内容</div>"),
                 qPrintable("toHtml 未保留 <div>，实际: " + html));

        QString md2 = MarkdownHandler::fromHtml(
            QStringLiteral("<div>容器内容</div>"));
        QVERIFY2(md2.contains("<div>容器内容</div>"),
                 qPrintable("fromHtml 未保留 <div>，实际: " + md2));

        // 有 data-type 的 <div> 不被保护（属于脚注/math 扩展）
        QString md3 = QStringLiteral("<div data-type=\"x\">内部</div>");
        QString html3 = MarkdownHandler::toHtml(md3);
        QVERIFY2(!html3.contains("ZZDV"),
                 qPrintable("data-type div 不应进入 ZZDV 保护，实际: " + html3));

        // 二次往返无残留
        QString html4 = MarkdownHandler::toHtml(md2);
        QVERIFY2(!html4.contains("ZZDV"),
                 qPrintable("残留 ZZDV 占位符，实际: " + html4));
    }
```

- [ ] **Step 5: 运行测试确认通过**

```bash
cd build && cmake .. -DCMAKE_BUILD_TYPE=Debug && make -j$(nproc) && cd ..
cd build && ctest --output-on-failure -R markdownhandler && cd ..
```
预期：所有既有用例 + 3 个新用例 PASS。

- [ ] **Step 6: 提交**

```bash
git add src/core/MarkdownHandler.cpp tests/test_markdownhandler.cpp
git commit -m "test: 新增 kbd/small/div 往返回归测试与 MarkdownHandler 占位符保护"
```

---

### Task 4: P1 标签 parseHTML 验证（已有 `<mark>`/`<sub>`/`<sup>`/`<hr>`/`<br>`）

**Files:**
- 无修改（验证仅观察，若发现缺陷则记录到 issues，不在本次修复）

**Interfaces:**
- Consumes: `editor.html` → `tiptap-all.js` → `editor.js`（已注册 Highlight/Subscript/Superscript/HorizontalRule/HardBreak 扩展）
- Produces: 验证报告（记录到 task-4-report.md）

- [ ] **Step 1: 验证 `<mark>` parseHTML**

在源码模式（Ctrl+\）输入：`<mark>高亮文字</mark>`，切回 WYSIWYG。
预期：TipTap Highlight 扩展的 parseHTML 识别 `<mark>` 标签，"高亮文字"显示高亮底色。
同时验证 `==高亮文字==` 语法正常。

- [ ] **Step 2: 验证 `<sub>` / `<sup>` parseHTML**

源码模式输入：`H<sub>2</sub>O` 和 `x<sup>2</sup>`，切回 WYSIWYG。
预期：TipTap Subscript/Superscript 扩展识别，文字变小上/下偏移。
同时验证 `~下标~` / `^上标^` 语法正常。

- [ ] **Step 3: 验证 `<hr>` parseHTML**

源码模式输入：`<hr>`，切回 WYSIWYG。
预期：TipTap HorizontalRule 识别，渲染为分隔线。

- [ ] **Step 4: 验证 `<br>` parseHTML**

源码模式输入：`第一行<br>第二行`，切回 WYSIWYG。
预期：TipTap HardBreak 识别，第一行和第二行之间为硬换行。

- [ ] **Step 5: 撰写验证报告**

将验证结果写入 `/home/daijd/claude-workspace/md-editor/.superpowers/sdd/2026-08-10-html-tags-support/task-4-report.md`：
- 每个标签：✅ 正常 / ❌ 缺陷（描述具体现象）
- 如有缺陷：记录到 issues，不在此任务中修复

- [ ] **Step 6: 提交**

```bash
git add .superpowers/sdd/2026-08-10-html-tags-support/task-4-report.md
git commit -m "docs: P1 HTML 标签（mark/sub/sup/hr/br）parseHTML 验证报告"
```

---

### Task 5: 版本推进 0.3.7 + CHANGELOG

**Files:**
- Modify: `CMakeLists.txt:2`（`VERSION 0.3.6` → `VERSION 0.3.7`）
- Modify: `CHANGELOG.md`（在 `[0.3.6]` 条目之前插入 `[0.3.7]`）

**Interfaces:** 无（收尾任务，版本号全局唯一，Memory `update-changelog-on-tag` 要求同步记录）

- [ ] **Step 1: 推进版本号**

`CMakeLists.txt:2`：
```cmake
project(md-editor VERSION 0.3.7 LANGUAGES CXX)
```

- [ ] **Step 2: 新增 CHANGELOG 条目**

在 `CHANGELOG.md` 的 `## [0.3.6]` 行之前插入：
```markdown
## [0.3.7] — 2026-08-10

### 新增

- **HTML 标签支持** — 新增 `<kbd>`（键盘按键）、`<small>`（小字）内联标记和 `<div>`（块级容器，仅包裹内联内容）的 TipTap 自定义扩展，支持手写 HTML 解析、渲染和 Markdown↔HTML 往返保真
- **已有标签验证** — `<mark>`（高亮）、`<sub>`/`<sup>`（上下标）、`<hr>`（分隔线）、`<br>`（硬换行）的手写 HTML 解析能力确认
```

- [ ] **Step 3: 构建并全量测试**

```bash
cd build && cmake .. -DCMAKE_BUILD_TYPE=Debug && make -j$(nproc) && cd ..
cd build && ctest --output-on-failure && cd ..
```
预期：所有测试 PASS。

- [ ] **Step 4: 提交并打 tag**

```bash
git add CMakeLists.txt CHANGELOG.md
git commit -m "chore: 版本推进至 0.3.7"
git tag v0.3.7
```

---

## 自查（Self-Review）

**规格覆盖**：
- §3.1 `<kbd>` → Task 1（buildKbdExtension + CSS）
- §3.2 `<small>` → Task 1（buildSmallExtension + CSS）
- §3.3 `<div>` → Task 2（buildDivBlockExtension，content: inline*，排除 data-type）
- §3.4 `<mark>` → Task 4（parseHTML 验证）
- §3.5 `<sub>`/`<sup>` → Task 4（parseHTML 验证）
- §3.6 `<hr>` → Task 4（parseHTML 验证）
- §3.7 `<br>` → Task 4（parseHTML 验证）
- §4 MarkdownHandler 改动 → Task 3（toHtml/fromHtml 双向 ZZ* 保护 + C++ 测试）
- §5 CSS 样式 → Task 1 Step 3-4
- §7 开放问题 → P0+P1 合并为一个 plan（5 tasks），P1 仅验证不修改

**类型一致**：
- `buildKbdExtension()` / `buildSmallExtension()` / `buildDivBlockExtension()` 在 Task 1/2 定义，在 Task 2 Step 2 引用注释中一致（仅注释引用，无跨任务代码依赖）
- `ZZDV` / `ZZKD` / `ZZSM` 占位符名在 Task 3 Step 1-3 定义，在 Step 4 测试断言中引用一致
- `extractHtmlInlineElements` 在 Task 3 Step 2 扩展（复用 ZZHI 体系），MarkdownHandler.cpp 中函数签名不变

**占位符扫描**：无 TBD/TODO；所有代码步骤含实际代码；手动验证步骤含具体操作与预期。
