# 占位符家族清单（单一来源）

- 基准提交：`d3548fb`（行号以该版本 `src/core/MarkdownHandler.cpp` 为准，6067 行；后续提交行号会漂移，家族名与语义以 grep 复核为准）
- 枚举方法：`grep -n collisionFreePlaceholder src/core/MarkdownHandler.cpp` 全集 + 逐点阅读归属
- 统计口径：**ZZ 家族 46 种**（72 个铸造调用点）、`\x01` 哨兵 3 种、HTML 注释标记 1 种（ZZLB）、toHtml 后处理瞬态 token 2 种。**活路径口径 44 家族**：ZZBQ 家族的 toHtml 半边与 ZZBQSEP 全族自 8b55a63 起铸造路径不可达（遗留机制，见 §2 注 ①）
- 契约断言：`tests/test_markdownhandler.cpp` 的 `testContract*` 系列用例（索引见文末）
- 关联文档：[br 与软换行往返机制](br-and-softbreak-roundtrip.md)（速查表收录早期 9 家族，本文为其全集超集）

## 1. 机制总览

```
铸造  collisionFreePlaceholder(collisions, "<PREFIX>", 序号)
      → 返回 "ZZ<PREFIX><n>ZZ"；O(1) 碰撞检测：源文档字面同形串已登记于
        collisions 集合时跳号（n 递增）直至无碰撞，故占位符表序号可非连续
碰撞索引  buildPlaceholderCollisionSet（toHtml :2170 / fromHtml :4221）
      → 正则 ZZ\w+ZZ 单遍收集用户文档字面 ZZ 形态文本
还原  四类机制（按家族性质选用）：
      a) batchRestorePlaceholders（:704）——剥 <p> 包装 + 裸 token 兜底回填
      b) batchReplaceExact（:735）——哈希查表精确串替换
      c) 通用 token 正则 + 集合/哈希成员判断的 globalReplaceWith 单趟重建
         （未命中集合的用户字面同形文本原样保留——往返保真关键不变式）
      d) 多趟还原循环（嵌套占位符，趟数 = 嵌套深度，HI ≤8 / emphCode 不定深）
泄漏兜底  assertNoPlaceholderLeak（:490，仅 Debug 构建）
      → toHtml 侧 17 组列表（:3892）、fromHtml 侧 4 组（:6062）
```

### 还原顺序契约（目前仅存在于源码注释，无结构性保障）

| 契约 | 依据注释 |
|------|---------|
| ZZDV 必须先于 ZZHI / ZZFR / ZZSB | :3497 附近「DV 还原 → 标题重组」次序说明 |
| ZZED 必须最后（可能内嵌于 sub/emphCode/mark 备份还原出的 HTML 文本中） | :3886-3888 |
| ZZHI 多趟还原 ≤8 趟（嵌套字面 HTML 逐层消解） | :3726-3733 |
| CB → BQ → HR → BL → BR → TL → emphCode 的块级还原链次序 | :3156-3251 步骤编号 |
| fromHtml 多行备份占位符须在 `\n{3,}` 压缩之后独占行分离 | :5696-5699 |

## 2. toHtml 侧家族（MD → HTML，toHtml :2166 起）

铸造于 toHtml 的家族共 19 种（其中 10 种与 fromHtml 同名复用，见 §4）；另有 ZZLB 为 HTML 注释标记，不走 collisionFreePlaceholder（§5）。

