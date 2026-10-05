# editorApi 接口清单（C++↔JS 收口，W4-5）

`window.editorApi` 是 C++ 经 `runJavaScript` 驱动编辑器的唯一命名空间。本文是它的全量清单：成员枚举、调用通道、公共方法签名与调用方、内部成员分类、私有直调裁定与白名单例外。

行号为 W4-5 收口提交时的实取值（editor.js 以当前提交为准，后续改动会漂移；方法名与签名是稳定参照，行号仅供定位）。

## 成员总账

| 项 | 数量 | 说明 |
|----|------|------|
| editorApi 成员总数 | 185 | editor.js 的 `window.editorApi = { … }` 对象字面量（起于 1281 行附近） |
| 公共成员 | 86 | 83 个原有 + W4-5 新增/转正 3 个（`closeModalDialogs`/`syncSourceViewContent`/`onImageSaveResult`） |
| 下划线私有成员 | 99 | 状态字段、内部辅助、7 个模态面板对象 |
| 其中被 C++ 生产代码调用的公共方法 | 74 | 其余 12 个公共成员仅 JS 内部消费（见下文） |

**总量对账**（回应评审「~64 处 runJavaScript / ~60 入口」口径）：

| 口径 | 数 | 说明 |
|------|----|------|
| `grep -rn runJavaScript src/`（含 .h、注释与 editor.js 一处提及） | 51 | 本轮 HEAD 实测 |
| 仅 .cpp（含注释） | 46 | 排除头文件 |
| 真实调用点（.cpp 内可执行语句） | 32 | EditorTab 12 / MainWindow 5 / SelfTestRunner 4 / PdfExporter 5 / MermaidRenderer 2 / MathMlRenderer 1 / TableColumnNormalizer 1 / ExportHtmlAsync 2 |

**口径对齐说明**：评审「~64」出自 01-发现综合 A-4，其原生口径是 review-ui-layer §1.3 的**混合统计**（「MW 约 50 处调用点」含经 `runEditorCommand`/`runJsCommand` 的间接转发 + 「MW/ET 直接 23 处」）——与「grep runJavaScript」口径不可直接对表。同口径（`git grep -o runJavaScript <ref> -- src/`）历史实测：评审时点 v0.20.0 = **43**、W4 基点 7a92764 = **48**、本轮 HEAD = **51**，W1–W4 同口径净增 +8（SelfTestRunner 成型、ExportHtmlAsync/ExportCoordinator 导出链、隐藏页导出器等，多在 W4 基点前已入）。另外 **PdfExporter/MermaidRenderer/MathMlRenderer/TableColumnNormalizer 的 runJavaScript 不属于 editorApi 面**——它们驱动各自的隐藏渲染页（见「相邻协议」）；32 个真实调用点剔除这 9 处后，editorApi 面直接入口为 **23** 处。

## 调用通道（C++ → JS）

| 通道 | 位置 | 特征 |
|------|------|------|
| `EditorTab::runJsCommand(js)` | EditorTab.cpp:454 | fire-and-forget 封装；MainWindow 经 `runEditorCommand`（MainWindow.cpp:665）委托当前 tab |
| `page()->runJavaScript(js, callback)` | 各处 | 需要返回值时直接用（getContent 快照链、contextMenu 链接查询、图片回填） |
| EditorTab setter 族 | EditorTab.cpp:375-422 | `setEditorTheme/setEditorLanguage/setEditorFont/setEditorWidth/applyCustomCss/setEditorSettings/pushDocDir`——各自拼字符串转 `runJsCommand`；新 tab 页面加载后由 `applyEditorConfigTo` 逐 tab 补推 |
| 别名守卫范式 | MainWindow.cpp:492-495、EditorTab.cpp:270-274 | `var ed=window.editorApi; if(!ed||!ed.editor)return;` / `var a=window.editorApi; if(!a||!a.isSourceMode)return;`——editorApi 未定义（页面脚本未执行完）时整体空退 |

