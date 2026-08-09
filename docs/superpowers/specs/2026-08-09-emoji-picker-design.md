# 表情符号选择器（Emoji Picker）设计

- 日期：2026-08-09
- 版本目标：0.3.1 → **0.3.2**
- 状态：已批准，待实现

---

## 1. 背景与问题

编辑器目前**没有任何插入表情符号（emoji）的入口**——工具栏无图标、菜单无项、图标目录无相关资源。

但 `editor.js` 里已存在一份约 150 条的 `EMOJI_SHORTCODES`（短代码 → Unicode）映射表，并通过 TipTap InputRule 实现了「输入 `:smile:` 自动变 😄」。也就是说能力底层已具备，只是缺一个**可见的选择器入口**。

本次任务：补一个工具栏图标 + 菜单项，点击后弹出**带分类标签 + 搜索框**的 emoji 选择器。

## 2. 方案选型

经讨论，确定采用：**JS 非模态浮层（popover），锚定在工具栏按钮下方，由 Chromium 渲染，点外部 / Esc 关闭。**

### 被否决的方案

| 方案 | 否决理由 |
|------|----------|
| C++ 原生弹出面板（仿 `TableGridPopup`） | Qt 在 Linux 上渲染彩色 emoji 依赖系统 `fonts-noto-color-emoji`，未安装时显示黑白或豆腐块；数百条 emoji 数据写在 C++ 静态结构里维护繁琐 |
| JS 模态弹窗（居中 + 遮罩，仿 `_mermaidEditor`） | 会暗化整个编辑区。对「插一个表情」这种轻量、高频的操作，模态遮罩体验过重 |

### 选定方案的关键点

- 浮层是 QWebEngineView 内的 DOM 元素，由 Chromium 渲染——彩色 emoji 在 Linux 上可靠（Chromium 自带 emoji 渲染，不依赖系统字体）。
- C++ 把工具栏按钮的屏幕坐标换算成 webview 内部坐标，传给 JS 定位 → 锚定在按钮正下方。
- 非模态：不遮挡、不暗化编辑区，编辑区仍可交互（点击编辑区即视为「点外部」→ 关闭）。

## 3. 架构与数据流

```
点击工具栏 [😀]
  → ToolBar::emojiClicked()
  → MainWindow::onInsertEmoji()
       · 通过 objectName "emojiAction" 找到 QAction → widgetForAction 得到按钮
       · 按钮 mapToGlobal(左下角) → webView->mapFromGlobal 换算成 webview 内坐标 (x,y)
       · runJsCommand("window.editorApi.openEmojiPicker(x, y)")
  → editor.js _emojiPicker.open(x, y)
       · 创建/挂载 DOM · 按 (x,y) 定位并钳制（防右/底溢出）
       · 渲染默认分类（笑脸）· 聚焦搜索框
       · bridge.modalDialogChanged(true)   ← 复用「JS 面板要 Esc」标记

用户交互：
  · 输入关键词 → 实时过滤（跨分类匹配 name / keywords / 短代码）
  · 点分类标签 → 切换分类
  · 方向键移动高亮 + Enter，或点击 emoji
       → editor.commands.insertContent(char)
       → addToRecent(char)（内存 + localStorage 持久化）
       → 【浮层保持打开】，搜索框重聚焦，便于连插
  · 点浮层外部 / 按 Esc / 点 ×  → close()

close()
  → 第一时间 bridge.modalDialogChanged(false)
  → 移除 DOM · 编辑器重聚焦
```

## 4. JS 浮层 `_emojiPicker`（editor.js）

仿照 `_mermaidEditor` 的对象结构，但**无遮罩、非模态、定位为浮层**。

> **挂载点**：`_emojiPicker` 作为子对象挂在 `window.editorApi` 上（与 `_mermaidEditor`、`_mathEditor` 一致），故 C++ 侧可用 `window.editorApi._emojiPicker.close()` 调用。