| 前缀 | 语义（触发形态） | 铸造点 | 备份容器 | 还原点 | 还原机制 → 产物 |
|------|----------------|--------|---------|--------|----------------|
| ZZTC | 表格空单元格（防 Qt 合并列） | :380、:389 | `cellPhs`/`cellBackups` | :3868 | token 正则+集合 → 清空为 `<td></td>` |
| ZZDV | 手写 `<div>` 块级容器（stashPlainDivs :594）；复杂引用预抽取整块（protectComplexQuoteBlocks :2157，递归 toHtml 产物） | :594、:2157 | `divBackups`/`divPhs` | :3497 | batchRestorePlaceholders → 原 `<div>`/`<blockquote>`（**须先于 ZZHI/FR/SB**） |
| ZZIA | 空 alt 图片 `![](src)` | :631 | `emptyAltSentinels` | :3140 | `alt="token"` 正则+集合 → `alt=""` |
| ZZSB | 下标 `~x~` | :905 | `subPhs`/`subContents` | :3818 | batchReplaceExact → `<sub>`（标记感知还原） |
| ZZES | 转义 `\=\=`（防 ==→mark 误转） | :930 | `escapedEqPhs`/`escapedEqBackups` | :3475 | batchReplaceExact → 字面 `==` |
| ZZED | 转义扩展定界符 `\~~` `\~` `\^` | :980 | `escapedDelimPhs`/`escapedDelimBackups` | :3889 | batchReplaceExact → 字面 `~~`/`~`/`^`（**toHtml 最后一步**） |
| ZZHI | 行内 HTML 标签 8 类：`<span style>` `<u>` `<kbd>` `<small>` `<mark>` `<sub>` `<sup>` `<br>` | :1050/:1063/:1076/:1089/:1102/:1115/:1128/:1141 | `htmlInlinePhs`/`htmlInlineBackups` | :3734-3739 | 多趟 batchReplaceExact ≤8 趟 → 原标签 |
| ZZFR | 脚注引用 `[^id]` | :1184 | `footnoteRefPhs`/`footnoteRefs` | :3741 | batchReplaceExact → `<sup data-type="footnoteRef">` |
| ZZMB | 块级公式 `$$…$$`（闭合 :2322 / EOF 兜底 :2335） | :2322、:2335 | `mathBlockPhs`/`mathBlocks` | :3821 | batchRestorePlaceholders → `<div data-type="block-math">` |
| ZZCB | 围栏代码块 ``` | :2404 | `codeBlockPhs`/`codeBlocks` | :3160 | batchRestorePlaceholders → `<pre><code>`（还原链第一步） |
| ZZBQ | 块引用内代码围栏（闭合 :2493 / EOF 兜底 :2894）⚠不可达遗留，见注 ① | :2493、:2894 | `bqCodeBlockPhs`/`bqCodeBlocks` | :3165 | batchRestorePlaceholders → `<pre margin-left:40px>`（fromHtml 侧 :5027 存活） |
| ZZBQSEP | 块引用结束边界标记（**无备份**，引用代码块后跟空行时发出）⚠不可达遗留，见注 ① | :2501 | `bqSepPhs`（存 token 自身） | :3359-3367 | `<p>token</p>` 整段移除（打破 Qt 的相邻引用合并） |
| ZZBL | 段落间连续空行（K≥2 折叠为 K−1 个标记） | :2609 | `blankLinePhs` | :3182 | `<p>token</p>` 正则+集合 → `<p></p>` |
| ZZTL | 任务列表项 `- [x]` | :2667、:2691 | `taskListPhs`/`taskLists` | :3223 | batchRestorePlaceholders → `<ul data-type="taskList">` |
| ZZBR | 段落内软换行（两行正文间单 `\n`；含未闭合 `$$` 兜底 :2858） | :2761、:2858 | `brSoftPhs`（无备份，token→固定产物） | :3210 | ` ?token ? ` 正则+集合 → `<br data-brk>` |
| ZZHR | 水平线 `---` / `<hr>`（防 setext 误解析） | :2777 | `hrPhs`/`hrMarkers`（备份固定 `<hr/>`） | :3170 | batchRestorePlaceholders → `<hr/>` |
| ZZEC | 强调包裹行内代码 `**…\`c\`…**`（protectEmphCode，prefix 参数="EC"） | :3066（字面量 :3068；经 :3053） | `emphCodePhs`/`emphCodeHtml`（三族共用） | :3230-3251 | 多趟 token 查表 → `<strong>` 含 `<code>` |
| ZZEM | `*…\`c\`…*`（prefix="EM"） | :3071（字面量 :3073；经 :3053） | 同上（共用容器） | 同上 | → `<em>` 含 `<code>` |
| ZZSC | `~~…\`c\`…~~`（protectStrikeCode） | :3106 | 同上（共用容器） | 同上 | → `<s>` 含 `<code>` |

> **注 ①（不可达遗留机制，2026-10-05 评审 I-1 实证）**：自 `8b55a63`（2026-09-13「块引用嵌套结构保真——预抽取」）起，含围栏的块引用行（`blockquoteCodeFenceLen` :111 匹配的 `> ` 围栏行必落入 `protectComplexQuoteBlocks` :2124 的 complexRe 反引号分支）在主循环之前**整段经 ZZDV 预抽取**——主循环 bq 分支铸造（:2493/:2501/:2894）与还原步骤（:3165/:3359）成为**不可达遗留机制**，金丝雀条目 `bqCodeBlockPhs`/`bqSepPhs`（:3893）为死条目。ZZBQ 家族经 fromHtml 侧 :5027 真实存活（仅 toHtml 半边空转）；ZZBQSEP 全空（六种输入形态插桩探针零触发，BQ 分支整体禁用全量测试仍绿）。清理属 src/ 改动，已作遗留登记（评审 N-3）。

