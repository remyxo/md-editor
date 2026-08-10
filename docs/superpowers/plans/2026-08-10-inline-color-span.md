# 内联颜色样式支持 + 移除两端对齐按钮 实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 让 `<span style="color:...">` 在 WYSIWYG 中真正可渲染/编辑/往返，并加上工具栏「文字色/背景色」调色板入口；同时移除「两端对齐」按钮精简工具栏。

**Architecture:** 在 TipTap 新增 `colorSpan` mark（方案 B：attrs 存原始 style 字符串，UI 限色但底层保真）。复用 `MarkdownHandler` 既有的 `<span style>` 行内保护逻辑（`extractHtmlInlineElements` / fromHtml 侧 `ZZHS` 保护，已对称），C++ 层预期无需改动，仅加回归测试验证。命令在 `editorApi` 层做样式合并（解析当前 style → 改 color 或 background-color → 重新 setMark），使文字色与背景色互不覆盖。工具栏两个按钮弹出 `QMenu` 调色板（12 预设色 + 无颜色 + 更多颜色 `QColorDialog`）。

**Tech Stack:** Qt6（ToolBar/MainWindow/QColorDialog/QTest）、TipTap/ProseMirror（`Mark.create`）、esbuild（重建 bundle）、QSS/SVG 图标。

## Global Constraints

- 所有文档与代码注释使用**中文**，代码标识符使用英文（CLAUDE.md 约束）。
- 版本号定义于 `CMakeLists.txt:2` 的 `project(md-editor VERSION x.y.z)`，C++ 侧经 `MD_EDITOR_VERSION` 宏引用；本计划目标版本 **0.3.6**。
- 提交规范：`feat:` / `fix:` / `chore:` 前缀 + 中文描述。
- 打 tag 时必须同步更新 `CHANGELOG.md`（记忆 `update-changelog-on-tag`），格式遵循 Keep a Changelog。
- 改 `tiptap-entry.js` 后必须在 `src/resources/web/` 下运行 `build-tiptap.sh` 重建 `tiptap-all.js`，并把重建产物一并提交（产物被 git 跟踪）。
- 改 `editor.js` **不需要**重新打包（它是独立脚本，非 bundle 一部分）。
- TipTap 层无 QTest 覆盖，相关验证为人工运行应用；C++ 层（MarkdownHandler）用 QTest。

---

## 文件结构

| 文件 | 职责 | 本计划动作 |
|------|------|-----------|
| `src/resources/web/tiptap-entry.js` | esbuild 打包入口，暴露 `window.TipTap*` | 新增暴露 `Mark` |
| `src/resources/web/tiptap-all.js` | 重建产物（git 跟踪） | 重建并提交 |
| `src/resources/web/editor.js` | TipTap 初始化 + `editorApi` + 状态同步 | 新增 `colorSpan` mark、合并命令、`notifyFormat` 字段；TextAlign 限定 alignments |
| `src/resources/icons/light/text-color.svg` `bg-color.svg` | 亮色主题颜色按钮图标 | 新建 |
| `src/resources/icons/dark/text-color.svg` `bg-color.svg` | 暗色主题颜色按钮图标 | 新建 |
| `src/resources/app.qrc` | Qt 资源清单 | 注册 4 个新 SVG |
| `src/ui/ToolBar.h` | 工具栏声明 | 新增颜色按钮成员/信号/私有方法；删除 justify 成员 |
| `src/ui/ToolBar.cpp` | 工具栏实现 | 颜色按钮 + 调色板菜单 + 格式回显；删除 justify |
| `src/ui/MainWindow.cpp` | 信号编排 | 连接颜色信号；删除 justify 连接 |
| `tests/test_markdownhandler.cpp` | MarkdownHandler QTest | 新增颜色 span 往返用例 |
| `CMakeLists.txt` | 版本定义 | 0.3.5 → 0.3.6 |
| `CHANGELOG.md` | 变更记录 | 新增 0.3.6 条目 |

---

## Task 1: 暴露 TipTap `Mark` 并重建 bundle

**Files:**
- Modify: `src/resources/web/tiptap-entry.js:9`、`src/resources/web/tiptap-entry.js:28-32`
- Rebuild: `src/resources/web/tiptap-all.js`

**Interfaces:**
- Produces: `window.TipTapMark`（供 Task 2 的 `editor.js` 引用）

**背景**：当前入口只 import 并暴露了 `Editor/Extension/InputRule/Node`，未暴露 `Mark`。Task 2 的 `colorSpan` mark 需 `TipTapMark.create()`。

- [ ] **Step 1: 修改 `tiptap-entry.js` 的 import**

把第 9 行：
```js
import { Editor, Extension, InputRule, Node } from '@tiptap/core';
```
改为：
```js
import { Editor, Extension, InputRule, Node, Mark } from '@tiptap/core';
```

- [ ] **Step 2: 暴露 `window.TipTapMark`**

