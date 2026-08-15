# 工具栏对齐/上下标下拉折叠 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 将工具栏「左/中/右对齐」三按钮、「上标/下标」两按钮分别折叠为仿「导出」样式的下拉按钮（WPS 风格：图标跟随光标状态 + 三段式菜单项），净减 4 个图标位。

**Architecture:** 完全复用现有 `m_exportButton` 模式（QToolButton + QMenu + eventFilter 弹出 + QSS menu-indicator chevron）。删 5 个 QAction，加 2 个 QToolButton；信号接口不变，MainWindow/editor.js 零改动。

**Tech Stack:** Qt 6 Widgets（QToolButton/QMenu/QSS），C++。

**Spec:** `docs/superpowers/specs/2026-08-15-toolbar-dropdown-consolidation-design.md`

## Global Constraints

- 所有注释使用中文，标识符英文（CLAUDE.md 规则）
- QSS chevron 规则必须与 `#pdfAction` 完全同规格：chevron-down.svg、`subcontrol-position: right bottom`、`subcontrol-origin: margin`、`bottom: 4px; right: 2px`、`width: 8px; height: 5px`
- 配色：light `#5f6368` / dark `#c4c9ce`（同「结构按钮」深灰组）
- 菜单快捷键文字仅显示（TipTap JS 侧已原生实现 Ctrl+Shift+L/E/R、Ctrl+, / Ctrl+.），不得让 QMenu shortcut 抢键
- 不改 MainWindow、editor.js、信号签名
- 版本号/CHANGELOG/tag 不动（用户安排批量更新）
- 提交前缀 `feat:`，中文描述

---

### Task 1: ToolBar.h —— 成员与辅助声明更新

**Files:**
- Modify: `src/ui/ToolBar.h`

**Interfaces:**
- Produces: `m_alignButton`、`m_subsupButton`（`QToolButton*`）、`m_alignMenu`、`m_subsupMenu`（`QMenu*`）、私有辅助 `void setAlignIcon(const QString &alignment)`、`void setSubsupIcon(bool sub, bool sup)`、`void rebuildAlignMenuChecks(const QString &alignment)`、`void rebuildSubsupMenuChecks(bool sub, bool sup)`、成员 `QString m_currentAlign`（记录当前对齐状态供主题切换用）
- 删除成员：`m_alignLeftAction`、`m_alignCenterAction`、`m_alignRightAction`、`m_alignGroup`、`m_subscriptAction`、`m_superscriptAction`

- [ ] **Step 1: 编辑 ToolBar.h**

删除（75-78 行区域）：

```cpp
    QAction *m_alignLeftAction;
    QAction *m_alignCenterAction;
    QAction *m_alignRightAction;
    QActionGroup *m_alignGroup;
```

`m_subscriptAction`、`m_superscriptAction` 若在头文件有成员声明一并删除（当前它们未声明为成员，仅构造函数局部——确认后无需改动此处）。

替换为：

```cpp
    QToolButton *m_alignButton = nullptr;   // 对齐下拉按钮（左/中/右折叠）
    QToolButton *m_subsupButton = nullptr;  // 上下标下拉按钮（上标/下标折叠）
    QMenu *m_alignMenu = nullptr;
    QMenu *m_subsupMenu = nullptr;
    QString m_currentAlign = QStringLiteral("left");  // 当前对齐状态，主题切换时重选图标
    // 按对齐状态设置按钮图标（light/dark 由 m_isDark 决定）
    void setAlignIcon(const QString &alignment);
    // 按上下标激活状态设置按钮图标与 checked 高亮
    void setSubsupIcon(bool sub, bool sup);
    // 同步菜单项勾选状态
    void rebuildAlignMenuChecks(const QString &alignment);
    void rebuildSubsupMenuChecks(bool sub, bool sup);
```

`m_subscriptAction`/`m_superscriptAction` 在 ToolBar.cpp 构造函数初始化列表中有初始化（29-30 行），需同步删除初始化列表对应两行。

- [ ] **Step 2: 编译验证（暂容忍 cpp 未同步的报错，直接进 Task 2 一起改）**

本任务不单独编译，与 Task 2 合并验证。

---

### Task 2: ToolBar.cpp —— 按钮构建与菜单

**Files:**
- Modify: `src/ui/ToolBar.cpp`

**Interfaces:**
- Consumes: Task 1 的成员声明
- Produces: `m_alignButton`/`m_subsupButton` 构建；菜单项 QAction 的 objectName：`alignLeftItem`/`alignCenterItem`/`alignRightItem`/`superscriptItem`/`subscriptItem`

- [ ] **Step 1: 删除旧对齐/上下标按钮构建代码**

