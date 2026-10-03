# 代码块围栏往返机制（嵌套围栏）

嵌套围栏（外层 ````` ```` ````` 内含 `` ``` ``）在 MD→HTML→MD 往返中的四个坑与修复记录。

## 坑 1：toHtml 的朴素围栏翻转（解析层）

**症状**：` ```markdown ` 源码块内嵌 ```` ```mermaid ````，内层围栏被误判为闭合，后续正文被吞入代码块。

**根因**：旧实现用 `inCodeBlock` 布尔翻转——任何 `` ``` `` 开头的行都切换状态，无视围栏长度。

**修复**（CommonMark 围栏规则）：跟踪**开启围栏的反引号数** `codeFenceLen`，仅当本行为纯反引号且长度 ≥ 开启长度时才闭合。同时整块收集为 `ZZCB<n>ZZ` 占位符，**内容不经过 `setMarkdown`**——Qt 的围栏解析同样朴素，嵌套内容直传会被二次错乱。

## 坑 2：fromHtml 围栏固定 3 反引号（序列化层）

**症状**：````` ````markdown `````（4 反引号）块切源码模式后变成 3 反引号，与内层 `` ``` `` 撞长，再切回 WYSIWYG 错乱。

**根因**：HTML 中只有 `language-markdown` class，**围栏长度信息在 MD→HTML 时天然丢失**；fromHtml 序列化时写死 3 反引号。

**修复**：序列化前扫描代码内容中**最长连续反引号串**，围栏取 `max(3, 最长 + 1)`。内容无反引号时 3 根即正确（与用户手写 4 根语义等价）；内容含 `` ``` `` 时自动加长保证闭合。

## 坑 3：相邻块级占位符被合并进同一段落（还原层）

**症状**：源码块紧邻块级公式（TipTap getHTML 中 `<pre>` 与 `<div data-type="block-math">` 相邻），切源码模式后闭合 `` ``` `` 与 `$$` 粘连成 `` ```$$ ``，围栏「看似丢失」。

**根因**：`QTextDocument::setHtml` 把相邻块级元素占位符之间的 `\n` 当**软换行**，两个占位符进入**同一个段落**；`toMarkdown` 输出为同一行、空格分隔（`ZZCB0ZZ ZZBM0ZZ`）。还原多行备份后首尾粘连。

**修复**：占位符还原**之前**，凡备份含 `\n` 的占位符强制独占一行——正则检测前后是否有同行内容，有则插入空行。

## 坑 4：列表内容缩进围栏的读写不对称（解析层，2026-10-03）

**症状**：列表项内代码块经切源码/保存后，围栏被 fromHtml 归位到「列表内容缩进」（如三级项内容列 8sp、一级有序项内容列 3sp）。再次解析（源码模式导出走同一 `toHtml`）时，≥4sp 的开栏不被认领，其 col-0 闭合围栏被误判为**新块开栏**，吞掉区间内整节尾部——四级~六级嵌套列表、后续一级项、下一个代码块全部成为一个无语言 `<pre>` 的字面内容（源码模式导出 HTML/PDF/DOCX 里整段渲染为等宽字面代码；取证见 w2-alpha 报告与 `scripts/probe-srcmode-md.sh`）。

**根因**：读写两侧的围栏缩进域不相交——