在第 32 行 `window.TipTapNode = Node;` 之后新增一行：
```js
window.TipTapNode = Node;
// 自定义 mark 构建用（colorSpan 内联样式 mark）
window.TipTapMark = Mark;
```

- [ ] **Step 3: 重建 bundle**

从仓库根运行（脚本以 `$(pwd)` 为依赖目录，需在 `node_modules` 所在的 `src/resources/web/` 下执行）：
```bash
cd src/resources/web && bash build-tiptap.sh && cd ../..
```
预期：脚本输出 esbuild 成功，`tiptap-all.js` 更新。

- [ ] **Step 4: 验证 bundle 含 Mark 导出**

```bash
grep -c "TipTapMark" src/resources/web/tiptap-all.js
```
预期：输出 ≥ 1。

- [ ] **Step 5: 提交**

```bash
git add src/resources/web/tiptap-entry.js src/resources/web/tiptap-all.js
git commit -m "feat: 打包入口暴露 TipTap Mark 供 colorSpan 扩展使用"
```

---

## Task 2: `colorSpan` mark + 样式合并命令 + notifyFormat

**Files:**
- Modify: `src/resources/web/editor.js:18`（全局引用）、`:296-314` 附近（新增扩展工厂）、`:597-633`（注册）、`:746-758` 附近（notifyFormat）、`:846-848` 附近（命令）

**Interfaces:**
- Consumes: `window.TipTapMark`（Task 1 产物）
- Produces: `editorApi.applyTextColor(color)` / `applyBgColor(color)` / `clearTextColor()` / `clearBgColor()`；`notifyFormat` 的 `format.colorSpanStyle` 字段

**设计要点**：
1. `colorSpan` mark：attrs 存原始 `style` 字符串。`parseHTML` 仅认**带 style 的 span**（无 style 的 span 返回 `false` 放行给脚注扩展，复用 underline 扩展的 `getAttrs ... : false` 范式）。mark 级 `renderHTML` 只输出空属性 `['span', {}, 0]`，具体 `style` 由 attribute 级 `renderHTML` 注入——这样**规避 TipTap v2/v3 的 mark `renderHTML` 签名差异**（attribute 级始终接收整个 attrs 对象）。
2. 样式合并：单个 mark 单个 style 字符串，直接 setMark 会让文字色与背景色互相覆盖。故 `applyTextColor`/`applyBgColor` 先 `getAttributes('colorSpan')` 读当前 style，解析出 `{color, bg}`，改其一，重新 stringify 后 setMark。解析用正则；`background-color` 与 `color` 用 `(^|;)\s*color` 边界区分，避免 `color` 吞掉 `background-color`。
3. 不在 mark 的 `addCommands` 里定义命令（避免版本 API 不确定性），直接在 `editorApi` 用 TipTap 内置 `commands.setMark` / `commands.unsetMark`。

- [ ] **Step 1: 加全局引用**

`editor.js:18` 的 `var TipTapNode = window.TipTapNode;` 之后新增一行：
```js
var TipTapNode = window.TipTapNode;
var TipTapMark = window.TipTapMark;
```

- [ ] **Step 2: 新增样式解析助手（模块级 function）**

在 `buildEmojiExtension()` 函数定义之前（约 `editor.js:294` 注释处之前）新增两个助手与 mark 工厂：
```js
// 解析 colorSpan 的 style 字符串 → { color, bg }（缺省属性省略）。
// 注意：用 (^|;) 锚定 color 前边界，避免匹配到 "background-color" 的尾部 color。
function _parseColorStyle(styleStr) {
    var o = {};
    if (!styleStr) return o;
    var bg = styleStr.match(/background-color\s*:\s*([^;]+)/i);
    var fg = styleStr.match(/(^|;)\s*color\s*:\s*([^;]+)/i);
    if (bg) o.bg = bg[1].trim();
    if (fg) o.color = fg[2].trim();
    return o;
}

// { color, bg } → 'color:..;background-color:..'（固定属性顺序，保证往返稳定）。
function _stringifyColorStyle(o) {
    var parts = [];
    if (o.color) parts.push('color:' + o.color);
    if (o.bg) parts.push('background-color:' + o.bg);
    return parts.join(';');
}

// 构造 colorSpan mark：承载 <span style="..."> 的内联样式（颜色为主，底层保真任意 style）。
// - parseHTML 仅认带 style 的 span；无 style 的 span（脚注 marker/content）返回 false 放行
// - mark 级 renderHTML 输出空属性 span，style 由 attribute 级 renderHTML 注入
//   （规避 TipTap v2/v3 的 mark renderHTML 签名差异）
function buildColorSpanExtension() {
    return TipTapMark.create({
        name: 'colorSpan',
        addAttributes: function() {
            return {
                style: {
                    default: '',
                    parseHTML: function(el) {
                        return el.getAttribute('style') || '';
                    },
                    renderHTML: function(attrs) {
                        return attrs.style ? { style: attrs.style } : {};
                    }
                }
            };
        },
        parseHTML: function() {
            return [{
                tag: 'span',
                getAttrs: function(el) {
                    return el.getAttribute('style') ? {} : false;
                }
            }];
        },
        renderHTML: function() {
            return ['span', {}, 0];
        }
    });
}
```

