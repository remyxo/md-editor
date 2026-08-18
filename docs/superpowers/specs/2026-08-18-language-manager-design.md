# LanguageManager（中英文运行时切换）设计文档

- 日期：2026-08-18
- 状态：已确认（设计分节评审通过）
- 关联：`docs/superpowers/specs/2026-08-17-follow-system-theme-design.md`（同类「运行时切换」的既有范式，主题链路已打通）

## 背景与目标

md-editor 目前全量硬编码中文文案，设置对话框外观页已有「界面语言」下拉（简体中文 / English），但无 `QTranslator` 翻译资源，切换**不生效**（半成品）。本设计实现真正的中英文运行时切换：

1. **新增 `LanguageManager`**：统一语言状态、`QTranslator` 装载、持久化，发 `languageChanged` 信号。
2. **全组件 `retranslateUi()`**：菜单/工具栏/状态栏/侧边栏/设置/搜索/右键菜单/对话框全部支持运行时重译。
3. **editor.js 字典 + `setLanguage`**：Web 编辑器内的弹窗、占位符、NodeView 标签随语言切换。
4. **设置对话框接线**：把已有但失效的 English 下拉项接通。

## 核心概念：源语言 = 中文，目标语言 = 英文

现有代码里的中文字符串**本身就是源字符串**。`tr("文件")` 的 source 即「文件」，英文翻译写在 `md-editor_en_US.qm` 里。

| 语言 | 取值 | 装载 | 存储 |
|------|------|------|------|
| 简体中文 | `zh_CN` | 不装 translator（源字符串直通） | QSettings `appearance/language` |
| English | `en_US` | 装载 `md-editor_en_US.qm` | 同上 |

`LanguageManager` 只认语言代码；「当前语言」持久化于 QSettings，缺省 `zh_CN`。

## 架构

```
                    ┌─ 设置对话框外观页「界面语言」下拉（已有，本次接通）
用户入口 ────────────┤
                    └─ （无菜单入口；语言选择仅在设置页，与现状一致）

                          appearance/language = "zh_CN"|"en_US"
                                │
                 MainWindow::onLanguageSwitch(lang)
                                │
              ┌─────────────────┼──────────────────────┐
              ▼                 ▼                      ▼
     LanguageManager(新)    全组件 retranslateUi()   所有 tab
     switchLanguage(lang)   （菜单/工具栏/状态栏/    setEditorLanguage()
     装/卸 QTranslator       侧边栏/搜索/设置/右键）  (editor.js 字典 + 重刷)
```

语言切换是一次性的全局动作（仅设置页触发），不做「跟随系统」——主题的 system 模式与本设计无关，保持正交。

## 组件设计

### ① LanguageManager（新增，`src/app/`，与 ThemeManager 同层）

```cpp
class LanguageManager : public QObject {
    Q_OBJECT
public:
    explicit LanguageManager(QObject *parent = nullptr);
    QString currentLanguage() const;             // "zh_CN" | "en_US"
    QStringList availableLanguages() const;      // {"zh_CN", "en_US"}
public slots:
    void switchLanguage(const QString &lang);
signals:
    void languageChanged(const QString &lang);
private:
    QString m_currentLanguage;
    QTranslator m_translator;   // 成员持有（installTranslator 不接管所有权）
};
```

| 职责 | 说明 |
|------|------|
| 初始装载 | 构造时读 `appearance/language`（缺省 `zh_CN`）；若 `en_US` 则 `load` + `installTranslator`。先于 MainWindow 构造 → 启动即正确语言 |
| 切换 | `switchLanguage`：`removeTranslator` → 若 `en_US` 则 `load(":/i18n/md-editor_en_US.qm")` + `installTranslator`；若 `zh_CN` 则不装。写 QSettings、更新 `m_currentLanguage`、`emit languageChanged` |
| 幂等 | 与当前语言相同时直接返回 |

### ② 翻译资源（标准 Qt 工具链）