字符串转义一律走 `core/JsEscape.h`（`jsSingleQuoted` / `forTemplateLiteral`），不得在调用点手写转义。

## 公共方法清单

稳定性级别：
- **稳定公共**——UI 直驱入口（工具栏/菜单/快捷键/右键菜单），签名变更必须同步 MainWindow/EditorTab 调用点，属跨层契约；
- **导出面**——三导出器内容获取专用，与导出视觉规范联动；
- **模式面**——源码模式切换协议，与 MainWindow::onToggleSourceMode 的回调链逐段对应；
- **内部**——当前仅 JS 内部消费（或作为 bridge 句柄），但挂在公共命名空间有历史/测试原因，收敛为内部语义前不删除。

调用方缩写：MW=MainWindow.cpp，ET=EditorTab.cpp，ST=SelfTestRunner.cpp，EH=ExportHtmlAsync.cpp。

### 生命周期与句柄

| 方法(行) | 签名 → 返回 | C++ 调用方 | 级别 |
|----------|------------|-----------|------|
| `editor` (1282) | TipTap Editor 实例（字段） | MW/ET/ST 各处守卫与探针（`ed.editor`） | 稳定公共（句柄） |
| `bridge` (1283) | EditorBridge QWebChannel 对象（字段，`bridge.cursorChanged(...)`） | 无（仅 JS 内部 + vitest） | 内部 |
| `isSourceMode` (1292) | `bool`（字段） | ST:529/541/639/652/776/793/805；ET 守卫；探针脚本 | 稳定公共（模式面） |
| `init` (1881) | `init()` → 构造 TipTap 实例、注册事件 | 无（editor.html 加载时 JS 自调） | 内部 |
| `warmup` (2692) | `warmup()` → 丢弃式 getHTML 预热冷 JIT | ET:257（大文档加载后 1.5s 定时）；探针 | 稳定公共 |

### 内容读写与快照

| 方法(行) | 签名 → 返回 | C++ 调用方 | 级别 |
|----------|------------|-----------|------|
| `getContent` (2129) | `getContent()` → 完整 HTML `string` | MW:796（进源码读取）；EH:42（Plain 导出）；ST:759/800 | 稳定公共 |
| `setContent` (2131) | `setContent(html)` → 销毁重建编辑器（undo 清零） | ET:241（loadFile 完成回调） | 稳定公共 |
| `getSaveSnapshot` (3703) | `getSaveSnapshot()` → `{html, version}` | MW:1265（保存）；ST:133 | 稳定公共 |
| `getSourceSnapshot` (3707) | `getSourceSnapshot()` → `{source, version}` | MW:1264（源码模式保存） | 稳定公共 |
| `markCleanIfUnchanged` (3711) | `markCleanIfUnchanged(docVersion)` → 落盘后按版本清脏 | MW:1275 | 稳定公共 |
| `markClean` (3696) | `markClean()` → 无条件清脏并复位版本 | 无（JS 内部；vitest 脏守护测试） | 内部 |
| `getExportHtml` (2098) | `getExportHtml()` → live colgroup 内联进 HTML | EH:41（Docx 模式注入列宽） | 导出面 |
| `getSourceContent` (2707) | `getSourceContent()` → 源码模式返回 textarea 内容，否则 `''` | EH:39（源码导出分支）；探针 | 导出面 |
| `highlightExportHtml` (2124) | `highlightExportHtml(html)` → 导出期代码高亮，失败原文直返 | EH:19 | 导出面 |
| `scrollToPos` (2723) | `scrollToPos(pos)` → 滚动到文档位置 | MW:487（大纲点击） | 稳定公共 |

### 源码模式

