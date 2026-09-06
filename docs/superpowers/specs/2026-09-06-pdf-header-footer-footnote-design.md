# PDF 页眉页脚、脚注增强与会话恢复守卫设计

> 状态：已确认（用户 2026-09-06 批准方向与设计），待写实现计划
> 日期：2026-09-06
> 来源：全文档未实现功能回顾（本会话）+ 用户指示四项全做
> 关联：`docs/superpowers/specs/2026-08-09-footnote-design.md`（脚注 MVP）、`docs/modal-dialog-esc-handling.md`（ESC 机制）

## 0. 范围总览

| # | 功能 | 来源 | 规模 |
|---|------|------|------|
| 1 | PDF 导出页眉页脚/页码（paged.js 分页引擎） | CLAUDE.md 已知问题、2026-08-15 spec「另行立项」 | 大 |
| 2 | 脚注增强四项（跳转互导/hover 气泡/块级定义/内联改名） | 2026-08-09 footnote spec §11 第二阶段 | 大 |
| 3 | 会话恢复路径 ≥2MB 软警告（一致性缺口） | 本会话回顾发现 | 小 |
| 4 | emoji 等浮层补录 ESC 文档 | 2026-08-09 emoji spec:184 待办 | 小（纯文档） |

四项相互独立，实现计划按任务切分，可独立验证提交。

---

## 1. PDF 页眉页脚/页码

### 1.1 需求（用户已确认）

- 页眉居中 = **导出文件名基名**（用户在保存对话框确认的名字；不取文档首个 H1，避免与文件名不一致的猜测）
- 页脚右侧 = `第 X 页 / 共 Y 页`
- 设置项 `export/pdfHeaderEnabled` / `export/pdfFooterEnabled`（默认开），进设置对话框导出区
- 现有 `export/paperSize` / `export/orientation` / `export/margin*` 语义不变

### 1.2 技术选型（用户已确认方案 A）

`QWebEnginePage::printToPdf()` 不暴露 Chromium 的 headerTemplate；CSS `@page` margin boxes Blink 不支持；固定定位元素无法做到每页变化的页码。三条原生路全断，引入 **paged.js** 分页引擎（vendored）接管分页，页眉页脚成为 CSS Paged Media 原生能力。

落选方案（备查）：B = 固定定位页眉 + QtPdf 光栅盖章页码（正文光栅化，文字不可选、文件膨胀，质量倒退）；C = CDP 远程调试端口 + Chromium 原生模板（调试端口进程级全局开启，安全面不可接受）。

### 1.3 管线改造（`PdfExporter`）

```
现状:  setHtml → loadFinished → __renderAllMermaid() → printToPdf(layout 含边距)
新:    setHtml → loadFinished → Promise[mermaid + paged] → printToPdf(边距=0)
```

- **顺序铁律：mermaid 先渲染、分页后跑**——SVG 定型后才切块，否则分页测量错位。KaTeX 在 DOMContentLoaded 同步渲染，天然在前
- paged.js 手动模式：`<script>window.PagedConfig={auto:false}</script>` 须先于 polyfill 加载；C++ 侧 `runJavaScript` 等待的 Promise 链：`__renderAllMermaid()` → `PagedPolyfill.preview()`，两者皆 settle 才打印（沿用现有 `.then/.catch 都返回 true` 的防挂起模式）
- **@page 规则由 `buildRenderHtml` 动态生成**：纸张/方向/四边距从 `export/*` QSettings 注入（如 `size: A4 landscape; margin: 18mm 18mm 20mm 18mm`）；`printToPdf` 传同纸张/方向 + **0 边距** QPageLayout——页面盒自带边距，CSS px→PDF pt（96→72dpi）精确映射，一盒一页

### 1.4 页眉页脚 CSS（`buildRenderHtml` 内生成）

```css
@page {
  size: <纸张> <方向>;
  margin: <上>mm <右>mm <下>mm <左>mm;
  @top-center   { content: "<标题>";        font-size: 9pt; color: #666; }
  @bottom-right { content: "第 " counter(page) " 页 / 共 " counter(pages) " 页";
                  font-size: 9pt; color: #666; }
}
```

- 页眉关 → 不生成 `@top-center`；页脚关 → 不生成 `@bottom-right`；全关 → 不引入 paged.js（走原管线，回归零风险）
- 标题文本经 CSS 字符串转义（`\` `"` 换行）
- PDF 固定亮色打印（现状不变），页眉页脚灰色不随主题

### 1.5 vendored 管理