## 3. fromHtml 侧家族（HTML → MD，fromHtml :4217 起）

铸造于 fromHtml 的家族共 37 种（27 种仅 fromHtml + 10 种与 toHtml 同名复用）。除注明外均铸入主线 `placeholders`/`backups`，经 :5724-5754 通用多趟还原；多行备份在 :5667-5691 独占行分离。

| 前缀 | 语义（触发形态） | 铸造点 | 备份内容 | 还原产物（md 语法） |
|------|----------------|--------|---------|-------------------|
| ZZBM | 块级公式 `<div data-type="block-math">` | :4287 | `$$…$$`（LaTeX 化） | `$$\nx\n$$` |
| ZZIM | 行内公式（inline-math span） | :4301 | `$…$` | `$x$` |
| ZZCB | `<pre>`（相邻空白分隔 pre 合并；围栏长度=内容最长反引号 run+1） | :4362 | 围栏代码块 | ```` ```lang\n…\n``` ```` |
| ZZHR | `<hr>` | :4398 | 固定 `---` | `---` |
| ZZCD | `<code>`（防 Qt 丢弃） | :4489 | `` `x` ``（+needsWrapGuardSpace 尾随空格保护，还原时消费） | `` `x` `` |
| ZZIC | mono span（`font-family` 匹配 monoSpanFamilyPattern） | :4507 | `` `x` `` | `` `x` `` |
| ZZMK | `<mark>` | :4526 | `==x==`（经 mdEquivTagsToSyntax + escapeExtDelimsOutsideTags） | `==x==` |
| ZZFR | `<sup data-type="footnoteRef">` | :4556 | `[^id]` | `[^id]` |
| ZZSB | `<sub>` | :4576 | `~x~` | `~x~` |
| ZZSP | `<sup>`（非脚注） | :4596 | `^x^` | `^x^` |
| ZZTL | `<ul data-type="taskList">` | :4632 | `- [x]` | `- [x]` |
| ZZFD | 脚注定义容器（每定义递归 fromHtml + expandPlaceholdersInPlace :4708） | :4721 | `[^id]: 定义` | `[^id]: 定义` |
| ZZNS | 删除线 ⚡ 强调 互嵌（无合法 MD 组合，字面 HTML 兜底；嵌套态 :4783 / 字面态 :4799） | :4783、:4799 | 字面 `<s>…</s>` HTML | 字面 HTML |
| ZZHE | 标题内强调（Qt 写出 `**x*` / `*x*` 不配对尾） | :4840 | `**x*` / `*x*` | `<h1><strong>x</strong></h1>` |
| ZZST（义一） | 删除线 `<s>`/`<del>`（非互嵌；字面回退经 strikeEdgeTildeCollision） | :4863、:4878 | `~~x~~` | `~~x~~` |
| ZZHS | 通用 `<span style>`（span 栈配对预计算提取） | :4927 | 原 span | `<span style="…">` |
| ZZUL | `<u>` | :4944 | `<u>x</u>` | `<u>x</u>` |
| ZZKD | `<kbd>` | :4956 | `<kbd>x</kbd>` | `<kbd>x</kbd>` |
| ZZSM | `<small>` | :4968 | `<small>x</small>` | `<small>x</small>` |
| ZZBQ | `<blockquote>`（递归 :5027 / 扁平 margin p :5053；递归经 expandPlaceholdersInPlace :5012） | :5027、:5053 | `> ` 前缀化 md | `> 引用` |
| ZZDV | 手写 `<div>` 块（跳过 taskItem 结构 div——`</label>` 判定） | :5074 | 原 `<div>` | `<div>` |
| ZZTC | 表格（对齐提取 + 空格注入 + 合并行记录区间内整体占位） | :5153 | 备份为空串（表格由 :5548 起规范化直接产出） | `\| … \|` 表格行 |
| ZZA | 段落/标题 text-align | :5231 | `<div align="…">` | `<div align="…">` |
| ZZBL | 空段落 | :5289 | `blankLinePhs`（副线容器） | `&nbsp;` 独占行（:5700 还原；列表/表格容器内跳过） |
| ZZNB | 连续 `&nbsp;` | :5312 | 原 nbsp 串 | ` …` |
| ZZBR | 手写 `<br>`（不带 data-brk） | :5326 | 原 `<br>` 标签串 | 字面 `<br>` |
| ZZBK | `<br data-brk>` 软换行（`brSoftPlaceholders` 副线容器） | :5342 | （无备份，上下文决定产物） | 按行上下文还原 `\n` / 列表内 `\n+4sp` / 引用内 `\n+2sp`（:5764-5807） |
| ZZGL | 通用列表（自绘缩进） | :5387 | 列表 md（含缩进） | `- 甲` / 缩进子列表 |
| ZZDS | 字面 `**`（尾部孤立形态走裸 `**` 备份） | :5462 | `\*\*` 或 `**` | 字面 `**` |
| ZZST（义二） | 孤立字面 `*`（字面符号状态机 D，防 em 误配） | :5479 | `\*` | `\*` |
| ZZEQ | 字面 `==` | :5485 | `\=\=` | `==` |
| ZZTT | 字面 `~~` | :5492 | `\~~` | `~~` |
| ZZTD | 字面 `~` | :5500 | `\~` | `~` |
| ZZCR | 字面 `^` | :5505 | `\^` | `^` |
| ZZLT | 字面 `<` | :5512 | `\<` | `<` |
| ZZGT | 字面 `>` | :5520 | `\>` | `>` |
| ZZLM | 段落行首列表标记（`1.` `-` `+` 等，防误判为列表） | :5615 | 转义标记 `1\.` `\-` | 原段落文本 |
| ZZIB | 空 alt 图片（injectEmptyAltSentinel :672 铸入 `emptyAltSentinels`） | :672 | （alt 位 token） | `![token](src)` → `![](src)`（:6050） |