| 方法(行) | 签名 → 返回 | C++ 调用方 | 级别 |
|----------|------------|-----------|------|
| `enterSourceMode` (2482) | `enterSourceMode(md)` → HTML→textarea + 快照 | MW:819；ST:767/802 | 模式面 |
| `exitSourceMode` (2591) | `exitSourceMode(html)` → 全量重建编辑器 | MW:918；ST:832 | 模式面 |
| `exitSourceModeOrGetMd` (2678) | `exitSourceModeOrGetMd()` → `true`（未编辑快速路径已完成）\| `string`（已编辑，返回 MD 待 C++ toHtml） | MW:861；ST:786/821 | 模式面 |
| `syncSourceViewContent` (2716) | `syncSourceViewContent(md)` → textarea + `_sourceValue` 快照一并刷新（**W4-5 新增**，收口原内联直调） | ET:273（loadFile 时处于源码模式的外部内容同步） | 模式面 |

### 行内格式

| 方法(行) | 签名 → 返回 | C++ 调用方 | 级别 |
|----------|------------|-----------|------|
| `toggleBold/Italic/Strike/Code/Underline/Highlight/Subscript/Superscript` (2174-2181) | `toggleX()` | MW:677/682/300/303/306/309/312/315 | 稳定公共 |
| `clearFormatting` (2422) | `clearFormatting()` | MW:318 | 稳定公共 |
| `setTextAlign` (2222) | `setTextAlign(align)` | MW:321 | 稳定公共 |
| `applyTextColor` (2244) / `clearTextColor` (2258) | `(color)` / `()` | MW:326 / MW:332 | 稳定公共 |
| `applyBgColor` (2251) / `clearBgColor` (2266) | 同上 | MW:329 / MW:335 | 稳定公共 |

### 块结构与插入

| 方法(行) | 签名 → 返回 | C++ 调用方 | 级别 |
|----------|------------|-----------|------|
| `setHeading` (2274) | `setHeading(level)` | MW:687 | 稳定公共 |
| `toggleBulletList/OrderedList/TaskList/Blockquote` (2279-2282) | `toggleX()` | MW:339/342/345/348 | 稳定公共 |
| `insertTable` (2283) | `insertTable(rows, cols)` | MW:696 | 稳定公共 |
| `insertCodeBlock` (2286) | `insertCodeBlock()` | MW:267/354 | 稳定公共 |
| `insertHorizontalRule` (2307) | `insertHorizontalRule()` | MW:357 | 稳定公共 |
| `insertLink` (2298) | `insertLink()` → 打开链接对话框（**W4-5 转正**：右键菜单原直调 `_linkEditor.open()`） | MW:721；ET:112（右键编辑超链接） | 稳定公共 |
| `insertImage` (2304) | `insertImage(path)` | MW:768 | 稳定公共 |
| `onImageSaveResult` (2374) | `onImageSaveResult(requestId, result)` → 图片落盘回调：成功替换占位图 src，失败删占位图（**W4-5 转正**：原 `_onImageSaveResult`） | MW:730/732（经 `jsImageSaveResult`） | 稳定公共（跨边界回调端口） |
| `insertMath` (3716) | `insertMath('inline'\|'block')` | MW:173/261/360 | 稳定公共 |
| `insertMermaid` (3721) | `insertMermaid()` | MW:363 | 稳定公共 |
| `insertFootnote` (3726) | `insertFootnote()` | MW:367/371 | 稳定公共 |
| `openEmojiPicker` (3893) | `openEmojiPicker(x, y)` → 锚点弹出 | MW:714 | 稳定公共 |
| `closeModalDialogs` (3900) | `closeModalDialogs()` → 逐面板关闭（link→mermaid→math→emoji→codeLang→footnote）（**W4-5 新增**，收口 Esc 处理器直调批次） | MW:163（Esc 快捷键） | 稳定公共 |
| `undo` / `redo` (3448-3449) | `undo()` / `redo()` | MW:272/273/285/288 | 稳定公共 |

### 表格

| 方法(行) | 签名 → 返回 | C++ 调用方 | 级别 |
|----------|------------|-----------|------|
| `addRowBefore/After`, `deleteRow`, `addColumnBefore/After`, `deleteColumn`, `deleteTable` (2291-2297) | 各 `()` | ET:131/135/139/144/148/152/157（右键表格子菜单） | 稳定公共 |
| `setTableColumnAlign` (2187) | `setTableColumnAlign(align)` → `bool` | 无（JS 内部表格对齐 UI） | 内部 |