删除 setupUi() 中「=== 文字对齐（互斥按钮组） ===」整块（140-155 行）与 `m_subscriptAction`/`m_superscriptAction` 两个 makeAction 块（119-125 行中的 sub/sup 两项），以及构造函数初始化列表中 `m_alignGroup(nullptr)`、`m_subscriptAction(nullptr)`、`m_superscriptAction(nullptr)` 相关行（`m_alignLeftAction(nullptr)` 等四个）。

- [ ] **Step 2: 构造函数初始化列表清理**

初始化列表删去：

```cpp
    , m_alignLeftAction(nullptr)
    , m_alignCenterAction(nullptr)
    , m_alignRightAction(nullptr)
    , m_alignGroup(nullptr)
```

（`m_subscriptAction`/`m_superscriptAction` 当前不在列表中则跳过。）

- [ ] **Step 3: 在原对齐按钮位置插入两个下拉按钮构建代码**

```cpp
    // === 文字对齐（下拉折叠：左/中/右） ===
    {
        m_alignButton = new QToolButton(this);
        m_alignButton->setObjectName("alignButton");
        m_alignButton->setToolTip("段落对齐");
        m_alignButton->setCheckable(false);
        m_alignButton->setArrowType(Qt::NoArrow); // 用自己的 chevron-down
        m_alignMenu = new QMenu(m_alignButton);
        auto addAlignItem = [this](const QString &objName, const QString &text,
                                   const QString &shortcut, const QString &icon) {
            QAction *a = new QAction(QIcon(icon), text, m_alignMenu);
            a->setObjectName(objName);
            a->setShortcut(QKeySequence(shortcut));  // 仅显示文字，菜单不抢键（见下）
            m_alignMenu->addAction(a);
            return a;
        };
        addAlignItem("alignLeftItem", "左对齐(&L)", "Ctrl+Shift+L",
                     ":/icons/light/align-left.svg");
        addAlignItem("alignCenterItem", "居中对齐(&E)", "Ctrl+Shift+E",
                     ":/icons/light/align-center.svg");
        addAlignItem("alignRightItem", "右对齐(&R)", "Ctrl+Shift+R",
                     ":/icons/light/align-right.svg");
        m_alignButton->setMenu(m_alignMenu);
        // 快捷键仅作显示：改为 ShortcutNoShortcut 上下文，避免 QMenu 抢 WebEngine 按键
        for (QAction *a : m_alignMenu->actions())
            a->setShortcutContext(Qt::ShortcutNoShortcut);
        m_alignButton->installEventFilter(this);
        addWidget(m_alignButton);
    }

    // === 上下标（下拉折叠：上标/下标） ===
    {
        m_subsupButton = new QToolButton(this);
        m_subsupButton->setObjectName("subsupButton");
        m_subsupButton->setToolTip("上标/下标");
        m_subsupButton->setCheckable(true);  // 光标处于上下标内容时高亮
        m_subsupButton->setArrowType(Qt::NoArrow);
        m_subsupMenu = new QMenu(m_subsupButton);
        auto addSubsupItem = [this](const QString &objName, const QString &text,
                                    const QString &shortcut, const QString &icon) {
            QAction *a = new QAction(QIcon(icon), text, m_subsupMenu);
            a->setObjectName(objName);
            a->setShortcut(QKeySequence(shortcut));
            m_subsupMenu->addAction(a);
            return a;
        };
        addSubsupItem("superscriptItem", "上标(&P)", "Ctrl+.",
                      ":/icons/light/superscript.svg");
        addSubsupItem("subscriptItem", "下标(&B)", "Ctrl+,",
                      ":/icons/light/subscript.svg");
        m_subsupButton->setMenu(m_subsupMenu);
        for (QAction *a : m_subsupMenu->actions())
            a->setShortcutContext(Qt::ShortcutNoShortcut);
        m_subsupButton->installEventFilter(this);
        addWidget(m_subsupButton);
    }
```

- [ ] **Step 4: 信号连接替换**

删除旧连接（233-235 行的 alignLeft/Center/Right 三行、231 行 subscript/superscript 两行），替换为：

```cpp
    // 对齐/上下标下拉菜单项 → 复用原信号，MainWindow 零改动
    for (QAction *a : m_alignMenu->actions()) {
        QString align = (a->objectName() == "alignLeftItem") ? "left"
                      : (a->objectName() == "alignCenterItem") ? "center" : "right";
        connect(a, &QAction::triggered, this, [this, align]{ emit alignSelected(align); });
    }
    connect(m_subsupMenu->findChild<QAction*>("superscriptItem"),
            &QAction::triggered, this, &ToolBar::superscriptClicked);
    connect(m_subsupMenu->findChild<QAction*>("subscriptItem"),
            &QAction::triggered, this, &ToolBar::subscriptClicked);
```

同时删除 setupUi 末尾 for 循环里的 `subscriptAction`/`superscriptAction` 分支（若存在——当前 247-270 行循环里没有这两个分支，无需改）。

