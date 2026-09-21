# PDF 页眉页脚版式：Chromium 打印约束与两路对齐

本文记录 PDF 导出页眉页脚版式的立论依据（2026-09-21 版式重做）。修改
`PdfExporter` 的版式常量、`__paginateNativeHf` 分页器或 `__fixTableColumns`
列宽归一时**务必先读本文**。

## 两条分页路径

| | nativeHf（原生快路径） | paged（paged.js） |
|---|---|---|
| 触发条件 | 页眉/页脚启用且正文无脚注引用 | 含脚注引用（`float:footnote` 随页浮动是 paged 独有） |
| 分页者 | Chromium 原生分页 + 本仓轻量切页 | paged.js 逐页 DOM 手术 |
| 页眉 | 包装表 thead 重复带（Chromium 打印每页重复表头） | `@top-center` 页边距盒 |
| 页脚 | abs 覆盖层（每页一片，片高 = 打印片段高） | `@bottom-right` 页边距盒 |
| 耗时 | ~0.5s/份 | ~0.8s/页 |

两路**必须产出同版式**（同一文档开/不开脚注不该换脸），故版式常量以 paged.js
实测落点为基准反向标定。

## 硬约束：Chromium 打印只能画在可打印区内（探针逐条实证）

独立探针程序（QWebEngineView + `printToPdf`）结论：

1. **可打印区 = 纸张 − Qt `QPageLayout` 边距**。Qt 对恰为 0 的边距不按 0 采用
   而套 ~18mm 默认值，故取 0.1mm 微量值（见 `pagedPrintMargins`）。
2. **越界绘制全是幽灵文本**：内容流矩阵在、像素无、QPdf bounds 全零。
3. **起始越界的 abs 盒被推到次页顶**（不画边距）——页脚不能靠负偏移画进下边距。
4. **`@page{margin}` 被 Qt 布局边距覆盖**（Qt 显式给边距时 CSS `@page` 边距不生效）。
5. **`tfoot` 不逐页重复**（`thead` 重复）——无法用表尾做逐页底部保留。
6. **`position:fixed` 逐页重复但同样只在可打印区内**。
7. **`break-before:page` 在 `<tr>`/`<tbody>` 上有效**（实测 8+6+4 行无丢行）。

⇒ 结论：**逐页底部保留（下边距）在 CSS 里没有机制**，页脚要落在纸底带，只能
让可打印区覆盖整张纸（Qt 边距 0.1mm），再用**流内结构**模拟页边距。

## 新版式（nativeHf）

```
纸顶 0
├─ 页眉带（包装表 thead，高 = 用户上边距）——文字底对齐压向正文顶线
│   ↳ 每页重复；页眉关时留空带（正文顶不随开关变）
├─ 正文（每页 ≤ 版心高 = 纸高 − 上下边距）
│   ↳ 由 __paginateNativeHf 逐页切：给页首块打 break-before:page
├─ 下边距带（版心底 → 纸底）——页脚覆盖层行盒顶贴版心底 + 净空
└─ 纸底
```

**页眉页脚贴页边距（Word 式，2026-09-21 定案）**：页眉压在正文顶线**之上**、
页脚紧贴正文底线**之下**，各留 `kHfHugGapMm = 3mm` 净空——位置**由用户在
「设置/导出」里的页边距推导**，不再写死"距纸底 8mm"（那是照 paged.js 居中
落点硬编码的：页边距带中央居中会让墨迹离纸边仅 ~8mm，而多数打印机不可打印
边距 4-6mm，余量太薄）。默认 18mm 页边距下：页眉墨迹 11.1~13.6mm、页脚墨迹
距纸底 ~10.9mm。

| 页边距 | 正文顶线 | 页眉墨迹底 | 正文底线 | 页脚墨迹顶 |
|---|---|---|---|---|
| 12mm | 12.0mm | 7.5mm | 285.0mm | 288.9mm |
| 18mm | 18.0mm | 13.6mm | 279.0mm | 282.9mm |
| 25mm | 25.0mm | 20.5mm | 272.0mm | 275.9mm |