| 方法 | 职责 |
|------|------|
| `buildDom()` | 构建浮层 DOM：顶部搜索框 + 中部可滚动网格 + 底部分类标签条。`position: fixed`，高 `z-index`，**无遮罩 div**。 |
| `open(x, y)` | 挂载 DOM；按 (x, y) 定位并钳制（右边溢出→右对齐；底部溢出→限高并让网格内部滚动）；注册 document 级 `mousedown` 捕获监听（目标不在浮层内→关闭）；`bridge.modalDialogChanged(true)`；聚焦搜索框。**默认选中分类：若「最近使用」非空则选它，否则选「笑脸与情感」。** |
| `render(items)` | 渲染 emoji 列表（分类视图或搜索结果）；记录当前高亮项用于键盘导航。 |
| `filter(query)` | 大小写无关匹配 `name` / `keywords`；有查询时隐藏分类条、平铺所有匹配项；无查询时恢复分类视图。 |
| `selectCategory(id)` | 切换分类并 `render`。 |
| `insert(char)` | `editor.commands.insertContent(char)`；**保持浮层打开**；搜索框重聚焦（保留当前过滤或清空，见 §8）；**插入后把该 emoji 加入「最近使用」**（见下）。 |
| `addToRecent(char)` | 维护最近使用列表：去重后置顶；上限 32 条；写入 `_recent`（内存）并持久化到 `localStorage`（见 §5.1）。触发当前若处于「最近使用」分类则刷新渲染。 |
| `close()` | **第一时间** `bridge.modalDialogChanged(false)`（不等任何异步）；移除 DOM；编辑器重聚焦。 |

### 键盘导航

搜索框内：
- `ArrowDown/ArrowUp/ArrowLeft/ArrowRight`：在网格中移动高亮项（按列数换算行列）。
- `Enter`：插入当前高亮 emoji（无高亮时插入第一个）。
- `Esc`：由 Qt 层 QShortcut 拦截后调用 `_emojiPicker.close()`（见 §6）。

### `editorApi` 新增方法

```js
openEmojiPicker: function(x, y) { window.editorApi._emojiPicker.open(x, y); },
insertEmoji: function(char) {
    if (this.editor) this.editor.commands.insertContent(char);
}
```

> `insertEmoji` 供未来其他入口或测试复用；浮层内部插入直接走 `editor.commands.insertContent`。

## 5. Emoji 数据集 `EMOJI_CATEGORIES`（editor.js）

- 约 **8 个分类**：笑脸与情感 / 手势与人物 / 动物与自然 / 食物与饮品 / 活动 / 旅行与地点 / 物品 / 符号与旗帜。
- 合计约 **300 条**精选常用 emoji。
- 每条结构：`{ char, name, keywords:[] }`，例如 `{ char:'😀', name:'grinning', keywords:['笑脸','开心','smile'] }`。
- 分类结构：`[{ id, icon, name, emojis: [...] }, ...]`。

### 5.1 「最近使用」一栏

- 作为分类标签条的**第一项**，`id = 'recent'`，仅当列表非空时显示。
- 数据：内存数组 `_emojiPicker._recent`（最近在前、去重、上限 **32**）。
- 持久化：写入 QWebEngineView 的 `localStorage`（键 `md-editor:emoji-recent`），`try/catch` 包裹——不可用或读取失败时降级为仅内存。这样「最近使用」在重启/重开文档后仍保留。
- `insert(char)` 成功插入后调 `addToRecent(char)`。
- `open()` 时默认选中：`_recent` 非空 → 选 `recent`；否则选第一个分类（笑脸）。

### 与现有 `EMOJI_SHORTCODES` 的关系

**独立，不合并、不改动 `EMOJI_SHORTCODES`。**

`EMOJI_SHORTCODES` 服务于已生效的 `:shortcode:` InputRule，改动它有破坏既有功能的风险。`EMOJI_CATEGORIES` 是新增数据，专门服务选择器。两者职责不同，本版保持分离；后续如需统一，再以 `EMOJI_CATEGORIES` 为单一数据源重构 InputRule（不在本次范围）。

## 6. C++ 接线

### 6.1 资源

