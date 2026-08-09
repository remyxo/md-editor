# 脚注（Footnote）功能设计

> 状态：已确认，待写实现计划
> 日期：2026-08-09
> 范围：MVP（架构正确、读写无误、上标+定义区可视化）

## 1. 目标与范围

为 md-editor 增加脚注支持。Markdown 标准规范不包含脚注，但 GitHub、GitLab、Typora、Pandoc、Notion 等主流平台均支持，是中文/学术写作的常见需求。

### MVP 目标

- **灵活标记**：脚注引用的标记为字面文本，用户可起任意中英文名（`[^1]`→`1`、`[^注1]`→`注1`、`[^附录A]`→`附录A`）。
- **读写无误**：任意标记的脚注 MD 打开→编辑→保存，`[^id]` 语法与定义内容 100% 往返不丢失。
- **WYSIWYG 可视化**：正文引用显示为上标标记，文末有可视化脚注定义区。
- **多次引用**：同一标记可在正文出现多次，共享同一条定义。

### 非目标（留第二阶段）

- 点击上标引用跳转到对应定义、定义区"返回"箭头
- hover 引用弹出气泡预览
- 脚注定义内容支持块级元素（列表/代码块等多段）

## 2. 背景与架构约束

- 编辑器为 Qt6 + TipTap/ProseMirror，QWebEngineView 内嵌 TipTap 页面。
- **MD 转换**基于 Qt 内置 `QTextDocument`（底层 md4c 解析器），**不认识** `[^id]` 脚注语法。现有不兼容语法（数学公式 `$$`、任务列表 `- [ ]`、下标 `~x~`）均通过"占位符预处理"解决，脚注沿用此模式。
- TipTap 扩展为两段式：`tiptap-entry.js`（esbuild 打包入口）→ `tiptap-all.js` → `editor.js` 的 `_buildExtensions()` 拼装。已有自定义节点先例 `buildMermaidNode`（`editor.js:320`）。
- 编辑器有 source mode（源码模式切换），脚注在源码模式下显示原始 `[^id]` 文本。
- `setContent` 每次打开文件会销毁并重建 Editor 实例，扩展必须全部在 `_buildExtensions` 内声明。

## 3. 数据模型（ProseMirror Schema）

在 `editor.js` 内用 `TipTapNode.create()` 自建三个节点，不引入第三方 npm 包。参考 `buildMermaidNode` 模式。

### 3.1 `footnoteReference` — 行内原子节点（上标标记）

| 项 | 值 |
|----|----|
| `name` | `footnoteReference` |
| `group` | `inline` |
| `inline` | `true` |
| `atom` | `true`（不可光标进入，整体选中） |
| `addAttributes` | `id: { default: '' }`（标记文本） |
| `parseHTML` | 匹配 `sup[data-type="footnoteRef"]`，读取 `data-id` |
| `renderHTML` | `<sup data-type="footnoteRef" data-id="{id}">{id}</sup>` |
| `addNodeView` | 自定义 NodeView，显示上标标记文本；双击触发改名 |

### 3.2 `footnoteContainer` — 文末块容器

| 项 | 值 |
|----|----|
| `name` | `footnoteContainer` |
| `group` | `block` |
| `content` | `footnoteDef+`（只含若干条定义） |
| `isolating` | `true`（防止外部段落合并进来） |
| `defining` | `true` |
| `parseHTML` | 匹配 `div[data-type="footnoteContainer"]` |
| `renderHTML` | `<div data-type="footnoteContainer">` |

文档末尾独一个容器节点。读入时若文档无任何脚注，则不创建该节点。

### 3.3 `footnoteDef` — 单条定义（容器子节点）

| 项 | 值 |
|----|----|
| `name` | `footnoteDef` |
| `group` | （仅属于 `footnoteContainer`） |
| `content` | `inline*`（单段内联富文本） |
| `addAttributes` | `id: { default: '' }`（与 reference 匹配） |
| `parseHTML` | 匹配 `div[data-type="footnoteDef"]`，读取 `data-id`，内容取 `.footnote-marker` 兄弟节点之后的部分 |
| `renderHTML` | `<div data-type="footnoteDef" data-id="{id}"><span class="footnote-marker">{id}</span> </div>`，content（inline）置于 marker 之后（`renderHTML` 数组用 `0` 标记 content 位置） |

**内容模型取舍**：MVP 用 `inline*`（支持加粗/斜体/链接/行内代码/行内公式等内联格式），不支持脚注内含列表、代码块等多段块级元素。覆盖 95% 场景，第二阶段升级为 `block+`。