- [ ] **Step 3: 在 `_buildExtensions` 注册 colorSpan**

在 `editor.js:624` 的 emoji push 之后新增（`if (TipTapNode) {...}` 之前）：
```js
        // Emoji 短代码 InputRule（:shortcode: → Unicode 字符）
        if (TipTapExtension && TipTapInputRule)
            exts.push(buildEmojiExtension());
        // 颜色 span（<span style> 内联样式 mark，parseHTML 仅认带 style 的 span）
        if (TipTapMark)
            exts.push(buildColorSpanExtension());
```

- [ ] **Step 4: `notifyFormat` 增加 colorSpanStyle**

在 `editor.js:758` 的 textAlign 赋值之后（`// 结构` 注释之前）新增：
```js
        // 文字对齐（未显式设置时属性为 null，工具栏按左对齐高亮）
        format.textAlign = ed.getAttributes('paragraph').textAlign
            || ed.getAttributes('heading').textAlign
            || 'left';
        // 颜色 span 的 style 字符串（文字色/背景色回显）
        format.colorSpanStyle = ed.getAttributes('colorSpan').style || '';
```

- [ ] **Step 5: 新增 editorApi 颜色命令**

在 `editor.js:848` 的 `setTextAlign` 方法之后（`setHeading` 之前）新增：
```js
    setTextAlign: function(alignment) {
        if (this.editor) this.editor.commands.setTextAlign(alignment);
    },
    // === 内联颜色（colorSpan mark 的样式合并命令） ===
    applyTextColor: function(color) {
        if (!this.editor) return;
        var cur = _parseColorStyle(this.editor.getAttributes('colorSpan').style || '');
        cur.color = color;
        this.editor.commands.setMark('colorSpan', { style: _stringifyColorStyle(cur) });
    },
    applyBgColor: function(color) {
        if (!this.editor) return;
        var cur = _parseColorStyle(this.editor.getAttributes('colorSpan').style || '');
        cur.bg = color;
        this.editor.commands.setMark('colorSpan', { style: _stringifyColorStyle(cur) });
    },
    clearTextColor: function() {
        if (!this.editor) return;
        var cur = _parseColorStyle(this.editor.getAttributes('colorSpan').style || '');
        delete cur.color;
        var s = _stringifyColorStyle(cur);
        if (s) this.editor.commands.setMark('colorSpan', { style: s });
        else this.editor.commands.unsetMark('colorSpan');
    },
    clearBgColor: function() {
        if (!this.editor) return;
        var cur = _parseColorStyle(this.editor.getAttributes('colorSpan').style || '');
        delete cur.bg;
        var s = _stringifyColorStyle(cur);
        if (s) this.editor.commands.setMark('colorSpan', { style: s });
        else this.editor.commands.unsetMark('colorSpan');
    },
```

- [ ] **Step 6: 构建并启动，手动验证渲染**