### 搜索替换（MW 经 `ed.` 别名调用，MainWindow.cpp:492-518）

| 方法(行) | 签名 → 返回 | 级别 |
|----------|------------|------|
| `startSearch` (3346) | `(text, caseSensitive, regex, wrapAround)` | 稳定公共 |
| `resumeSearch` (3360) | 同上（切 tab 恢复） | 稳定公共 |
| `searchNext` (3388) / `searchPrev` (3395) | `()`（MW:150/154 F3 快捷键也走） | 稳定公共 |
| `replaceOne` (3416) / `replaceAll` (3422) | `(replacement)` | 稳定公共 |
| `stopSearch` (3428) | `()` | 稳定公共 |

### 脚注导航

| 方法(行) | 签名 → 返回 | 级别 |
|----------|------------|------|
| `findFootnoteDefPos` (2737) / `findFootnoteRefPos` (2746) | `(label)` → 位置 `pos\|-1` | 内部（JS 内脚注跳转链） |
| `jumpToFootnoteDef` (2774) / `jumpToFootnoteRef` (2783) | `(pos)` | 内部（同上） |
| `renameFootnote` (3768) | `(oldLabel, newLabel)` → `bool` | 内部（脚注改名对话框确认路径） |

### 外观与设置

| 方法(行) | 签名 → 返回 | C++ 调用方 | 级别 |
|----------|------------|-----------|------|
| `setTheme` (3450) | `(theme)` | ET:381 | 稳定公共 |
| `setLanguage` (3459) | `(lang)` | ET:388 | 稳定公共 |
| `setFont` (3495) | `(family, size)` | ET:395 | 稳定公共 |
| `setWidth` (3670) | `(percent)` | ET:402 | 稳定公共 |
| `applySettings` (3678) | `({lineNumbers, wordWrap, tabSize})` | ET:420 | 稳定公共 |
| `applyCustomCss` (3480) | `(css)` | ET:410 | 稳定公共 |
| `setLineNumbers` (1533) | `(enabled)` | 无（经 `applySettings`；JS 内部） | 内部 |
| `setEditable` (3655) | `(editable)` | MW:1376（语法指南只读锁） | 稳定公共 |
| `setDocDir` (2396) | `(dir)` → 授权相对图片目录 | ET:371（`pushDocDir`） | 稳定公共 |

### 状态通知（JS → C++ 方向的主动推送入口，C++ 只在切 tab 时请求重推）

| 方法(行) | 签名 → 返回 | C++ 调用方 | 级别 |
|----------|------------|-----------|------|
| `notifyHeadings` (1924) | → `bridge.headingsChanged(JSON)` | MW:629 | 稳定公共 |
| `notifyFormat` (1935) | → `bridge.formatChanged(JSON)` | MW:630 | 稳定公共 |
| `notifyHistory` (1989) | → `bridge.historyChanged(canUndo, canRedo)` | MW:631 | 稳定公共 |
| `notifyWordCount` (1996) | → `bridge.wordCountUpdated(chars, words)` | 无（onUpdate 自动推） | 内部 |
| `notifyCursor` (3516) | → `bridge.cursorChanged(line, col, pos)` | 无（事务监听自动推） | 内部 |
| `syncFormatState` (3506) | → 四项 notify 串行重推 | MW:636 | 稳定公共 |

### 无 C++ 调用方的公共成员汇总（12 个）

`bridge`、`setLineNumbers`、`init`、`notifyWordCount`、`setTableColumnAlign`、`findFootnoteDefPos`、`findFootnoteRefPos`、`jumpToFootnoteDef`、`jumpToFootnoteRef`、`notifyCursor`、`markClean`、`renameFootnote`——全部有 JS 内部消费者（页面初始化、onUpdate 自动推送、脚注跳转/改名链），非死代码；vitest 白盒测试亦直接调用部分成员。**处置约定：不删、不再扩大使用面；新需求先评估是否本就该是内部成员。**

