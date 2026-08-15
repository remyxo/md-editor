# 工具栏下拉折叠设计：对齐 + 上下标（WPS 风格）

日期：2026-08-15
状态：待评审

## 背景与目标

1920×1080 屏幕下工具栏图标已铺满。将低频的「左/中/右对齐」三按钮、「上标/下标」两按钮分别折叠为带下拉菜单的单一按钮，参照 WPS Office 的交互模式。净减 4 个图标位（3→1、2→1）。

## 交互设计

### 对齐按钮（objectName: `alignButton`）

- **图标**：跟随光标处段落对齐方式切换（align-left / align-center / align-right.svg）；无显式对齐属性时默认 align-left
- **单击行为**：任意位置单击即弹出菜单（与「导出」按钮一致，InstantPopup 模式）
- **菜单项**（三段式：图标 + 菜单名 + 快捷键文字）：
  - `左对齐(L)`　快捷键 `Ctrl+Shift+L`
  - `居中对齐(E)`　快捷键 `Ctrl+Shift+E`
  - `右对齐(R)`　快捷键 `Ctrl+Shift+R`
  - 当前对齐方式对应的菜单项打勾
- **注意**：快捷键文字仅作显示。这些快捷键由 TipTap TextAlign 扩展在 JS 侧原生实现（已在 tiptap-all.js 中生效），不由 QMenu 的 setShortcut 触发（避免与 WebEngine 焦点冲突的双重触发）

### 上下标按钮（objectName: `subsupButton`）

- **图标**：
  - 光标在下标内容 → `subscript.svg`
  - 光标在上标内容 → `superscript.svg`
  - 两者都不是 → **默认 `subscript.svg`**（参照 WPS 默认设计，用户确认）
- **checked 高亮**：光标处于上标或下标内容时按钮 checked（复用现有 `QToolButton:checked` QSS 背景高亮），否则不勾选
- **菜单项**：
  - `上标(P)`　快捷键 `Ctrl+.`
  - `下标(B)`　快捷键 `Ctrl+,`
  - 当前项打勾（同上下标都激活时两个都勾）
  - 同样仅显示，实际按键由 TipTap Subscript/Superscript 扩展处理

## 实现结构

### ToolBar（`src/ui/ToolBar.cpp` / `.h`）

删除：
- `m_alignLeftAction` / `m_alignCenterAction` / `m_alignRightAction` 三个 QAction 与 `m_alignGroup`
- `m_subscriptAction` / `m_superscriptAction` 两个 QAction

新增（完全仿 `m_exportButton` 现有模式）：
- `QToolButton *m_alignButton`：`setMenu` 三项菜单、`setArrowType(Qt::NoArrow)`、`installEventFilter` 弹出
- `QToolButton *m_subsupButton`：同上，两项菜单
- 两个按钮 `setCheckable(true)`（导出按钮是 false，这里需要 checked 高亮回显）

状态同步（`updateFormatState(const QJsonObject &)`）：
- `textAlign` 字段 → 切 `m_alignButton` 图标 + 菜单项勾选（默认 left）
- `subscript` / `superscript` 字段 → 切 `m_subsupButton` 图标 + `setChecked` 任一激活即 true + 菜单项勾选

其他适配：
- `setSourceMode(bool)`：两个新按钮随其他编辑按钮一起禁用（导出按钮维持始终可用）
- `applyIconTheme()`：两按钮图标随 dark/light 主题切换（`m_isDark` 判路径）
- 信号接口不变：`alignSelected(QString)`、`subscriptClicked()`、`superscriptClicked()` 保留 → **MainWindow 零改动**

### QSS（`light.qss` / `dark.qss`）

两主题各新增与 `#pdfAction` 完全一致的规则（同尺寸、同 chevron-down.svg、仅配色不同）：

```css
/* light.qss */
QToolButton#alignButton,
QToolButton#subsupButton { color: #5f6368; }
QToolButton#alignButton::menu-indicator,
QToolButton#subsupButton::menu-indicator {
    image: url(:/icons/light/chevron-down.svg);
    subcontrol-position: right bottom;
    subcontrol-origin: margin;
    bottom: 4px; right: 2px;
    width: 8px; height: 5px;
}
```

dark.qss 同结构，`color: #c4c9ce`，图标路径 `:/icons/dark/chevron-down.svg`。配色同现有「结构按钮」深灰组。

### 图标资源

全部复用现有 SVG（`align-left/center/right.svg`、`subscript.svg`、`superscript.svg`、`chevron-down.svg`，light/dark 两套已存在），无新增资源。

## 菜单项三段式的实现要点

- `QAction::setIcon()` → 左侧图标
- `QAction::setText("左对齐(&L)")` → 菜单名 + Alt 助记
- `QAction::setShortcut(QKeySequence("Ctrl+Shift+L"))` → 右侧快捷键文字
- 菜单弹出方式与导出一致：`eventFilter` 拦截 `MouseButtonPress` → `menu()->popup(...)`；快捷键显示但 **Widget 快捷键上下文不激活**（QMenu 内 QAction 不加入主窗口，不会抢 WebEngine 的按键）

## 错误处理与边界

- 光标处段落无 textAlign 属性（普通左对齐段落）→ 显示左对齐图标，菜单勾「左对齐」
- `setSourceMode(true)` 禁用后菜单不可弹出（Qt 自动：禁用按钮不产生 press 事件）
- 主题切换时若正显示 center/right 图标，需按当前记录的对齐状态重新解析主题路径（实现时在 `applyIconTheme` 中按当前状态选图标，而非固定路径）

## 测试计划

现有 C++ 测试（4 个）与 JS 测试（28 个）不涉及 ToolBar UI，回归通过即可。手动验证清单：

1. 两按钮单击弹菜单，菜单项三段式显示正确（图标/名称/快捷键）
2. 选择对齐/上下标后编辑区内容正确变化
3. 光标在居中段落 → 对齐按钮变居中图标且菜单勾「居中对齐」
4. 光标在下标内容 → 上下标按钮变下标图标 + checked 高亮 + 菜单勾「下标」
5. 默认状态：对齐按钮左对齐图标，上下标按钮下标图标，无高亮
6. 暗色主题下两按钮图标与 chevron 箭头正确切换
7. 源码模式下两按钮灰化，切回恢复
8. 快捷键 Ctrl+Shift+L/E/R、Ctrl+, / Ctrl+. 仍直接生效（TipTap 侧），无双重触发
9. 1920×1080 下工具栏总宽度明显缩短（净减 4 个图标位）

## 不做的事（YAGNI）

- 不折叠其他按钮（后续视效果再议）
- 不实现「点主体直接应用」的分区域点击
- 不新增图标资源
- 不改 MainWindow / editor.js（信号与格式 JSON 均不变）