```bash
cd build && cmake .. -DCMAKE_BUILD_TYPE=Debug && make -j$(nproc) && cd ..
./scripts/run-editor.sh
```
手动验证（无 QTest 覆盖 TipTap 层）：
1. 源码模式（`Ctrl+\`）输入 `普通<span style="color:red">红</span>尾`，切回 WYSIWYG → 「红」渲染为红色。
2. 选中文字 → （Task 5 完成后用工具栏；本步可先用浏览器控制台执行 `window.editorApi.applyTextColor('#00aa00')` 验证）→ 文字变绿。
3. 源码往返：保存后重新打开，颜色仍在。

- [ ] **Step 7: 提交**

```bash
git add src/resources/web/editor.js
git commit -m "feat: 新增 colorSpan mark 支持内联颜色样式渲染与往返"
```

---

## Task 3: MarkdownHandler 颜色 span 往返回归测试

**Files:**
- Test: `tests/test_markdownhandler.cpp`（在现有 `private slots:` 区内新增用例）

**Interfaces:**
- Consumes: `MarkdownHandler::toHtml` / `fromHtml`（既有，预期不改）

**目的**：证明 `MarkdownHandler` 既有的 `<span style>` 行内保护（toHtml 侧 `ZZHI`、fromHtml 侧 `ZZHS`）对 colorSpan 往返已**对称且充分**，C++ 层无需改动。若测试失败，再在 MarkdownHandler 内补；预期通过。

- [ ] **Step 1: 写失败测试（先于确认实现）**

在 `test_markdownhandler.cpp` 的 `testSpanInnerFootnote`（约 `:670`）之后新增三个用例：
```cpp
    void testColorSpanRoundTrip()
    {
        // 源码手写 <span style="color:red"> 经 toHtml 保护往返后还原
        QString md = QStringLiteral("普通文本<span style=\"color:red\">红色</span>尾部");
        QString html = MarkdownHandler::toHtml(md);
        QVERIFY2(html.contains("<span style=\"color:red\">红色</span>"),
                 qPrintable("toHtml 未保留颜色 span，实际: " + html));

        // fromHtml（编辑器 getHTML → 保存）侧对称保护
        QString md2 = MarkdownHandler::fromHtml(
            QStringLiteral("<p>普通文本<span style=\"color:red\">红色</span>尾部</p>"));
        QVERIFY2(md2.contains("<span style=\"color:red\">红色</span>"),
                 qPrintable("fromHtml 未保留颜色 span，实际: " + md2));

        // 二次往返 toHtml→fromHtml→toHtml，无占位符残留
        QString html2 = MarkdownHandler::toHtml(md2);
        QVERIFY2(html2.contains("<span style=\"color:red\">红色</span>"),
                 qPrintable("二次往返颜色 span 丢失，实际: " + html2));
        QVERIFY2(!html2.contains("ZZHI") && !html2.contains("ZZHS"),
                 qPrintable("残留占位符，实际: " + html2));
    }

    void testColorSpanMultiStyleRoundTrip()
    {
        // 多属性 style（文字色 + 背景色）完整往返
        QString md = QStringLiteral("<span style=\"color:#ffffff;background-color:#e23b3b\">红底白字</span>");
        QString html = MarkdownHandler::toHtml(md);
        QVERIFY2(html.contains("color:#ffffff") && html.contains("background-color:#e23b3b"),
                 qPrintable("多属性 style 往返丢失，实际: " + html));
    }

    void testColorSpanWithBoldRoundTrip()
    {
        // 粗体包裹颜色 span（编辑器内 <strong><span style>…</span></strong> 的 .md 形态）
        QString md = QStringLiteral("**<span style=\"color:red\">粗红</span>**");
        QString html = MarkdownHandler::toHtml(md);
        QVERIFY2(html.contains("<strong>") && html.contains("<span style=\"color:red\">粗红</span>"),
                 qPrintable("粗体+颜色 span 往返异常，实际: " + html));
    }
```

- [ ] **Step 2: 运行测试，确认通过（验证现有实现已充分）**

```bash
cd build && cmake .. -DCMAKE_BUILD_TYPE=Debug && make -j$(nproc) && cd ..
cd build && ctest --output-on-failure -R markdownhandler && cd ..
```
预期：三个新用例 PASS（MarkdownHandler 既有保护逻辑已对称）。若任一 FAIL：在 `src/core/MarkdownHandler.cpp` 定位对应保护正则（toHtml `:71-72`、fromHtml `:1103-1105`）按失败信息修补，再跑。

- [ ] **Step 3: 提交**

```bash
git add tests/test_markdownhandler.cpp
git commit -m "test: 增补颜色 span 往返与多属性/粗体叠加回归用例"
```

---

## Task 4: 移除「两端对齐」按钮

**Files:**
- Modify: `src/ui/ToolBar.h:63`、`src/ui/ToolBar.cpp:25,134-141,210,315,319,323`、`src/resources/web/editor.js:619-621`

**Interfaces:**
- 无新增；仅移除 justify 相关声明/连接/回显。

**回归评估**：对齐本就往返有损、从未持久化到 .md，移除按钮无数据影响（规格 §6）。

- [ ] **Step 1: `ToolBar.h` 删除 justify 成员**

删除第 63 行：
```cpp
    QAction *m_alignJustifyAction;
```

- [ ] **Step 2: `ToolBar.cpp` 删除构造函数初始化**

删除第 25 行：
```cpp
    , m_alignJustifyAction(nullptr)
```

- [ ] **Step 3: `ToolBar.cpp` 删除按钮创建与加入互斥组**

删除第 134-141 行的 justify 创建与 `addAction`：
```cpp
    m_alignJustifyAction = makeAction(this, &m_iconPaths, "alignJustifyAction",
        "两端对齐 (Ctrl+Shift+J)", ":/icons/light/align-justify.svg");
    m_alignJustifyAction->setCheckable(true);
    m_alignGroup->addAction(m_alignJustifyAction);
```
（仅删以上 3 句涉及 justify 的；`m_alignGroup->addAction(m_alignLeft/Center/Right)` 与 `setChecked(true)` 保留。）

- [ ] **Step 4: `ToolBar.cpp` 删除 justify 的 connect**

删除第 210 行：
```cpp
    connect(m_alignJustifyAction, &QAction::triggered, this, [this]{ emit alignSelected("justify"); });