- **构建依赖**：`find_package(Qt6 6.4 … LinguistTools)`；本地与 CI 需 `qt6-tools-dev`（提供 `lupdate`/`lrelease`）。`qt6_add_lrelease` 在 Qt 6.2 已引入，6.4.2 可用。
- **源文件**：`src/resources/i18n/md-editor_en_US.ts`，用 `lupdate` 从 `tr()` 提取生成（上下文自动正确），人工填英文 translation。
- **生成**：构建时 `qt6_add_lrelease` 编译 `.ts` → `md-editor_en_US.qm`，打进 qrc（`:i18n/md-editor_en_US.qm`）。
- **装载**：`QTranslator::load(":/i18n/md-editor_en_US.qm")`（qrc 路径可直接 load）。

### ③ C++ 侧：`tr()` + `retranslateUi()`

- 所有用户可见字符串（`setText`/`setToolTip`/`addAction`/`addTab`/`addItem`/`setWindowTitle`/`setPlaceholderText` 等）包 `tr()`。
- 每个组件把字符串赋值从 `setupUi()` 拆到新增的 `retranslateUi()`，`setupUi()` 末尾调用；组件保存文本控件/动作的指针以便重设。
- 受影响组件与要点：

| 组件 | 要点 |
|------|------|
| `MenuBar` | 各菜单/动作（含「最近文件」「导出」「主题」「插入」子项）需存成员或按 objectName 查找后重设 |
| `ToolBar` | 动作已有成员（`m_boldAction` 等）；对齐/上下标/导出/颜色下拉的菜单项与 tooltip 需补齐成员或 objectName 重设；标题下拉项「正文/H1…」重设 |
| `StatusBar` | 初始文案「行 1, 列 1」「字符: 0 / 词: 0」需缓存当前值，`retranslateUi` 按当前值重渲染（`updateCursorPosition`/`updateWordCount` 复用同一模板） |
| `SideBar` / `FileTree` / `OutlineTree` | 标签页名、按钮、右键菜单、输入框标题 |
| `TabWidget` | 「未命名-N」前缀、关闭确认对话框文案 |
| `SearchBar` | 查找/替换占位符、toggle tooltip、替换/全部替换按钮 |
| `SettingsDialog` | 全部标签页/分组/表单标签/按钮；`QDialogButtonBox` 改为显式 `tr("确定")`/`tr("取消")` 按钮 |
| `AboutDialog` | 标题、描述、确定按钮 |
| `EditorTab` | 中文右键菜单（撤销/重做/剪切/复制/粘贴/全选/编辑超链接/访问/表格子菜单） |
| `MainWindow` | 瞬态文案（对话框标题、状态栏消息、QFileDialog 标题）走 `tr()`，下次出现即正确，无需持久重译 |

### ④ JS 侧：`I18N` 字典 + `setLanguage`

- 新增 `I18N` 字典对象（`zh_CN` / `en_US` 两个键集），默认 `zh_CN`；`i18n(key)` 按当前语言取值。
- 弹窗（链接/公式/Mermaid/代码语言/emoji）、占位符、NodeView 标签（复制代码/双击修改语言/双击修改脚注标记/空图表/链接悬浮提示）改用 `i18n()`；**静态标签额外标 `data-i18n="key"`**。
- `setLanguage(lang)`：更新当前语言 → 全量刷 `document.querySelectorAll('[data-i18n]')` 的 `textContent` → 占位符用函数读当前语言并触发重渲染。
- **emoji 分类名保持中文**（本次范围决定：视为数据，不翻译）；emoji 搜索关键词 `kw` 数据不动。
- 语言切换发生在设置对话框（模态，编辑区失焦）期间，编辑器内各弹窗不会同时打开，故**不需对已打开的弹窗做实时重渲染**——下次打开即用新语言。

### ⑤ 设置对话框接线（接通半成品）

