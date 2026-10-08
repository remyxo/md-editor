# 插入菜单公式项改造 + 导出 DOCX 快捷键 设计

- 日期：2026-10-08
- 状态：设计已确认（2026-10-08），待实现
- 版本目标：不涉及（用户未要求推进版本号）

---

## 1. 需求

1. 插入菜单「公式」改名为「行内公式」，并在其下方新增「块级公式」菜单项，快捷键 Ctrl+Shift+M。
2. 导出 DOCX 增加快捷键（用户选定 Ctrl+Shift+D）。
3. （2026-10-08 追加）导出 HTML 增加快捷键：Ctrl+Shift+H（用户选定，全应用空闲已核）。

## 2. 现状（关键事实）

- **块级公式能力已存在**：`editorApi.insertMath('block')`（`src/resources/web/editor.js:3602`）打开块级公式编辑面板；`MainWindow.cpp:169-174` 已注册 Ctrl+Shift+M `QShortcut` 触发它，但**没有菜单入口**。
- 行内公式：菜单 `公式(&M)`（`MenuBar.cpp:141`）带 Ctrl+M，信号 `MenuBar::insertMath` → `MainWindow` → `insertMath('inline')`。
- Ctrl+M 由菜单 QAction 携带快捷键、不另建 QShortcut——两者并存会报 "Ambiguous shortcut"（见 `MainWindow.cpp:144-145`、`169-170` 注释先例）。
- 导出菜单：PDF 有 Ctrl+P（`MenuBar.cpp:46`）；DOCX / HTML 无快捷键。Ctrl+Shift+D 全应用空闲（已核对全部 QKeySequence 与工具栏仅显示用的快捷键）。
- 帮助文档 `markdown-guide.md:344/683-684` 已记载「Ctrl+M 插入行内公式 / Ctrl+Shift+M 插入块级公式」，与本次命名一致，**不需改动**。
- 英文翻译 `src/resources/i18n/md-editor_en_US.ts` 含 `公式(&M)`（→ "&Formula"）与 `插入公式 (Ctrl+M)` 条目。

## 3. 设计

### 3.1 插入菜单

- `m_mathAct` 文案：`公式(&M)` → `行内公式(&M)`（保留 Ctrl+M 与助记符 M，Alt+M 行为不变）。
- 紧随其后新增 QAction `m_mathBlockAct`：`块级公式(&B)`（B = Block 助记符），快捷键 Ctrl+Shift+M。
- MenuBar 新增信号 `void insertMathBlock();`。
- `MainWindow::connectSignals()`：连接 `insertMathBlock` → `runEditorCommand("window.editorApi.insertMath('block');")`。
- **删除** `MainWindow.cpp:169-174` 的 Ctrl+Shift+M `QShortcut`——快捷键改由菜单 QAction 携带（与 Ctrl+M 同一先例），避免与菜单动作重复触发 Ambiguous shortcut；注释同步更新。
- 两处文案（`setupUi()` 与 `retranslateUi()`）同步修改。

### 3.2 导出快捷键（DOCX / HTML）

- `m_exportDocxAct->setShortcut(QKeySequence("Ctrl+Shift+D"));`（`MenuBar.cpp:48` 附近）。
- `m_exportHtmlAct->setShortcut(QKeySequence("Ctrl+Shift+H"));`（`MenuBar.cpp:50` 附近；追加需求，冲突核查：仅 Ctrl+H「替换」近邻，不构成冲突）。

### 3.3 一致性附带

- 工具栏 ∑ 按钮提示：`插入公式 (Ctrl+M)` → `插入行内公式 (Ctrl+M)`（`ToolBar.cpp:234` 与 `retranslateUi()` 内两处）。
- `en_US.ts` 同步：
  - `公式(&M)` 条目替换为 `行内公式(&M)`（译文 "Inline &Formula"），新增 `块级公式(&B)`（译文 "&Block Formula"）。
  - `插入公式 (Ctrl+M)` 改为 `插入行内公式 (Ctrl+M)`（译文 "Insert Inline Formula (Ctrl+M)"）。
- `markdown-guide.md` 快捷键表补导出三行（PDF `Ctrl+P` / DOCX `Ctrl+Shift+D` / HTML `Ctrl+Shift+H`），保持文档与行为同步。

### 3.4 不做的事

- 不推进版本号、不动 CHANGELOG（版本推进须用户明确指示）。
- 不新增工具栏块级公式按钮；导出 PDF 快捷键维持 Ctrl+P 不变。

## 4. 验证计划

1. 重新编译 `cmake --build build`（C++ 改动，CLAUDE.md 原则 9）。
2. Xvfb 无头启动，确认 stderr 无 "Ambiguous shortcut" 警告。
3. 界面目视：插入菜单显示「行内公式 Ctrl+M」「块级公式 Ctrl+Shift+M」；文件→导出 显示「导出 DOCX... Ctrl+Shift+D」「导出 HTML... Ctrl+Shift+H」；键盘触发 Ctrl+Shift+M 弹出块级公式面板、Ctrl+Shift+D / Ctrl+Shift+H 弹出保存对话框。
4. 英文界面复验新译文（切换到 English 后两菜单项文案正确）。
