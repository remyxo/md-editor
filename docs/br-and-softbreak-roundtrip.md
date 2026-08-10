# `<br>` 与软换行往返机制

## 设计原则：HTML → MD 转换规则

**有 Markdown 等价语法的，转为 MD 语法；没有的，保留字面 HTML 标签。**

| HTML 标签 | MD 语法 | 分类 | 处理方式 |
|-----------|---------|------|----------|
| `<sub>` | `~x~`（Pandoc） | 有 MD 语法 | fromHtml 转为 `~x~` |
| `<sup>` | `^x^`（Pandoc） | 有 MD 语法 | fromHtml 转为 `^x^` |
| `<mark>` | `==x==`（GFM） | 有 MD 语法 | fromHtml 转为 `==x==` |
| `<hr>` | `---` / `***` | 有 MD 语法 | fromHtml 转为 `---` |
| `<br>` | **无** — `\n` 只是字符不是语法 | **无 MD 语法** | fromHtml 保留字面 `<br>` |
| `<kbd>` | 无 | 无 MD 语法 | 保留字面 `<kbd>` |
| `<small>` | 无 | 无 MD 语法 | 保留字面 `<small>` |
| `<span style>` | 无 | 无 MD 语法 | 保留字面 `<span style>` |
| `<div>` | 无 | 无 MD 语法 | 保留字面 `<div>` |

> **核心判断**：`\n` 是一个字符（ASCII 0x0A），不是 Markdown 语法。将 `<br>` 替换为 `\n` 意味着丢失了"这是一个硬换行标签"的语义信息——这在 Markdown 中无法表达，因此必须保留为字面 HTML 标签。

## 问题域

Markdown 编辑器同时存在两种换行来源，且均输出为 HTML `<br>`：

| 来源 | MD 表示 | 触发路径 | 在 fromHtml 中 |
|------|---------|----------|---------------|
| 手写 `<br>` 标签 | 字面 `<br>`（4 字符） | toHtml ZZHI 占位符 | **必须保留为字面 `<br>`** |
| 行间单 `\n`（软换行） | `\n` 字符 | toHtml BRMARKER 机制 | **必须还原为 `\n`** |

两者在 toHtml 后均以 `<br>` 形态进入编辑器 HTML，fromHtml 无法区分来源，导致：
- 若全部保留为 `<br>`：普通换行也被转成 `<br>` 标签，文件格式被破坏
- 若全部转为 `\n`：手写 `<br>` 在源码模式中消失

## 方案：`data-brk` 属性标记 + 双 TipTap 节点

### 架构分层

```
toHtml (MD → HTML)
  ├─ 手写 <br>  →  extractHtmlInlineElements → ZZHI → <br>         (无标记)
  └─ 行间 \n    →  BRMARKER 预处理 → <br data-brk>               (有标记)

编辑器 (TipTap/ProseMirror)
  ├─ <br>         →  buildCustomHardBreak (hardBreak 节点)          parseHTML: 'br:not([data-brk])'
  └─ <br data-brk> →  buildBrkExtension   (brk 节点)               parseHTML: 'br[data-brk]', renderHTML 保留属性

fromHtml (HTML → MD)
  ├─ <br>         →  ZZBR 占位符 → 恢复为字面 "<br>"               (用户标签保真)
  └─ <br data-brk> →  不匹配 ZZBR → setHtml → toMarkdown → \n     (换行符还原)
```

### 关键代码位置

| 文件 | 位置 | 作用 |
|------|------|------|
| `MarkdownHandler.cpp:isBrOnlyLine()` | 行 6 | 检测 `<br>` 独占行，用于 BRMARKER 抑制 |
| `MarkdownHandler.cpp:extractHtmlInlineElements()` | 行 161 | toHtml：`<br>` → ZZHI（不含 data-brk 的原始标签） |
| `MarkdownHandler.cpp:isParagraphLine()` | 行 30 | `<br>` 独占行仍是正文行（允许其后的 `\n` 触发 BRMARKER） |
| `MarkdownHandler.cpp` BRMARKER 条件 | 行 583 | `!isBrOnlyLine(next)` — 抑制 `<br>` 前的 BRMARKER |
| `MarkdownHandler.cpp` BRMARKER 替换 | 行 698 | `MARKER → <br data-brk>`（标记 BRMARKER 来源） |
| `MarkdownHandler.cpp` fromHtml ZZBR | 行 1326 | 正则 `^(?!.*data-brk)` 排除标记换行 |
| `editor.js:buildCustomHardBreak()` | - | 替代 StarterKit 内置 HardBreak，仅匹配普通 `<br>` |
| `editor.js:buildBrkExtension()` | - | 匹配并保留 `<br data-brk>`，BRMARKER 标记往返 |

### `<br>` 独占行的 BRMARKER 抑制

当 `<br>` 独占一行时（前后均为正文），抑制前一行的 BRMARKER 但允许 `<br>` 自己触发 BRMARKER：

```
输入:
  这是新行。        ← BRMARKER ✓ (next 是正文)
  这是新行。        ← BRMARKER ✗ (next 是 <br> 独占行，isBrOnlyLine 抑制)
  <br>             ← BRMARKER ✓ (next 是正文)
  这是新行。        ← BRMARKER ✓
  这是新行。        ← else 分支
```

结果：`text1<br data-brk>text2<br><br data-brk>text3<br data-brk>text4`

手写 `<br>` 两侧恰好各一个 BRMARKER `<br>`，fromHtml 后形成 1 个空行 + 字面 `<br>` 标签。

## 相关占位符速查

| 占位符 | 方向 | 含义 |
|--------|------|------|
| `ZZHI` | toHtml | HTML 内联元素（`<span style>` `<u>` `<kbd>` `<small>` `<mark>` `<sub>` `<sup>` `<br>`） |
| `ZZBR` | fromHtml | 手写 `<br>` 标签（不含 `data-brk`，还原为字面 `<br>`） |
| `ZZA` | fromHtml | 段落/标题 text-align → `<div align="...">` |
| `ZZDV` | toHtml + fromHtml | 手写 `<div>` 块级容器 |
| `ZZSB` / `ZZSP` | toHtml + fromHtml | 下标 `~x~` / 上标 `^x^` → `<sub>` / `<sup>` |
| `ZZFR` | both | 脚注引用 `[^id]` → `<sup data-type="footnoteRef">` |
| `ZZKD` / `ZZSM` | fromHtml | `<kbd>` / `<small>` 标签保真 |
| `ZZMK` | fromHtml | `<mark>` → `==text==` |
| `BRMARKER` | toHtml | 行间 `\n` 软换行临时标记 → `<br data-brk>` |

## 修改约束

修改以下任一环节时，**必须保证三层一致性**：

1. **toHtml 标记层**：BRMARKER 生成的 `<br>` 必须带有 `data-brk` 属性，手写 `<br>` 不得带
2. **TipTap 解析层**：两个自定义节点必须同时注册，`parseHTML` 选择器互斥（`br:not([data-brk])` vs `br[data-brk]`）
3. **fromHtml 还原层**：ZZBR 正则必须排除 `data-brk`，带标记的 `<br>` 穿过 setHtml/toMarkdown 自然转为 `\n`