- 新建 `src/resources/icons/light/emoji.svg`：viewBox `0 0 24 24`、`fill="none"`、`stroke="#222"`、`stroke-width="2"`、圆头连接，画一张笑脸（圆 + 两点 + 弧线嘴）。
- 新建 `src/resources/icons/dark/emoji.svg`：同上，`stroke="#d0d4d8"`。
- `src/resources/app.qrc`：light 区与 dark 区各加一行 `<file>icons/{light,dark}/emoji.svg</file>`。

### 6.2 工具栏 `ToolBar`

- `ToolBar.h`：`signals:` 增加 `void emojiClicked();`。
- `ToolBar.cpp`：
  - 在「插入按钮」区块加 `makeAction(this, &m_iconPaths, "emojiAction", "插入表情", ":/icons/light/emoji.svg");`（objectName = `emojiAction`）。
  - 在 `findChildren<QAction*>` 信号连接循环里加：`else if (a->objectName() == "emojiAction") connect(a, &QAction::triggered, this, &ToolBar::emojiClicked);`
  - 主题切换自动生效（`makeAction` 已登记 `m_iconPaths`）。

### 6.3 菜单栏 `MenuBar`

- `MenuBar.h`：`signals:` 增加 `void insertEmoji();`。
- `MenuBar.cpp`：在 `m_insertMenu` 末尾增加：
  ```cpp
  QAction *emojiAct = m_insertMenu->addAction("表情符号...");
  connect(emojiAct, &QAction::triggered, this, &MenuBar::insertEmoji);
  ```
  暂不绑定快捷键（避免与现有 Ctrl+M/Ctrl+K 等冲突）。

### 6.4 主窗口 `MainWindow`

- `connectSignals()`：
  ```cpp
  connect(m_toolBar, &ToolBar::emojiClicked, this, [this]() { onInsertEmoji(); });
  connect(m_menuBar, &MenuBar::insertEmoji, this, [this]() { onInsertEmoji(); });
  ```
- 新增 `onInsertEmoji()`（私有槽或 lambda 包装）：
  ```cpp
  void MainWindow::onInsertEmoji() {
      EditorTab *tab = m_tabWidget->currentEditorTab();
      if (!tab) return;
      // 定位锚点：工具栏 emoji 按钮的左下角；菜单触发时无按钮，用光标位置兜底
      QAction *a = m_toolBar->findChild<QAction*>("emojiAction");
      QWidget *btn = a ? m_toolBar->widgetForAction(a) : nullptr;
      QPoint gbl = btn ? btn->mapToGlobal(QPoint(0, btn->height())) : QCursor::pos();
      QPoint local = tab->webView()->mapFromGlobal(gbl); // 默认 zoom=1.0：CSS px = 部件 px
      QString js = QString("window.editorApi.openEmojiPicker(%1, %2);")
                       .arg(local.x()).arg(local.y());
      tab->runJsCommand(js);
  }
  ```
  > 需在 `MainWindow.h` 声明 `void onInsertEmoji();`。
- Esc 分支（`MainWindow.cpp:106-115`）的 modal 命令追加关闭浮层：
  ```cpp
  runEditorCommand("window.editorApi._mermaidEditor.close();"
                   "window.editorApi._mathEditor.close();"
                   "window.editorApi._emojiPicker.close();");
  ```

## 7. Esc 机制复用说明

浮层是非模态的，但仍复用 `bridge.modalDialogChanged(true/false)` 与 `MainWindow::m_modalDialogOpen` 标记，使顶层 `QShortcut("Esc")` 能关闭它。

- 标记的实质含义从「模态对话框打开」**放宽为「某个 JS 浮层 / 面板打开、希望接管 Esc」**。
- 因浮层无遮罩，用户在编辑区正常编辑；按 Esc 时 `m_modalDialogOpen == true` → 关闭浮层（而非隐藏搜索栏），行为正确。
- 待办：在 `docs/modal-dialog-esc-handling.md` 补一节说明 emoji 浮层复用此标记（变量语义放宽）。

## 8. 边界情况

