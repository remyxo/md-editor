# 侧边栏两面板独立折叠动效设计

日期：2026-08-25

## 1. 背景与需求

- 总设计 §6.1「设计语言」规划：**侧边栏折叠、面板尺寸调整时使用 QPropertyAnimation 过渡**，当前未实现（`SideBar` 开关均瞬间显隐）。
- 侧边栏机制已从「单一侧边栏整体开关」演变为「**目录树 / 大纲分别控制**」（`setFileTreeVisible` / `setOutlineVisible`，QTabWidget 双 tab）。本次动效需适配该机制。
- 需求（用户确认）：
  1. 侧边栏改为**上下两个可独立折叠的面板**（目录树在上、大纲在下），每个面板带展开/收起动画；
  2. 单个面板收起 → 收成一条标题栏（点击可展开）；
  3. **两个面板都收起 → 侧边栏整体宽度动画收起**（可最大化编辑区）；
  4. 菜单开关（文件树/大纲）与标题栏点击均可控制。
- 顺带评估的「表格右键插入菜单」：已实现（`EditorTab.cpp:108-142`「表格」子菜单含上方/下方插入行、左/右插入列等），**本次不涉及**。

## 2. 现状分析

| 项 | 现状 |
|---|---|
| `SideBar`（`src/ui/SideBar.cpp/.h`） | `QTabWidget` 承载目录树/大纲两个 tab + 底部「打开文件夹...」按钮；`setFileTreeVisible`/`setOutlineVisible` 用 `setTabVisible` 显隐 tab；`updateVisibility` 在两面板全关时 `setVisible(false)` 瞬间消失 |
| `FileTree` / `OutlineTree` | 裸 `QTreeView`，无自带标题栏 |
| `MainWindow` 布局 | 水平 `QSplitter`：SideBar(index 0) + TabWidget(index 1)，`setStretchFactor(0,0)` 侧边栏固定宽；`setFileTreeVisible`/`setOutlineVisible` 由菜单 checkable action 驱动（`MainWindow.cpp:177-181`） |

## 3. 设计

### 3.1 组件架构（方案 A：通用可折叠面板 + SideBar 重排）

```
SideBar（固定宽 ~240，宽可动画）
├── CollapsibleSection「目录树」
│   ├── 标题栏：[▼ 目录树]  ← 点击切换折叠
│   └── content: FileTree（QTreeView，高度动画）
├── CollapsibleSection「大纲」
│   ├── 标题栏：[▼ 大纲]    ← 点击切换折叠
│   └── content: OutlineTree（QTreeView，高度动画）
└── [打开文件夹...] 按钮（跟随目录树面板展开状态）
```

- 新增 `src/ui/CollapsibleSection.{h,cpp}`：通用「标题栏 + 内容区」可折叠组件。
- 重写 `SideBar`：`QTabWidget` → `QVBoxLayout`（两 Section + 按钮）。
- `MainWindow` / `MenuBar` **不改**：仍调 `setFileTreeVisible(bool)` / `setOutlineVisible(bool)`。

### 3.2 CollapsibleSection（新增类）

```
CollapsibleSection : QWidget
├── header：QToolButton（flat、Checkable）显示「▼ 标题」；点按切换
├── content：QWidget *（由 setContent 注入）
└── QPropertyAnimation *m_heightAnim（动画 content maximumHeight）
```

接口：
- `setTitle(const QString &)` / `setContent(QWidget *)`
- `setCollapsed(bool)` / `isCollapsed()`
- 信号 `collapsedChanged(bool)`

动画：
- 收起：动画 content `maximumHeight` 当前高 → 0；展开：0 → `content->sizeHint().height()`。
- 时长 300ms，`QEasingCurve::InOutCubic`。
- 展开动画开始时 `content->setVisible(true)`；收起完成保持可见（maximumHeight=0 自然裁剪），不额外 `setVisible(false)`（避免再展开时闪白）。
- `content` 设 `setMinimumHeight(0)`，动画期间不压缩；展开完成后如需可恢复 sizeHint。
- 切换时若动画进行中，先 `stop()` 再从当前 `maximumHeight` 起新动画（防跳动）。
- 标题栏箭头文本：展开 `▼` / 收起 `▶`，随状态更新。

### 3.3 SideBar 重写

