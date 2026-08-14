# 图片落盘 + 图片缩放 + 搜索增强

> 状态：搜索增强（P3）已确认并决议落地，待写实现计划；图片（P1/P2）已于 0.5.0 落地
> 日期：2026-08-13
> 范围：三个相对独立的功能合为一个阶段，分 P0–P3 四期实施，每期独立可测可提交
> 版本基线：`0.3.10`（`CMakeLists.txt`）

## 0. 决策汇总

| 维度 | 决策 |
|------|------|
| 图片存储位置 | `.md` 同级 `images/` 子目录，MD 中用**相对路径** |
| 未保存文档 | 禁用图片粘贴/拖入，提示「请先保存文档」 |
| 图片命名 | 拖拽保留原名（重名加 `-2/-3`）；剪贴板粘贴用 `image-<时间戳>.<ext>` |
| 图片格式 | 剪贴板位图→PNG；拖拽文件→保留原始字节 |
| 缩放持久化 | HTML `<img width>` 字面保留（MarkdownHandler 新增占位符规则） |
| 缩放交互 | 四角手柄，默认锁宽高比，Shift 解锁 |
| 搜索架构 | 自写 TipTap 扩展（ProseMirror `Decoration.inline`），彻底替换 Chromium `findText` |
| 搜索匹配逻辑 | 纯函数 `searchMatcher`，vitest 测 |
| 正则替换 | 支持 `$1/$2/$&` 反向引用 |
| 全词匹配 | Unicode 词边界后置过滤（中文友好），非 ASCII `\b` |
| 源码模式搜索 | 本阶段不纳入（仅 WYSIWYG） |
| 替换栏 UI | 可折叠；Ctrl+F 仅查找，Ctrl+H 展开替换行 |
| 自动测试 | JS 加 vitest；C++ 加 QtTest；CI 启用 `BUILD_TESTS=ON` 并跑 vitest |

---

## 1. 背景与现状

探索确认三个功能目前都接近空白：

- **图片落盘**：仅有 `MainWindow::onInsertImage()`（`MainWindow.cpp:525`）把**绝对路径原样塞进 `<img src>`**；无粘贴/拖拽处理、无落盘、无 requestId 回调、无 `images/` 概念；C++ 侧零图片代码。窗口级 `dropEvent`（`MainWindow.cpp:851`）只认 `.md`，图片被丢弃。
- **图片缩放**：用 stock `@tiptap/extension-image`（`editor.js:9,933`），无自定义 NodeView、无 resize。标准 MD `![]()` 不带尺寸。
- **搜索**：依赖 Chromium 原生 `QWebEnginePage::findText`（`MainWindow.cpp:330-338`）；无计数、无上下导航、无正则/全词；大小写 flag 与替换脱节（替换走 JS `indexOf`，`MainWindow.cpp:339-380`）；StatusBar 无计数位（`StatusBar.h:19-22`）；TipTap 侧无任何搜索/高亮扩展。
- **图片往返**：`MarkdownHandler` 对 `image/![]/<img/src` **零特殊处理**，完全依赖 Qt md4c 原生转换；`test_markdownhandler` 89 用例中零图片测试。
- **测试**：仅 3 个纯 C++ 逻辑测试（`test_editorbridge/filemanager/markdownhandler`，QtTest，无 GUI/Xvfb）；**JS 侧零测试框架**；CI 当前 `-DBUILD_TESTS=OFF` 不跑测试。
- **JS↔C++ 通信**：现有两种模式均为 C++ 主动发起（fire-and-forget `runJavaScript`，或取返回值的 `runJavaScript(js, lambda)`）；JS→C++ 走 QWebChannel bridge（slot→signal→MainWindow），但 **slot 返回值不回传 JS**——「JS 请求 → C++ 处理 → JS 回调」的 round-trip 模式项目内**尚无先例**。

### MVP 目标

1. **图片粘贴/拖拽落盘**：剪贴板粘贴或拖入图片 → 自动写入文档同级 `images/` → 插入相对路径引用，文档移动后图片不失效。
2. **图片缩放**：选中图片显示四角手柄，拖拽改尺寸，尺寸在 MD 中以 `<img width>` 持久化，往返不丢失。
3. **搜索增强**：正则/全词/大小写 toggle + 匹配计数（`n/m`）+ 上下导航 +（可选）反向引用替换，统一走 ProseMirror 高亮。
4. **全部可自动测试**：JS 引入 vitest，C++ 扩展 QtTest，CI 接入测试。