（三种页边距下两路实测落点一致，差 ≤0.2mm。页边距越小页脚离纸边越近——那
是用户自己的边距选择，页眉页脚始终在其边距带内、不与正文相压。）

两路落点公式同式：行盒顶 = 正文底线 + `kHfHugGapMm`。nativeHf 侧是覆盖层
span 的 `top: calc(100% − (下边距 − kHfHugGapMm))`；paged 侧是
`@top-center{vertical-align:bottom;padding-bottom:3mm}` /
`@bottom-right{vertical-align:top;padding-top:3mm}`（paged.js 把页边距盒里的
vertical-align 翻译成 align-items，bottom→flex-end 即"贴向正文一侧"）。

- Qt 打印边距 = 0.1mm（`nativeHfPrintMargins`，打印区覆盖整张纸）
- 左右页边距 = `#hfcell` 的 padding；包装表 `table-layout:fixed` 截住 min-content 撑爆
- 页眉带内文字 `vertical-align:bottom` + `padding-bottom = kHfHugGapMm`（页边距
  过小时先保净空、必要时让文字占满整带）
- 页脚偏移 = 下边距 − `kHfHugGapMm`（行盒顶贴正文底线 + 净空；行盒内字形上偏
  ~1.4mm，视觉净空约 4.4mm）
- 页脚右缘退右页边距，与正文右边界对齐

## 轻量分页（`__paginateNativeHf`）

1. 收集**原子单元**：顶层块；表格按 `tbody` 行（续页扣重复表头高）、列表按 `li`、
   引文下钻、代码块按 `__splitPreLines` 拆出的行块；其余整块（mermaid SVG/图片由
   `max-height` 上限保证不超版心）。
2. 贪心切页：`块顶相对当前页顶 + 块高 > 页内容高` ⇒ 给该块打 `break-before:page`；
   已在页首的块不再打（重复 break-before 会插白页）。
3. 页内容高 = 纸高 − 上下边距（公称纸高，px 由 C++ 经 `window.__hfPageContentPx`
   下发）。实际可打印区比预算多 ~17mm，故预算内的块必装得下、断页恒由本函数决定，
   与自然分页无竞争。

**测量前提（关键）**：分页测量在**屏幕媒体**里做，屏幕布局必须 = 打印布局——

- 渲染视图 resize 到打印区宽（隐藏控件默认 640×480 会窄 30%，块高系统性偏大）；
- 测量期临时 `documentElement.style.overflow='hidden'`（滚动条会吃掉 ~15-30px 视口宽），
  测完即清（绝不带进打印媒体：`html{overflow:hidden}` 在打印下会裁掉整份文档）；
- 基础文档 CSS 对 nativeHf**不包** `@media print`（paged 路径由 paged.js 的
  PrintMedia 处理器解包，legacy 路径包着即可）。

## 成品防线（失守即回退 paged.js，绝不交错稿）

- `tall > 0`（单块超页高，切页器搬不动）：C++ 侧经轮询读到即提前回退，省一遍白印；
- 逐页校验页码「第 k 页/共 n 页」（色锚定 + 幽灵文本防线，见既有实现）；
- 新增：正文墨迹落入「正文底线 +0.5mm ~ 页脚行盒顶」的净空带 ⇒ 回退
  （该带上限随 `kHfHugGapMm` 与用户下边距推导，与页脚落位同源）。

## 折行一致性（长串/表格列宽的共同前提）

`overflow-wrap:anywhere` 必须在**文档级**（`body`）声明，不能只挂在 `th,td` 上：
nativeHf 的正文整体包在包装表单元格里，`th,td` 的规则会被全篇继承；paged 路径
没有这层包装，同样的规则不生效——同一份文档的「连续无空格长串」在快路径折行、
在 paged 路径横向溢出被页盒 `overflow:hidden` 静默裁掉（实测 2.3 节超长列表项
墨迹直达纸边 209.8mm vs 正文右界 191.9mm）。取 `anywhere` 而非 `break-word`：
前者同时收敛 min-content，避免长串撑爆文档 min-content 触发 shrink-to-fit 全局缩印。