状态：`bool m_fileTreeExpanded`、`bool m_outlineExpanded`（默认 true，对应菜单初始勾选）。

- `setFileTreeVisible(bool)` / `setOutlineVisible(bool)`（**签名不变**）：
  - 传入 true → 若侧边栏整体处于收起态，先做**宽度展开**动画（0 → `m_savedWidth`），完成后（或并行）展开对应面板；若侧边栏已展开，直接展开面板。
  - 传入 false → 收起对应面板（内容高度 → 0）。若这是最后一个展开的面板，动画链：面板收起完成 → 触发**侧边栏宽度收起**动画（当前宽 → 0），并记忆 `m_savedWidth`。
- 宽度动画：`QPropertyAnimation(this, "maximumWidth", ...)` 300ms InOutCubic。
  - 收起：当前宽 → 0；展开：0 → `m_savedWidth`。
  - 侧边栏 `setMinimumWidth(0)`（否则宽度无法收到 0）。
  - 宽度收到 0 后**不**调 `setVisible(false)`（保持 0 宽，QSplitter 不重排抢占编辑区空间）。
- `updateVisibility` 逻辑移除（由动画链替代）。
- 「打开文件夹...」按钮 `setVisible(m_fileTreeExpanded)`——仅在目录树面板展开时显示。

### 3.4 动画链与竞态

- 收起链：`面板收起完成(finished) → 若两面板均已收起 → 触发宽度收起`。
- 展开链：侧边栏若处于收起态（宽度 0）→ 先宽度展开，`finished` 后再展开目标面板；若侧边栏已展开 → 直接展开面板（无宽度动画）。
- 快速连续切换：每个动画启动前 `stop()` 旧动画，从当前实际 `maximumHeight`/`maximumWidth` 起算；状态标志以最后一次 toggle 为准。
- 语言切换 `retranslateUi()`：更新两个标题栏文案与「打开文件夹...」按钮（Section 标题 + 按钮）。

### 3.5 行为变化汇总

| 场景 | 旧行为 | 新行为 |
|---|---|---|
| 默认 | 双 tab（一次只见一个） | 两面板同时展开（目录树在上、大纲在下） |
| 收起单面板 | tab 立即消失 | 面板高度动画收成一条标题栏 |
| 收起两面板 | 侧边栏立即消失 | 两面板各收成标题栏 → 侧边栏宽度动画收起 |
| 重新展开 | 立即出现 | 宽度动画展开 → 面板高度动画展开 |
| 「打开文件夹」 | 跟随文件树可见性 | 跟随目录树面板展开状态 |

## 4. 边界与风险

- **QSplitter 内 maximumWidth 动画**：需 `setMinimumWidth(0)` 且确认 splitter 不因宽度变化重排；若验证失效，备选改为动画 `QWidget::maximumWidth` + 宽度结束后 `setFixedWidth(0)`。
- **展开高度目标**：`QTreeView::sizeHint().height()` 可能偏小（树默认最小高度），需确认或设合理下限。
- **动画与 setVisible 交互**：宽度 0 时保持可见（0 宽），避免 splitter 重新分配空间导致跳动。
- **两面板都收起时窗口 resize**：m_savedWidth 为收起前宽度，窗口 resize 不影响展开目标。

## 5. 测试策略

- 单元测试（新建 `tests/test_collapsiblesection.cpp` 测新组件；扩展 `tests/test_sidebar.cpp` 测 SideBar 状态机）：
  - CollapsibleSection：`setCollapsed` 状态标志翻转、`collapsedChanged` 信号、动画目标值（可 `QTest::qWaitForWindowExposed` 后断言 maximumHeight）。
  - SideBar 状态机：单面板收起/展开、两面板全收 → 宽度收起、全收后展开一面 → 宽度展开。
- 手工验收：
  - 标题栏点击 / 菜单开关动效平滑、无跳动；
  - 两面板全收后侧边栏消失、编辑区扩展；重开后宽度记忆正确；
  - 语言切换后标题栏与按钮文案更新；
  - 全屏 / 窗口缩放时行为正常。

## 6. 非目标

- 不改变 `setFileTreeVisible`/`setOutlineVisible` 接口与菜单逻辑。
- 不做面板拖拽调高（竖向 splitter 手柄）。
- 不做动画时长/缓动曲线可配置。
- 表格右键插入菜单：已实现，不涉及。