```

- [ ] **Step 5: `ToolBar.cpp` 删除 updateFormatState 中的 justify 回显**

在 `updateFormatState`（`:310-324`）中删除 4 处 justify 引用：
```cpp
        m_alignJustifyAction->blockSignals(true);   // 删
        m_alignJustifyAction->setChecked(a == "justify");   // 删
        m_alignJustifyAction->blockSignals(false);  // 删
```

- [ ] **Step 6: `editor.js` 防御性禁用 justify**

把 `editor.js:619-621`：
```js
        // 文字对齐：作用于段落与标题（对齐信息不在 .md 中保存，往返有损）
        if (TipTapTextAlign)
            exts.push(TipTapTextAlign.configure({ types: ['heading', 'paragraph'] }));
```
改为：
```js
        // 文字对齐：作用于段落与标题（对齐信息不在 .md 中保存，往返有损）
        // 工具栏已移除两端对齐按钮，这里限定 alignments 防御性禁用 justify
        if (TipTapTextAlign)
            exts.push(TipTapTextAlign.configure({ types: ['heading', 'paragraph'], alignments: ['left', 'center', 'right'] }));
```

- [ ] **Step 7: 构建并启动验证**

```bash
cd build && cmake .. -DCMAKE_BUILD_TYPE=Debug && make -j$(nproc) && cd ..
./scripts/run-editor.sh
```
验证：工具栏对齐区只剩左/居中/右三个按钮，三者互斥与点击对齐正常工作。

- [ ] **Step 8: 提交**

```bash
git add src/ui/ToolBar.h src/ui/ToolBar.cpp src/resources/web/editor.js
git commit -m "feat: 移除两端对齐按钮精简工具栏"
```

---

## Task 5: 工具栏颜色按钮 + 调色板 + MainWindow 接线 + 格式回显

**Files:**
- Create: `src/resources/icons/light/text-color.svg`、`bg-color.svg`、`src/resources/icons/dark/text-color.svg`、`bg-color.svg`
- Modify: `src/resources/app.qrc:11,44` 附近、`src/ui/ToolBar.h`、`src/ui/ToolBar.cpp`、`src/ui/MainWindow.cpp:233` 附近

**Interfaces:**
- Consumes: `editorApi.applyTextColor/applyBgColor/clearTextColor/clearBgColor`（Task 2 产物）、`notifyFormat` 的 `colorSpanStyle`（Task 2 产物）
- Produces: `ToolBar` 信号 `textColorSelected(QString)` / `bgColorSelected(QString)` / `textColorCleared()` / `bgColorCleared()`；按钮交互

**UI 设计**（规格 §5）：
- 「文字色」按钮：A 字 + 下方红色色条；「背景色」按钮：半透明黄色矩形 + A 字。
- 点击弹 `QMenu`：「无颜色」项 → 清除对应属性；12 色网格（黑/灰/红/橙/黄/绿/青/蓝/紫/粉/棕/白，每色一个 `QPushButton`）；「更多颜色…」→ `QColorDialog`。
- 格式回显：缓存当前 `color`/`background-color`，构建网格时给当前色加蓝色边框高亮。

- [ ] **Step 1: 新建 4 个 SVG 图标**

`src/resources/icons/light/text-color.svg`（A 字 + 红色色条）：
```svg
<svg xmlns="http://www.w3.org/2000/svg" width="20" height="20" viewBox="0 0 20 20">
  <path d="M5 13 L10 4 L15 13 M7 10 L13 10" stroke="#2b2b2b" stroke-width="1.6" fill="none" stroke-linecap="round" stroke-linejoin="round"/>
  <rect x="4" y="15" width="12" height="2.5" rx="0.5" fill="#e23b3b"/>
</svg>
```

`src/resources/icons/light/bg-color.svg`（半透明黄底 + A 字，矩形先绘、A 字在上）：
```svg
<svg xmlns="http://www.w3.org/2000/svg" width="20" height="20" viewBox="0 0 20 20">
  <rect x="3" y="3" width="14" height="14" rx="1.5" fill="#ffd400" opacity="0.45"/>
  <path d="M6 13 L10 5 L14 13 M7.3 10 L12.7 10" stroke="#2b2b2b" stroke-width="1.5" fill="none" stroke-linecap="round" stroke-linejoin="round"/>
</svg>
```

`src/resources/icons/dark/text-color.svg`（描边改浅色）：
```svg
<svg xmlns="http://www.w3.org/2000/svg" width="20" height="20" viewBox="0 0 20 20">
  <path d="M5 13 L10 4 L15 13 M7 10 L13 10" stroke="#dcdcdc" stroke-width="1.6" fill="none" stroke-linecap="round" stroke-linejoin="round"/>
  <rect x="4" y="15" width="12" height="2.5" rx="0.5" fill="#e23b3b"/>