## 4. 双向同名复用家族（各自独立占位符表，不共享状态）

`CB`、`HR`、`BQ`、`BL`、`TL`、`BR`、`TC`、`DV`、`SB`、`FR` 共 10 家族在 toHtml 与 fromHtml 各自铸造、各自还原（同名不同表；`BR`/`BK`、`IA`/`IB` 则是方向专用的姊妹前缀）。同向内不会混淆，跨向阅读代码时注意语义差异（如 `ZZBR` toHtml=软换行占位、fromHtml=手写 `<br>` 字面备份）。

## 5. 非 ZZ 家族的标记与瞬态 token

| 标记 | 形态 | 位置 | 说明 |
|------|------|------|------|
| ZZLB | `<!-- ZZLB<n>ZZ -->`（HTML 注释） | 铸造 :2630 | 列表项间空行强制拆分列表；**Qt setMarkdown 丢弃注释即「还原」**，无还原代码、无备份——泄漏金丝雀列表亦不含它。不走 collisionFreePlaceholder（注释形态天然低碰撞，计数另列） |
| \x01CODE | `\x01CODE<n>\x01` | 铸造 :888/:960/:1029/:1170/:2923 等 | 行内代码暂存哨兵（下标/转义定界符/HTML 内联/脚注引用/div 保护各路径共用一表）；强调包裹机制跨函数传递消费 |
| \x01EUT | `\x01EUT<n>\x01` | 铸造 :803 | escapeUnclosedHtmlTags 内部：code span 先暂存防误转义；函数内还原 |
| \x01EUI | `\x01EUI<n>\x01` | 铸造 :623 | protectEmptyAltImages 内部：先暂存 code span 再匹配空 alt；函数内还原 |
| \_\_CODE\<n\>\_\_ | toHtml 后处理瞬态 | :3388 铸 / :3476-3483 还原 | mono→code 步骤的函数内短命 token，不跨函数、不入金丝雀 |
| \_\_IMATH\<n\>\_\_ | toHtml 后处理瞬态 | :3404 铸 / :3411-3418 还原 | 行内公式 span 步骤同上 |

**\x01 贪婪陷阱**：C++ 十六进制转义贪婪，`"\x01EUT"` 实为 `0x1E+"UT"`——哨兵串必须写成 `"\x01" "EUT"` 分离形式（:620、:800 已有注释）。

## 6. 与 review-conversion-layer.md 的 41 计数对账