## 内部成员分类（99 个 `_` 前缀）

| 类别 | 代表成员 | 说明 |
|------|---------|------|
| 加载/脏状态 | `_loading`、`_dirty`、`_docVersion`、`_forceDirty`、`_restoreDirtyIfUnchanged`、`_markDirty`、`_readOnly` | 脏状态机制见 CLAUDE.md；`_loading` 锁抑制 setContent 内的 onUpdate |
| 源码模式状态 | `_sourceValue`、`_srcSearch`、`_srcOnSourceEdit`、`_srcWrapCache`、`_srcStop` | textarea 快照与源码搜索；`_sourceValue` 是快速路径退出的比对基准 |
| 模态面板对象（7 个） | `_linkEditor`、`_mermaidEditor`、`_mathEditor`、`_emojiPicker`、`_codeLangPicker`、`_footnoteEditor` + `_mathEditing` | 面板清单的唯一直调入口是 `closeModalDialogs()`（W4-5 起） |
| 图片摄取 | `_imagePending`、`_ingestImageFile` | 占位符标记 `__pending_<requestId>__`，落盘完成走公共 `onImageSaveResult` |
| 布局/行号 | `_lineStarts`、`_lineColAt`、`_lineGutterOn/_lineGutterRO/_lineGutterEl` | 光标行列换算与行号 gutter |
| TipTap 事务/历史 | `_onUpdate`、`_syncPlaceholder` 等 | 内部实现细节，与公共面无直调关系 |

**跨边界纪律：C++ 不得直调任何 `_` 成员**。现存白名单例外见下节。

## 收口裁定（W4-5）

评审发现的 C++ 私有直调点共 7 处（4 个语义点），逐点裁定与等价论证：

| # | 位置（收口前） | 直调内容 | 裁定 | 等价论证 |
|---|---------------|---------|------|---------|
| 1 | MainWindow Esc 处理器（6 条语句） | `_linkEditor/_mermaidEditor/_mathEditor/_emojiPicker/_codeLangPicker/_footnoteEditor.close()` | **补 API** `closeModalDialogs()` | 语句逐条等价搬移，close 顺序不变（link→mermaid→math→emoji→codeLang→footnote）；editorApi 未定义时同样抛 TypeError（原为 6 条中的首个抛出，现聚合方法内首个抛出，均中断本条命令串） |
| 2-3 | MainWindow `jsImageSaveResult` 成功/失败两分支 | `onImageSaveResult`（原 `_onImageSaveResult`） | **补 API**（更名转正） | 仅更名；JS 内部零调用者（全仓 grep 确认调用面只有 MainWindow 两处），更名无内部涟漪 |
| 4 | EditorTab 右键「编辑超链接」 | `_linkEditor.open()` | **改公共** `insertLink()` | 二者守卫一致（`insertLink` 首行 `if (!this.editor) return;`，`open()` 首行 `var ed=window.editorApi.editor; if (!ed) return;`），且 `open()` 注释自述「工具栏/菜单与双击编辑链接统一入口」；工具栏路径本就走 `insertLink` |
| 5 | EditorTab loadFile 源码模式同步块 | `_sourceValue` 赋值 + `_srcSearch` 判断 + `_srcOnSourceEdit()` | **补 API** `syncSourceViewContent(md)` | 语句逐条等价搬移进方法体；C++ 侧 IIFE 守卫（`if(!a\|\|!a.isSourceMode)return;`）保持原样 |
| 6 | SelfTestRunner `waitEditorDocReady` | `!editorApi._loading`（就绪判据） | **标注例外** | 测试工装白盒探针：就绪语义（加载锁内/外）本就是内部状态，为其造公共 API 反而扩大接口面；生产 C++ 从不读它 |
| 7 | SelfTestRunner perf 预热等待 | `!!editorApi._warmed` | **标注例外** | 同上——预热完成标志的等待逻辑与 EditorTab 的预热实现天然耦合，属 perf 计时口径的一部分 |