</svg>
```

`src/resources/icons/dark/bg-color.svg`：
```svg
<svg xmlns="http://www.w3.org/2000/svg" width="20" height="20" viewBox="0 0 20 20">
  <rect x="3" y="3" width="14" height="14" rx="1.5" fill="#ffd400" opacity="0.45"/>
  <path d="M6 13 L10 5 L14 13 M7.3 10 L12.7 10" stroke="#dcdcdc" stroke-width="1.5" fill="none" stroke-linecap="round" stroke-linejoin="round"/>
</svg>
```

- [ ] **Step 2: `app.qrc` 注册新图标**

在 `src/resources/app.qrc` 的亮色区（`highlight.svg` 行之后）追加：
```xml
        <file>icons/light/highlight.svg</file>
        <file>icons/light/text-color.svg</file>
        <file>icons/light/bg-color.svg</file>
```
在暗色区（`dark/highlight.svg` 行之后）追加：
```xml
        <file>icons/dark/highlight.svg</file>
        <file>icons/dark/text-color.svg</file>
        <file>icons/dark/bg-color.svg</file>
```

- [ ] **Step 3: `ToolBar.h` 增加声明**

新增 include（文件顶部 `#include <QComboBox>` 之后）：
```cpp
#include <QComboBox>
class QMenu;
```

在 `signals:` 区（`superscriptClicked()` 之后、`alignSelected` 之前）新增：
```cpp
    void subscriptClicked();
    void superscriptClicked();
    void textColorSelected(const QString &color);
    void bgColorSelected(const QString &color);
    void textColorCleared();
    void bgColorCleared();
    void alignSelected(const QString &alignment);
```

在 `private:` 区新增方法声明、颜色按钮成员、当前色缓存成员（`m_superscriptAction` 之后、`m_alignLeftAction` 之前）：
```cpp
    QAction *m_superscriptAction;
    QAction *m_textColorAction;
    QAction *m_bgColorAction;
    QMenu *buildColorMenu(bool forText);
    QString m_currentTextColor;   // 回显：当前光标处 colorSpan 的文字色
    QString m_currentBgColor;     // 回显：当前光标处 colorSpan 的背景色
    QAction *m_alignLeftAction;
```

- [ ] **Step 4: `ToolBar.cpp` 增加 include**

文件顶部 include 区新增：
```cpp
#include <QMenu>
#include <QWidgetAction>
#include <QGridLayout>
#include <QPushButton>
#include <QColorDialog>
#include <QRegularExpression>
```

- [ ] **Step 5: `ToolBar.cpp` 构造函数初始化新成员**

在初始化列表 `m_superscriptAction(nullptr)` 之后新增（删除 justify 后该位置紧跟对齐成员前）：
```cpp
    , m_subscriptAction(nullptr)
    , m_textColorAction(nullptr)
    , m_bgColorAction(nullptr)
    , m_alignLeftAction(nullptr)
```

- [ ] **Step 6: `ToolBar.cpp` setupUi 创建颜色按钮**

在 `m_superscriptAction` 创建块之后（Task 4 删除 justify 后的 `addSeparator()` 之前，即原 `:120` 的 `addSeparator()` 处）新增：
```cpp
    m_superscriptAction = makeAction(this, &m_iconPaths, "superscriptAction",
        "上标", ":/icons/light/superscript.svg");
    m_superscriptAction->setCheckable(true);

    // 颜色按钮：点击弹出调色板（非 checkable）
    m_textColorAction = makeAction(this, &m_iconPaths, "textColorAction",
        "文字颜色", ":/icons/light/text-color.svg");
    m_bgColorAction = makeAction(this, &m_iconPaths, "bgColorAction",
        "背景颜色", ":/icons/light/bg-color.svg");

    addSeparator();
```

- [ ] **Step 7: `ToolBar.cpp` 实现调色板菜单构建器**