### 非目标

- 远程图片（http URL）下载落盘——剪贴板仅有 URL 文本时不下载，按普通文本处理。
- base64 内联图片落盘迁移——存量 base64 图片保持原样。
- 图片裁剪/旋转/批量管理。
- 搜索「在选区内」范围、搜索历史。
- DOCX/PDF 导出嵌入图片二进制（现有占位符逻辑保持）。

---

## 2. 功能一：图片粘贴/拖拽落盘

### 2.1 数据流（requestId 回调协议）

项目首个 JS↔C++ round-trip。复用现有 slot→signal→MainWindow 编排模式，回调走现有 `runJavaScript`。

```
editor.js                          EditorBridge(C++)            ImageManager / MainWindow
────────                           ────────────────             ──────────────────────
handlePaste / handleDrop
  读 blob → 定 ext → base64
  requestId = ++counter
  bridge.requestImageSave(              ──slot──►
     requestId, base64, ext, name)                     emit imageSaveRequested(...)
                                                          MainWindow 按 tab 取 filePath
                                                          ├ 无 filePath → 回调 unsaved
                                                          └ 有 → ImageManager.saveImage(...)
                                                              resolve images/ dir + 命名 + 原子写
  _pending[requestId] = resolve ◄──runJs──  window.editorApi._onImageSaveResult(
                                               requestId, {ok,path} | {ok:false,err})
  成功 → 插入 image 节点 src=相对路径
  失败 → 插入提示文本「⚠ 请先保存文档再插入图片」/「⚠ 图片保存失败」
```

**可靠性要点**：
- `EditorBridge` 每个 tab 独立实例（`EditorTab.cpp:302` 注册），slot 在对应 tab 的 bridge 上触发，MainWindow 连接时捕获 `tab`，可正确取该 tab 的 `filePath`。
- `requestId` 注册表 `_pending` 让并发请求（一次拖多张图）各自拿到正确结果，不串扰。
- 落盘细节：`ext` 由 MIME（`image/png`→`png`）或拖拽文件名决定；命名碰撞 `photo.png → photo-2.png`；写盘用 FileManager 同款 tmp+rename 原子写。

### 2.2 拖拽分层

窗口级 `dropEvent`（`MainWindow.cpp:851`）仍只接 `.md`（开新标签）；图片落入编辑器由 ProseMirror `editorProps.handleDrop` 在 webview 内部处理。两者互不干扰。

### 2.3 新增组件

| 组件 | 位置 | 职责 |
|------|------|------|
| `ImageManager` | `src/core/ImageManager.{h,cpp}`（新建，静态函数，仿 `MarkdownHandler`） | `saveImage(docFilePath, bytes, suggestedName) → 相对路径`：建 `images/` 目录、命名/碰撞处理、原子写、返回 `images/xxx.png` |
| EditorBridge slot+signal | `src/core/EditorBridge.{h,cpp}` | slot `requestImageSave(requestId, base64, ext, name)` → signal `imageSaveRequested(...)` |
| MainWindow 编排 | `src/ui/MainWindow.cpp` | 按 tab 连接 signal；读 `filePath` 属性；调 `ImageManager`；回调 `_onImageSaveResult` |
| editor.js handlePaste/handleDrop | `editor.js` `_buildExtensions` 的 `editorProps` | 拦截剪贴板/拖拽，提取 image blob，发起 requestId 请求；`_pending` 注册表 + `_onImageSaveResult` |

### 2.4 错误处理

| 场景 | 处理 |
|------|------|
| 文档未保存（无 filePath） | 回调 `{ok:false,err:'unsaved'}` → JS 插入提示文本「⚠ 请先保存文档再插入图片」 |
| 磁盘写入失败/权限 | 回调 `{ok:false,err:'io'}` → 插入「⚠ 图片保存失败」，不插入坏图 |
| 剪贴板无图片（纯文本/URL） | 不拦截，走默认粘贴 |
| 拖入非图片文件 | `handleDrop` 返回 false，交给默认 |
| 超大图（>20MB） | 落盘前检查字节数，超限提示「⚠ 图片过大」 |

### 2.5 测试

