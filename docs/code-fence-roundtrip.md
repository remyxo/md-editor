# 代码块围栏往返机制（嵌套围栏）

嵌套围栏（外层 ````` ```` ````` 内含 `` ``` ``）在 MD→HTML→MD 往返中的三个坑与修复记录。

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

## 通用范式

```
MD ──toHtml──► HTML ──TipTap parseHTML──► PM doc ──getHTML──► HTML' ──fromHtml──► MD'
```

1. **解析与序列化都要按 CommonMark 规则处理围栏长度**，不能假设固定 3 反引号。
2. **HTML 是有损中间表示**（不记录围栏长度、info string 之外的细节），序列化时需从**内容推断**所需围栏长度。
3. **占位符穿过 QTextDocument 时，段落结构会被重排**——多行备份的占位符还原前必须保证独占一行。
4. **C++ 单测通过 ≠ 端到端正确**：TipTap 真实 `getHTML()` 的输出形态（属性顺序、块间空白）与合成 HTML 不同，验证往返必须走真实 app 链路。

## 相关测试

`tests/test_markdownhandler.cpp`：`testNestedFenceCodeBlock`、`testNestedFenceRoundTrip`、`testCodeBlockAdjacentBlockMathRoundTrip`、`testAdjacentCodeBlocksNotMerged`。