专项审查（2026-09-27，`~/Desktop/md-editor优化专项-0927/review-conversion-layer.md` §①）记载「家族共 41 种（grep `collisionFreePlaceholder(collisions,` 统计）」。本清单实测 **46 种**，差异 +5 全部归因于**统计方法**，非家族集漂移：

1. **基线一致性已验证**：以审查所对照的提交 `0ce74d2`（2026-09-14，5725 行——与审查报告记载行数一致）导出比对，44 个字面前缀集合与当前 HEAD **逐名相同、零增零删**（其后 7 个提交均为性能等价重构/修复，未新增家族）。差异纯由 grep 口径产生。
2. **多行调用漏计 +4**：审查的单行 grep 看不到换行书写的前缀实参——`ZZBQSEP`（:2501）、`ZZHE`（:4840）、`ZZIA`（:631）、`ZZMB`（:2322）四处调用前缀在下一行。复现审查口径（单行 grep distinct）恰得 40 个字面前缀，与 44 之差正是这 4 个。
3. **`prefix` 变量调用按 1 计 +1**：ZZEC/ZZEM 两家族经 protectEmphCode 的 `prefix` 参数铸造（:3053 一处表达式，:3066/:3071 两个调用点传 "EC"/"EM"，字面量在 :3068/:3073）。grep 该行得到的是变量名 `prefix` 而非两个字面量，40+1 恰为审查的 41。
4. **旁证**：审查 §① 行内的家族名清单（40 个名字）含 **`BV`**——代码中不存在该前缀，疑为 BK/BM 笔误；且恰好缺席上述 4 个多行家族。其「toHtml 侧 24 种」的说法同为该口径产物（实测 toHtml 铸造 19 种 + ZZLB）。

**结论**：以 46 为准；审查的 41 = 46 − 4（多行漏计）− 1（EC/EM 合并计数）。两份记录的差异已闭环，无实现层分歧。

## 7. "ST" 一名两义（F-8/R7 记录，本任务不改代码）

fromHtml 中 `ST` 前缀承担两种语义，且同处一个碰撞域：

- **义一（删除线备份）**：:4863、:4878，备份 `~~x~~`；
- **义二（孤立字面 `*` 保护）**：:5479，备份 `\*`（字面符号状态机 D）。

任务简报中「ST 系 StartFragment 与 setext 两义」的猜测**与代码现状不符**：全文无 StartFragment/setext 相关的 ST 前缀（`---` 的 setext 误解析防护走的是 ZZHR 占位，见 :2766-2774 注释）。审查报告 F-8 的判定（strike vs 孤立星号）与代码一致，本清单按此记录。

审查建议 R7（义二改名 `ST`→`SA`）**本轮不实施**：家族名参与生成串与泄漏断言正则（`ZZ[A-Z]+[0-9]+ZZ` 不受影响），但改名属 src/ 改动，与本任务「纯加法、653 指纹不动」的约束冲突；如实施，须同步更新本清单与 F-8 引用。当前以「义一/义二」标注区分。

## 8. 契约断言索引（tests/test_markdownhandler.cpp `testContract*` 系列）