- **C++ `tests/test_imagemanager.cpp`**（QtTest，仿 `test_filemanager`）：临时目录下验证——文件正确写入、内容字节一致、相对路径 `images/xxx.png` 正确、原名保留、碰撞后缀 `-2/-3`、`images/` 自动创建、空字节/非法名防御。
- **`tests/test_editorbridge.cpp` 新增用例**：`requestImageSave` slot → `imageSaveRequested` signal 转发正确（`QSignalSpy`）。
- 协议往返、handlePaste/handleDrop 的 DOM/剪贴板行为留作手动验证（无 JS 自动化测试框架前；引入 vitest 后可补纯逻辑）。

---

## 3. 功能二：图片缩放

### 3.1 ResizableImage 自定义 Node（替换 stock Image）

自定义 TipTap Node `ResizableImage`：

| 项 | 值 |
|----|----|
| `name` | `resizableImage` |
| `addAttributes` | `src`、`alt`、`title`、`width`（`default: null`，仅非空时渲染） |
| `parseHTML` | `{ tag: 'img', getAttrs: el => ({ src, alt, title, width: el.getAttribute('width') || null }) }` |
| `renderHTML` | `width` 非空 → `['img', { src, alt, title, width }]`；否则 `['img', { src, alt, title }]` |
| `draggable` | `true`（与 stock 一致） |
| `addCommands` | `setImage`、`setImageWidth(px)` |

NodeView（仿现有 Mermaid NodeView `editor.js:696`）：
- 图片作为 `node` 视图，外层包容器；
- 选中（`selected`）时显示四角 `.resize-handle`；
- mousedown 拖拽按 dx 比例改 `width`（px，最小 32，上限容器宽）；
- **默认锁宽高比**（仅改 width，浏览器按原图比例自适应高度），Shift 临时解锁（未来若引入 height attr 再支持自由形变）；
- 松手提交 `setImageWidth` transaction。

### 3.2 往返保真（核心难点，TDD 先行）

`<img width>` 必须在 MD→HTML→MD 不丢。规则：**带尺寸的图 → 字面 HTML `<img>`（与 `<br>`/`<kbd>`/`<span style>` 同类）；无尺寸图 → 标准 `![](src)`**。

MarkdownHandler 新增 `ZZIMG` 占位符路径（与现有 `ZZHI`/`ZZBR` 同机制）：

- **fromHtml（保存）**：`setHtml` 前扫描 `<img>`，凡带 `width`/`height`/`style` 的 → 存表替换为 `ZZIMGn` 占位符；`toMarkdown` 后还原为字面 `<img width="400" src="..." alt="...">`。无尺寸的图照旧走 md4c → `![](src)`。
- **toHtml（加载）**：MD 中的字面 `<img width>` 经 `setMarkdown`，必要时同样占位符保护其不被 Qt 丢弃；TipTap `ResizableImage.parseHTML` 读回 width。

> ⚠ **本方案最高风险点**：Qt md4c 对字面 `<img width>` 的处理行为未知。
> **兜底**：P2 第一步即用 TDD 写往返测试（`<img src=x.png width="400">` → MD → HTML → width 仍在），测试红则迭代占位符实现直到转绿。**测试转绿前不写 NodeView**——把风险在写 NodeView 之前消除。

### 3.3 改动清单

| 层 | 文件 | 改动 |
|----|------|------|
| 打包入口 | `tiptap-entry.js` | `ResizableImage` 若需独立模块则在入口暴露（否则在 editor.js 内用 `TipTapNode.create`） |
| TipTap 扩展 | `editor.js` | 新增 `buildResizableImage()` 替换 `TipTapImage.configure(...)`；NodeView + resize 手柄 + 拖拽逻辑 |
| 命令 | `editor.js` | `setImageWidth(px)`；`insertImage` 适配新节点名 |
| CSS | `editor.css` | `.resize-handle` 样式（亮/暗主题） |
| 往返 | `MarkdownHandler.{h,cpp}` | `ZZIMG` 占位符：toHtml/fromHtml 双向 `<img width>` 保护 |

### 3.4 测试

- **`tests/test_markdownhandler.cpp` 新增往返用例**：带 width 的 `<img>` 往返保真；无 width 走 `![]()`；width+alt 同存；width 数值规范化（`400` vs `400px`）。
- NodeView 拖拽行为：可选 vitest（jsdom + 模拟事件），否则手动验证。

---

## 4. 功能三：搜索增强

### 4.1 SearchExtension（ProseMirror plugin + Decoration.inline）