在 `setupUi()` 函数定义之后、`setReadMode` 之前新增成员函数：
```cpp
// 构建颜色选择弹出菜单：无颜色 + 12 预设色网格 + 更多颜色（QColorDialog）。
// forText=true 为文字色按钮，false 为背景色按钮。
QMenu *ToolBar::buildColorMenu(bool forText)
{
    QMenu *menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);

    // 无颜色
    QAction *noneAct = menu->addAction(forText ? QStringLiteral("无文字颜色") : QStringLiteral("无背景颜色"));
    connect(noneAct, &QAction::triggered, this, [this, forText]() {
        if (forText) emit textColorCleared();
        else emit bgColorCleared();
    });

    menu->addSeparator();

    // 12 预设色网格（6 列 × 2 行）
    static const QStringList COLORS = {
        "#000000", "#7f7f7f", "#e23b3b", "#e8833a", "#d4a017", "#2e8b57",
        "#17a2b8", "#2563eb", "#7c3aed", "#db2777", "#8b4513", "#ffffff"
    };
    const QString cur = (forText ? m_currentTextColor : m_currentBgColor).toLower();

    QWidget *grid = new QWidget;
    QGridLayout *layout = new QGridLayout(grid);
    layout->setSpacing(4);
    layout->setContentsMargins(6, 6, 6, 6);
    int idx = 0;
    for (const QString &c : COLORS) {
        QPushButton *btn = new QPushButton;
        btn->setFixedSize(24, 24);
        btn->setToolTip(c);
        QString border = (!cur.isEmpty() && cur == c.toLower())
            ? QStringLiteral("2px solid #2196f3")     // 当前色高亮
            : QStringLiteral("1px solid #999");
        btn->setStyleSheet(QStringLiteral(
            "QPushButton{background:%1;border:%2;border-radius:3px;}"
            "QPushButton:hover{border:2px solid #2196f3;}").arg(c, border));
        connect(btn, &QPushButton::clicked, this, [this, forText, c, menu]() {
            menu->close();
            if (forText) emit textColorSelected(c);
            else emit bgColorSelected(c);
        });
        layout->addWidget(btn, idx / 6, idx % 6);
        ++idx;
    }
    QWidgetAction *gridAct = new QWidgetAction(menu);
    gridAct->setDefaultWidget(grid);
    menu->addAction(gridAct);

    menu->addSeparator();

    // 更多颜色（QColorDialog）
    QAction *moreAct = menu->addAction(QStringLiteral("更多颜色…"));
    connect(moreAct, &QAction::triggered, this, [this, forText]() {
        QColor chosen = QColorDialog::getColor(Qt::white, this,
            forText ? QStringLiteral("选择文字颜色") : QStringLiteral("选择背景颜色"));
        if (!chosen.isValid()) return;   // 用户取消
        if (forText) emit textColorSelected(chosen.name());
        else emit bgColorSelected(chosen.name());
    });
    return menu;
}
```

- [ ] **Step 8: `ToolBar.cpp` 连接颜色按钮 → 弹出菜单**

在 setupUi 的信号连接区（Task 4 删除 justify connect 的位置附近、对齐 connect 之后）新增：
```cpp
    connect(m_alignRightAction, &QAction::triggered, this, [this]{ emit alignSelected("right"); });
    connect(m_textColorAction, &QAction::triggered, this, [this]{
        buildColorMenu(true)->exec(QCursor::pos());
    });
    connect(m_bgColorAction, &QAction::triggered, this, [this]{
        buildColorMenu(false)->exec(QCursor::pos());
    });
```

- [ ] **Step 9: `ToolBar.cpp` updateFormatState 增加颜色回显解析**

在 `updateFormatState` 末尾（Task 4 删除 justify 后的 `}` 之前）新增：
```cpp
    // 颜色 span 回显：解析当前 colorSpanStyle 缓存 color / background-color
    if (f.contains("colorSpanStyle")) {
        QString style = f["colorSpanStyle"].toString();
        QRegularExpression colorRe("(?:^|;)\\s*color\\s*:\\s*([^;]+)",
            QRegularExpression::CaseInsensitiveOption);
        QRegularExpression bgRe("background-color\\s*:\\s*([^;]+)",
            QRegularExpression::CaseInsensitiveOption);
        auto cm = colorRe.match(style);
        auto bm = bgRe.match(style);
        m_currentTextColor = cm.hasMatch() ? cm.captured(1).trimmed() : QString();
        m_currentBgColor = bm.hasMatch() ? bm.captured(1).trimmed() : QString();
    }
```

- [ ] **Step 10: `MainWindow.cpp` 连接颜色信号 → editorApi**

在 `MainWindow.cpp:233` 的 `alignSelected` connect 之后新增：
```cpp
    connect(m_toolBar, &ToolBar::alignSelected, this, [this](const QString &alignment) {
        runEditorCommand(QString("window.editorApi.setTextAlign('%1');").arg(alignment));
    });
    // 颜色按钮：#rrggbb 无单引号，安全；防御性转义以防扩展支持命名色
    connect(m_toolBar, &ToolBar::textColorSelected, this, [this](const QString &color) {
        runEditorCommand(QString("window.editorApi.applyTextColor('%1');").arg(color.replace('\'', "\\'")));
    });
    connect(m_toolBar, &ToolBar::bgColorSelected, this, [this](const QString &color) {
        runEditorCommand(QString("window.editorApi.applyBgColor('%1');").arg(color.replace('\'', "\\'")));
    });
    connect(m_toolBar, &ToolBar::textColorCleared, this, [this]() {
        runEditorCommand("window.editorApi.clearTextColor();");
    });
    connect(m_toolBar, &ToolBar::bgColorCleared, this, [this]() {
        runEditorCommand("window.editorApi.clearBgColor();");
    });
```

- [ ] **Step 11: 构建并启动，手动验证（端到端）**