同一规则是**三处产物的共同前提**——HTML 导出（`HtmlExporter::bodyCss` 两套主题的
`body` 规则）漏掉它时，「连续无空格长串」在两条 PDF 路径都折行、唯独 HTML 产物
横向溢出被裁（2026-09-21 用户实测 2.3 节前两个列表项，`li` 内
`scrollWidth>clientWidth`）。改折行规则须三处同查。

`anywhere` 与 `break-word` 的差别不止长串折行：前者收敛 min-content，**宽表会
随之压缩进正文宽**（与 PDF 同款观感，实测 12 列宽表每列 ~10.7mm、列头折两行），
后者宽表照旧横向滚动、与 PDF 分叉——HTML 导出取 `anywhere` 即为此。

## 表格列宽归一（三处一致的前提）

`__fixTableColumns` 的 intrinsic 两趟测量（min-content 保底 + max-content 期望 →
「短列保真、长列吸收」分配）必须在**与打印同宽**的屏幕环境里跑，否则分配随视口漂移。

**2026-09-21 起提为三处共用**：实现落在 `HtmlExporter::tableColumnNormalizeScript()`
（定义 `window.__normalizeTableColumns(cfg)`），PDF 侧经 `__fixTableColumns` 兼容层
调用（函数名与调用点不变，测量样式传打印基准 10pt / 4px 8px / 固定字族），HTML
导出在 `load` 后调用 `{toTable:true,slack:2}`。HTML 侧三处专属差异：

- **可用宽取每表自身盒宽**（`display:block` 时即容器内容宽）：`body.clientWidth`
  含 body 左右内边距，实测用宽 64px ⇒ 分配整体缩水（状态列 69px < 所需 74px，
  「已完成」仍一字一行）；
- **`toTable`**：把表格由 `display:block`（宽表横向滚动的无 JS 兜底）切回 `table`，
  之后 `table-layout:fixed` 才有意义；须先量盒宽再切，顺序反了拿到的是收缩宽；
- **`slack=2px`**：列宽走百分比再乘回表宽，小数布局与边框折叠会吃掉零点几像素，
  卡在 max-content 边界的短列仍折行（实测状态列 74.69px vs 48+26=74px 差 0.3px、
  编号列 58.70 vs 32+26=58 差 0.1px）。PDF **不传** slack：其 10pt 基准余量较宽、
  该文档实测无边界折行，传 2px 会令其 11→10 页（已验证产物不必要的变动）。

**坑**：遍历 `querySelectorAll('table')` 时必须跳过 nativeHf 的包装表 `table.hf`——
它的 `querySelectorAll('td,th')/('tr')` 覆盖全文档，会给每个单元格写下「本表格列宽」
（单列包装表即 100%），污染随后所有内层表格的测量（实测：短列被压成逐字竖排）。
修复前后两路径列宽/行距/页数完全一致。

## 验证方法（改版式后必做）

1. 无 GUI 工装：把 `PdfExporter.cpp` 直接链进一个小程序，喂 MD/HTML，导出 PDF
   （见提交说明的 harness 做法）；
2. 像素测量：`pdftoppm -r 300` + 逐行墨迹带分析，读页眉墨迹顶、页脚距纸底、
   逐页内容高（`pdftohtml`/`pdftotext -bbox` 读文本坐标）；
3. 两路对照：同一文档分别走 nativeHf 与 paged（临时注入脚注引用可强制 paged），
   比对页数、页眉/页脚落点、表格行距与列位置；
4. 组合覆盖：纵向/横向、仅页眉/仅页脚/仅正文、含脚注；
5. 轮询自述：`window.__hfPagInfo`（breaks/units/tall/断点元素）经 JS 控制台输出，
   与成品 PDF 的页首元素逐一核对。

## 相关文件

- `src/core/PdfExporter.cpp` — 版式常量、`__paginateNativeHf`、`__fixTableColumns`、
  成品校验与回退链
- `tests/test_pdfexporter.cpp` — 版式锁形断言（改常量须同步）
- paged 侧版式基准：`pageCss` 里的 `@top-center` / `@bottom-right` 规则
