# 侧边栏面板独立开关设计

日期：2026-08-23
状态：已确认（用户批准）

## 背景与问题

原结构：视图菜单「侧边栏」控制整个 SideBar 显隐，「大纲」控制侧边栏内大纲标签页显隐。
病态：侧边栏隐藏时，大纲菜单仍可勾选但面板不可见——「勾选状态与可见性脱钩」。

用户需求：侧边栏视为「目录树 + 大纲」两个面板的组合，菜单直接控制两个面板，
全不勾时侧边栏隐藏。

## 方案选择

**采用：双开关即真相**——状态仅有两个布尔（目录树可见、大纲可见），
侧边栏显示 = 至少一个为真。勾选与可见性一一对应，消灭病态状态类。

否决：
- 保留 collapsed 总开关 + 双面板（三态正交会重现「勾了却看不见」病态）
- 菜单保留「侧边栏」总开关 + 子开关（用户已定两项平铺，且多一层状态）

## 用户确认的交互决策

| 决策点 | 结论 |
|--------|------|
| 单面板时标签栏 | 始终显示（不隐藏 tab 头） |
| 「打开文件夹...」按钮 | 跟随目录树显隐 |
| Ctrl+B | 整体收/展：两勾→记组合全收；有未勾→恢复组合（默认两勾） |
| 持久化 | 不持久化，重启默认两勾（与原「侧边栏」行为一致） |

## 详细设计

### 1. MenuBar

- 「侧边栏」菜单项删除（`m_sidebarAct`、`toggleSidebar()` 信号移除）
- 新增「目录树」：checkable、默认勾选、objectName `fileTreeAction`、
  触发 `toggleFileTree(bool visible)` 信号（`triggered(checked)` 直连传参）
- 「大纲」保持：objectName `outlineAction`，信号 `toggleOutline(bool)`（已存在）
- 两项均不携带菜单快捷键
- `retranslateUi()` 同步新文案

### 2. SideBar

- 内部持有 `m_fileTreeVisible`、`m_outlineVisible`（初始均 true）
- `setFileTreeVisible(bool)`：设 tab 0 显隐 + 「打开文件夹...」按钮显隐
- `setOutlineVisible(bool)`：设 tab 1 显隐
- 隐藏「当前停驻页」时，先切到另一个**可见**的页（都不可见则无需处理）
- 两个 setter 末尾统一调 `updateVisibility()`：
  `setVisible(m_fileTreeVisible || m_outlineVisible)`
- `setCollapsed()/isCollapsed()/m_collapsed` 移除（被双开关取代）
- tab 文字「文件」改为「目录树」（与菜单命名一致，`retranslateUi` 同步）

### 3. MainWindow

- 删 `toggleSidebar` 接线；新增 `toggleFileTree` 接线（与 `toggleOutline` 同型）
- Ctrl+B：`QShortcut(QKeySequence("Ctrl+B"), this)`，槽逻辑：
  - 两项都勾 → 记录当前组合到 `m_lastPaneCombo`，两项 `setChecked(false)`
    并同步 SideBar（侧边栏收起）
  - 有任一未勾 → 恢复 `m_lastPaneCombo`（初始 `true, true`），
    **恢复后立即把 `m_lastPaneCombo` 重置为 `true, true`**
    （否则「当前状态 == 记忆组合」时再按无响应，循环卡死）
- 菜单勾选回写复用项目惯例 `findChild<QAction*>("fileTreeAction")`
  （同源码模式 `sourceModeAction` 模式）

### 4. 不变量

- 菜单勾选 = 面板可见，任何时候一致；「全不勾」是合法状态（= 侧边栏隐藏）
- 重启后始终恢复两勾（不写 QSettings）
- 打开文件夹时侧边栏收起 → 文件树静默更新，维持现状（不在本次范围）

## 验证计划

手动矩阵（用户目视）：
1. 仅勾目录树 / 仅勾大纲 / 两勾 / 全不勾 → 对应面板组合，全不勾时侧边栏隐藏
2. 只剩大纲时「打开文件夹...」按钮消失；只剩目录树时按钮在
3. 停驻页被隐藏时自动切到另一可见页
4. Ctrl+B：两勾→全收；再按→恢复原组合；部分勾选时按→变两勾
5. 语言切换后菜单与 tab 文案正确

构建验证：`cmake --build build` 通过且二进制时间戳新于全部改动源文件。

## 涉及文件

| 文件 | 改动 |
|------|------|
| `src/ui/MenuBar.h/.cpp` | 删侧边栏项、加目录树项、信号调整、retranslateUi |
| `src/ui/SideBar.h/.cpp` | 双开关状态、setFileTreeVisible、updateVisibility、删 collapsed |
| `src/ui/MainWindow.h/.cpp` | 接线调整、Ctrl+B QShortcut、m_lastPaneCombo |