| 测试函数 | 覆盖家族 | 断言形式 |
|---------|---------|---------|
| testContractToHtmlZZCB_BQ | ZZCB、ZZBQ（fromHtml 半边） | 围栏 → `<pre>`；引用内围栏 → `<blockquote>` 内 `<pre>`；简单引用 → `> ` 前缀还原。**ZZBQ(toHtml) 断言经 DV 预抽取路径成立，不锁定 BQ 家族链**（§2 注 ①；作为行为回归保留） |
| testContractToHtmlZZBQSEP | ZZBQSEP（行为回归） | 边界标记语义：`后续` 段落在 `</blockquote>` 之外 + token 不泄漏。**断言经 DV 预抽取路径成立，不锁定 BQSEP 家族链**（§2 注 ①；作为行为回归保留） |
| testContractToHtmlZZHR_LB | ZZHR（双向）、ZZBL(toHtml)、ZZLB | 标记类聚合：`<hr/>` / `---` / `<p></p>` / 两个 `<ul>` 拆分 |
| testContractToHtmlZZBR_BK | ZZBR(toHtml)、ZZBK(fromHtml) | `<br data-brk` / 列表项软换行结构存活（收敛容忍） |
| testContractToHtmlZZTC | ZZTC（双向） | 空单元格 `<td></td>`（数据行）+ 表格管道存活 |
| testContractToHtmlZZDV_A | ZZDV（双向）、ZZA | div 不被 `<p>` 包裹 + 往返保真；复杂引用递归存活；`text-align` 段落 → `<div align="center">`（ZA 真断言，mdconv 实测形态） |
| testContractToHtmlZZIA_IB | ZZIA、ZZIB、\x01EUI | `alt=""` 保形 + `![](src)` 往返 + 无 \x01 |
| testContractToHtmlZZSB_SP | ZZSB（双向）、ZZSP | `<sub>2</sub>`（负向：无 `~2~`）/ `~2~` / `^甲^` |
| testContractToHtmlZZES_ED | ZZES、ZZED | 转义保护型：字面 `==`/`~~`/`~`/`^` 存活 + 负向（不得转 mark/s/sub/sup） |
| testContractToHtmlZZHI | ZZHI + \x01CODE | 8 类行内标签逐一循环（存活+零泄漏+无哨兵）+ code 共存 |
| testContractToHtmlZZFR_FD | ZZFR（双向）、ZZFD | `footnoteRef`/`footnoteDef` + `[^9]` 往返 + `[^7]: 嵌套定义` |
| testContractToHtmlZZMB_BM_IM | ZZMB、ZZBM、ZZIM | block-math div + `$$` 往返 + `$a+b$` |
| testContractToHtmlZZEC_EM_SC | ZZEC、ZZEM、ZZSC | 三族聚合：strong/em/s 各含 `<code>`（多趟还原） |
| testContractToHtmlZZTL | ZZTL（双向） | `<ul data-type="taskList">` + `- [ ]`/`- [x]` 勾选态 |
| testContractFromHtmlZZMK | ZZMK | `==高亮==` md 语法还原 |
| testContractFromHtmlZZCD_IC | ZZCD、ZZIC | 行内代码 / mono span → 反引号（\_\_CODE\_\_ 瞬态覆盖说明；fromHtml CB 铸造仅 :4362 一处，本用例经该路径） |
| testContractFromHtmlZZST_NS | ZZST 义一、ZZNS | `~~删除线~~` / 互嵌内容存活+零泄漏（收敛容忍） |
| testContractFromHtmlZZHE | ZZHE | 标题内强调 md 形态 `#` + `**粗体标题**` 存活 |
| testContractFromHtmlZZUL_KD_SM_HS | ZZUL、ZZKD、ZZSM、ZZHS | 四类 span 标签循环：标签存活 |
| testContractFromHtmlZZGL_NB | ZZGL、ZZNB | 嵌套列表缩进 / 字面 `&nbsp;` 串存活 |
| testContractFromHtmlZZBL | ZZBL（双向） | `<p></p>` → `&nbsp;` 行闭环 |
| testContractFromHtmlZZBR | ZZBR(fromHtml) | 字面 `<br>` 存活 + 负向：无 data-brk |
| testContractFromHtmlLiteralStateMachine | ZZDS、ZZST 义二、ZZEQ、ZZTT、ZZTD、ZZCR、ZZLT、ZZGT、ZZLM | 状态机 D 九家族聚合循环：剥离反斜杠后字面符号存活（兼容裸/转义归一方向）+ 零泄漏 + ZZDS 裸形态精确断言 |
| testContractSentinels | \x01CODE、\x01EUT（\x01EUI 见 ZZIA_IB） | 转义/暂存产物正确 + 输出无 `\x01` + 零泄漏 |

断言设计原则（按家族性质分形，不强行统一模板）：

- **内容备份型**（大多数）：最小触发输入 → 断言还原产物（该家族独有的 md/HTML 语法）存在 + `ZZ<NAME>` token 不泄漏；
- **边界标记型**（ZZBQSEP/ZZLB/ZZBL/ZZBR）：断言的是**结构语义**（引用边界/列表拆分/空行数/软换行），而非备份内容——无内容可断言；
- **转义保护型**（ZZES/ZZED/状态机 D 九族）：负向断言为主（不得误转 `<mark>`/`<em>`）+ 字面符号存活；
- **收敛容忍型**（ZZNS 等走 Converge 级的家族）：断言泄漏零 + 语义双方存活，不断言字节级形态（与 test_roundtrip_matrix 的等级设定一致）；
- **哨兵型**（\x01 三族）：断言函数产物 + 全输出无 `\x01` 控制字符。