- [ ] **Step 5: 实现四个辅助函数（加在 buildColorMenu 前）**

```cpp
// 按对齐状态切换按钮图标（跟随光标回显）
void ToolBar::setAlignIcon(const QString &alignment)
{
    m_currentAlign = alignment;
    QString theme = m_isDark ? "dark" : "light";
    QString icon = (alignment == "center") ? "align-center"
                 : (alignment == "right") ? "align-right" : "align-left";
    m_alignButton->setIcon(QIcon(QString(":/icons/%1/%2.svg").arg(theme, icon)));
}

// 按上下标激活状态切换按钮图标与 checked 高亮（默认下标图标）
void ToolBar::setSubsupIcon(bool sub, bool sup)
{
    QString theme = m_isDark ? "dark" : "light";
    QString icon = sup ? "superscript" : "subscript";
    m_subsupButton->setIcon(QIcon(QString(":/icons/%1/%2.svg").arg(theme, icon)));
    m_subsupButton->setChecked(sub || sup);
}

void ToolBar::rebuildAlignMenuChecks(const QString &alignment)
{
    for (QAction *a : m_alignMenu->actions()) {
        QString align = (a->objectName() == "alignLeftItem") ? "left"
                      : (a->objectName() == "alignCenterItem") ? "center" : "right";
        a->setChecked(align == alignment);
    }
}

void ToolBar::rebuildSubsupMenuChecks(bool sub, bool sup)
{
    if (QAction *a = m_subsupMenu->findChild<QAction*>("superscriptItem"))
        a->setChecked(sup);
    if (QAction *a = m_subsupMenu->findChild<QAction*>("subscriptItem"))
        a->setChecked(sub);
}
```

菜单项需 checkable：在 Step 3 的 `addAlignItem`/`addSubsupItem` lambda 中 `a->setCheckable(true);`（在 `m_xxxMenu->addAction(a);` 之前加一行）。

- [ ] **Step 6: 初始状态设置**

在 setupUi() 两个按钮构建块末尾（`addWidget` 之后）加：

```cpp
        setAlignIcon("left");
        rebuildAlignMenuChecks("left");
```

与

```cpp
        setSubsupIcon(false, false);
        rebuildSubsupMenuChecks(false, false);
```

- [ ] **Step 7: 编译**

Run: `cd build && cmake --build . -j$(nproc) 2>&1 | grep -E "error|Error" | head`
Expected: 无输出（编译通过）

- [ ] **Step 8: Commit**

```bash
git add src/ui/ToolBar.cpp src/ui/ToolBar.h
git commit -m "feat: 工具栏对齐/上下标折叠为下拉按钮（WPS 风格）"
```

---

### Task 3: 状态同步与主题/源码模式适配

**Files:**
- Modify: `src/ui/ToolBar.cpp`（updateFormatState / applyIconTheme / setSourceMode / eventFilter）

**Interfaces:**
- Consumes: Task 2 的 `setAlignIcon` 等 4 个辅助函数与菜单 objectName
- 格式 JSON 字段（不变）：`textAlign`（"left"/"center"/"right"）、`subscript`、`superscript`（bool）

- [ ] **Step 1: updateFormatState() 替换对齐同步块**

删除 385-397 行的旧对齐互斥块，替换为：

```cpp
    // 对齐下拉按钮：图标 + 菜单勾选跟随光标
    if (f.contains("textAlign")) {
        QString a = f["textAlign"].toString();
        if (a.isEmpty()) a = "left";
        setAlignIcon(a);
        rebuildAlignMenuChecks(a);
    }
```

- [ ] **Step 2: updateFormatState() 替换上下标 TOGGLE_CHECK**

删除 `TOGGLE_CHECK(m_subscriptAction, "subscript")`、`TOGGLE_CHECK(m_superscriptAction, "superscript")` 两行（377-378 行），替换为：

```cpp
    // 上下标下拉按钮：图标 + checked 高亮 + 菜单勾选跟随光标
    if (f.contains("subscript") || f.contains("superscript")) {
        bool sub = f["subscript"].toBool();
        bool sup = f["superscript"].toBool();
        setSubsupIcon(sub, sup);
        rebuildSubsupMenuChecks(sub, sup);
    }
```

- [ ] **Step 3: setSourceMode() 适配**

在 `m_exportButton` 单独控制的代码（354-356 行）处追加：

```cpp
    // 对齐/上下标下拉按钮是 QToolButton widget，随编辑按钮一起启停
    if (m_alignButton) m_alignButton->setEnabled(!on);
    if (m_subsupButton) m_subsupButton->setEnabled(!on);
```

- [ ] **Step 4: applyIconTheme() 适配**

在导出按钮主题处理（433-437 行）后追加：