```bash
cd build && cmake .. -DCMAKE_BUILD_TYPE=Debug && make -j$(nproc) && cd ..
./scripts/run-editor.sh
```
验证清单：
1. 工具栏出现「文字色」「背景色」两按钮，图标正确（亮/暗主题切换图标跟随）。
2. 选中文字 → 点文字色 → 选红色 → 文字变红；选背景色 → 黄色 → 黄底。
3. 同一选区先设文字色再设背景色 → 两者共存（不互相覆盖）。
4. 点「无颜色」→ 对应属性清除（文字色/背景色独立）。
5. 点「更多颜色…」→ QColorDialog 选色 → 应用。
6. 光标停在带色文字上 → 重开调色板 → 当前色块蓝色高亮。
7. 保存 → 关闭 → 重开文件 → 颜色仍在（端到端往返）。
8. 带色 span 与脚注共存：`<span style="color:red">a[^1]b</span>` 打开后脚注引用仍正常（脚注 span 不被 colorSpan 吞）。

- [ ] **Step 12: 提交**

```bash
git add src/resources/icons src/resources/app.qrc src/ui/ToolBar.h src/ui/ToolBar.cpp src/ui/MainWindow.cpp
git commit -m "feat: 工具栏新增文字色/背景色调色板并接线 editorApi"
```

---

## Task 6: 版本推进 0.3.6 + CHANGELOG

**Files:**
- Modify: `CMakeLists.txt:2`、`CHANGELOG.md:5` 之前

**Interfaces:** 无（收尾）。

- [ ] **Step 1: 推进版本号**

`CMakeLists.txt:2`：
```cmake
project(md-editor VERSION 0.3.6 LANGUAGES CXX)
```

- [ ] **Step 2: 新增 CHANGELOG 条目**

在 `CHANGELOG.md` 第 4 行（`## [0.3.5]` 之前）插入：
```markdown
## [0.3.6] — 2026-08-10

### 新增

- **内联颜色样式** — 新增 TipTap `colorSpan` mark，`<span style="color:…">`（及背景色）在 WYSIWYG 中真正可渲染/编辑/往返；底层保留任意 style 字符串（UI 限色但保真），文字色与背景色可独立叠加不互相覆盖
- **工具栏颜色入口** — 「文字色」「背景色」两按钮，弹出调色板（12 预设色 + 无颜色 + 更多颜色 `QColorDialog`），光标位于带色文本时回显高亮当前色

### 变更

- **精简工具栏** — 移除「两端对齐」按钮（对齐本就往返有损、未持久化），保留左/居中/右

```

- [ ] **Step 3: 构建并跑全部测试确认无回归**

```bash
cd build && cmake .. -DCMAKE_BUILD_TYPE=Debug && make -j$(nproc) && cd ..
cd build && ctest --output-on-failure && cd ..
./build/src/md-editor --version
```
预期：所有测试 PASS；`--version` 输出 `0.3.6`。

- [ ] **Step 4: 提交**

```bash
git add CMakeLists.txt CHANGELOG.md
git commit -m "chore: 版本推进至 0.3.6"
git tag v0.3.6
```
（tag 按既有惯例打 `v0.3.6`。）

---

## 自查（Self-Review）

**规格覆盖**：
- §2 colorSpan mark（方案 B，含 parseHTML span 冲突处理）→ Task 2 Step 2。
- §3 数据流往返 → Task 3 回归测试 + Task 5 Step 11.7 端到端验证。
- §4 改动清单逐项：打包入口 Task 1；扩展/命令/notifyFormat Task 2；MarkdownHandler 预期无需改 → Task 3 验证；MainWindow 编排 + 工具栏 UI Task 5；移除 justify Task 4。
- §5 工具栏 UI（按钮/调色板/QColorDialog/回显）→ Task 5。
- §6 移除 justify（含 editor.js alignments）→ Task 4。
- §7 边界：JS 注入单引号转义（Task 5 Step 10）；QColorDialog 取消（Task 5 Step 7 isValid 守卫）；方案 B 保真（Task 2 mark 存任意 style）；空选区 setMark 行为（TipTap 设 storedMarks，可接受）。
- §8 测试：C++ QTest → Task 3；TipTap 手动 → Task 2 Step 6 + Task 5 Step 11。
- §9 版本 0.3.6 + CHANGELOG → Task 6。

**类型一致**：`editorApi` 命令名（`applyTextColor/applyBgColor/clearTextColor/clearBgColor`）在 Task 2 定义、Task 5 Step 10 引用一致；`ToolBar` 信号名（`textColorSelected/bgColorSelected/textColorCleared/bgColorCleared`）在 Task 5 Step 3 声明、Step 7/8 emit、Step 10 connect 一致；`format.colorSpanStyle` 在 Task 2 Step 4 产出、Task 5 Step 9 消费一致；`m_currentTextColor/m_currentBgColor` 在 Step 3 声明、Step 7 读取、Step 9 写入一致。

**占位符扫描**：无 TBD/TODO；所有代码步骤含实际代码；手动验证步骤含具体操作与预期。