- **写侧（fromHtml / QtMarkdownWriter + 列表序列化）**：列表项内 codeBlock 按**列表内容缩进**输出围栏（`        ```cpp`），内容行与闭合围栏为 col-0 懒形态（同块引用代码块的 lazy 形态）。合法 CommonMark（缩进 ≤ 列表内容缩进 + 3 的围栏属于列表项子块）。
- **读侧（toHtml 扫描器 `isTopLevelFenceLine`）**：只认 **≤3sp 缩进**的围栏行（CommonMark **顶层**围栏规则），≥4sp 一律拒绝——fromHtml 自己写得出的形态自己读不回。

**修复**（方向 A，扫描器认领域扩展，ZZBQ 前例向列表上下文的延伸）：

1. **任意缩进围栏行判定** `isFenceLineAnyIndent`（`isTopLevelFenceLine` 改为其 ≤3 特例）；
2. **列表内容缩进基线** `listCtxIndent`：扫描循环维护「最近列表项的内容列」（`listItemContentColumn`：无序标记宽 2、有序 = 数字位 + 2，与 fromHtml 序列化口径一致）。列表项行刷新；空白行保持（松散列表分隔）；缩进 ≥ 基线的续行保持；顶层/更浅缩进的非列表行重置 -1；
3. **认领门**：开栏缩进 ≥4 时仅当 `listCtxIndent ≥ 0` 且 `开栏缩进 ≤ 基线 + 3`（列表项子块的合法围栏列）才认领——顶层 ≥4sp 缩进是**缩进代码块**（CommonMark ex.134），不认领（负例测试守护）；
4. **配对规则**（CommonMark 缩进围栏语义）：闭合围栏须同字符、长度 ≥ 开栏、纯围栏行（info string 仅开栏可有）、**缩进 ≤ 开栏缩进**（覆盖 fromHtml 的 col-0 懒闭合与开栏同缩进两种闭合产出路径）；开栏缩进随块记录（`codeFenceOpenIndent`）；
5. **占位符发出**：与顶层围栏同款**顶格**发出（ZZCB 既有铸造/还原机制零改动，仅扩认领范围）。

**设计偏离记录**（w2-alpha 方案 vs 实测）：方案原定「占位符带列表内容缩进前缀发出（ZZBQ 同款）」——实测 md4c 对 ≥4sp 文本行**不做**列表项懒延续：缩进前缀形态（无论是否空行隔离）产出的 `<pre>` 落位（`<ol>` 直接子块 / 行内粘连进 `<li>`）fromHtml 均无法正确序列化（往返泄漏原始 `<ul style=…>` 文本）。顶格发出使占位符成为列表项段落的懒延续，`<pre>` 粘连进 `<li>`，TipTap 解析时提升为项内 codeBlock 块、fromHtml 按列表内容缩进输出围栏——即回到 fromHtml 自产的合法形态，读写对称（`toHtml(gen1)` 与 `toHtml(顶格原稿)` 逐字节一致，已入测试锚）。修复后源码模式两跳往返收敛（selftest roundtrip 真实链路 converged_at=1）。

**配对组合穷举表**（进测试 `testIndentedFencePairingMatrix`）：

| 维度 | 取值 |
|------|------|
| 开栏缩进 | 4 / 6 / 8sp（= 列表内容列：L2 无序 4、两位数有序 4、L3 有序 8） |
| 闭栏缩进 | 0（懒形态）/ 开栏同缩进 / 中间档（C ≤ I 均闭合） |
| 围栏内容行 | col-0 / 与开栏同缩进 / 列表标记形（`- 看似列表`）/ 更深缩进 |
| info string | 仅开栏可有；带尾文本的围栏行是内容不是闭合 |
| 负例（不认领） | 顶层 4sp（ex.134 缩进代码块）/ 无列表上下文深缩进 / 列表已被顶层段落终结后的深缩进 |

**653 指纹影响**（conv-byte-snapshot 铁闸）：commonmark 653 例中仅 **ex.263**（`1.  foo` 松散列表内 4sp 围栏 + 内容列内容）digest 变化——该例从 md4c 窄口径自然解析改为与顶格围栏同款的扫描器认领路径；其行为与「同一文档的 col-0 围栏孪生形态」完全一致（既有行为），两趟收敛不变。发散数 66 持平，gfm 棘轮 1 持平。

## 块引用内代码围栏（`> ```）

块引用内代码块（`> ``` `）是另一组往返坑，涉及 BRMARKER、块引用重构与空行边界三处叠加。