```cpp
    // 对齐/上下标按钮：按记录的当前状态重选主题图标
    if (m_alignButton) setAlignIcon(m_currentAlign);
    if (m_subsupButton) {
        // 从菜单勾选状态恢复激活态，避免另存状态成员
        bool sup = false, sub = false;
        if (QAction *a = m_subsupMenu->findChild<QAction*>("superscriptItem")) sup = a->isChecked();
        if (QAction *a = m_subsupMenu->findChild<QAction*>("subscriptItem")) sub = a->isChecked();
        setSubsupIcon(sub, sup);
    }
```

- [ ] **Step 5: eventFilter() 泛化到两个新按钮**

把 440-450 行的 `obj == m_exportButton` 条件扩展：

```cpp
    // 下拉按钮统一：点击即弹出菜单（导出/对齐/上下标）
    QToolButton *dropBtn = nullptr;
    if (obj == m_exportButton) dropBtn = m_exportButton;
    else if (obj == m_alignButton) dropBtn = m_alignButton;
    else if (obj == m_subsupButton) dropBtn = m_subsupButton;
    if (dropBtn && event->type() == QEvent::MouseButtonPress) {
        if (dropBtn->menu()) {
            dropBtn->menu()->popup(
                dropBtn->mapToGlobal(QPoint(0, dropBtn->height())));
        }
        return true;
    }
```

- [ ] **Step 6: 编译并跑 C++ 测试**

Run: `cd build && cmake --build . -j$(nproc) && ctest --output-on-failure 2>&1 | tail -4`
Expected: 编译通过，4/4 tests passed

- [ ] **Step 7: Commit**

```bash
git add src/ui/ToolBar.cpp
git commit -m "feat: 对齐/上下标下拉按钮状态同步与主题适配"
```

---

### Task 4: QSS 两主题 chevron 与配色规则

**Files:**
- Modify: `src/resources/themes/light.qss`（111-122 行区域）
- Modify: `src/resources/themes/dark.qss`（110 行区域）

**Interfaces:**
- Consumes: objectName `alignButton`、`subsupButton`
- 规格必须逐字对齐 `#pdfAction` 的 menu-indicator 规则

- [ ] **Step 1: light.qss 添加规则**

在 `QToolButton#pdfAction::menu-indicator` 块之后加：

```css
/* 对齐/上下标下拉按钮 — 深灰图标，chevron 同导出规格 */
QToolButton#alignButton,
QToolButton#subsupButton { color: #5f6368; }
QToolButton#alignButton::menu-indicator,
QToolButton#subsupButton::menu-indicator {
    image: url(:/icons/light/chevron-down.svg);
    subcontrol-position: right bottom;
    subcontrol-origin: margin;
    bottom: 4px;
    right: 2px;
    width: 8px;
    height: 5px;
}
```

- [ ] **Step 2: dark.qss 添加规则**

在 `QToolButton#pdfAction::menu-indicator` 块之后加（同结构）：

```css
/* 对齐/上下标下拉按钮 — 深灰图标，chevron 同导出规格 */
QToolButton#alignButton,
QToolButton#subsupButton { color: #c4c9ce; }
QToolButton#alignButton::menu-indicator,
QToolButton#subsupButton::menu-indicator {
    image: url(:/icons/dark/chevron-down.svg);
    subcontrol-position: right bottom;
    subcontrol-origin: margin;
    bottom: 4px;
    right: 2px;
    width: 8px;
    height: 5px;
}
```

- [ ] **Step 3: 重编译（QSS 打包在 qrc 中）并启动手动验证**

Run: `cd build && cmake --build . -j$(nproc)`
Expected: 编译通过

启动 `./scripts/run-editor.sh`，逐项核对 spec 测试清单 1-9（菜单三段式、图标跟随、高亮、主题切换、源码模式、快捷键无双重触发、总宽缩短）。

- [ ] **Step 4: Commit**

```bash
git add src/resources/themes/light.qss src/resources/themes/dark.qss
git commit -m "feat: 对齐/上下标下拉按钮 QSS（chevron 同导出规格）"
```

---

## Self-Review 结果

1. **Spec 覆盖**：交互（图标跟随/默认下标/checked 高亮）、三段式菜单、QSS 规格、主题切换、源码模式、信号零改动、YAGNI 项 —— 均有对应 Task/Step。
2. **占位符**：无 TBD/TODO；所有代码块完整可落地。
3. **类型一致性**：`setAlignIcon(QString)`、`setSubsupIcon(bool,bool)`、`rebuildAlignMenuChecks(QString)`、`rebuildSubsupMenuChecks(bool,bool)` 与菜单 objectName 在 Task 1-4 间一致；信号 `alignSelected`/`subscriptClicked`/`superscriptClicked` 名与 MainWindow 现有 connect 一致。