自写 TipTap Extension，`addProseMirrorPlugins()` 返回一个 plugin：

- state field 存 `{ query, flags:{case,regex,wholeWord}, matches:[{from,to}], current }`。
- `apply`：query/flags/doc 变化时（去抖）重算 matches。
- `decorations(state)`：每个匹配 `Decoration.inline(from,to,{class:'search-match'})`；当前匹配额外 `Decoration.inline(...,{class:'search-match-current'})`。
- **位置映射**（标准 prosemirror-search 技法）：遍历 `doc` 文本节点拼接字符串，块边界插入不可匹配分隔符 `\u0000`，记录「字符索引 → doc pos」映射；matcher 在字符串上跑，结果 `[start,end)` 映射回 doc 区间。块边界分隔符保证不跨段匹配。
- editorApi 新增：`startSearch(query,flags)`、`searchNext()`、`searchPrev()`（循环回绕 + `scrollIntoView`）、`replaceOne(rep)`、`replaceAll(rep)`、`stopSearch()`。

### 4.2 searchMatcher 纯函数（核心算法）

```js
// 输入：含 \u0000 块分隔的拼接文本；输出：匹配区间 [{start,end}]
searchMatcher(text, query, { regex, wholeWord, caseSensitive }) → [{ start, end }]
```

正则模式按 flags 构建 `RegExp`（`g` + 可选 `i`），非法正则返回 `[]`；纯文本模式先转义。**全词用 Unicode 词边界后置过滤**（非 `\b` 包裹）：`isWordChar = /[\p{L}\p{N}_]/u`，匹配 `[s,e)` 需两侧均非词字符；字面/正则模式统一此后置过滤（不破坏正则的 `|`/锚点）。**纯函数、无 ProseMirror 依赖、vitest 重点覆盖。**

### 4.3 replaceOne / replaceAll（含反向引用）

- `replaceOne(rep)`：取当前匹配原文 `m`；regex 模式用 `m.replace(regex, rep)` 解析 `$1/$2/$&`；非 regex 模式 rep 按字面。`tr.insertText(resolved, from, to)`，重算 matches 并前进到下一个。
- `replaceAll(rep)`：从文档末尾向前替换（避免位置漂移），逐个用同法解析；统计次数回传。

### 4.4 SearchBar UI 重构

折叠式两行布局：

```
[ 查找...     ] [Aa][.*][W|] [↑][↓] [3/12] [✕]      ← 第一行始终显示
[ 替换为...   ] [替换][全部替换]                     ← 第二行 Ctrl+H 展开
```

- `findEdit.textChanged` → 增量查找（实时重算）。
- toggles：`Aa`（区分大小写，复用现有）、`.*`（正则）、`W|`（全词）。
- 计数 label 显示 `current/total`，无匹配时 `0/0`（禁用导航/替换按钮）。
- SearchBar 信号重构为携带全部参数：`searchParamsChanged(text, caseSensitive, regex, wholeWord)`、`findNextRequested()`、`findPrevRequested()`、`replaceRequested(rep)`、`replaceAllRequested(rep)`、`replaceModeToggleRequested(bool)`、`closeRequested()`。

### 4.5 C++↔JS 协议（新 bridge 接口）

| 方向 | 接口 | 说明 |
|------|------|------|
| C++→JS | `editorApi.startSearch(query, case, regex, wholeWord)` | 触发匹配 + 高亮 |
| C++→JS | `editorApi.searchNext()` / `searchPrev()` | F3/Enter/按钮 |
| C++→JS | `editorApi.replaceOne(rep)` / `replaceAll(rep)` | 替换 |
| C++→JS | `editorApi.stopSearch()` | 关闭清高亮 |
| JS→C++ | `bridge.searchMatchChanged(total, current)` | 新 slot→signal，回传计数 → SearchBar 显示 `n/m` |

### 4.6 快捷键

| 键 | 动作 |
|----|------|
| Ctrl+F | 显示搜索栏（仅查找模式） |
| Ctrl+H | 显示搜索栏 + 展开替换行 |
| F3 / Enter | 下一个匹配 |
| Shift+F3 / Shift+Enter | 上一个匹配 |
| Esc | 关闭搜索栏（模态对话框优先级更高，沿用现有分流） |

### 4.7 测试