**未动但已登记**：取证探针脚本 `scripts/docx-table-parity-probe.js:268`、`scripts/srcmode-md-probe.js:258-259` 直读 `_sourceValue`（比对 textarea 判定「源码已编辑」）。与 SelfTestRunner 同性质（工装白盒），不为其造 API；若未来语义变化（如快速路径判据更替），由探针脚本自行跟进。

## 白名单例外（允许引用私有的边界）

| 边界 | 引用 | 理由 |
|------|------|------|
| SelfTestRunner.cpp:157 | `_loading` | 无头自测就绪判据（上表 #6） |
| SelfTestRunner.cpp:736 | `_warmed` | perf 预热等待（上表 #7） |
| scripts/*-probe.js | `_sourceValue` 等 | CDP 取证工装，非出厂代码 |
| src/resources/web/*.test.js（vitest） | `_dirty/_docVersion/_mathEditor/_lineStarts/_lineColAt/_markDirty` 等 | 单元级白盒断言，测的就是内部状态机 |

除上述边界外，`grep -rn 'editorApi\._' src/ --include='*.cpp'` 应恒为零命中——新增直调先按「改公共 / 补 API / 标注例外」三选一并记录论证。

## 相邻协议（不属于 editorApi，但同属 C++→JS 面）

| 协议 | 页面 | 全局 |
|------|------|------|
| PDF 导出隐藏渲染页 | PdfExporter 注入的独立 HTML | `__preparePrint` / `__printState` / `__stampPageNumbers` / `__pagedBoxH` / `__pagedFnH` / `__ppDbg` / `__hfPagTall` / `__measureH`；CSS 选择器 `.pagedjs_page` |
| Mermaid 后台渲染 | MermaidRenderer 隐藏页 | `__mermaidResults` |
| MathML / 表格列宽归一 | MathMlRenderer / TableColumnNormalizer 隐藏页 | 各自 IIFE 内部协议 |
| 折行点缓存探针 | 编辑器页（探针脚本用） | `window.__wrapCacheState`（scripts/mode-switch-probe.js） |
| 搜索插件实例 | 编辑器页 | `window.searchPlugin`（editor-page 内部，startSearch 系列的实际实现方） |

这些面的稳定性约束记录在各自专题文档（pdf-header-footer-layout.md 等），不在本文展开。

## 变更纪律

1. **新增公共方法**：先确认无 JS 内部等价物可复用；命名不带 `_`；在本文对应域表补行（方法/签名/返回/C++ 调用方/级别）；C++ 调用点经 `runJsCommand`/`runJavaScript`，字符串转义走 `core/JsEscape.h`。
2. **改签名/更名**：本文表格 + 全部 C++ 调用点 + vitest 同步改；`grep -rn 'editorApi\.<name>' src/ scripts/ tests/` 清零后才算完成。
3. **周期复核**：
   ```bash
   # 枚举 editorApi 成员（应与本文表格一致：185 = 公共 86 + 私有 99）。
   # 必须限定在字面量行界内——全文件跑会多出头部 _buildExtensions 返回对象的
   # 3 个 TipTap extension 成员（addNodeView/addProseMirrorPlugins）记 188。
   # 行界随代码漂移：先按锚文本定位起止，勿硬编码行号（本轮为 1281–5253）
   start=$(grep -n 'window.editorApi = {' src/resources/web/editor.js | cut -d: -f1)
   end=$(awk -v s="$start" 'NR>s && /^};/{print NR; exit}' src/resources/web/editor.js)
   sed -n "${start},${end}p" src/resources/web/editor.js | grep -cE '^    [A-Za-z_$][A-Za-z0-9_$]*\s*:'
   # C++ 私有直调（白名单外应零命中）
   grep -rn 'editorApi\._' src/ --include='*.cpp' --include='*.h'
   # runJavaScript 调用点对账（本轮 HEAD 实测 51，历史锚点见「总量对账」）
   grep -rn 'runJavaScript' src/ | wc -l
   ```
4. 涉及 editor.js 改动后必须重编译（qrc 打进二进制，见 CLAUDE.md 关键原则 9）。
