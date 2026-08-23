# Changelog

本项目遵循 [Keep a Changelog](https://keepachangelog.com/zh-CN/1.0.0/) 规范，版本号遵循 [语义化版本](https://semver.org/lang/zh-CN/)。

## [0.12.3] — 2026-08-23

### 新增
- **源码切换按钮图标与提示随模式互换** — 进入源码模式后按钮换为 WYSIWYG 显示器图标（屏幕 + 内容行 + 支架，明暗主题各一），悬停提示变为「切换为所见即所得模式」，返回普通模式恢复源码图标与原提示；图标路径同步注册到主题注册表（`m_iconPaths`），切换深浅色主题不回退

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