- **vitest `searchMatcher`**：大小写、正则、wholeWord、空查询、跨 `\u0000` 分隔不匹配、重叠匹配、特殊字符转义。
- **vitest SearchExtension decoration**：用真实 `EditorState` + schema 在 Node 跑，断言 decoration 区间与 class（`search-match` / `search-match-current`）；next/prev 回绕；replaceOne/replaceAll 含 `$1` 反向引用后文档内容正确、计数正确。
- SearchBar 折叠/信号逻辑：可选 QtTest（无 GUI 纯逻辑部分）。

---

## 5. 跨切面：测试体系与 CI

- **JS 测试基建（P0）**：`src/resources/web/` 加 vitest（devDep）+ jsdom（Node 下跑 ProseMirror state 所需）；`package.json` 加 `"test": "vitest run"`；测试文件 `*.test.js` 与源码同目录。纯函数 + prosemirror-state 在 Node 下即可，无需真实编辑器 DOM。
- **C++ 测试**：新增 `tests/test_imagemanager.cpp`；`test_markdownhandler.cpp` 加图片往返用例；`test_editorbridge.cpp` 加图片/搜索 slot 转发用例。`tests/CMakeLists.txt` 注册新可执行。
- **CI（`.github/workflows/build-deb.yml`）**：去掉 `-DBUILD_TESTS=OFF`；在 Qt 构建前加 `cd src/resources/web && npm ci && npm test`；`ctest` 接入。ubuntu-24.04 默认带 node；Qt 6.4.2 够用（图片/搜索不依赖更高版本 API）。
- **本地**：`cd build && cmake --build . && ctest --output-on-failure`；`cd src/resources/web && npm test`。

---

## 6. 执行顺序（分阶段，每期独立可测可提交）

| 阶段 | 内容 | 依赖 | 可测产出 |
|------|------|------|----------|
| **P0** | 测试基建：vitest 接入 + CI 启用 ctest/npm test | 无 | 空 vitest 跑通、CI 绿 |
| **P1** | 图片落盘：ImageManager + requestId 协议 + handlePaste/handleDrop | P0 | `test_imagemanager` + bridge 转发测试；手动验粘贴/拖拽 |
| **P2** | 图片缩放：`<img width>` 往返占位符（TDD 先行）→ ResizableImage NodeView | P0（紧随 P1，共用图片节点） | `test_markdownhandler` 图片往返用例绿 → 再做 NodeView |
| **P3** | 搜索增强：searchMatcher → SearchExtension → SearchBar 重构 → 快捷键 | P0 | vitest matcher + decoration；手动验 UI |

---

## 7. 主要风险与对策

| 风险 | 对策 |
|------|------|
| Qt md4c 对 `<img width>` 处理未知 → 往返丢尺寸 | **P2 第一步 TDD 往返测试**，红则迭代占位符；测试转绿前不写 NodeView |
| requestId 协议是项目首个 JS↔C++ round-trip，易错 | 复用现有 slot→signal 模式；并发用 `_pending` 注册表；先写 bridge 转发测试 |
| 搜索 doc 文本 ↔ 位置映射复杂 | 标准 `\u0000` 分隔技法；matcher 与映射分离，各自 vitest |
| 相对路径在不同打开方式下失效 | 始终相对 `.md` 所在目录；导出解析相对路径时一并处理 |
| CI 跑测试拖慢构建 | 仅在 PR/push 跑；vitest 仅纯逻辑，秒级 |
| 未保存文档场景的 UX 生硬（提示文本进文档） | 提示文本作为暂态插入后可立即 undo；或改用状态栏 toast（实现时择优） |

---

## 8. 决议落地（2026-08-14 确认；图片相关未决项随 P1/P2 已落地，从略）

- ✅ **全词匹配 = Unicode 词边界后置过滤**（中文友好），非 ASCII `\b`。
- ✅ **替换纳入本阶段**：查找/替换统一走新引擎，修复全部替换位置漂移，支持 `$1/$2/$&`。
- ✅ **vitest 纳入本阶段**：devDep + jsdom + CI npm test；searchMatcher/Decoration 走 TDD。
- ⬜ **源码模式搜索本阶段不纳入**：仅作用于 WYSIWYG 文档；源码模式打开搜索栏给状态栏提示。
- 去抖 150ms、导航回绕 + 选区 + scrollIntoView、Ctrl+F 仅查找 / Ctrl+H 展开替换行（设计 §4.6）。