- `SettingsDialog` 新增 `languageChanged(const QString &lang)` 信号（仿 `themeModeChanged`），`onAccepted` 发射 `m_langCombo->currentData()`。
- `MainWindow::onShowSettings` 连接该信号到 `onLanguageSwitch`；切换后对话框自身也 `retranslateUi()`（对话框此时正打开）。
- 语言持久化统一由 `LanguageManager::switchLanguage` 完成（与主题经 `onThemeSwitch` 统一持久化同构），`SettingsDialog` 不再自己写 QSettings。

## 数据流

**路径 A — 用户在设置页切换语言**：
```
SettingsDialog.onAccepted → emit languageChanged(lang)
→ MainWindow::onLanguageSwitch(lang)
   → LanguageManager.switchLanguage(lang)     // 装/卸 translator + 写 QSettings + 发信号
   → m_menuBar/toolBar/statusBar/sideBar/searchBar/tabWidget… retranslateUi()
   → 所有 tab setEditorLanguage(lang)          // editor.js setLanguage
   → SettingsDialog.retranslateUi()（对话框自身）
```

**路径 B — 启动恢复**：
```
main() → Application 构造 → LanguageManager 构造（读 appearance/language，装载 translator）
→ MainWindow 构造（各组件 tr() 构造即正确语言）
→ 首个 tab 构造/加载完成后 pageLoadFinished 补推 setEditorLanguage(当前语言)
```

**路径 C — 新建 tab 继承语言**（对齐主题既有缺口修复）：
```
pageLoadFinished → MainWindow 补推 setEditorLanguage(当前语言)
```

## 错误处理

| 场景 | 行为 |
|------|------|
| `.qm` 资源缺失 / load 失败 | `load` 返回 false → 不 install，`qWarning` 日志，界面保持中文（源字符串直通），不崩溃 |
| `.ts` 编译失败（lrelease 语法错） | 构建报错，即时暴露，非运行时问题 |
| 重复切换同一语言 | `switchLanguage` 幂等直接返回 |
| 未知语言代码 | 忽略（不写 QSettings、不发信号），保持当前语言 |

原则：**降级而非报错**。翻译资源缺失时退回中文（源语言），功能完整，仅语言未切换。

## 测试策略

| 层 | 测试 |
|----|------|
| 单元测试（ctest） | ① `LanguageManager::switchLanguage` 幂等与状态/持久化正确 ② 未知语言忽略 ③ `.qm` 缺失时 currentLanguage 不变 |
| 手动验证清单 | ① 设置页切 English → 菜单/工具栏/状态栏/侧边栏/搜索/设置/右键菜单/About 全部变英文 ② 打开 JS 弹窗（链接/公式/Mermaid/代码语言/emoji）文案英文、emoji 分类名仍中文 ③ 新建标签页继承当前语言 ④ 重启记住语言 ⑤ 切回中文全部还原 ⑥ 重编译后二进制生效（qrc 资源） |

## 范围

**范围内**：`LanguageManager` 新组件、标准 Qt 翻译资源（`.ts`/`.qm`/LinguistTools）、全组件 `retranslateUi()`、editor.js 字典 + `setLanguage`、设置对话框接线与持久化、新建 tab 语言补推。

**范围外**：
- Qt 内建标准按钮文案（`QMessageBox` 的 Yes/No、`QFileDialog` 内部按钮、`QColorDialog`/`QFontComboBox` 内部）——需打包 `qtbase_*.qm`，本版不做（现状：这些按钮已恒为英文，本设计不使其更糟）。后续可单独跟进。
- 语言「跟随系统」（本设计仅手动切换，与主题 system 模式正交）。
- Markdown 帮助文档（`markdown-guide.md`）内容翻译。
- 除中英文外的其他语言。

## 版本

按项目版本推进规则，属新增功能（新组件 + 全量重译 + 资源接入），规模偏大，建议 `+0.1.0`。当前处于 0.8.2 批量攒批阶段，最终版本号实现完成后与用户确认再定，同步 CMakeLists / CHANGELOG / tag。
