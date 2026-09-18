# Changelog

本项目遵循 [Keep a Changelog](https://keepachangelog.com/zh-CN/1.0.0/) 规范，版本号遵循 [语义化版本](https://semver.org/lang/zh-CN/)。

## [0.16.1] — 2026-09-17

### 修复 — macOS
- **「打开方式」推荐列表灰色** — CFBundleDocumentTypes 此前只按扩展名声明（CFBundleTypeExtensions），现代 macOS 推荐列表按 UTI 匹配，md-editor 被灰掉、需切「全部应用」才能选。补 `LSItemContentTypes: net.daringfireball.markdown`（系统 UTI，Typora/MacDown 同款）；删除与系统争抢扩展名的自定义 UTI（`com.md-editor.markdown`）；CFBundleTypeExtensions 保留作老系统兜底；打包脚本注入后加 PlistBuddy 断言
- **双击 .md 打开后窗口空白** — macOS 上文件路径经 Apple Event → QFileOpenEvent 到达而非 argv，此前无人处理。Application 捕获 FileOpen 发 `fileOpenRequested`，MainWindow 复用 `onFileDoubleClicked` 打开（冷启动与运行中再打开均覆盖）；新增 test_application 回归
- **启动期切标签页的 JS 未捕获异常** — `onTabChanged` 的三连推送（notifyHeadings/Format/History）无 `window.editorApi` 守卫，页面脚本未执行完的窗口期触发（macOS 冷启动尤甚），且因同段执行连带跳过后两条推送；补守卫与相邻代码一致
- **同路径文件重开不再开重复标签** — 双击/Finder 打开已打开的文件改为聚焦既有标签页（与 VS Code/Typora 等主流编辑器一致）：重复标签各自保存是后写者静默覆盖，未保存修改会静默丢失；未保存修改与外部重载警示原样保留。canonical 路径判定（相对路径/符号链接/冗余分隔符归一），新增 test_editorpath 回归
- **macOS 元数据触碰误报「已被外部程序修改」** — Finder 再次双击已打开文件时，Gatekeeper/Spotlight 的元数据触碰（xattr/atime）也触发 fileChanged，内容未变却弹重载警告。外部变更报警前先核实 size+mtime 与监控基线一致与否（与 VS Code 同款判定），未变则静默吞掉；新增 testMetadataOnlyEventSuppressed 回归
- **DMG 拖拽安装布局** — 安装窗口改为 app 图标（左）+ Applications 替身（右）+ 渐变背景箭头提示，杜绝用户直接在挂载卷内双击运行 app（Launch Services 僵尸注册的标准来源，见下方升级提示）；背景图为打包期零依赖生成（`gen-dmg-background.py`），新增 test_dmgbg 回归
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