- `src/resources/web/paged.polyfill.js` 入 `app.qrc`，qrc: 基 URL 加载（离线，无 CDN）
- 版本精确钉死，照 `docs/vendor-lowlight-plugin.md` 惯例写 `docs/vendor-pagedjs.md`：来源 URL、版本号、同步/升级流程、与上游的差异（应为零，纯 dist 落盘）

### 1.6 测试与验证门

- 单测（`test_pdfexporter.cpp` 扩展）：`buildRenderHtml` 字符串契约——开关组合下 `@top-center`/`@bottom-right`/pagedjs script 的有无比对、标题转义、纸张/方向/边距注入、全关时不含 pagedjs 引用
- E2E 手工清单（分页是渲染行为，单测覆盖不了）：
  1. 多页文档（≥5 页）：页码连续、总页数正确、每页页眉一致
  2. KaTeX 公式（行内 + 块级）不碎、位置正确
  3. mermaid 图完整不被切断（三类：flowchart/甘特/时序）
  4. 表格跨页断行、长代码块、图片、中文长文
  5. 横版、A5、自定义四边距
  6. 单页文档显示「共 1 页」；空文档不崩
  7. 页眉关/页脚关组合各导出一次
- **逃生门**：E2E 出现硬阻断（如 SVG 被切且无解）→ 停下回报用户重议方案 B

### 1.7 已知限制（记入文档）

- 用户自定义 CSS 若含 `@page` 规则可与本机制冲突（用户自担，README/文档不展开）
- 极长不可断元素（超一页高的图片/代码块）在分页引擎下溢出裁切——与现状行为一致

---

## 2. 脚注增强（四项）

### 2.1 定义支持块级内容（schema 升级）

- `footnoteDef` content：`inline*` → `block+`（多段/列表/代码块/引用等任意块）
- DOM 结构：`div[data-type=footnoteDef][data-id=X] > span.footnote-marker(只读，float:left) + div.footnote-content(contentDOM)`
  - marker 浮动 → 首段文字绕排，视觉与现单段形态连续
  - `parseHTML` 现有 `.footnote-content` 定位逻辑（editor.js:704）天然兼容
  - 空定义：ProseMirror 自动补空段落，无需特判
- `footnoteContainer` content 保持 `footnoteDef+` 不变

### 2.2 MD 往返（核心风险区，pandoc 式续行规则）

**语法**：定义行之后缩进 ≥4 空格的行（含空行后接缩进行）归属该定义：

```
[^1]: 第一段
    - 列表项（续行缩进）

    第二段（空行 + 缩进 = 新段落）
```

**toHtml（MD→HTML，`MarkdownHandler` 预处理）**：
1. 识别定义行后，继续捕获续行（≥4 空格或 1 tab 缩进；空行后仍有缩进行则延续；遇非缩进行终止）
2. 定义块整体替换为 `ZZFD{n}ZZ` 占位符（含续行，全部移出正文流）
3. 定义内容 MD（首行余文 + 去缩进续行）→ **复用主管线整体转换**（非裸 `setMarkdown`）——现有 `buildFootnoteDefHtml`（MarkdownHandler.cpp:867）仅做下标保护 + 裸 `setMarkdown`，数学/任务列表/高亮等在定义内一律不转换；块级升级后该差异不可接受。实现为**嵌套调用**：定义 MD 走完整 MD→HTML 主管线（占位符保护全套生效），管线带「禁用脚注提取」标志（嵌套定义按字面处理）与深度守卫（限 1 层，防自引用递归），结果包进 `<div data-type="footnoteDef" data-id=…><div class="footnote-content">…</div></div>`
4. 代码块/行内代码内 `[^x]:` 不转换——沿用现有代码保护（预处理顺序在代码块提取之后）

**fromHtml（HTML→MD）**：
1. `footnoteDef` 内 `.footnote-content` 的块级 HTML → 同样**复用 fromHtml 主管线嵌套转换**（任务列表/数学等占位保护对称生效，取 body 内容）
2. 输出 `[^id]: 首行`，后续行一律补 4 空格缩进；段落间空行保留（空行本身不缩进——pandoc 兼容）

**限制（记入文档）**：
- 嵌套脚注（定义内再定义 `[^2]:`）不支持，内层按字面文本处理
- 定义内 mermaid/数学公式块 schema 允许，导出渲染不保证（不承诺）
- 续行捕获与 Markdown 缩进代码块语义重叠（4 空格缩进行在定义内一律视为定义内容，不再是缩进代码块）——pandoc 同款取舍

**续行判定**：仅**缩进续行**（≥4 空格 / 1 tab）归属定义；**非缩进的「懒续行」不归属**（留在正文，与 MVP 单行定义行为一致，fromHtml 端因此总是缩进输出，往返稳定）。

