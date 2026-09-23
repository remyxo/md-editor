# Changelog

本项目遵循 [Keep a Changelog](https://keepachangelog.com/zh-CN/1.0.0/) 规范，版本号遵循 [语义化版本](https://semver.org/lang/zh-CN/)。

## [0.19.1] — 2026-09-23

表格列宽一致性批次：三处「布局被首算时刻的状态固化」类根因 + 分配器过约束
区间的均分缺陷 + PDF 两分页路径甘特图渲染宽分叉。核心方法论：**列宽分布是
（内容测量值, 可用宽）的函数**——任何改变二者之一的事件（窗口缩放、设置
变更、路径切换）若不触发重算，布局就停留在过去。

### 修复 — 表格列宽不随窗口宽度重算（同内容不同窗口布局不同）
- 症状：两个仅差两表格行的文档在 WYSIWYG 中列宽布局不同（用户实测对比）；
  窗口缩放/侧栏开合后列宽不更新
- 根因（CDP 受控复现 + Xlib 真实缩窗取证）：百分比虽是相对值，但**分布本身
  随可用宽变化**（窄窗偏保底、宽窗偏多列极值——同内容备注列占比 23%→68% 单调
  随宽），且 `_layoutTables` 仅 init/setContent/内容编辑触发，无 resize 监听
- 修复：`_attachTableLayoutObserver`（ResizeObserver 监听编辑区宽度，变化
  ≥1px 经 300ms 防抖重算；setContent 重建 #editor 后重挂，同 `_lineGutterRO` 惯例）
- 验证：修复前缩窗百分比冻结、修复后随宽重算且回宽逐位还原；同宽双文件
  百分比逐位一致

### 修复 — 改字体/字号/自动换行/自定义 CSS 后表格列宽不重算
- 根因：`applySettings`(wordWrap)、`setFont`、`applyCustomCss` 均不调度重算，
  而字号/white-space 变化改变 min/max-content 测量基准；宽度观察器兜不住
  （字号变化只改文本高度，宽度不变被守卫跳过）
- 修复：三处入口各加 `_scheduleTableLayout()`

### 修复 — 过约束区间多列表格被均分，超长不可断内容列拿不到宽度
- 症状：12 列表在窄内容区（< ~830px）12 列均分，含 1350px 不可断 JWT URL
  的列也只拿 8.3%
- 根因：分配器 `floor=min(maxW, min(3×minW, avail/n))` 的 `avail/n` 封顶
  在所有列 3×minW 都超均份额时把内容差异全部抹平
- 修复：`lift` 偏好（第 5 参缺省 0.5）——minW 超均份额的列 floor 抬到
  `min(minW, lift×avail)`（单列保底至多认领半宽），压缩分支按占比让宽；
  未超均份额的列行为不变。实测列07 8.3%→28.5%，短列 >4%；编辑器/HTML/PDF/
  DOCX 四链路同时生效（共享模块单一来源）
- 配套：HTML 导出 bodyCss 明暗两套 `th,td` 补 `overflow-wrap:anywhere`
  （编辑器 editor.css 与 PDF 导出既有同款，三面对齐，export-visual-spec 补条目）

### 修复 — PDF 两分页路径甘特图渲染宽不一致（高度差约 21%）
- 症状：同一文档的有脚注版（paged.js 路径）比无脚注版（nativeHf 路径）甘特图
  高 ~21%（同满宽显示），第 9 页被挤掉 3 行
- 根因（隔离实验）：甘特图 viewBox 宽=渲染时刻容器宽（body 789/656/900/535
  → viewBox 宽逐一跟随）、高恒定（行数驱动）；两路径渲染视图宽不同
  （nativeHf 整纸 ~789px / paged 页内容 ~656px），最终都以内容宽显示
  （`width="100%"`）⇒ 显示高度比 = 789/653 ≈ 1.21（实测行高 24.2 vs 19.7px）
- 修复：`mermaid.render` 第三参传在流 holder，令布局宽=显示宽，两路径均 1:1；
  流程图/时序图等内容驱动图型不受容器宽影响；HTML 导出行为不变

## [0.19.0] — 2026-09-22

导出批次：三份导出器（PDF/HTML/DOCX）共用抽提 + 死代码清理 + **PDF 分页几何
根治**（分页前测量几何、表格预分页容量模型）。共用抽提部分**产物行为不变**；
PDF 版式相关改动改前先读 [`docs/export-visual-spec.md`](docs/export-visual-spec.md)
（视觉规范对照与判定）与
[`docs/pdf-header-footer-layout.md`](docs/pdf-header-footer-layout.md)（打印版式与分页约束）。

### 修复 — 两路 PDF 标题字号差 10%（包装单元格字号基准被 th,td 带偏）
- 症状（用户对比两份几乎相同的文档）：有脚注文档（paged）的 PDF 标题**比无脚注文档（nativeHf）明显大**；表格与页脚完全一致，只有标题差一整档
- 根因（逐项测量 + 渲染页计算样式）：nativeHf 把正文整篇包进包装表单元格 `<td id="hfcell">`，而基础 CSS 有 `th,td{...font-size:10pt}` ⇒ 单元格内**一切 em 相对值按 10pt 解析**：`h1{1.7em}`→17pt（而非 18.7pt）、`h2`→13pt、`h3`→11pt，行内 `code{0.9em}`→9pt（paged 侧 9.9pt）。绝对值字号（表格单元格 10pt、页脚 9pt）不受影响，故只有标题看起来分叉。实测 h1/h2/h3 高度与宽度**一律 1.100×**，表格与页脚 1.000×
- 修复：`#hfcell{font-size:11pt;...}` 显式钉回设计基准（id 特异性高于 th,td）。**有脚注 PDF 不变**（本就是 11pt 基准），无脚注 PDF 的标题补回 10%（h1 17→18.7pt），两路标题实测高宽比 1.000 ✓，页数不变（各 11 页）
- 教训记入 `docs/export-visual-spec.md`：**任何包装元素都可能改变 em 解析基准**——新增包装层（表格单元格/容器 div）时须显式声明字号，否则内部相对字号静默漂移

### 修复 — 含脚注 PDF 分页前测量几何错位（页尾大片留白、行被挤到次页）
- 症状（用户实测）：「第一个表格的第 10 行在第二页，应该在第一页，第一页第 8 行下面应该够放 1 行」；「1.3 长宽双超表格，在第 5 页下方有足够的空间可以放好几行」
- 根因（三层，逐项实测）：
  ① **屏幕布局宽取了纸宽**（A4 791px），而 paged.js 分页后正文在**页盒内容宽**（18mm 边距下 658px）里重排——同一行屏幕量 30px、成品 52px。预分页按 30px/行 切块，每块塞进 ~1.7 倍行数 ⇒ paged.js 只能再切碎（丢行路径）且页尾大片留白；列宽归一的 intrinsic 测量同样失真；
  ② **基础 CSS 对 paged 路径包在 `@media print` 里**，由 paged.js 在 `preview()` 中解包，而列宽归一与预分页切块都跑在解包**之前** ⇒ 量的是「无基础 CSS」的裸布局（实测行高 30px vs 成品 52px）；
  ③ **滚动条压制只罩住列宽归一**，预分页切块时滚动条已放出（视口再窄 ~15px）
- 修复：paged 路径渲染视图宽取**页盒内容宽**（纸宽 − 用户左右边距；nativeHf 仍取纸宽——它的正文横跨整张纸）；基础 CSS 对 nativeHf 与 paged **都不包** `@media print`（屏幕布局 = 打印布局，legacy 路径仍包着）；滚动条压制罩住全部分页前测量，进 `preview()` 前才清
- 效果（含脚注夹具）：逐页正文墨迹由 124.8 / 206.6 / 80.1 / 99.1mm 变为 **261~276mm**（内容底线 279mm），页 1 排满 001~011 + 页底脚注，页数 11（与基线一致）

### 修复 — 含脚注 PDF 跨页表格丢行（表格预分页；paged.js 表碎片缺陷绕行）
- 症状（用户实测）：含脚注文档（paged 路径）跨页表格在断页处**整行消失**——「1.1 纵向超长表格」第 11 行、「1.2 横向超宽表格」首数据行；页数与其余行落页不变，肉眼极难察觉
- 根因（打印前 DOM 取证 + 0.6mm 边界扰动复现 + paged.js 内部取证）：表格末行"恰好溢出"时，paged.js 为它新建的续表碎片会在部分几何下**留在当前页并撑成内容盒整高**（实测行盒 957px vs 计算高 55px）⇒ 溢出判定 `hasOverflow`（比 `max(渲染高, 父scrollHeight) > 版心`）判成"没溢出"⇒ 既不裁剪也不重排 ⇒ 打印时整块不绘制。触发是亚毫米级的，与字体度量环境有关；与脚注浮动**无关**（停用后仍复现），真正相关的是"有脚注 ⇒ 走 paged 路径"
- 修复：**表格预分页**（`__prepaginateTables`，分页前、列宽归一之后调用）——把**超过一页的外层表**按行切成多张，每块装得下一页，paged.js 只需在块之间做正常块级分页，**不再需要表碎片**，整类缺陷消失。**容量模型（2026-09-22 重做，以"应用路径复现"为准）**：
  ① 续块带 `break-before:page` **从页顶开始** ⇒ 只需保证单块装得下一页，与全文档页格无关（旧实现按 `y/Hpx` 累加页格，而真实页高随该页有无脚注变化，单一常数步长的差逐页累积，长表所在页越切越短）；
  ② 单页可用高 = 版心高 − **实测**脚注区高（测量遍量得、C++ 注回；不硬编码、不按脚注定义体高估），**按页判定**——无脚注的页白留一个脚注区高＝每页少一行；
  ③ 首块用**起始页剩余高**（从流首逐页前推），续块各自按所在页容量；**位移累加 push**：切块把内容推到新页 ⇒ 后续内容整体下移，未切块的屏幕流测量值不能再当页格坐标用（实测 1.3 表顶模型 4350 / 真实 5337，差近一整页——后续表全按错的页算余量 ⇒ 块溢出 ⇒ paged.js 切表 ⇒ 丢行）；
  ④ **安全余量 ≥20px + 首行放不下就整表挪页**（`pdf-tbl-head`）：溢出是亚像素级的（实测 1.1 表首块 710px + 真实起点 248 超内容盒 956 **仅 2px** 即触发切表 ⇒ 第 11 行变错位碎片）；
  ⑤ **脚注浮动先于切块**：def 变引用旁 inline-block 会把其后内容下推（实测表顶 222 → 248），先切块就按未浮动位置算 ⇒ 首块多切一行即溢出
- 试错记录（避免重蹈）：① 事后"搬行回收"三版实现均**不安全**（会把原本正常的行搬没——用户环境实测 1.2 表头整行消失；按 `data-ref` 配对必然落空：paged.js 给同源各碎片分配**不同** ref）已否决；② 固定比例切块（80%）浪费页尾（11 → 14~15 页）；③ 切块后用当前 `y` 重算页尾等于没翻页，切出连续小块（20 页）；④ 位移增量用未带位移的 `y0` 会重复计入前序位移（1.3 表模型多算 121px ⇒ 块切小 ⇒ 页尾留白）
- **工装教训**：必须喂**编辑器 `getContent()` 的 HTML**（TipTap 序列化），不能喂 `MarkdownHandler::toHtml()` 直出的 HTML——同一夹具 35,975 vs 96,405 字符、表格行高 56px vs 52px，此缺陷只在编辑器 HTML 下复现（直出 HTML 一切正常，此前据此误判"已修复"）
- 验证（**应用路径**、含脚注夹具）：60 个序号行**无缺**（逐页核对 001~010/011~027/028~044/045~060）✓、1.2 表 6 行整表一页（甲乙丙丁行在位）✓、1.3 表 16 行全在 ✓、**页数 11 不变** ✓、页眉页脚在位 ✓；自动验收（CJK 字符袋 + 表格行文本全篇在位 + 逐页留白 + 页眉页脚墨迹 + 页数对照）仅剩 1 项：第 7 页留白——**固有限制**（3.1 节 Mermaid 图实测约 250mm，任何页都放不下它）；单测 24/24
### 重构 — 抽出 `ExportCommon`（三导出器共用，单一来源）
- 新增 `src/core/ExportCommon.{h,cpp}`：资源读取、HTML 属性转义、`</style>`
  剥离（自注入面）、图片白名单策略 + 内容嗅探 + 目录围栏、`mimeForExt`、
  EMU 标度、**页码文案三段**。收录原则写在文件头：只收语义完全一致、与媒介
  无关者；打印/屏幕/Word 的差异项留在各自实现（不为统一而统一）
- 页码文案（`kPageText*`）四方同源：paged 页边距盒、nativeHf 覆盖层、
  成品校验正则、DOCX 页脚字段。此前改文案漏改校验正则 → nativeHf 每份产物
  判「页码漂移」→ 静默回退 paged.js，且无报错指向真因；现加**漂移锁测试**
  （正则必须匹配文案，改文案即红）
- KaTeX 渲染段提为 `HtmlExporter::katexRenderScript()` 两路共用（PDF 侧原为
  无 `window.katex` 守卫的副本，现统一为带守卫版——无公式文档不再落 ReferenceError）
- 实测三产物不变：PDF 同文档 11 页、页脚墨迹 282.79~285.58mm 与改前逐像素一致；
  HTML 表格列宽（编号 61px/状态 77px 全单行）与 2.3 长串折行不变

### 修复 — DOCX 图片内联缺体积上限（安全）
- `readImageFile` 与 `HtmlExporter::inlineImages` 是同一防线的两份实现，后者早已
  带 50MB 上限、前者**没有**（注释却写着「同型防线」）。抽共用后 DOCX 侧一并补齐；
  格式白名单仍各按能力给（HTML 含 svg/webp，Word 只认 png/jpeg/gif）

### 清理 — 死代码、失真注释与无用接口
- nativeHf 第一遍「墨底测量」整段（`fullLevel` 只写不读、`geo`/逐页文本提取只喂
  一条 `HF_DEBUG` 日志）收进 `if (HF_DEBUG)`：正常导出省一次全 PDF 内容流解析
- 删 `m_hfTops`（只写占位 0，唯一读取的 else 支不可达）、`canSplit`（两个分支都
  `return false`，`mark()` 因此等价于 `safe?1:0`，一并内联掉）
- 删 `MermaidRenderer::Result::svg`（每图把完整 SVG 跨进程回传后无人消费；DOCX 已
  定案用 PNG 兜底，将来要做矢量嵌入可从 git 历史取回）
- 订正陈旧注释 6 处：nativeHf「校正轮」（机制已随分段堆叠撤销，代码里没有轮次
  循环）、`m_hfTops` 注入描述、可断性注释（原文称「li 内部一律判为不可断」，与
  `desc(el,el,true)` 的实际行为不符）、`preSplit` 重复两遍的注释收敛并补 CHANGELOG 指针
- 删未用 include（`PdfExporter` 的 `<QImage>`、`PdfPageGeometry` 的 `<cstdlib>/<cstring>`）、
  `DocxExporter` 的 `input/col` 空分支与不可达的 `svg` content-type 分支

### 测试
- 19/19 全绿；新增页码文案漂移锁（`testPageNumberTextMatchesVerifyPattern`）。
  过程中发现该测试一度被插到 `private:` 之后而未注册（QTest 只跑 slots），已修正
  并全仓扫描确认无同类未注册测试

### 待办（用户记账）
- PDF 与 DOCX 的代码块语法高亮（见 CLAUDE.md「已知问题 & 待完善」）

## [0.18.0] — 2026-09-21

PDF 页眉页脚版式对齐批次：原生快路径（nativeHf）版式重做，与 paged.js 两路产出逐项对齐。

### 修复 — 原生快路径版式与 paged.js 分叉（首页表格 9 行 vs 10 行）
- **页眉搬进纸顶带** — 旧版式页眉是内容区顶部的 thead 重复带，吃掉正文区顶部 8.5mm，首页表格因此少一行。新版式打印区覆盖整张纸（Qt 边距 0.1mm），thead 带高 = 用户上边距（带内标题墨迹垫到纸顶 ~7.4mm，与 paged.js `@top-center` 实测落点一致），正文顶 = 上边距，页眉不再占正文区
- **页脚落进纸底带** — 旧版式页脚画在内容区底（距纸底 ~22mm、与末行表格边框相压，靠 22px 单元格底垫 + 白底遮挡兜住）。新版式页脚覆盖层落在下边距带内，22px 底垫 hack 随之撤销——表格行高恢复自然，与 paged 同源
- **页眉页脚贴页边距（Word 式）而非居中悬空** — 旧版式照 paged.js 默认把页眉页脚居中在页边距带里，墨迹离纸边仅 ~8mm（多数打印机不可打印边距 4-6mm，余量太薄），且位置写死不随设置走。现改为：页眉压在正文顶线之上、页脚紧贴正文底线之下，各留 3mm 净空，**位置全部由「设置/导出」的页边距推导**——默认 18mm 下页眉墨迹 11.1~13.6mm、页脚距纸底 ~10.9mm。两路落点公式同式：nativeHf 侧为覆盖层 `top: calc(100% − (下边距 − 净空))`，paged 侧为 `@top-center{vertical-align:bottom}` / `@bottom-right{vertical-align:top}`（paged.js 会把它翻译成页边距盒的 align-items）
- **轻量分页模拟下边距** — Chromium 打印没有逐页底部保留机制（`tfoot` 不逐页重复、`@page` 边距被 Qt 布局边距覆盖、`position:fixed` 只在可打印区内重复、`abs` 越界盒被推到次页顶，探针逐条实证），故由 `__paginateNativeHf` 在屏幕媒体里量块高、按「每页只装版心高」贪心切页，给页首块打 `break-before:page`（表格按行、列表按项、引文下钻、代码块按行）；测量须在打印区同宽视口内（渲染视图 resize 到打印区宽 + 测量期临时禁滚动条——滚动条会吃掉视口宽让屏幕布局窄于打印）
- **成品双防线** — 单块高于一页提前回退（自然分页会把碎片铺进纸底带压住页脚）；成品逐页校验新增「正文墨迹落入纸底留白带」拦截，失守即回退 paged.js（慢但正确）
- 仅页眉仍单遍直出；页眉开关不再改变正文版式（关页眉时 thead 留空带占位）

### 修复 — 连续无空格长串在 paged 路径被裁断（两路折行分叉）
- `th,td{overflow-wrap:anywhere}`（表格列宽归一用）在 nativeHf 路径因正文整体包在包装表单元格里而**全篇继承**、paged 路径无此包装故不继承——同一份文档「2.3 横向超宽色块」的三个超长列表项，快路径三串都自动折行、paged 路径两串横向溢出被页盒 `overflow:hidden` 静默裁掉（实测墨迹直达纸边 209.8mm，正文右界 191.9mm）。规则提到文档级 `body{overflow-wrap:anywhere}`，两路折行一致（实测两路最深右缘均 191.7mm，长串首尾片段完整在文）；取 `anywhere` 而非 `break-word` 是为同时收敛 min-content，避免长串撑爆文档 min-content 触发 shrink-to-fit 全局缩印

### 修复 — HTML 导出表格短列被压成一字一行（列宽归一，与 PDF 共用实现）
- 症状（用户实测 1.1 节 60 行表）：「编号」列装不下「001」（折两行）、「状态」列把「已完成」压成一字一行（折三行）。根因：浏览器 auto 布局下 CJK 列可任意字间断行 ⇒ min-content 塌缩为单字宽，富余宽按 max-content 比例尽归长列——PDF 导出早以 `__fixTableColumns`（两趟 intrinsic 测量 +「短列保真、长列吸收」分配）治过同一症状，HTML 导出侧没有，靠浏览器自动列宽
- 实现提为两路共用：`HtmlExporter::tableColumnNormalizeScript()` 定义 `window.__normalizeTableColumns(cfg)`；PDF 侧经 `__fixTableColumns` 兼容层调用（函数名与两个调用点不变，测量样式仍传打印基准 10pt / 4px 8px / 固定字族 ⇒ 行为保持，实测该文档仍 11 页、页脚墨迹 282.79~285.58mm 与改动前逐像素一致）。HTML 导出在 load 后调用 `{toTable:true,slack:2}`
- 三处 HTML 专属细节：① **可用宽取每表自身盒宽**（`display:block` 时即容器内容宽）——`body.clientWidth` 含左右内边距 64px，实测会让分配整体缩水（状态列 8.44%≈69px < 所需 74px，仍一字一行）；② `toTable` 把表格由 `display:block`（宽表横向滚动的无 JS 兜底）切回 `table`，`table-layout:fixed` 才有意义；③ **slack=2px**：列宽走百分比再乘回表宽，小数布局与边框折叠会吃掉零点几像素，卡在 max-content 边界的列仍折行（实测状态列 74.69px vs 需 74px 差 0.3px、编号列 58.70 vs 58 差 0.1px）；PDF 不传 slack——其 10pt 基准余量较宽、此文档实测无边界折行，传 2px 会让该文档 11→10 页
- 实测（用户导出件的真 body 重导出，视口 900px）：1.1 节表全部单元格单行（编号列 61px、状态列 77px，行高 86→59px）；2.3 超长列表仍折行（11/11/9 行、零溢出）；两张宽表压缩入宽零溢出（12 列 65~91px、10 列各 82px）

### 修复 — HTML 导出超长无空格串横向溢出（与 PDF 两路同源）
- HTML 导出的 `body` 规则缺折行声明（`overflow-wrap` 为默认 `normal`），「2.3 横向超宽色块」的前两个超长列表项横向撑爆正文盒（实测 li 内 `scrollWidth>clientWidth`、内容右溢被裁），第三个（含可断点）正常折行——与 PDF 侧修复前同型。同一份文档在 nativeHf、paged 两处都折行，只有 HTML 产物不折
- 两套主题的 body 规则补 `overflow-wrap:anywhere`（与 PDF 基础 CSS 同款）。取 `anywhere` 而非 `break-word`：前者同时收敛 min-content，**宽表随之压缩进正文宽，与 PDF 既定行为一致**（含脚注 PDF 实测 12 列宽表每列 ~10.7mm、列头「列/01」折两行、最右 184mm < 正文右界 192mm）；`break-word` 不改 min-content，宽表照旧横向滚动，反而与 PDF 分叉
- 同文件单点 A/B（仅在用户导出件上注入这一条声明，其余逐字节相同）：2.3 列表项 1/2 由溢出 → 折行（11/11 行、零溢出），列表项 3 不变（9 行）；主表（61 行）高度 6930px 不变、列宽随 min-content 重分配（58/119/54/51/539 → 49/81/54/54/583px）；两张宽表由横向滚动改为压缩入宽（溢出 923/77px 归零，表高 561→764 / 962→1356px），三个 h3 标题逐项不变

### 修复 — HTML 导出脚注编号与内容换行
- `HtmlExporter::bodyCss` 两套主题都缺脚注版式规则：`footnote-content` 是块级 div，缺 `float:left` 绕排时行内 marker 后跟块级元素被拆成匿名块、编号独占一行（用户实测导出 HTML：脚注名与脚注内容换行）。照 `editor.css` 复刻补入浅/暗两套（marker 浮动 + 首子元素去顶距 + 容器分隔线/字号 + 引用角标配色）；PDF 模板早有同款复刻，这次补齐 HTML 侧

### 修复 — 跨页代码块灰底越界画到纸底（压过页脚）
- 快路径可打印区覆盖整张纸（Qt 边距 0.1mm），Chromium 把跨页块的**背景**一直画到片段容器末端＝纸底：实测 5.1 嵌套围栏代码块灰底 242→296.6mm，越过版心底（279）压过页脚画到页边。改由**逐行 `.pl` 块**承载底色与内边距（行块 `break-inside:avoid` 永不跨页 ⇒ 背景不越界），逐行重排后 JS 清掉 `pre` 自身底色；首末行补垂直内边距与圆角复刻整块外观。实测灰底止于 273.7mm（版心底之内）
- 连带修复：`<code>` 自身的底色/内边距在代码块内一并清掉（`pre code{background:none;padding:0}`）。逐行重排后 `<code>` 的块级子元素会切开其行内盒，首尾各留一个空行内碎片、各带一份底色与内边距，在代码块上下各画出一条细灰条（用户截图）；此前被 `pre` 的底色盖住（两色几乎相同）不可见

### 修复 — 表格列宽两路径不一致（根因：包装表污染 intrinsic 测量）
- `__fixTableColumns` 遍历 `querySelectorAll('table')` 时把 nativeHf 的包装表 `table.hf` 也算进去：它的 `querySelectorAll('td,th')/('tr')` 覆盖全文档，于是给**每个**单元格写下「本表格列宽」（单列包装表即 100%），污染随后所有内层表格的 intrinsic 测量——短列被压成逐字竖排、与 paged 路径列宽分叉的唯一根因。跳过包装表后两路径列宽/行距/页数完全一致
- 基础文档 CSS 的 `@media print` 包装对 nativeHf 不再包（paged 路径由 paged.js 的 PrintMedia 处理器解包，legacy 路径包着即可）：屏幕布局 = 打印布局是分页测量与列宽测量的共同前提
- nativeHf 的 mermaid/图片页高上限与 paged 同源取 2mm 安全间隙（旧版扣 8.5mm 重复表头——新版式页眉带已移出正文区）
- paged 路径的列宽测量同样受益（渲染视图统一 resize），其输出列宽随之一致

### 已知边界 — 深嵌套列表文档回退 paged（2026-09-21 记录）
- 探针实测：`break-before:page` 只在**块边界**被 Chromium 可靠执行（顶层块、表格行、`<li>` 任意层级），`<li>` 内部的非 li 元素上会失效（内容错位到页尾）；而 paged.js 是"量好切开搬运"（DOM 手术），能在 li 内部任意下刀——这是两条路径最本质的能力差。分页器据此限定可断点，li 内部内容交给 Chromium 自然分页；这类内容会一路流到自然上限（比版心底低一个下边距），与贴边距后的页脚行盒相压 → 成品校验拦截 → 整篇回退 paged（布局与参考版逐页一致，但 ~12s）
- 只下钻到 li 一层的旧收集方式会把「6 级嵌套列表」的首个 li（含整棵子树）当成不可分巨块，整块推走留大片空白（用户实测）——已改为递归收集叶块修复
- 就地拆 li 的"局部 DOM 手术"（把 li 在断点处一分为二、后半挂 break-before，使 li 内部也能断）已实现并经探针验证机制可行，但屏幕模拟与打印成品仍有 ~8mm 系统偏差（拆分后 li 间距与原始嵌套不完全一致），故当前**暂缓启用**，待做成布局中性后再开

### 验证
- 桌面真实文档（复杂场景综合测试 11 页 / 含脚注 11 页）两路径逐项对齐：页眉 7.28 vs 7.37mm、页脚距纸底 7.45 vs 7.37mm、页数 11 vs 11、表格行距（13.5/13.8mm）与列位置一致
- A4 横向、仅页眉、仅页脚、仅正文、脚注文档各组合实测通过；19 项单元测试全绿
- 版式机制与取证记录见 [`docs/pdf-header-footer-layout.md`](docs/pdf-header-footer-layout.md)

## [0.17.0] — 2026-09-19

导出性能批次（源自 2026-09-15 起的性能取证与实验）。

### 新增 — PDF 导出原生分页提速（页眉/页脚文档 ~10-20×）
- **原生分页 + 覆盖层页眉页脚** — 页眉/页脚启用的文档不再走 paged.js 逐页 DOM 手术（取证 ~0.8s/页，16 页 13s），改走 Chromium 原生分页管线：**页眉**用 table `thead` 重复带承载（Chromium 打印每页重复表头）；**页码**用 abs 覆盖层画进下边距（`top=kU+8`，U=每页有效内容高=内容区高−页眉带）；含页脚时两遍打印定总页数（仅页眉单遍直出）。端到端实测 21 页 660ms / 28 页（含 880px 不可分块压力）806ms——对 paged.js 约 **20×**
- **成品逐页页码校验** — QPdfDocument 逐页提取校验「第 k 页/共 n 页」与实际页序一致，漂移即回退 paged.js 重导（慢但正确），绝不出错页码成品；nativeHf 任一环节失败同样回退
- 含脚注引用的文档仍走 paged.js（`float:footnote` 随页浮动是 paged 独有，原生无对应能力）
- 机制经独立探针程序逐项实证：thead 每页重复、abs 覆盖层按内容区坐标精确落位且不增页、近整页高不可分块压力零漂移

### 优化 — 导出按需注入（性能）
- **重型库按内容命中注入** — 无流程图的文档不再白付 3.5MB mermaid.min.js（加载+解析 ~350ms），无公式不付 katex 272KB+CSS、无代码块不付 highlight：PDF 与 HTML 导出同型改造；纯文本文档 HTML 产物从 ~4.5MB 瘦身到数百 KB。渲染链自带守卫（if(window.mermaid) 等），库缺席即整段跳过
- **MathMl/Mermaid 渲染器单例常驻** — 进程级复用（旧版每次导出新建 QWebEngineView），aboutToQuit 主动销毁先于 WebEngine 全局回收；并发防御（上一轮未结算时本轮降级回退源码文本）；DOCX 导出器完成即断连防重入（单例长于导出器生命周期，旧连接误唤醒曾致重写旧文件）
- **分页进度状态栏** — paged.js 大文档分页以十秒计（取证 ~0.8s/页），「正在分页：已生成 N 页」实时反馈（复用既有轮询零额外开销）

### 实验 — CDP 原生打印（已否决删除）
- 曾实现 CDP（Chrome DevTools Protocol）`Page.printToPdf` 快路径（Chromium 原生分页 + 原生页眉页脚模板，取证 16 页 13s → <2.2s），真机验证 Debian 与 macOS 的 Qt WebEngine 均报 `'Page.printToPdf' wasn't found`——该方法是 Chromium 无头模式专用，嵌入式 Chromium（Qt WebEngine/CEF 同构）非无头运行不注册，无 flag 可开。代码已整块删除，此路不通的结论留档免重蹈

### 修复 — nativeHf 页码与页脚稳定性（用户多轮截图实证收敛）
- **页脚位形像素取证定案** — Chromium 打印把每页绘制**硬裁到内容盒（fragment）内**：越片段末的一切绘制（relative 平移、abs 溢出、1px 微行盒 past-end）产出全是**不可见幽灵文本**（内容流矩阵在、QPdf bounds 全零、像素无）；逐页底部保留带无机制可实现（tfoot 不逐页重复、`box-decoration-break:clone` 块级分片不适用、@page 边距整体同进退）。最终形态：abs 行盒 +2px 越段末（A4/18mm 基线 777.7pt，各页含末页均一可见）+ 白底 span 遮挡与最深内容墨迹（表格行底边框）的 ~9pt 残余同带
- **幽灵文本验证防线** — 成品逐页页码校验的匹配文本只取 QPdf bounds 非退化字符：矩阵基的色锚定测量对幽灵文本全盲，曾致全档幽灵页码假通过
- **页码分段堆叠重写** — 每片恰一页高（首片扣 thead：页 1 真实占位、页 2+ 重复表头覆盖绘制不耗流），几何分段与内容推挤/墨底解耦——取代 T/Hc 流模型累计与实测校正轮（21 页文档墨底累计漂移整页的旧方案）
- **页码漂移双根源** — ① mermaid SVG 固有宽撑爆文档 min-content → shrink-to-fit 全局缩印（实测 0.5×）→ 每页流耗翻倍 → 覆盖层逐页早移：svg 补 `max-width:100%` 钳制；② QPdf 提取器把「第 12 页」数字拆 run 伪影：校验改正则全局扫描
- **校验白名单剥离** — 提取流逐字形 `\r\n` 与不可见非字符混进页码字形序列是「标记消失」假象的唯一真凶：匹配前白名单剥离
- **PdfPageGeometry 内容流几何测量模块** — CTM/GState/文本矩阵/Form XObject/图片 Do/路径绘制/裁剪纪律/miniz Flate 解压 + 页脚色锚定（`rgb(102,102,103)` 蓝通道 +1 为文档内唯一标识）：补齐 SVG/图片等无文本内容元素的底沿测量缺口（QPdf 文本接口量不到矢量图形）；8 项单测
- **表格行/mermaid 与页脚同带相压** — 单元格底垫（`th,td` 行底 22px 净空，页脚带只剩边线穿过、页码白底跨线可读）+ 行原子性（Chromium 行分片绘制越过分页器末端直插下边距的实测）+ nativeHf 接入跨引擎列宽归一（`__fixTableColumns` 两趟 intrinsic 测量，与 paged 路径同源，短列塌缩/长列霸宽双症状）
- **分页指纹对称修复** — `__preText`/`__verifyContent` 两侧对称剔除 script/style：MarkdownHandler 产物的内嵌文档 head 与 mermaid SVG 内嵌 `<style>`（每图 ~1KB）单侧计入即假阳性（实测 28913→28154 字差触发无谓回退）

### 新增 — PDF 导出进度对话框
- paged.js 大文档导出以十秒计、nativeHf 也有秒级两遍打印，全程静默会让用户误以为卡死而随手关闭程序（半成品文件 + 隐藏渲染视图随窗口销毁）。模态对话框：左侧 12 帧蓝色旋转弧 GIF（透明底、明暗主题通用），导出完成前不可关闭（无关闭按钮、Esc 无效、标题栏 X 拦截）
- **两阶段进度**：分页「正在排版分页 · 已生成 k 页」（复用 pagedProgress 轮询）；写盘「共 N 页 · 正在写出 PDF…」（新 `writingPdf` 信号——Chromium printToPdf 是一次性黑盒，无逐页写出进度，页数未知路径降级为「正在写出 PDF…」）
- **成功自动关闭；失败转错误态不自动关闭**——停动画、显示失败原因（可选中复制）、出现「关闭」按钮，用户确认后才消失（静默自动关闭会让用户不知道导出失败）
- GIF 资源形状锁入测试（尺寸/帧数/透明底/每帧墨迹像素）：生成管线两教训——`putpalette` 必须打到每帧 P 图上（打在模板图不生效 → Pillow 逐帧重量化出本地调色板，QGifHandler 解码即毁）；GIF 无半透明，alpha 渐隐拖尾设计不成立（统一实心蓝开口弧）

### 构建
- 新增依赖 Qt6 Pdf（页数统计与逐页页码校验）：deb 依赖/CI（deb/macOS）、win11 VM provision、clang 容器镜像同步

## [0.16.1] — 2026-09-19

### 修复 — macOS
- **「打开方式」推荐列表灰色** — CFBundleDocumentTypes 此前只按扩展名声明（CFBundleTypeExtensions），现代 macOS 推荐列表按 UTI 匹配，md-editor 被灰掉、需切「全部应用」才能选。补 `LSItemContentTypes: net.daringfireball.markdown`（系统 UTI，Typora/MacDown 同款）；删除与系统争抢扩展名的自定义 UTI（`com.md-editor.markdown`）；CFBundleTypeExtensions 保留作老系统兜底；打包脚本注入后加 PlistBuddy 断言
- **双击 .md 打开后窗口空白** — macOS 上文件路径经 Apple Event → QFileOpenEvent 到达而非 argv，此前无人处理。Application 捕获 FileOpen 发 `fileOpenRequested`，MainWindow 复用 `onFileDoubleClicked` 打开（冷启动与运行中再打开均覆盖）；新增 test_application 回归
- **启动期切标签页的 JS 未捕获异常** — `onTabChanged` 的三连推送（notifyHeadings/Format/History）无 `window.editorApi` 守卫，页面脚本未执行完的窗口期触发（macOS 冷启动尤甚），且因同段执行连带跳过后两条推送；补守卫与相邻代码一致
- **同路径文件重开不再开重复标签** — 双击/Finder 打开已打开的文件改为聚焦既有标签页（与 VS Code/Typora 等主流编辑器一致）：重复标签各自保存是后写者静默覆盖，未保存修改会静默丢失；未保存修改与外部重载警示原样保留。canonical 路径判定（相对路径/符号链接/冗余分隔符归一），新增 test_editorpath 回归
- **macOS 元数据触碰误报「已被外部程序修改」** — Finder 再次双击已打开文件时，Gatekeeper/Spotlight 的元数据触碰（xattr/atime）也触发 fileChanged，内容未变却弹重载警告。外部变更报警前先核实 size+mtime 与监控基线一致与否（与 VS Code 同款判定），未变则静默吞掉；新增 testMetadataOnlyEventSuppressed 回归
- **DMG 拖拽安装布局** — 安装窗口改为 app 图标（左）+ Applications 替身（右）+ 微渐变背景引导拖拽安装；箭头收窄并与图标中心线对齐（首版 188px 且低于图标中心 60px，真机反馈「太大太丑」），箭头下方加中文提示「拖动图标到 Applications 以完成安装」（与替身图标名一致）；背景为 @2x 144dpi 静态资产（`scripts/dmg-background.png`，本地 PIL+苹方生成后提交——CI 无字体环境不现场生成），test_dmgbg 锁定资产设计要素
- **挂载卷内直跑硬阻断** — 双击挂载卷里的 app 图标：弹「请先拖曳 md-editor 图标到 Applications 图标以完成安装」后直接退出，不进主界面（Finder 双击本身无法拦截，应用进程起来后立即自断是应用侧最强阻断）——在镜像内运行会让 Launch Services 登记多个应用路径（「打开方式」推荐列表置灰的根因，见下方升级提示）。路径判定 `MacInstallCheck::isTransientLocation`：app 位于挂载卷根（DMG 拖拽形态）或 Gatekeeper 转位目录（/AppTranslocation）才拦，二级卷子目录里的正经安装不误伤；新增 test_macinstallcheck 回归
- 升级提示：从 ≤0.16.0 覆盖安装后，Launch Services 可能残留旧注册导致「打开方式」推荐列表仍灰——先清空废纸篓里的旧版本、推出旧 DMG 挂载卷（僵尸条目两大来源），再 `lsregister -f /Applications/md-editor.app`；仍不行用终极手段 `lsregister -kill -seed -lint -r -v && killall lsd` 重建 LS 库后重启（新装用户不受影响）

## [0.16.0] — 2026-09-15

全面代码审查修复批次（3 个并行审查覆盖往返保真核心 / Web 层 / C++ 外壳与安全面，约 50 项发现，本版完成 P0-P3 四个批次），叠加阶段 1-5 补充批次（往返保真 / 性能 / DOCX 脚注 / 块引用嵌套 / 收尾加固）与 DOCX/PDF 脚注导出的 Word 对齐。

### 修复 — 数据丢失与损坏（P0）
- **块引用内行内格式全剥离** — fromHtml 引用步骤先于行内转换运行，`inner.remove(<[^>]*>)` 把引用内所有标签剥掉：WYSIWYG 对引用内文字加粗每次保存即丢语义（`> **加粗**` → `> 加粗`），链接/图片直接消失。修复：引用抽取后移至行内占位保护之后 + `blockquoteInnerInlineToMd` 手动转换 Qt 侧标签族
- **数学块正则 `\3` 反向引用越界** — 仅 2 个捕获组引用 `\3`（越界组号被 Qt 当字面量输出），数学块前 ≥2 空行时 div 开标签被替换成字面 `\3`（数据损坏）
- **`~~~` 波浪围栏与缩进围栏整族无保护** — 顶层围栏状态机只认零缩进反引号：`~~~` 内容行被 BRMARKER 注入 `<br data-brk>`，往返后代码行无分隔拼接、语言串打碎。新增 `isTopLevelFenceLine`（CommonMark 全族）+ 闭合同字符互斥
- **源码模式保存/导出旧快照** — `getContent()` 恒取 WYSIWYG 文档，源码模式编辑后 Ctrl+S 写入旧内容、编辑静默丢失且 markClean 清脏标记；三导出同受害。保存取 `getSourceContent()` 原样写盘，导出经 `getEditorHtmlAsync`
- **writeFile 不查写错误** — 磁盘满时部分内容的 tmp 直接替换原文件（静默截断 + 界面报已保存）。重构为 QSaveFile 原子提交 + 状态检查，失败路径恢复监控 + 模态警告框（曾 fileWriteError 零连接，唯一反馈是 3 秒状态栏小字）
- **关闭窗口不等异步保存** — closeEvent 调异步保存后立即退出，回调随窗口销毁不再执行：点了「保存」但未落盘。改逐 tab 异步保存队列
- **mermaid XSS（编辑器 + 导出双面）** — `securityLevel:'loose'` 跳过 sanitizeUrl/sanitizeText，恶意图表标签把 `<img onerror=…>` 带进 SVG 经 innerHTML 执行（编辑器页持有 QWebChannel bridge 即特权执行；导出的自包含 HTML 打开即触发）。三处改 strict + `_sanitizeMermaidSvg` 增补 script/事件属性/javascript: 清洗纵深
- **HTML 导出任意本地文件内联** — `inlineImages` 无围栏无校验无上限：`![](../../.ssh/id_rsa)` 把任意文件 base64 嵌进产物。docDir canonical 围栏 + 内容级 MIME 白名单 + 50MB 上限

### 修复 — 保真与嵌套完整性（P1）
- **DOCX 引用块完整化** — 曾嵌套引用内层闭合截断外层（其后段落丢失）、引用内 pre/table 文本丢失、列表退化。全量分派 + 递归 + 逐段引用样式（层级缩进）
- **DOCX 色值校验** — CSS 命名色查表 / `#RGB` 展开 / 非法值跳过（曾输出 `w:val="RED"` 违反 ST_HexColor，Word 弹修复框）
- **DOCX 嵌套 span 颜色栈** — CSS 级联语义（内层闭合后外层颜色恢复）+ 数学 span 精确配对
- **嵌套 span 平衡提取** — toHtml/fromHtml 两处 `(.*?)</span>` 停在第一个闭合处，备份不平衡、结构逐轮退化。改 `findMatchingSpanClose` 平衡扫描
- **多位有序列表边界**（`10.` 与 `1.` 行为一致）、**表格规范化围栏感知**（围栏内 `|a|b|` 字节不变）、**无色 span 不再剥壳**（保留 font-family 等）
- **后台标签页通知污染当前页 UI** — headings/format/history 加当前 tab 守卫 + 切页主动重推
- **保存窗口脏标记竞态** — `_docVersion` 快照比对，保存期间再编辑不清脏
- 标签关闭记指针（防下标漂移关错页）、三导出器 exportError 接线、外部重载 dirty 警示、编辑器导航白名单（qrc/localimg/about）、openUrl/扩展名白名单、insertImage 路径转义、localimg 目录围栏（docDir 白名单注册）

### 优化 — 性能（P2）
- MarkdownHandler 六处热点单趟化/缓存：escapeUnclosedHtmlTags（每标签正则构造 + 全尾扫描 → indexOf 词边界）、ZZHI 终止判定、软换行还原、脚注定义展开（token 扫描 + 查表）、列表容器/等宽字体模式缓存
- 编辑器上报防抖（大纲 300ms/字数 200ms，onSelectionUpdate 去冗余上报）、源码区输入合批 80ms（曾每击键强制回流 + 全文正则）、会话恢复去 O(N²)（新 tab 单页应用配置）

### 加固 — 择要（P3）
- Math/Mermaid 离屏渲染器 30s 看门狗（挂死不再无声失联）；watcher 重复注册去重 + 关 tab 不误停同文件监控；customCss 剥 `</style>` 防注入；DOCX colgroup 列宽正则锚定；`_imagePending` 清理与死代码清理；vendored QtMarkdownWriter 上游缺陷记录入文档

### 修复 — 审查补充批次：往返保真（阶段 1、4）
- **空 alt 图片整条丢失（P0）** — Qt setMarkdown 对 `![](x)` 整条丢弃，图片「打开→保存」一次即从文档消失（独占行/段落内/引用内均复现）。toHtml 占位 alt 保形 + fromHtml 空标记哨兵（vendored writer 不再凭空写 `![image](x)`）；行内代码内字面 `![](x)` 暂存豁免
- **标题内强调保存退化（P1）** — Qt toMarkdown 不输出 `<hN>` 内的强调标记（`<p>` 内正常），`# a **c**` 保存即退化为 `# a c`。fromHtml 补 strong/em 占位保护；toHtml 兜底正则改平衡扫描（标题含多内联段时惰性组曾产悬空 `</span>` 畸形 HTML）
- **列表项内图片保存即丢（含任务列表/嵌套列表）** — ① fromHtml 列表序列化把 `<img>` 当无意义标签剥掉；② toHtml 产出的 `<li><img></li>` 被 TipTap 弹出列表、留下用户从未写的空条目——图片前补空 `<p>` 成稳定不动点；③ 含图条目其余内容并入单行（Qt 续行机制对块级内容不可用）。七种列表形态经真实 TipTap 链路字节往返验证
- **属性转义与实体解码（P1）** — 块级公式 data-latex / 围栏语言串 / 脚注 data-id 此前裸拼，值含 `"` 时属性提前终结（公式后半段丢失）；读取侧不做 HTML 实体解码，对齐列 `&` 每保存一轮多一层且不收敛（`&amp;` → `&amp;amp;`）。escapeHtmlAttr 统一转义 + 读取侧统一解码；历史已损坏文件载入即自愈。505 条往返矩阵改走真实实体路径（此前 C++ 直连漏掉整类「未转义/未解码」问题）
- **块引用嵌套结构保真（阶段 4 / P2-8）** — 引用内列表变裸列表（Qt 把 `<ul>` 弹出引用）、`> > a` 塌平、`> ---` 的 hr 逃出引用、`> ``` 围栏体丢 `>` 前缀（重开即逃出引用）。toHtml 复杂引用预抽取走字面 blockquote 通道；fromHtml 平衡配对替代惰性首闭 + 内容递归 + 占位符就地展开再加前缀

### 新增 — DOCX/PDF 脚注导出对齐 Word（阶段 3 + macOS Word 校验）
- **DOCX 原生脚注（P1-7）** — `[^id]` 引用导出为 Word 原生脚注（新增 word/footnotes.xml 部件 + 自动编号上标 + FootnoteText/FootnoteReference 样式），正文不再输出 `[^id]: ...` 字面段落；同一 id 多处引用复用同号；定义内 runs 经部件级关系清洗（跨部件悬挂 r:id 会被 Word 判「无法读取的内容」）
- **重复引用兼容 macOS Word** — Word 脚注「一脚注一锚点」，同一 w:id 的第二个 footnoteReference 触发修复框（4 变体 A/B 隔离实证）：首引用 = 原生标记 + 隐藏书签 `_RefFtn<N>`，重复引用 = NOTEREF 交叉引用域（显示同号、可点击跳转、随重编号更新）。同批修 CT_PPrBase/CT_RPr 序列三处违规（代码块 pPr、docDefaults rPr、Heading 样式）、悬空样式引用补定义、marker 双编号、容器空白泄漏；macOS Word 复验无弹窗
- **PDF 脚注随页浮动 + 分隔线** — paged 路径注入 `float:footnote`：文末容器定义移到首个引用旁，分页引擎搬入引用所在页页底脚注区（灰色分隔线 + 缩小字号，Word 同款）；重复引用不重复锚点（与 DOCX NOTEREF 同构）。编号与内容同行（marker 浮动版式注入，paged/legacy 共用）。修上游 paged.js 两缺陷：溢出拆分碎片被无条件 `text-align-last:justify` 逐字拉伸（注入 CSS 覆盖）、脚注续页 `clonePage` 不更新总页数（「第3页/共2页」→ 按真实页元素数覆写）；内容指纹拆维（正文剔除脚注 + 脚注字符多重集），重排/拆分不误报、真丢失仍拦截

### 优化 — 性能（阶段 2）
- **fromHtml 热点 O(K·n) 清零（P1-6）** — 剩余 18 处 `match(s,offset)` 全串扫描循环单遍化（globalMatch 单遍重建、span 单遍栈配对、表格两趟并一）；热点路径正则全部 static const。实测（-O2）：3000-span 9342ms→76ms（123×）、4000 行表格 16316ms→276ms、4000 行内公式 3738ms→64ms；新增 Release-only 基准测试（5-10 倍余量防数量级回归）

### 加固 — 阶段 5 择要
- PdfExporter customCss 剥 `</style>`（与 HtmlExporter 对称，防样式块提前终结注入任意标记）；localimg:// 目录白名单改引用计数（换文件撤销旧目录注册，长期会话授权面不随打开目录单调增长）；模态对话框恢复脏状态加 `_docVersion` 版本守卫（对话框期间置脏不再回退假干净）；MathMlConverter 自闭合经探针实证为误报（新增回归护栏固化）；新增 docs/html-attr-escape-and-entity-decode.md 专题文档

## [0.15.0] — 2026-09-13

### 新增
- **DOCX 导出页眉页脚与页码（Word 原生部件 + 活字段）** — 页眉部件为文档标题居中单段（9pt 灰 #808080），页脚「第 X 页 / 共 Y 页」右对齐，X/Y 为 PAGE/NUMPAGES 简单字段——Word 打开时按字段重算（活页码，续编辑自动更新）；sectPr 同步声明纸张尺寸与页边距（twips），使 DOCX 页与导出设置一致、图片上限与实际页宽匹配；设置页独立开关（默认开，与 PDF 的页眉页脚开关互不影响），标题取导出文件名基名、无标题防御性不产页眉部件；headerReference/footerReference 置于 pgSz 之前（CT_SectPr schema 顺序），部件关系与 [Content_Types] 完整声明
- **PDF 导出内联脚本语法校验测试** — buildRenderHtml 的 preparePrint 是几十段拼接的 JS 字符串，子串断言测不出语法错误：一处语法错即整块解析失败、`__printState` 永不置位、C++ 侧 30s 超时静默回退原生分页（产物丢页眉页脚且无报错指向真因）。新增 QJSEngine 包未调用函数体的纯语法检查（解析覆盖全串、顶层语句不执行、无 DOM 依赖）

### 修复
- **DOCX 导出超高图片/超长 mermaid 图被截断** — 嵌入图片此前只有宽度上限（clampToMaxWidth），纵向超长 mermaid（TB 数十节点）宽度远小于页宽而高度数倍于页高，Word 不对内联图片跨页排版、超出页高部分直接被裁。改为宽高联合等比缩放（clampToPageArea，取更紧约束、只收缩不放大），高度上限与 PDF 导出的 capH 同源（页高 − 上下边距 − 2mm 安全间隙）；mermaid 绘图、块级 `<img>`、行内 `<img>` 三个嵌入点统一生效，长竖版截图同受益
- **DOCX 导出 `<small>` 小号字与正文一样大** — parseInline 内联标签分支无 small（strong/em/u/s/code/mark/sub/sup 均有），穿透后文字继承 docDefaults 的 11pt；编辑器与 PDF 走浏览器 small 默认缩放（约 0.83×），仅 DOCX 丢效果。RunFormat 增 smallText 标志，toPrXml 输出 `w:sz=18`（9pt，对齐浏览器 smaller 比例）
- **PDF 导出超宽表格右溢截断、跨页列错位、短列被挤压** — 三个同源症状：① 长 URL 等无断行点 token 撑大列 min-content（auto 布局表格永不小于 min-content）→ 整表右溢页宽被 sheet（overflow:hidden）静默右裁：th/td 增 anywhere 断行（规则置于 @media print 之外——paged.js 屏幕分页测量与打印折行必须一致，否则测高分页高度失真）；② paged.js 跨页表格碎片是 cloneNode(false) 浅克隆（保 style 属性、丢 colgroup 子元素），各碎片 auto 布局按自身内容重算列宽 → 换页前后列不对齐；③ 直接锁定屏幕量取的 auto 布局列宽，短列（编号/状态）被长列 max-content 按比例挤到单字宽、「001」「已完成」逐字竖排折行。修复：`__fixTableColumns` 分页前预处理——两趟 intrinsic 测量（min-content 作保底、max-content 作期望；测量时内联打印字号/边距/字族，暂关 anywhere——拉丁整词/CJK 逐字才是自然断行单元）→「短列保真、长列吸收」分配（floor = min(期望, 3×保底, 等份额)，富余按 slack 比例摊给长列，极多列等比压缩）→ colgroup 重写为百分比固定列 + 每行单元格内联百分比宽 + table-layout:fixed（列几何与内容解耦且自描述在行上，碎片浅克隆也带走）；可用宽为 C++ 注入的页面内容宽 px（@page 宽 − 左右边距，横向取短边）
- **README License 修正** — MIT 残留声明改为 GPL-3.0（0.13.0 起项目已统一 GPL-3.0）

## [0.14.0] — 2026-09-07

### 新增
- **PDF 导出页眉页脚与页码（vendored paged.js 分页引擎）** — vendor paged.js 0.4.3（qrc 打包、npm dist 字节级核对），`PdfPageSpec` 注入 `@page` 规则：页眉文档标题（@top-center）、页脚页码「第 X 页 / 共 Y 页」（@bottom-right），设置对话框独立开关；页眉页脚全关时走原生打印管线（零注入，等价回归锁定）。双纸向与全纸型发显式毫米尺寸（Letter/Legal 等 non-A/B 纸的 DOM 页盒与打印纸一致，规避 paged.js 命名尺寸表大小写失配）；mermaid SVG 与普通图片页高容纳上限（超高图等比缩入内容区不拦腰截断、超宽图不静默右裁——横向导出丢页眉页脚根治）；内容双指纹自检（文本 + 图片 src 多重集），任何分页静默丢失/重复铺排即还原走原生打印（零丢失优先于页眉页脚）；引擎加载失败/30s 超时同样回退（状态栏明示「本次无页眉页脚」）；表格跨页断行修复（表头复现）
- **脚注五项增强** — ①定义支持块级内容：`footnoteDef` schema 升 `block+`（多段/列表/表格/代码块/任务列表/数学），`toHtml` 缩进续行捕获 + `fromHtml` 平衡扫描提取，定义内容复用主管线嵌套转换；②点击引用跳转定义、定义区返回箭头跳回（闪烁/抖动定位反馈）；③引用 hover 气泡预览定义前两块（非模态、顶端自动翻转）；④内联改名弹层替换原生 prompt（重名/非法字符校验、ESC 接线、Enter 确认）；⑤删除引用后级联清理孤儿定义与空容器（文件加载/外部重载不触发——手写孤儿定义是内容；源码模式编辑退出以进入前快照豁免文件固有孤儿；删多引用之一不清理；undo 同组恢复）
- **源码模式行号栏逐逻辑行显示** — 旧实现为 CSS counter 按顶级节点计数：代码块/多行引用整体记 1 行，pre/ul/ol/table 不显示行号，与状态栏可视行语义不一致；重做为逐逻辑行渲染（textblock 占 1 行、软折行不增行、代码块逐换行、hardBreak/brk 各 +1、表格按行、块级公式按 latex 可见行数），与状态栏行列同源，列宽随位数自适应
- **侧边栏面板状态跨会话记忆** — 面板显隐与分割比例 QSettings 持久化，启动经 `applyInitialPanes` 恢复（阻断信号链防比例污染）；首次启动默认只显示大纲
- **大文件打开守卫** — ≥2MB 非二进制文件软警告确认（会话恢复路径同守卫，拒绝则跳过该条目不开空标签）；>10MB 硬上限拒开；二进制（NUL 字节）拒读

### 修复
- **未闭合 `$$`/围栏致 toHtml 崩溃或丢内容** — 未闭合块级公式 `$$` 与未闭合 ```` ``` ```` 围栏均为「开启即入占位符列表、闭合才入备份列表」的 EOF 错配，`batchRestorePlaceholders` 越界 abort（源码模式输入 `$` 后退出即崩）；未闭合 `> ``` ` 不崩但已收集内容被静默丢弃（往返成 `&nbsp;`）。修复：EOF 收尾三段——`$$` 弹占位符按字面透传（ZZBR 保行结构）、```` ``` ```` 按闭合同款补备份（CommonMark 惯例到 EOF 均为代码块）、`> ``` ` 双补备份与占位符
- **脚注内表格保存丢失（含末尾乱码）** — `fromHtml` 表格规范化以 trimmed 基准整行重写丢弃前导缩进，定义内表格行落回第 0 列致定义断裂、内容逃逸为正文 pipe 字面文本；Qt `toMarkdown` 对全空单元格表格分隔行输出 0 连字符退化成非法 `|||`（正文空表格同受累，先于本批）。修复：行重写保留前导空白、`tableAligns`/`tableFullMergeRows` 第 0 列判别消费、缩进分隔行既有对齐标记保留、fromHtml 空单元格 ZZTC 注入（裸 `<td></td>` 与 TipTap `<td><p></p></td>` 双形态）
- **MarkdownHandler 大文档转换崩溃与卡顿** — ①占位符还原把全部占位符拼进一条 alternation 正则，数千分支超 PCRE2 编译上限 → 还原静默不执行（Debug 断言 abort / Release 占位符泄漏进正文）；改通用 token 模式单遍匹配 + 集合判断，未命中的用户字面 ZZ 文本原样保留（7 处）；②toHtml O(n²) 热点（泄漏检查逐占位符全串 contains 等，3MB/3.4 万行实测主线程阻塞 24s 致系统「应用无响应」）改单遍 globalMatch 扫候选 + 集合判断，24s → 1s

## [0.13.0] — 2026-08-30

### 新增
- **空行与多空格往返保真（字面 `&nbsp;` 可移植空行方案）** — WYSIWYG 空行以字面 `&nbsp;` 独占行表示（Joplin/MarkText 等跨编辑器兼容）：`toHtml` 把段落间连续空行（K≥2）保留 1 个作段落分隔、其余转 `<p></p>` 空段落占位符；`fromHtml` 把空段落逐行还原为字面 `&nbsp;` 行（1:1 无增删）；连续空格输入转为「1 普通空格 + (N-1) NBSP」防 Qt `toMarkdown` 折叠丢空格；brk 软换行紧邻处按 Enter 拆段先清理 brk 节点；列表项间空行以 ZZLB 注释占位强制拆分列表，往返保留
- **切换源码模式视口定位到光标所在标题** — 进入源码模式记录光标所在 heading 序号，在 MD 中按序号定位标题行（跳过代码块围栏内的行），复用测量式滚动把视口垂直居中到该标题；光标在正文段落时定位到所在章节标题
- **序列化去 80 列折行（vendor QtMarkdownWriter）** — Qt `toMarkdown()` 硬编码 80 列折行且无 API 关闭，中文稀疏空格段落被强制列位切断时吞掉断点字符（数据不可恢复）。vendor Qt 序列化器（行为保持版，`setWrapEnabled` 去折行开关）从源头逐块单行输出，吞字、列表/引用续行拆块、折行点 strong 闭合空格丢失一并根治，长脚注遗留折行同步修复；等价性回归测试 437 组字节对比锁定与上游行为一致
- **模式切换与首载性能优化** — 源码未编辑时退出在 JS 内复用内存文档完成切换（免 C++ toHtml 与 HTML 传输，不再误清撤销历史与脏标记）；keep-warm 布局（进源码仅隐藏编辑区，退出免全量重排）；大文档冷 JIT 空闲预热（首次切源码 ~1s → ~25ms）；会话恢复非活动标签页惰性加载；mermaid 可见优先渲染（IntersectionObserver 600px 预渲染带 + (主题,源码)→SVG LRU 缓存）；代码块高亮异步化（init 零高亮/空闲分批/增量重算，vendored lowlight plugin）。实测 684 行指南文档：启动累计 3289→1469ms、首载 setContent 969→457ms
- **往返保真矩阵回归体系** — 新增 `tests/test_roundtrip_matrix.cpp`（505 用例）：全元素 × 6 内容变体、两层嵌套全排、三层结构化矩阵；4 轮往返「收敛 + 双确认」判定、双轨语义守卫、哨兵泄漏/内容存活/累积探针不变量，作为后续往返改动的回归防线
- **项目采用 GPL-3.0 许可证** — vendor QTextMarkdownWriter（Qt Company，GPL-3.0 可选许可）后项目整体统一 GPL-3.0，无组合许可摩擦与 LGPL §4 重链接义务

### 修复
- **表格空单元格往返泄漏 ZZBL 占位符** — TipTap 把空 `<td></td>` 序列化为 `<td><p></p></td>`（单元格必须含段落节点），`fromHtml` 的空段落保护只跳过列表容器、不跳过表格容器，把单元格内空段落替换成 ZZBL 占位符后进入 `| … |` 行、还原时整行不等于占位符 → 泄漏（如 `ZZBL2ZZ` 出现在表格行）。修复：空段落保护同样跳过 table/td/th/tr/tbody 等表格容器，空单元格保留为 `| |`
- **源码字面 `&nbsp;` 行往返失真** — 无空行分隔时（`Line 1\n&nbsp;\nLine 2`）`&nbsp;` 行被 Qt `setMarkdown` 当同一段内软换行，往返后空行前带空格、多空行漂移。修复：`toHtml` 把独立成行的字面 `&nbsp;` 实体当空行生成占位符（还原为空段落），`&nbsp;` 行各自 1 空段、两侧纯空行不折叠；`isParagraphLine` 排除 `&nbsp;` 行避免相邻正文触发软换行标记；`emptyPRe` 显式纳入 NBSP 字符（Qt 正则 `\s` 实测不匹配 U+00A0），空段落统一还原为字面 `&nbsp;` 行
- **最后一行是标题时源码模式往返多出一行 `&nbsp;`** — 双层根因：① `exitSourceMode` 用 TipTap 原生 `commands.setContent`，末尾块为标题/表格时自动追加 `<p></p>`；② StarterKit 内置 trailingNode 扩展，任何 transaction 后末尾非可输入块都自动追加空段。修复：`exitSourceMode` 改用与 `loadFile` 一致的重建方式 `setContent`，并禁用 trailingNode（末尾标题内按 Enter 仍正常新建段落）
- **切回 WYSIWYG 大纲跳末尾但视口在开头** — 切回后光标被放到文档末尾（commands.setContent/trailingNode 追加），大纲高亮跟随到末尾而视口停在顶部。修复：进入源码模式记录光标位置，退出重建后恢复（`setTextSelection` 触发 `cursorChanged` 驱动大纲高亮，`scrollIntoView block:start` 与大纲点击跳转一致）
- **字面 `ZZ*` 占位符形态文本往返损坏** — 占位符碰撞检测此前仅覆盖 ZZCB/ZZBL/ZZNB 三类前缀，用户文档字面包含 `ZZSB0ZZ` 等形态文本时被还原阶段误替换。修复：碰撞检测扩展至全部前缀（同时检测源文与已产出内容，冲突跳号），还原按存储的实际占位符串精确回填；修复 ZZST 前缀双占用（删除线保护与孤立 `*` 保护同号互相覆盖致内容丢失）
- **强调包裹行内代码全链路往返丢失** — md4c 对「直接包裹行内代码的 `**`/`*`」整对丢弃（信息在 setMarkdown 层已丢）。修复：`toHtml` 后处理外科手术补包（MD/HTML 双侧候选求交 + span 数量内容核对，失败整体放弃）；后跟空白的孤立 `**` 归一为裸形式（首轮字节保真）；数千行内代码大文档的巨型占位符 alternation 超 PCRE2 编译限制崩溃，改通用模式 + QHash 两段查表；TipTap Code mark 放开 `excludes` 允许与强调共存
- **水平线三连修** — 正文段落后的 `---`（如 `3\n---\n4`）此前被误判 setext 二级标题（往返变 `## 3`），改按水平线处理（标题\n=== 仍为 setext 一级标题）；`<hr>`/`<hr/>`/`<HR>` 标签行与 `---` 同语义（此前被转义为字面 `\<hr\>`）；行内代码内标签（`` `<hr>` ``）不再双重转义显式为 `&lt;hr&gt;`
- **嵌套转换六处修复（矩阵抓获）** — 下标内容允许 `~~` 单元（`~**~~x~~**~` 不丢层）；粗斜体直包代码含紧贴中文 flanking 险恶形态（`***`x`***`）；ZZHI 嵌套字面 HTML 多趟还原（`==<sub>a<u>u</u>b</sub>==` 类）；ZZSB 标记感知转义（受支持标签/实体直通，字节保真）；mark/sup 备份内层 MD 等价标签换回 MD 语法（`==<s>删</s>高==` 不冻结为字面 HTML）；下标备份贴边/奇数 `~` run 精细转义
- **强调标记边界空格与句号吞字** — 折行在强调标记边界误补空格产生 `**结论** ：` 损坏，且该空格成为唯一折行点触发 Qt 折行吞字（断点处 `。` 被当空格吞掉）。修复：`fromHtml` 定向归一化「加粗/斜体闭合 + 空格 + 全角标点」为无空格从源头消除折行点（合法空格不受影响；折行本身已由 vendored writer 根治，此为纵深防御）
- **大文档往返性能** — `globalReplaceWith` 单遍重建、占位符集合判重、循环内固定模式正则提升为 static、多趟逆序还原：10k 行文档往返 ~28s → ~500ms
- **切标签页时状态栏行列未刷新** — 切回已加载标签页只触发格式同步无光标事件，行列停留旧值。修复：`syncFormatState` 主动上报当前 selection.head，同步状态栏行列与大纲高亮

### 变更
- **Windows 便携 zip 不再分发** — Release 仅保留安装器 exe / macOS dmg / Linux deb；NSIS 安装器已覆盖安装需求，zip 制品（139MB/版）是 Actions 制品存储配额（免费版 500MB）超限主因

## [0.12.4] — 2026-08-23

### 新增
- **状态栏文件编码显示** — 打开文件时经 FileManager 自动检测编码（BOM → UTF-8 校验 → 系统编码回退），状态栏新增「编码: …」标签显示 `UTF-8 BOM` / `UTF-16 LE/BE` / `UTF-8` / `GBK`（Windows 中文系统经 `GetACP` 映射）；编码逐标签页独立，切换标签、外部重载文件时同步刷新
- **双击关闭标签页** — 连接 `QTabBar::tabBarDoubleClicked` 复用现有关闭流程（脏文件先弹保存确认，不丢数据）
- **大纲光标自动高亮** — 编辑器光标移动时随 `cursorChanged` 额外发送 ProseMirror 文档位置 `pos`，OutlineTree 高亮「最后一个 `pos ≤ 光标位置`」的标题并滚入可见区；大纲重建后按最近光标位置恢复高亮。点击大纲由「仅滚动」改为同步 `setTextSelection` 移动光标，高亮随之跟随

### 修复
- **显式 LaTeX 环境（aligned/matrix/cases 等）往返丢失** — `toHtml` 对多行公式无条件包裹 `aligned`、`fromHtml` 无条件剥离，致用户手写的 `\begin{aligned}` 等被双重包裹或剥离。修复：`toHtml` 对含 `\begin{` 的显式环境不再包裹 `aligned`（仅折叠换行）；`fromHtml` 以「中间无 `&` 且无嵌套 `\begin{`」判别自动包裹的 `aligned` 与显式环境，后者逐字还原 `\begin{…}`/`\end{…}`/`\\` 行分隔；matrix/pmatrix/cases/aligned 逐字往返且二次往返幂等
- **删除线 `~~…~~` 往返丢失** — Qt `setMarkdown` 把 `~~x~~` 渲染为 `<span text-decoration:line-through>`，而 TipTap Strike 契约是 `<s>`，`fromHtml` 又无还原 → 往返退化为字面 `<span>`。修复：`toHtml` 后处理 line-through span → `<s>`；`fromHtml` 新增 `<s>`/`<del>`/`<strike>`/line-through span → `~~…~~` 占位符还原，`~~H~2~O~~`（删除线含下标）逐字往返
- **表格空单元格 `||` 往返被重定位** — Qt `setMarkdown` 把 `|1||3|` 的空单元格误解析并重定位到行尾。修复：`toHtml` 新增 `normalizeTableRowForQt`，仅当表格行含空单元格时用反引号/转义感知的 `splitTableCells` 重拼为 `| cell |`（行内代码内的 `||` 不误拆）
- **块引用多段落间空行丢失** — `>` 空行被 trimmed 过滤致两段引用被合并。修复：`isParagraphLine` 对「仅 `>`」返回 false，两段引用保持独立
- **UTF-8 BOM 未剥离与写回未保留** — 读入剥离 UTF-8/UTF-16 BOM（无 BOM 先按 UTF-8 校验、无效回退系统编码，解决中文 Windows GBK 文件被当 UTF-8 读乱码）；写回保留原文件 BOM 状态（新文件不带 BOM）

## [0.12.3] — 2026-08-23

### 新增
- **源码切换按钮图标与提示随模式互换** — 进入源码模式后按钮换为 WYSIWYG 显示器图标（屏幕 + 内容行 + 支架，明暗主题各一），悬停提示变为「切换为所见即所得模式」，返回普通模式恢复源码图标与原提示；图标路径同步注册到主题注册表（`m_iconPaths`），切换深浅色主题不回退

### 修复
- **表格列对齐（`:---:` / `---:`）往返丢失** — 交叉审查指出 `fromHtml()` 步骤 6 将分隔行统一为 `---`；实测定位为两层真因：① Qt `toHtml()` 把列对齐放在单元格内层 `<p align>` 上，而 TipTap 只解析 `<td>/<th>` 自身属性，加载即丢；② Qt `toMarkdown()` 从不输出对齐标记（恒 `-`），步骤 6 统一 `---` 时标记本就不存在。修复：`toHtml()` 按列聚合对齐提升到 `<td style="text-align:...">`（含表头行，编辑器内整列可见对齐）；`fromHtml()` 在 `setHtml` 前提取每列对齐存档、剥离表格内 text-align 声明，分隔行规范化时注回 `:---:` / `---:`（与 Qt 写出行为解耦，版本无关）；工具栏对齐按钮在单元格内改为更新 cell 的 `align` 属性（GFM 列对齐语义，可往返）。已验证：C++ 单测 131 通过 + jsdom 真实 TipTap 三段式端到端（`toHtml` → TipTap `getHTML` → `fromHtml`）对齐标记完整还原；`:---` 显式左对齐在 Qt 层与默认同形，往返后规范化为 `---`（语义等价）
- **表格单元格内用工具栏对齐会破坏表格结构** — 单元格内段落被 TextAlign 设置 `<p style="text-align:...">` 后，`fromHtml()` 的 ZZA 对齐保护将其包成 `<div align>` 占位符占位，还原后表格损坏为 `\| <div align="center">x</div> \|` 字面量嵌入单元格；现随列对齐修复一并解决（表格内 text-align 声明在 `setHtml` 前剥离，对齐走 cell 属性通道）
- **表格内工具栏改对齐只生效单个单元格，往返被旧值覆盖** — 首版联动用 `updateAttributes` 只更新光标所在单元格，列内其余单元格（尤其表头，加载时被整列写入对齐）保留旧值；保存侧"列内首个非空对齐"从表头行扫起，旧值获胜——把居中列改为左/右对齐，往返仍变回 `:---:`。修复：新增 `setTableColumnAlign()`，定位光标单元格列号后遍历表格各行统一 `setNodeMarkup` 该列全部单元格（含表头），与 GFM 列属性语义及加载侧整列提升对偶；jsdom 实测居中列→左对齐保存为 `---`、→右对齐保存为 `---:`

## [0.12.2] — 2026-08-22

### 修复
- **Windows 双击启动附带黑色空控制台窗口** — `add_executable` 缺 `WIN32` 标志，exe 按 PE 控制台子系统（Subsystem=3）链接，资源管理器双击启动时 Windows 为其附带一个 conhost 空窗口（历届 Windows 版均有，非 0.12.1 引入）；补上标志改链 GUI 子系统（WinMain 入口由 Qt6 入口点库自动提供，`main()` 无改动，Linux 构建不受影响），并在 `package-windows.ps1` 增加 PE 子系统断言（非 GUI 即打包失败，CI 与本地 VM 共用此脚本）防回归
- **双击代码块标签更改语言后页面跳回顶部** — 语言选择浮层打开期间编辑器失焦、原文档选择已丢失，关闭浮层后无参 `focus()` 把光标与滚动重置到文档头；改为显式 `setTextSelection` 放回该代码块内部并 `scrollIntoView` 后再交还焦点
- **代码块语言标签字号过小难辨认** — 11px 纯小写实际墨水高度仅约 6px（等宽小写 x-height 只有字号的 ~53%，`c`/`auto` 这类无升降部词显小主要源于此）；调整为 15px + 首字母大写撑起视觉高度
- **工具栏图标间距过大致 1920x1080@125% 缩放溢出屏幕** — 按钮 padding `7px 10px`→`4px 6px`、工具栏 spacing `6px`→`3px`、分隔符 margin `4px 6px`→`2px 4px`（图标 20×20 不变），工具栏需求宽度约 1585→1210 逻辑像素，1536px（1920@125%）下余 300px+；亮暗两主题同步

## [0.12.1] — 2026-08-22

### 修复
- **DOCX 导出触发 Word「发现无法读取的内容」修复框** — 两处 OOXML/OPC 违规（WPS/LibreOffice 宽容不报，Word 严格校验弹修复框，macOS Word 双击实测复现/修复）：① zip 包误带 ODF 式 mimetype 首条目，该部件无扩展名又未在 `[Content_Types].xml` 声明，属内容类型无法解析的部件（每个导出必带）；② 单元格水平对齐 `<w:jc>` 误写入 `<w:tcPr>`（CT_TcPr 无此子元素，对齐表格触发），移入单元格段落 `<w:pPr>`

## [0.12.0] — 2026-08-22

### 新增
- **Windows 本地 KVM 虚拟机构建链路** — 宿主一键搭建/驱动（`setup-windows-vm-host.sh`）+ guest 置备（MSVC/Qt/NSIS/ImageMagick，`provision-windows-vm.ps1`），本地即可产出与 CI 同源的便携 zip + NSIS 安装器；打包产物统一落 `dist/`；运维文档含踩坑速查表（`docs/windows-build-vm.md`）

### 修复
- **macOS 包在 macOS 15 (Sequoia) 及以下无法启动** — CI runner 为 macOS 26，未设 `CMAKE_OSX_DEPLOYMENT_TARGET` 时 Info.plist 的 `LSMinimumSystemVersion` 跟随 SDK 写成 26，旧系统弹「应用程序的这个版本不能与此版本的macOS配合使用」拒绝启动（v0.11.1 的 macOS 包因此从 release 移除）；显式设为 12.0（Qt 6.8 官方最低支持版本）
- **Windows 图标四角白底** — ImageMagick 的 `-background none` 是 setting 只作用于其后读取的图像，原参数顺序下 SVG 已用默认白色画布栅格化、四角 alpha 丢失，装出白边图标；调整为置于 SVG 之前

## [0.11.1] — 2026-08-21

### 修复
- **Windows/macOS 行内代码加载即丢失（跨平台字体差异）** — Qt 给行内代码设置的字体 `systemFont(FixedFont)` 族名平台相关（Linux="monospace"、Windows="Courier New"、macOS="Menlo"），`toHtml()`/`fromHtml()` 硬编码只认 `'monospace'`，Windows/macOS 上 monospace span → `<code>` 转换失效，code 语义在文件加载时即被 TipTap 丢弃（切源码模式显形：反引号丢失、内容被转义如 `\*\*A\*\*`）；改为 `monoSpanFamilyPattern()` 三大平台已知名 + 动态本平台 FixedFont 族名匹配，版本/平台无关（详见 `docs/cross-platform-monospace-font.md`）
- **表格单元格含行内代码时空格不规整** — 表格规范化不再因单元格含反引号而整行跳过，改为 `splitTableCells()` 反引号感知拆分（行内代码内的 `|` 与转义 `\|` 不作分隔符），含行内代码的表格行统一 `| cell |` 风格
- **移除语言切换问题排查遗留的调试输出** — `[Language]` 系列日志清理

## [0.11.0] — 2026-08-20

### 新增
- **Windows/macOS 跨平台 CI 自动打包** — 新增 GitHub Actions 三平台构建流水线；macOS 用 Qt 6.8.2 通用二进制（arm64+x86_64）经 macdeployqt 产出 dmg；Windows 用 MSVC + Qt 6.8.2 经 windeployqt 产出便携 zip + NSIS 安装器；应用图标由 CI 从 SVG 生成 .ico/.icns（不提交二进制）；CMakeLists 平台化（macOS 走 MACOSX_BUNDLE，CPack DEB 仅限 Linux）

### 修复
- **块引用内代码块与多项往返保真修复** — 块引用内代码围栏：BRMARKER 不再破坏围栏，代码内容与语言信息串保真；块引用重构：修复 `<p` 误匹配 `<pre` 拆碎代码块，连续 margin-left 块按含 `<pre>` 与否合并；空行边界：代码块后空行分隔的块引用经 ZZBQSEP 标记保留；一并修复段落软换行行尾空格、段落到块引用软换行、水平线 `---` 丢失、setext `===` 幽灵 `<br>`、表格分隔行 `---` 缩成 `-`、块级占位符尾随空格
- **行内代码内容含反引号时往返定界符错乱** — `fromHtml()` 序列化行内代码时写死单反引号，内容含反引号会提前闭合；改为按内容最长连续反引号串 +1 定界，首尾反引号补空格分隔（与代码块围栏同源）
- **语言持久化兜底与插入菜单文案统一** — LanguageManager 语言未变时也落盘，默认 zh_CN 不再依赖默认值兜底；插入菜单「插入公式」改为「公式」
- **单测 QSettings 重定向 NativeFormat** — LanguageManager/DocxExporter 测试走 NativeFormat，重定向到临时目录，避免单测改写用户 `~/.config/md-editor` 语言设置
- **跨平台 CI 构建修复** — macOS：pip PEP 668 加 `--break-system-packages`、macOS 26 SDK 移除 AGL 框架导致 Qt 6.8.2 链接失败（彻底移除 `FindWrapOpenGL.cmake` 的 AGL 探测块）、补装 qtpositioning；Windows：显式定位 choco 安装的 makensis、`makensis` 加 `/NOCD` 禁止切到 .nsi 目录、NSIS 卸载段改用保留名 `Uninstall`、版本号提取正则适配 `LANGUAGES` 子句

## [0.10.2] — 2026-08-19

### 变更
- **移除 Linux 专属依赖，迈向 Windows/macOS 跨平台** — 主题跟随系统由 QDBus/xdg-desktop-portal 改为 `QStyleHints::colorScheme`（Qt 6.5+，跨平台），删除 `Qt6::DBus` 依赖；DOCX 导出由调用外部 `zip` 命令改为内置 miniz 3.0.2 单文件库（堆内存归档，去除系统 `zip` 依赖）；文件管理器打开文件由 `xdg-open` 改为 `QDesktopServices::openUrl`

### 修复
- **DOCX mimetype 条目内容修正** — OPC 首条目 `mimetype` 的内容由带 `.main+xml` 后缀的错误值修正为 `application/vnd.openxmlformats-officedocument.wordprocessingml.document`（STORE 不压缩）
- **长列表项/段落被 80 列自动换行拆散（往返保真）** — Qt `QTextDocument::toMarkdown()` 硬编码 80 列换行，把含空格的长段落/列表项/引用块在词边界折成多行，导致有序列表项被拆成多条、段落被拆成多行（再次加载时被提升为 `<br>`）。`fromHtml()` 新增反换行后处理：先以占位符保护 `<br data-brk>` 软换行，再逐行拼回折行，仅保留真实块边界

## [0.10.1] — 2026-08-18

### 修复
- **DOCX 导出代码块字体过小** — 代码块字体由 9pt 调整为 11pt
- **文件管理器双击打开 .md 文件失效** — `.desktop` 增加 `%f` 占位符，`main.cpp` 解析命令行文件路径并优先打开，双击不再只新建空白文档
- **暗色模式新建/打开标签页首帧闪白** — 页面加载期 `QWebEnginePage` 清屏色与首帧 `<body>` 均提前设为暗色，消除先白后暗

## [0.10.0] — 2026-08-18

### 变更
- **构建/测试基准迁移至 Ubuntu 26.04 + Qt 6.10.2，最低版本要求提升到 Qt 6.8** — CI 从 `ubuntu-24.04`（Qt 6.4.2）升级到 `ubuntu-26.04`（Qt 6.10.2）；最低 Qt 由 6.4 提升到 6.8（`find_package` 与 DEB 运行时依赖同步收紧），6.4.x 停止支持；用官方 Qt 6.10.2 全量复跑 164 项非 WebEngine 测试通过，Markdown 往返转换行为不变（占位符保护版本无关，覆盖 6.8.2→6.10.2）

## [0.9.0] — 2026-08-18

### 新增
- **界面语言中英文运行时切换** — 新增 LanguageManager（QTranslator）组件，设置页切换语言后即时刷新菜单/工具栏/状态栏/侧边栏/搜索栏/右键菜单/关于等全部界面，以及 TipTap 编辑器内的弹窗、悬浮提示与占位符；全组件接入 `tr()`/`retranslateUi()`，`editor.js` 增加 I18N 字典与 `setLanguage`；语言偏好持久化（`appearance/language`，缺省中文）；emoji 分类名保持中文
- 主题「跟随系统」模式：经 xdg-desktop-portal 实时跟随系统深浅色
- 新增 SystemThemeWatcher 组件（`src/app/`），无 portal 环境自动降级亮色

### 修复
- 新建/恢复标签页编辑区不跟随当前主题（暗色下新 tab 是亮色）
- 设置对话框切换主题不联动 Web 编辑器

### 变更
- 未保存更改对话框按钮文案调整：`不保存` → `不保存并关闭`，`Don't Save` → `Close Without Saving`
- 对齐/上下标下拉菜单项图标左侧留白：图标不再紧贴菜单边缘

## [0.8.1] — 2026-08-16

### 新增

- **DOCX 数学公式原生导出（OMML）** — 行内/块级公式由「导出 `$...$` 源码文本」升级为 Word 原生公式：离屏 QWebEngine 用 KaTeX 渲染为 MathML（`MathMlRenderer`），再经 `MathMlConverter` 递归转换为 `<m:oMath>`（行内）/`<m:oMathPara>`（块级，居中），渲染失败回退源码文本
- **DOCX 导出图片** — 内嵌 PNG/JPEG/GIF（OOXML media 部件），svg/webp/bmp/tiff 及空文件回退占位；超宽图片按导出纸张/页边距等比缩小
- **DOCX 导出 mermaid 图** — 离屏渲染 SVG 并 canvas 栅格化 PNG 嵌入
- **PDF 导出图片/mermaid** — 图片 base64 内联，mermaid 离屏渲染异步打印

### 修复

- **DOCX mermaid 时序图箭头丢失** — Word/WPS 的 SVG 渲染器忽略 `<style>` 与 `<marker>`，改由 Chromium 栅格化为 PNG（箭头完整），不再挂 svgBlip
- **DOCX 任务列表退化为普通列表/复选框丢失** — 无序列表（bullet）+ ☐/☒ 前缀，复选框 run 显式指定 Segoe UI Symbol（Calibri/黑体无 U+2610/U+2612 字形）
- **PDF 任务列表 checkbox 与文字分行** — 任务列表用 flex 布局，checkbox 与文字同行
- **DOCX 默认字体不适中文** — 正文由 Calibri 改为西文 Arial + 中文黑体（对齐 PDF 的 Noto Sans SC 无衬线观感）
- **DOCX 行内公式 / emoji 范围 / 引用块 pPr 三缺陷**、**脚注往返保真**

### 变更

- **导出默认文件名** — 已命名文件取文件基名（去路径与后缀）+ 导出类型后缀，未命名用 `untitled.xxx`
- **工具栏上下标图标重绘** — 上标/下标图标改为从 MathJax 字形（x²/x₂）逐字节抽取的曲线（微加粗处理），替换原手绘线条图标，明暗两主题同步更新；下拉按钮默认图标由下标改为上标
- **行内代码图标重绘** — 替换为 Lucide code-xml 抽取路径（尖括号 + 斜杠），明暗两主题同步更新
- **清除样式图标重绘** — 替换为 Lucide eraser 抽取路径，并参照参考图按像素测量比例沿橡皮轴向法向压缩约 0.9（长宽比 1.41→1.57），明暗两主题同步更新

## [0.8.0] — 2026-08-15

本次为导出增强大版本：新增自包含 HTML 导出与自定义 CSS 样式，并清理项目记账。

### 新增

- **HTML 导出** — 文件/工具栏「导出 HTML」，产出单文件自包含 HTML：KaTeX/Mermaid/highlight.js 及图片全部 base64 内联，双击浏览器离线打开即完整渲染（数学公式/图表/代码高亮/图片），跟随当前明暗主题
- **自定义 CSS** — 设置「外观」新增自定义 CSS 编辑框，注入编辑器实时生效，并在 HTML/PDF 导出时一并应用（覆盖默认样式）
- **highlight.min.js 纳入 qrc** — 供 HTML 导出内联代码高亮

### 变更

- **文档记账清理** — CLAUDE.md「已知问题」移除已实现项（PDF 导出/搜索/数学公式）；README 移除已删除的自动保存；CHANGELOG 补录 DOCX 导出

## [0.7.0] — 2026-08-15

本次为往返保真 + 链接编辑增强大版本：Markdown 代码块围栏与内联 HTML 往返保真全面修复，链接插入/编辑统一为双字段对话框并新增双击编辑，语法指南大幅扩充。

### 新增

- **链接插入/编辑统一对话框** — 工具栏/菜单「插入链接」弃用原生 `prompt`，与双击/右键编辑链接共用 `_linkEditor` 双字段对话框（链接文字 + 链接地址）
  - 新增双击链接打开编辑对话框（此前仅右键菜单可编辑）
  - 光标处无选区也可插入：确认后在光标位置插入 `[文字](URL)`
  - 文字与 URL 均必填：为空时标红并聚焦、保持对话框打开，仅 Esc/取消/X 可关闭；输入时清除标红
- **Markdown 语法指南扩充** — 新增下标/上标、高亮、按键、小字、下划线、换行、内联样式、`<div>` 居中等「常用 HTML 标签」写法对照表

### 修复

- **代码块围栏往返三缺陷** — 嵌套围栏（````markdown 内嵌 ```cpp）往返后围栏长度退化为 ```；相邻代码块/块级公式占位符被合并粘连（```$）；行内代码内的 `~x~`/`^x^`/`<sub>`/`<div>` 被误转为下标/结构标签。均已修复并新增 6 个往返回归测试
- **ZZDV 守卫 `\x01` 占位符泄漏** — 行内代码正则跨行误配对，`\x01CODE` 占位符被吞进 div 备份后泄漏进 HTML（指南「常用 HTML 标签」区损坏）；正则禁止跨行配对并增加含占位符的 div 兜底跳过
- **只读文档搜索导航不聚焦** — 只读指南下点击下一个/上一个匹配，TipTap focus 命令与 ProseMirror 选区同步均因只读失效，选区不显示；改为 `view.focus()` 强制聚焦并主动滚动到匹配
- **Ctrl+M 插入公式不生效** — 菜单「插入公式」QAction 与独立 QShortcut 双重注册 Ctrl+M，Qt 报 "Ambiguous shortcut overload" 致两者皆不触发；移除冗余 QShortcut

### 变更

- **Markdown 语法指南优化** — 去除「标题」一节 #~###### 六个渲染预览（污染大纲目录树），二级标题统一编号（1. 标题 … 12. 快捷操作速查）
- **开发指南** — 新增原则 9「改资源/源码后必须重新编译」；新增代码块围栏往返机制专题文档

## [0.6.1] — 2026-08-15

小版本：工具栏布局优化（下拉折叠）与启动期大纲修复。

### 新增

- **工具栏对齐/上下标下拉折叠（WPS 风格）** — 左/中/右对齐三按钮、上标/下标两按钮分别折叠为带下拉菜单的单一按钮，净减 4 个图标位（1920×1080 不再铺满）
  - 菜单三段式（图标 + 名称 + 快捷键文字），当前项打勾；快捷键由 TipTap 原生实现，菜单仅显示
  - 按钮图标跟随光标状态切换（对齐方式 / 上下标类型），chevron 下拉箭头与「导出」按钮同规格
  - 居中/右对齐时对齐按钮高亮（左对齐为默认态不高亮）；光标处于上下标内容时上下标按钮高亮
  - 主题切换（亮/暗）与源码模式禁用均已适配

### 修复

- **启动时大纲为空** — `notifyHeadings` 在启动时序三处落空（init 先于 QWebChannel 就绪、就绪回调未补推、setContent 被 `_loading` 锁抑制且漏调），需点击编辑区才显示；现于 setContent 与 QWebChannel 就绪回调补推，任意时序启动即显示
- **暗色主题下对齐/上下标菜单项图标不换肤** — 菜单项 QAction 注册进 `m_iconPaths` 主题注册表

## [0.6.0] — 2026-08-14

本次为搜索增强大版本：用自写 TipTap 扩展 + ProseMirror plugin（`Decoration.inline` 高亮）替换 Chromium `findText`，查找/替换全面增强。

### 新增

- **搜索增强** — 正则 / 全词（Unicode 词边界）/ 大小写 toggle、匹配计数 `n/m`、上下导航（回绕）、替换与全部替换（正则反向引用 `$1/$2/$&`），统一走自写 ProseMirror `Decoration.inline` 高亮（替换 Chromium `findText`）
- **搜索 UI 重构** — SearchBar 两行折叠布局（Ctrl+F 仅查找 / Ctrl+H 展开替换行）、F3 / Shift+F3 导航、Enter / Shift+Enter 导航、150ms 去抖
- **编辑菜单搜索/替换项** — 菜单入口与快捷键统一由 QAction 携带
- **标签页搜索会话** — 切换标签页自动按当前查询词重新搜索；查询参数未变时恢复上次匹配位置（3/4 不回退 1/4）
- **JS 测试基建** — 接入 vitest + jsdom，`npm test` 覆盖 searchMatcher（16 用例）与 searchPlugin（11 用例），CI 运行 JS 测试

### 修复

- **全部替换位置漂移** — 改为从尾向前替换，不同长度替换不再错位
- **搜索栏信号悬空引用崩溃** — lambda 按引用捕获栈局部变量，输入触发信号时 core dump，改按值捕获
- **全词匹配中文失效** — CJK 不参与词边界判定，「异常路径」等中文短语全词搜索不再只剩标点边界一处
- **搜索导航焦点** — 点 ↓/↑ 后焦点移入编辑器，匹配处选区可见
- **搜索栏 ✕ 按钮显示不清** — 全局 QPushButton padding 挤压窄按钮，`#searchBar` 收紧 padding
- **Ctrl+F/Ctrl+H 歧义重载** — 删除与菜单 QAction 重复的 QShortcut
- 替换与查找的大小写/正则脱节（现共用同一 matcher）

## [0.5.0] — 2026-08-14

本次为图片功能大版本，集中落地粘贴/拖拽图片落盘与本地图片渲染，并修复块级图片的往返保真问题。

### 新增

- **粘贴/拖拽图片落盘** — 粘贴或拖拽图片时自动保存到文档同目录 `images/` 文件夹，markdown 中存为相对路径（`ImageManager` 落盘工具 + `EditorBridge.requestImageSave` 通信 + `MainWindow` 编排 + 占位图回调替换）
- **图片自定义 NodeView** — `attrs.src` 存储原始路径保往返保真，渲染时经 `_resolveImageSrc` 解析；新增 `localimg://` 自定义 URL scheme（`LocalImageSchemeHandler` 读盘返回字节），绕过 qrc 页面无法加载 `file://` 本地图片的限制
- **CI 运行测试** — GitHub Actions 开启 `BUILD_TESTS` 并运行 `ctest`

### 修复

- **块级图片粘在标题/正文上** — `fromHtml` 把 TipTap 裸顶层 `<img>` 包进 `<p>`，使 `QTextDocument` 正确识别为独立块（不再产出 `# 标题![image]`）
- **标题/正文后图片渲染多余空行** — `toHtml` 把 `<p><img/></p>` 解包为裸 `<img>`，消除 TipTap 提升块级图片后残留的空 `<p>`
- **标题→图片空行往返丢失** — 不再折叠标题与图片之间的空行，保留用户写入的块间空行（往返保真）
- **绝对路径图片无法显示** — `_resolveImageSrc` 补 `file://` 前缀
- **图片读取失败占位图残留** — `FileReader` 补 `onerror` 清理占位图

### 修改

- **DEB 包元数据** — maintainer 改为 remyxo，描述补充 HTML 标签 / DOCX

## [0.3.10] — 2026-08-12

### 新增

- **代码块语法高亮** — 基于 highlight.js 的 github/github-dark 主题，支持 35 种语言（shell、python、java、C/C++、JavaScript 等）
- **代码块语言标签** — 代码块左上角显示当前语言名，悬停可见
- **代码块语言切换** — 双击语言标签弹出搜索下拉框，可更换语言类型
- **代码块复制按钮** — 右上角复制图标，点击将代码复制到剪贴板，复制后显示 ✓ 反馈
- **亮暗主题自适应** — 代码块高亮主题随编辑器亮/暗主题自动切换
- **DOCX 导出** — 文件→导出→导出 DOCX，基于 QTextDocument 解析 + zip 打包生成 Word 文档
- **导出按钮下拉菜单** — 工具栏「导出」折叠为 PDF/DOCX 下拉（后扩展 HTML）

### 修改

- **超链接编辑统一** — 右键菜单「编辑超链接...」同时支持选中文字创建链接和编辑已有链接
- **源码模式逐标签页独立** — 源码模式状态从全局迁移至每个标签页独立管理

## [0.3.9] — 2026-08-11

### 新增

- **编辑超链接** — 右键链接菜单新增「编辑超链接」选项，弹出模态对话框可修改链接 URL 和文字，支持删除链接
- **源码模式状态栏提示** — 切换源码模式时状态栏行/列值右侧显示「源码模式」

### 修复

- **任务列表源码模式 `div` 泄漏** — 修复 divBlock parseHTML 捕获 taskItem 内部 `<div>` 导致源码模式出现 `<div></div>` 伪影
- **QTextDocument 链接蓝色污染** — 修复 toHtml 中 QTextDocument 给所有链接文字注入蓝色 `<span style="color:...">` 导致往返污染
- **colorSpan 误捕获字体样式** — colorSpan parseHTML 现仅匹配含 color/background-color 的 span，避免 QTextDocument 的 font-weight/font-style span 被误解析
- **粗体/斜体往返污染** — toHtml 将 QTextDocument 注入的纯字体 span 统一转为语义标签 `<strong>`/`<em>`
- **表格表头粗体残留** — toHtml 输出中表格首行不再含 font-weight span
- **转义等号 `\=` 被高亮误捕获** — 新增 extractEscapedEquals 保护转义等号免受 `==highlight==` 正则干扰
- **标题中 `div align` 字体污染防御** — fromHtml 侧新增防御步骤剥离残留的 heading font-span + strong
- **表格格式规范化** — fromHtml 统一输出 GFM 风格 `| cell |` 格式

## [0.3.8] — 2026-08-11

### 移除

- **阅读模式** — 移除用户可切换的阅读模式（`Ctrl+R` 菜单项、工具栏切换按钮、状态栏模式动态切换）。编辑模式已支持 Ctrl+点击链接在浏览器中打开，阅读模式不再必要。Markdown 语法参考帮助页仍保持永久只读。

### 新增

- **编辑模式 Ctrl+点击链接** — 按住 Ctrl（macOS: Cmd）点击链接在系统默认浏览器中打开 URL

## [0.3.7] — 2026-08-10

### 新增

- **HTML 标签支持** — 新增 `<kbd>`（键盘按键）、`<small>`（小字）内联标记和 `<div>`（块级容器，仅包裹内联内容）的 TipTap 自定义扩展，手写 HTML 解析、渲染和 MD↔HTML 往返保真

### 修复

- **sub/sup 原始 HTML 保护** — 修复 MD 文件中手写 `<sub>`/`<sup>` 标签被 QTextDocument 静默丢弃的问题，toHtml 新增 ZZHI 占位符保护，fromHtml 统一转为 Pandoc `~x~`/`^x^` 语法
- **mark 原始 HTML 保护** — 修复 MD 文件中手写 `<mark>` 标签被 QTextDocument 静默丢弃的问题，toHtml 新增 ZZHI 占位符保护，fromHtml 统一转为 `==text==` 语法
- **段落对齐往返保真** — 修复 WYSIWYG 中设置的左/居中/右对齐在源码模式/保存后丢失的问题，fromHtml 将 `text-align` CSS 转为 `<div align="...">` 容器确保双向往返
- **br 硬换行保护** — 修复 MD 文件中手写 `<br>` 标签被 QTextDocument 丢弃导致其后内容消失的问题，toHtml 新增 ZZHI 占位符保护

## [0.3.6] — 2026-08-10

### 新增

- **内联颜色样式** — 新增 TipTap `colorSpan` mark，`<span style="color:…">`（及背景色）在 WYSIWYG 中真正可渲染/编辑/往返；底层保留任意 style 字符串（UI 限色但保真），文字色与背景色可独立叠加不互相覆盖
- **工具栏颜色入口** — 「文字色」「背景色」两按钮，弹出调色板（12 预设色 + 无颜色 + 更多颜色 `QColorDialog`），光标位于带色文本时回显高亮当前色

### 变更

- **精简工具栏** — 移除「两端对齐」按钮（对齐本就往返有损、未持久化），保留左/居中/右

## [0.3.5] — 2026-08-09

### 修复

- **源码模式实时脏标记** — 修复源码模式（`Ctrl+\`）下编辑文本不实时标脏，直到切回 WYSIWYG 模式才更新；现绑定源码区 textarea 的 input 事件实时调用 `_markDirty`，对齐 WYSIWYG 的 onUpdate 行为，退出时解绑避免残留引用
- **公式与表格间空行膨胀** — 修复多行块级公式 `$...$` 与表格/段落间原本 1 行空行，保存往返（HTML→MD）后膨胀为 3 行；`fromHtml` 现于占位符还原【之前】压缩 md4c 块边界多余空行（`\n{3,}`→`\n\n`）并移除块级公式备份的尾换行
- **Mermaid 对话框预览消失** — 修复双击 mermaid 图弹出的编辑对话框下方预览图为空（`updatePreview` 调用了从未定义的 `_mermaidRenderSafe` 同步抛 `ReferenceError`）；改回 `window.mermaid.render`，与编辑区内联渲染 `paint()` 一致

## [0.3.4] — 2026-08-09

### 新增

- **Markdown 源码切换** — 源码模式（`Ctrl+\`）由 HTML 视图改为真正的 Markdown+HTML 混合视图；可转 MD 的格式（粗体、上下标、高亮等）输出 MD 语法，无法用 MD 表达的（文字颜色 `<span style>`、下划线 `<u>`）保留原始 HTML，双向无损往返
- **源码切换异步编排** — `onToggleSourceMode` 由 C++ 侧 `MarkdownHandler` 参与转换，异步回调编排进入/退出流程，防重入锁抵御快速双击
- **工具栏源码模式态** — `ToolBar::setSourceMode` 在源码模式下灰化所有编辑按钮（保留源码/阅读/PDF 可用）

### 修复

- **标题源码切换多余转义** — 修复二级标题 `## 1. 这是标题2` 切换源码后多出反斜杠（`## 1\. xxx`）；去除 md4c 对 ATX 标题行首列表标记（`数字.`/`数字)`/`-`/`+`）的多余转义，同时保留 `*`（涉强调语义）与正文转义（去掉 `1\.` 会被解析为有序列表）
- **源码模式往返数据丢失** — 修复 `<span style>`/`<u>` 内部的下标、脚注引用在源码往返中丢失，调整提取/还原顺序（先下标/脚注，后 HTML 内联标签）
- **跨标签页内容清空** — 修复切换标签页时错误标签页内容被清空（窗口级源码模式状态在 `onTabChanged` 重置）

## [0.3.3] — 2026-08-09

### 新增

- **脚注支持** — TipTap 脚注节点（reference/container/def），工具栏/插入菜单入口，`insertFootnote`/`renameFootnote` 命令
- **脚注 MD↔HTML 往返** — `MarkdownHandler` 支持 `[^id]` 脚注语法转换与往返，保存/加载不丢失

### 修复

- **脚注内上下标数据丢失** — 修复脚注定义内容中下标/上标往返数据丢失，调整后处理还原顺序

## [0.3.2] — 2026-08-09

### 新增

- **Emoji 选择器** — 浮层面板（分类+搜索+最近使用，非模态），工具栏/插入菜单入口，按使用频率统计常用区
- **Emoji 图标资源** — light/dark 主题 emoji 工具栏按钮图标

### 修复

- **Emoji 选择器布局** — 修复搜索框与常用区被压缩不可见、顶部定位溢出导致搜索行被裁剪
- **Emoji 选择器键盘导航** — 修复 ArrowUp 在项数少于列数时高亮丢失

### 优化

- **工具栏图标重绘** — 行内代码图标 `</>`、代码块图标 `{}`（SVG 圆弧路径）、重画删除线图标
- **Mermaid 控制台消息过滤** — C++ 侧屏蔽 Mermaid 负 rect 报错日志

## [0.3.1] — 2026-08-08

### 修复

- **Mermaid 渲染** — 修复 CJK 字符宽度偏差导致 rect 负宽度报错（三层防线加固）、inline 渲染字体过大（SVG width=100% 等比放大根因）、甘特图缩块（viewBox 锚定修复）
- **Mermaid 对话框** — 加遮罩点击关闭、遮罩阻断滚轮穿透、大图预览限高不再出屏
- **任务列表往返** — 修复任务列表保存/加载丢失与列表后段落被吞并
- **文件监控** — 修复替换式外部保存后监控丢失、一次外部保存产生多个事件导致重复弹窗

### 变更

- **模态对话框 ESC 关闭** — 改为 C++ Bridge 通知机制，统一分流处理

## [0.3.0] — 2026-08-08

### 新增

- **Mermaid 图表** — 在编辑器内嵌 Mermaid 流程图/时序图等，双击弹出源码编辑器（实时预览、Ctrl+Enter 确认），vendored `mermaid.min.js`（同 KaTeX 模式），主题切换自动重绘
- **上下标** — Pandoc 语法 `~x~`（下标）/ `^x^`（上标）输入与往返（如 `H~2~O`、`2^10^`），工具栏新增下标/上标按钮
- **文字对齐** — 左/居中/右/两端对齐四按钮互斥，快捷键 Ctrl+Shift+L/E/R/J（注：对齐信息在保存为 .md 时丢失，标准 Markdown 无对应语法）
- **Emoji 短代码** — 输入 `:shortcode:` 自动替换为 Unicode 字符（如 `:smile:` → 😄），内建常用短代码映射，无需额外依赖

### 修复

- **代码块语言标记往返** — `fromHtml` 现正确保留 `language-xxx` 类，围栏代码块语言标记不再丢失（同时锁定 Mermaid 源码往返契约）
- **代码实体反转义** — 修复代码块/行内代码中 `<`、`>`、`&` 被 HTML 实体化后无法还原的潜在 bug（原绕过 QTextDocument 实体解码）

## [0.2.1] — 2026-08-08

### 新增

- **撤销/重做工具栏** — Undo/Redo 按钮，TipTap 状态实时同步启停
- **文件可靠性** — 外部变更检测弹窗、最近文件列表（最多 10 条）、拖拽 .md 到窗口打开
- **自写入抑制** — `FileManager::writeFile` 临时取消文件监控，防止自身写入误报外部修改

### 修复

- **脏状态误报** — 修复点击空白区域即标脏的问题（`init()` 加 `_loading` 包裹 + `onUpdate` 加 `transaction.docChanged` 守卫）
- **行号默认开启** — 修正 `lineNumbers` 默认值为 `false`

### 优化

- **重构 editor.js** — 消除 `init()`/`setContent()` 重复代码，提取 6 个辅助方法
- **删除自动保存** — 移除未完整接线的自动保存功能及相关设置 UI
- **状态栏布局** — 编辑模式标签移至行列右侧
- **代码清理** — 删除 `MenuBar::m_formatMenu` 死成员，补充 FileManager 测试 3 个用例

## [0.2.0] — 2026-08-07

### 新增

- **数学公式（LaTeX）支持** — KaTeX + TipTap Mathematics 扩展，支持行内 `$...$` 和块级 `$$...$$`
- **数学公式所见即所得编辑** — 双击公式弹出编辑面板，实时 KaTeX 预览，确认更新
- **插入数学公式** — `Ctrl+M` 行内 / `Ctrl+Shift+M` 块级，工具栏 ∑ 按钮
- **Markdown 语法参考** — 帮助菜单打开只读标签页，含完整数学公式 LaTeX 语法指南
- **PDF 导出页边距自定义** — 设置对话框支持上/下/左/右页边距（厘米）
- **PDF 导出方向选择** — 图标按钮切换纵向/横向
- **字数统计** — 状态栏显示字符数/单词数
- **阅读模式** — `Ctrl+R` 切换，链接可点击跳转
- **工具栏阅读模式按钮** — 一键切换编辑/阅读
- **插入菜单** — 新增行内代码、代码块、插入公式菜单项

### 修复

- 数学公式在保存/另存为时不丢失（`fromHtml` 正则属性顺序无关）
- 数学公式在导出 PDF 时正确渲染（内联 KaTeX CSS/JS）
- 块级公式编辑器与 PDF 导出均居中显示
- 块级公式前多余空行移除
- 多行块级公式 aligned 环境处理
- 加载文件后 Ctrl+Z 撤销历史清零
- PDF 导出样式优化（行距、字号、表格间距）
- 大纲跳转使用 scrollIntoView
- 阅读模式不触发脏标记
- 设置对话框纸张方向按钮选中态可见（QPushButton:checked 样式）
- 主题切换后阅读模式状态保持
- 数学公式编辑面板取消不产生脏标记

## [0.1.1] — 2026-07-28

### 新增

- 基本 Markdown 编辑（TipTap + ProseMirror）
- 文件树 + 大纲侧边栏
- 亮色/暗色主题切换
- 自动保存（定时器）
- 源码模式切换
- 表格编辑（插入行列、删除）
- PDF 导出（QWebEngineView 渲染）
- 搜索栏查找/替换/全部替换
- 会话恢复（窗口位置、打开文件）
- 中文右键上下文菜单
- 设置对话框（通用/编辑器/外观/导出）

### 修复

- 初始版本，无已知修复

## [0.1.0] — 2026-07-25

### 新增

- 项目初始化，基本框架搭建