- **定位钳制**：`x + 宽度 > innerWidth` → 改为右对齐（`right: ...` 或 `left = innerWidth - 宽度 - 边距`）；`y + 高度 > innerHeight` → 限高，让中部网格内部滚动。
- **编辑器失焦后插入**：ProseMirror 在编辑器失焦时仍保留选区状态，`editor.commands.insertContent(char)` 在原光标处插入。
- **连插**：点 emoji 后浮层不关闭、搜索框重聚焦；插入后是否清空搜索框——**清空**（每次插入后回到当前分类视图，便于继续按视觉挑选）。
- **最近使用**：去重置顶，上限 32 条；持久化到 `localStorage`。`localStorage` 不可用时降级为仅内存（不影响功能）。重开应用/文档后「最近使用」分类仍可显示历史记录。
- **切换标签页**：若用户切换文档标签，应关闭当前浮层。可在 `EditorTab` 失焦/隐藏或标签切换时调 `close()`（实现时确认最佳挂载点；最简方案：`open` 时记录所属 tab，切换时由 C++ 触发关闭）。
- **主题切换**：emoji 是 Unicode 字符，与主题无关；仅工具栏/菜单图标需 light/dark 两份（已在 §6.1 覆盖）。
- **只读模式**：只读（阅读）模式下点 emoji 应无效果或关闭入口。`onInsertEmoji` 中检查 `tab->isPermanentReadOnly()`，是则直接 return（与阅读模式其他编辑入口一致）。

## 9. 测试

JS 浮层难以在 C++ `ctest` 中自动化，采用**手动验证清单**：

1. 工具栏出现 emoji 图标，亮/暗主题切换图标颜色正确。
2. 菜单「插入 > 表情符号...」存在并可触发。
3. 点工具栏图标 → 浮层出现在按钮正下方，不遮挡/暗化编辑区。
4. 搜索框输入关键词（如「心」「heart」）→ 实时过滤，跨分类命中。
5. 点分类标签 → 切换显示对应分类。
6. 方向键移动高亮，Enter 插入当前项；点击 emoji 插入。
7. 插入后浮层保持打开，可连插多个；搜索框清空回到分类视图。
8. Esc → 关闭浮层；点浮层外部（含编辑区）→ 关闭。
9. 插入的 emoji 在 MD↔HTML 往返、保存重开后正确保留。
10. 只读模式下入口不生效。
11. 切换文档标签页时浮层关闭（不残留到下一个文档）。
12. 「最近使用」：插入若干 emoji 后，该栏按最近在前显示、去重；重启应用重开浮层仍可见历史（`localStorage` 持久化生效）。

`MarkdownHandler` 无需修改——emoji 是纯 Unicode 文本，HTML 往返无损。

## 10. 非目标（YAGNI，本版不做）

- 肤色 / 性别修饰符组合
- 自定义 / 上传 emoji
- 快捷键绑定（如 Ctrl+Shift+E）
- 以 `EMOJI_CATEGORIES` 统一重构 `EMOJI_SHORTCODES` InputRule

## 11. 受影响文件清单

| 文件 | 类型 | 改动 |
|------|------|------|
| `src/resources/icons/light/emoji.svg` | 新建 | 笑脸图标（stroke #222） |
| `src/resources/icons/dark/emoji.svg` | 新建 | 笑脸图标（stroke #d0d4d8） |
| `src/resources/app.qrc` | 修改 | 加 2 行图标登记 |
| `src/ui/ToolBar.h` / `.cpp` | 修改 | emojiClicked 信号 + 按钮 + 信号连接 |
| `src/ui/MenuBar.h` / `.cpp` | 修改 | insertEmoji 信号 + 菜单项 |
| `src/ui/MainWindow.h` / `.cpp` | 修改 | onInsertEmoji 定位 + 连接 + Esc 分支追加 close |
| `src/resources/web/editor.js` | 修改 | EMOJI_CATEGORIES + _emojiPicker + editorApi 两个方法 |
| `CMakeLists.txt` | 修改 | 版本 0.3.1 → 0.3.2 |
| `docs/modal-dialog-esc-handling.md` | 修改 | 补充 emoji 浮层复用 modalDialogChanged 标记的说明 |

无需改 `src/CMakeLists.txt`（无新增 C++ 源文件，选择器在 JS 侧）。无需 esbuild（`editor.js` 直接加载）。