**往返单测**（`test_markdownhandler.cpp` 扩展）：多段落定义、含列表/代码块定义、空行+缩进分段、懒续行留在正文、空定义、定义后紧跟正文不误吞、代码块内定义行不转换、块级定义往返字节等价。

### 2.3 点击跳转互导

- **单击引用 → 跳定义**：click 事件经 ~250ms 延迟判别（双击改名不让位）；文档树 `descend` 找同 id `footnoteDef` 的 pos → 复用 `scrollToPos`（editor.js:2395）滚动 → def DOM 加 `footnote-flash` 类（CSS 高亮动画 1.5s 后自动移除）
- **定义 ↩ 返回箭头**：marker 悬停显示 `↩` 按钮，点击找**首个**同 id `footnoteReference` → 同款滚动 + 高亮
- **悬空目标**（引用无定义/定义无引用）：不跳转，标记抖动动画提示；纯 JS，不进 C++ Bridge
- 多次引用定义共用：返回箭头固定回首个引用（与 MVP「多次引用共享定义」语义一致）

### 2.4 hover 气泡预览

- `mouseenter` 引用上标 → 克隆对应 def `.footnote-content` 内容入气泡（上限前 2 个块 + 「…」省略，max-width 320px；引用位于视口上部则下方弹出）
- `mouseleave`（引用与气泡均离开，300ms 宽限）关闭；非模态浮层，**不**占用 `modalDialogStateChanged` 标志
- 空定义显示「（空）」；找不到 def 显示「未定义」
- 亮/暗主题各配色（editor.css）

### 2.5 内联改名弹层

- 仿 `_linkEditor` 建 `_footnoteEditor`：锚定引用处的小弹层，输入框预填当前 id，Enter/「确定」调现有 `renameFootnote(from, to)`（editor.js:3257），「取消」/失焦关闭
- 校验：非空、不含 `]` 与空白、不与现有任一 id 重名（重名时输入框红框 + 提示文案，不关闭）
- 双击引用打开（替代 `prompt`，删除 prompt 路径）
- 打开/关闭走 `modalDialogStateChanged` → C++ ESC 链（`MainWindow.cpp:134-138`）补 `window.editorApi._footnoteEditor.close();`——C++ 改动，需重编译
- i18n：中英 key（`footnote.renameTitle`/`footnote.duplicateId`/`footnote.invalidId` 等，照现有 `footnote.*` 组）

### 2.6 测试

- 往返单测见 2.2
- 交互项（跳转/气泡/弹层）：Vitest 能覆盖的纯逻辑（id 查找/校验/续行解析抽成纯函数）+ E2E 手工清单（跳转滚到位并高亮、返回箭头、气泡内容与主题、ESC 关弹层、双击改名同步全部引用与定义）

---

## 3. 会话恢复软警告（#6）

- `onFileDoubleClicked` 的 ≥2MB 确认框抽成私有 helper `confirmLargeFile(path)`（同款文案与判定：`kLargeFileWarnBytes ≤ size ≤ kMaxReadBytes` 且非二进制）
- `restoreSession` 循环内 readFile 前调用；**拒绝 → 跳过该文件**（不 read、不开标签、不 watch、不进最近文件）——与手动打开拒绝路径完全一致
- 标签下标错位已由「活动标签按路径定位」机制（MainWindow.cpp:1254）覆盖，无新增风险
- 验证：手工 E2E（会话含 2MB+ 文件重启：确认→恢复、拒绝→跳过且活动标签定位仍正确）；MainWindow 无单测骨架，不新增

## 4. emoji 等浮层补录 ESC 文档（#5）

`docs/modal-dialog-esc-handling.md` 补一节「浮层复用与语义放宽」：

- `modalDialogStateChanged` 标志实际使用者：超链接/Mermaid/数学公式（文档已有）+ **emoji 选择器、代码语言选择器**（emoji spec:184 待办）+ **脚注改名弹层**（本次新增，见 §2.5）
- 变量语义从「模态对话框打开」放宽为「编辑器内浮层打开」，ESC 分流逻辑不变
- 纯文档改动，无代码

---

## 5. 收尾与版本

- CLAUDE.md：已知问题区移除 PDF 页眉页脚行；关键文件表加 paged.polyfill.js；专题文档加 vendor-pagedjs.md
- README 功能列表：PDF（含页眉页脚/页码）、脚注增强描述
- footnote MVP spec（2026-08-09）文末增补执行记录指向本 spec
- CHANGELOG 留待版本收口（记忆 `update-changelog-on-tag`）；本批为 feat 大增量，收口建议 **+0.1.0**，届时与用户确认
- 遵循项目约定：实现期间 fix/feat 提交不带版本推进，用户目视验证通过后才 commit