### 坑 A：BRMARKER 破坏围栏（解析层）

**症状**：`> 前\n> ```\n> int x;\n> ```\n> 后` 中，`> ``` ` 被 `isParagraphLine` 判为正文行，软换行标记注入围栏内，`setMarkdown` 把 `<pre>` 拆成多段、代码内容与语言全部丢失。

**修复**：新增 `blockquoteCodeFenceLen()`（识别单层 `> ``` ` / `> ~~~` 并返回围栏长度），`isParagraphLine` 对其返回非正文；行循环里把块引用代码块整体收集（`stripBlockquotePrefix()` 剥离 `>` 前缀），用 `ZZBQ<n>ZZ` 占位符保护并**保留语言信息串**（`setMarkdown` 不保留代码语言，须像顶层代码块一样手动收集）。

### 坑 B：`<p` 误匹配 `<pre`（块引用重构层）

**症状**：块引用后处理正则 `R"(<p(...margin-left:40px...)...)"` 中 `<p` 是 `<pre` 的前缀，把 `<pre>…</pre>` 与其后的 `<p>…</p>` 吞成一个块，代码块被拆碎。

**修复**：正则改为 `<(p|pre)\b`（`\b` 阻止 `<p` 匹配 `<pre` 开头）。块引用重构改为**状态机**——把连续的 `margin-left:40px` 块分组，**含 `<pre>` 的整段合并为一个 `<blockquote>`**，纯 `<p>` 序列每个独立成块（保持 `> a\n\n> b` 两个独立引用的往返）。

### 坑 C：空行边界被 `setMarkdown` 折叠（边界层）

**症状**：`> 前\n> ```\n> code\n> ```\n\n> 后`（代码块后空行再接块引用）与「同一块引用内多段」产出**完全相同**的扁平 `margin-left:40px` 块，空行信息丢失，后处理把 `> 后` 误并入前一块引用。

**修复**：块引用代码块闭合时若下一行是空行，发出 `ZZBQSEP` 边界标记（顶层段落形态，透传穿过 `setMarkdown`）；它在块引用重构时打破「连续 margin-left 块」合并，随后在后处理移除（相邻 `<blockquote>` 由 `toMarkdown` 自动补空行）。

### 往返形态说明

块引用代码块往返后，代码内容行规范化为 **lazy 形式**（不带 `>` 前缀）：

```
> 前
> ```
int x;
```
> 后
```

这是 Qt `toMarkdown` 的固有输出，语义等价（CommonMark 中围栏内代码内容无需 `>` 前缀）。

## 通用范式

```
MD ──toHtml──► HTML ──TipTap parseHTML──► PM doc ──getHTML──► HTML' ──fromHtml──► MD'
```

1. **解析与序列化都要按 CommonMark 规则处理围栏长度**，不能假设固定 3 反引号。
2. **HTML 是有损中间表示**（不记录围栏长度、info string 之外的细节），序列化时需从**内容推断**所需围栏长度。
3. **占位符穿过 QTextDocument 时，段落结构会被重排**——多行备份的占位符还原前必须保证独占一行。
4. **C++ 单测通过 ≠ 端到端正确**：TipTap 真实 `getHTML()` 的输出形态（属性顺序、块间空白）与合成 HTML 不同，验证往返必须走真实 app 链路。

## 相关测试

`tests/test_markdownhandler.cpp`：`testNestedFenceCodeBlock`、`testNestedFenceRoundTrip`、`testCodeBlockAdjacentBlockMathRoundTrip`、`testAdjacentCodeBlocksNotMerged`、`testBlockquoteCodeFenceRoundTrip`、`testIndentedListFenceClaimed`、`testIndentedFencePairingMatrix`、`testIndentedFenceOutsideListNotClaimed`、`testSourceModeIndentedFenceNoSectionSwallow`（坑 4 用例族）。