### 3.4 关联与一致性

- reference 与 def 通过相同 `id` 关联。
- 改名 = 改 `id`：一个 command 遍历文档，把所有该 `id` 的节点（references + 对应 def）的 `id` 一起更新，保证引用与定义始终同步。

## 4. 创建与编辑交互

### 4.1 工具栏插入 `insertFootnote()`

```
1. 扫描文档中所有 footnoteReference/footnoteDef 的 id
2. 从纯数字 id 中取 max，+1 作为新标记；若无纯数字 id，则从 1 开始
3. 在光标处插入 footnoteReference（id=新标记）
4. 若文档无 footnoteContainer，在文档末尾创建一个
5. 在容器末尾追加 footnoteDef（id=新标记，内容为空）
6. 焦点留在正文光标处（不打断写作）
```

### 4.2 改名（"任意名字"的入口）

- 双击 `footnoteReference` 上标 → 弹原生 `prompt('脚注标记:', 当前id)`
- 确认后执行改名 command：遍历文档，把所有 `id === 旧值` 的 reference 与 def 的 `id` 设为新值
- 取舍：用 `prompt` 而非专门弹窗，零 UI 成本，与现有 `insertLink` 的做法一致

### 4.3 编辑定义内容

- 直接在文末定义区对应的 `footnoteDef` 内编辑（它是富文本，光标可进入、可用所有内联格式命令）

### 4.4 source mode

源码模式下显示原始 `[^id]` 引用文本与 `[^id]: 内容` 定义行（source mode 本就显示原始 MD，无需特殊处理——脚注 MD 文本在切源码时由 fromHtml 生成）。

## 5. MD↔HTML 转换（MarkdownHandler）

最关键、最易错的部分。沿用现有占位符模式，新增 `ZZFR`（引用）/`ZZFD`（定义）前缀，避开已占用的 `ZZMB/ZZTL/ZZSB/ZZCB/ZZBQ/ZZMK/ZZCD/ZZIM/ZZBM`。

### 5.1 toHtml（MD→HTML，读文件）

**预处理**（逐行扫描，在现有预处理阶段加入）：

1. 识别**定义行**：正则匹配 `^\[\^([^\]]+)\]:\s*(.*)$`
   - 抽出 `id` 与 `内容`，按 `id` 去重（同 id 多次定义取首次）
   - 整行替换为占位符 `ZZFD{n}ZZ`（定义行要从正文流移除，因为要归集到文末）
   - 备份：`<div data-type="footnoteDef" data-id="{id}">{内容}</div>`
2. 识别**引用**：正则匹配 `\[\^([^\]]+)\]`（且不在定义行、不在代码内）
   - 替换为占位符 `ZZFR{n}ZZ`
   - 备份：`<sup data-type="footnoteRef" data-id="{id}">{id}</sup>`

**转换**：`QTextDocument::setMarkdown(processed)` → `toHtml()`。

**后处理**：

3. 还原所有 `ZZFR{n}ZZ` → 对应 `<sup data-type="footnoteRef" ...>`
4. 若存在任意 `ZZFD`：在生成的 HTML **末尾**（最后一个块元素之后）插入 `<div data-type="footnoteContainer">`，内含所有 `<div data-type="footnoteDef" ...>`（按首次定义顺序）
5. 代码块 `<pre>`/行内 `<code>` 内的 `[^x]` **不转换**——纳入现有代码保护机制（脚注预处理须在代码块提取之后、或对代码内容跳过）

### 5.2 fromHtml（HTML→MD，存文件）

1. 提取所有 `<sup data-type="footnoteRef" data-id="...">` → 占位符 `ZZFR{n}ZZ`，备份 `[^{id}]`
2. 提取 `<div data-type="footnoteContainer">` → 遍历其内每个 `<div data-type="footnoteDef" data-id="...">`：
   - 把 def 内容（内联 HTML）转成 MD 片段
   - 备份 `[^{id}]: {内容md}`
   - 整个容器替换为占位符 `ZZFD{n}ZZ`
3. `QTextDocument::setHtml(processed)` → `toMarkdown()`
4. 逆序还原：`ZZFR{n}ZZ` → `[^{id}]`；`ZZFD{n}ZZ` → 在 MD 末尾追加各 `[^{id}]: {内容}` 行

### 5.3 边界与容错

| 场景 | 处理 |
|------|------|
| 同 id 多次引用 | MD 中多次 `[^id]`，读入时多 个 reference 共享一条 def（def 按 id 去重） |
| 悬空引用（有 ref 无 def） | reference 正常显示标记，无对应 def，不报错 |
| 定义内容含内联格式 | def 内容双向 HTML↔MD 转换（复用 QTextDocument 或现有内联转换逻辑） |
| 定义散落文档中间 | 读入时统一归集到文末容器 |
| `[^x]` 出现在代码内 | 不转换，原样保留 |

## 6. 视觉样式（editor.css）

```css
/* 正文脚注引用：上标、蓝色、可交互感 */
sup[data-type="footnoteRef"] {
  color: #1967d2;
  cursor: pointer;
  font-size: 0.75em;
  vertical-align: super;
  line-height: 0;
}
sup[data-type="footnoteRef"]:hover { text-decoration: underline; }

/* 文末脚注容器：分隔线 */
div[data-type="footnoteContainer"] {
  margin-top: 2em;
  padding-top: 0.75em;
  border-top: 1px solid var(--border-color, #e0e0e0);
  font-size: 0.9em;
}

/* 单条定义：标记着色 + 内容缩进 */
div[data-type="footnoteDef"] {
  margin: 0.25em 0;
  padding-left: 1.5em;
  text-indent: -1.5em;
}
/* 标记前缀（由 renderHTML 产出的只读 span） */
.footnote-marker {
  color: #1967d2;
  font-weight: 600;
  margin-right: 0.35em;
}
```

暗色主题对应调整颜色（`editor.css` 内已有暗色变量/选择器，跟随即可）。

## 7. 工具栏 / 菜单 / 图标接入

| 文件 | 改动 |
|------|------|
| `src/ui/ToolBar.h` | 加 `QAction *m_footnoteAction;` + 信号 `void footnoteClicked();` |
| `src/ui/ToolBar.cpp` `setupUi()` | `makeAction(..., "footnoteAction", "脚注", ":/icons/light/footnote.svg")`；在 findChildren 连接循环加 `else if objectName=="footnoteAction" connect(... footnoteClicked)` |
| `src/ui/MainWindow.cpp` `connectSignals()` | `connect(m_toolBar, &ToolBar::footnoteClicked, this, [this](){ runEditorCommand("window.editorApi.insertFootnote();"); });` |
| `src/ui/MenuBar.cpp` | 插入菜单加"脚注"项（可选，与工具栏并行） |
| `src/resources/icons/light/footnote.svg` + `dark/footnote.svg` | 新建图标，`stroke="currentColor"`，归入插入按钮蓝色调组 |
| `src/resources/app.qrc` | 注册两个新 svg `<file>` |
| `src/resources/themes/light.qss` + `dark.qss` | 加 `QToolButton#footnoteAction { color: #1967d2; }` |

工具栏按钮**非 checkable**（插入型动作，参考 `tableAction`/`hrAction`）。

## 8. TipTap 扩展打包

- 自建节点全部写在 `editor.js` 内（`buildFootnoteReference`/`buildFootnoteContainer`/`buildFootnoteDef`），无需改 `tiptap-entry.js`、无需新增 npm 依赖、无需重跑 `build-tiptap.sh`。
- 在 `_buildExtensions()` 末尾 `exts.push(buildFootnoteReference(), buildFootnoteContainer(), buildFootnoteDef())`。

## 9. 测试（扩展 `tests/test_markdownhandler.cpp`）

MD↔HTML 往返用例：

1. 单脚注：`正文[^1]` + `[^1]: 定义`
2. 中文标记：`[^注1]` + `[^注1]: 中文脚注`
3. 多次引用同 id：`[^1]…[^1]` + 单条定义
4. 悬空引用：`[^1]` 无定义
5. 代码块内 `[^x]` 不转换：`` `[^x]` `` 与 ```` ```\n[^x]\n``` ```` 原样保留
6. 定义含富文本：`[^1]: **加粗** 与 [链接](url)`

## 10. 已知限制

- 脚注定义内容不支持块级元素（多段/列表/代码块），仅单段内联富文本。
- 无跳转/气泡交互（第二阶段）。
- 改名通过 `prompt`，无内联编辑 UI。

## 11. 第二阶段（未来）

- 点击 reference 跳转定位到对应 def 并高亮；def 旁"返回"箭头跳回首个引用。
- hover reference 弹气泡预览定义内容。
- def 内容模型升级为 `block+`，支持多段块级元素。
- 内联编辑标记（替代 prompt）。
