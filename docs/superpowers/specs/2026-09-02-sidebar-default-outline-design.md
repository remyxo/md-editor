# 侧边栏启动默认只显示大纲 + 面板状态持久化设计

日期：2026-09-02

## 1. 背景与需求

- 需求（用户提出）：**启动时目录树默认不显示，只显示大纲**。
- 澄清确认（用户选定）：
  1. **记住上次选择**：用户展开过目录树后写入 QSettings，下次启动保持展开（对齐 VS Code 侧边栏记忆习惯）；
  2. **持久化范围：两面板都记住**——大纲、目录树各自记住上次状态，行为对称。
- 启动时本就不恢复上次的文件夹（目录树初始为空），收起目录树不影响打开文件能力（菜单「打开文件夹」仍在）。

## 2. 现状分析

| 项 | 现状 |
|---|---|
| `SideBar`（`src/ui/SideBar.cpp`） | 大纲在上、目录树在下（垂直 splitter，初始 6:4）；两面板默认都展开（`m_fileTreeVisible = true` / `m_outlineVisible = true`）；折叠联动（菜单勾选同步、收起沉底、全收整体隐藏、按钮跟随）已全部存在 |
| `CollapsibleSection` | `setCollapsed(collapsed, animate)` 无动画重载已存在；**无论 animate 与否都 emit `collapsedChanged`**（`CollapsibleSection.cpp:98`） |
| `MenuBar` | `fileTreeAction` / `outlineAction` 默认 `setChecked(true)`（`MenuBar.cpp:83/89`） |
| 持久化 | 侧边栏面板状态**无任何持久化**（启动只恢复 `appearance/theme`） |
| 变更路径 | 视图菜单、标题栏点击、Ctrl+B 整体收/展——三条路径最终都汇聚到 `fileTreeVisibleChanged` / `outlineVisibleChanged` 两信号（`MainWindow.cpp:185-192` 的连接 lambda） |
| 测试 | `tests/test_sidebar.cpp` 已有完整单测模式（`findChild` 取面板、`QTest::qWait` 等动画、`QSignalSpy` 验信号） |

**关键坑**：启动恢复不能直接调 `setFileTreeVisible(false)`——它会走 `collapsedChanged` 信号链里的 `rememberExpandedSizes()`，在构造期（未布局、splitter 尺寸未定形）把垃圾尺寸存进 `m_savedSplitSizes`，之后用户点开目录树时 `restoreExpandedSizes()` 恢复出坏比例；且单参 `setCollapsed` 带 300ms 动画，启动会闪一段收起动画。

## 3. 设计

### 3.1 行为定义

| 场景 | 启动表现 |
|------|---------|
| 首次启动（QSettings 无记录） | 只显示大纲，目录树收起（`打开文件夹…`按钮随之隐藏，菜单「目录树」不勾选） |
| 上次目录树展开过 | 目录树展开（展开比例机制不受影响） |
| 上次大纲收起过 | 大纲收起、目录树在上（沉底规则照常） |
| 上次两面板全收 | 侧边栏整体隐藏（复现宽度 0 + 隐藏态，无动画）；Ctrl+B 恢复双展开（`m_lastPaneCombo` 初始 `{true,true}`，行为自洽） |

三条变更路径（视图菜单 / 标题栏点击 / Ctrl+B）全部实时写 QSettings。

### 3.2 SideBar 新增 `applyInitialPanes(bool fileTree, bool outline)`

```cpp
void SideBar::applyInitialPanes(bool fileTree, bool outline)
{
    // QSignalBlocker 阻断两面板的 collapsedChanged —— 否则会走 setupUi 的信号链：
    // rememberExpandedSizes 在构造期（splitter 尺寸未定形）把垃圾比例存进
    // m_savedSplitSizes，用户之后点开目录树会恢复出坏比例
    const QSignalBlocker fb(m_fileSection);
    const QSignalBlocker ob(m_outlineSection);
    m_fileSection->setCollapsed(!fileTree, false);    // 无动画重载
    m_outlineSection->setCollapsed(!outline, false);
    m_fileTreeVisible = fileTree;
    m_outlineVisible = outline;
    m_openFolderBtn->setVisible(fileTree);
    updateSectionOrder();     // 恢复「大纲收起+目录树展开」时交换面板顺序
    m_wholeCollapsed = !fileTree && !outline;
    if (m_wholeCollapsed) {   // 复现全收态：无动画直接隐藏
        setMaximumWidth(0); setMinimumWidth(0); setVisible(false);
    }
    emit fileTreeVisibleChanged(fileTree);      // 统一发信号 → 菜单同步 + 持久化
    emit outlineVisibleChanged(outline);
}
```

- `setCollapsed(x, false)` 置态无动画，但仍 emit，所以必须用 `QSignalBlocker` 阻断后手动置全部关联状态（可见标志、按钮、面板顺序、整体收起）。
- `showEvent` 的 `setSizes({360,240})` **不改**：收起面板受 `maximumHeight`（标题栏高）约束，QSplitter 分配时自动 clamp，由测试验证。
- **SideBar 类自身构造默认保持双展开**——「首次默认」语义放在 MainWindow 的 QSettings 默认值里，SideBar 保持纯组件，现有 `initial_state()` 等测试零改动。

### 3.3 MainWindow 接线

**读**（构造函数，`connectSignals()` 之后、会话恢复之前）：

```cpp
// 恢复侧边栏面板可见性（首次启动默认只显示大纲；两面板状态跨会话记忆）
QSettings settings("md-editor", "md-editor");
m_sideBar->applyInitialPanes(
    settings.value("sidebar/fileTreeVisible", false).toBool(),   // 首次默认收起
    settings.value("sidebar/outlineVisible", true).toBool());
```

**写**（现有两个信号 lambda 各加两行，天然覆盖全部三条路径）：

```cpp
connect(m_sideBar, &SideBar::fileTreeVisibleChanged, this, [this](bool visible) {
    QAction *act = m_menuBar->findChild<QAction *>(QStringLiteral("fileTreeAction"));
    if (act) act->setChecked(visible);
    QSettings settings("md-editor", "md-editor");
    settings.setValue("sidebar/fileTreeVisible", visible);       // 新增
});
// outlineVisibleChanged 同理
```

- 键名 `sidebar/fileTreeVisible` / `sidebar/outlineVisible`，对齐现有 `session/`、`editor/`、`appearance/` 分组风格。
- 时序：`applyInitialPanes` 在信号 lambda 已连接后调用，emit 一次 → 菜单勾选同步 + 把刚读的值写回 QSettings（幂等，无副作用）。
- `m_lastPaneCombo`（Ctrl+B 恢复组合）不持久化：恢复组合是会话内便利，启动后首次 Ctrl+B 的组合即当前恢复态。

## 4. 测试计划（`tests/test_sidebar.cpp` 新增 4 用例）

| 用例 | 断言 |
|------|------|
| `restore_collapses_file_tree_only` | `applyInitialPanes(false, true)` → 目录树收起 / 大纲展开 / 按钮隐藏 / 不触发整体收起（宽度 > 0） |
| `restore_all_off_hides_sidebar` | `applyInitialPanes(false, false)` → maximumWidth 0、不可见（注意：测试中 SideBar 须挂在父 QWidget 下再 `parent->show()`——顶层直接 `sb.show()` 会强行覆盖预先置的 `setVisible(false)`，与真实应用「子 widget 显隐标记」语义不符） |
| `restore_then_reopen_keeps_ratio`（关键回归） | 恢复收起 → show → 再 `setFileTreeVisible(true)` → 大纲高度仍大于目录树（证明 `m_savedSplitSizes` 未被构造期垃圾污染） |
| `restore_emits_signals` | QSignalSpy 验证两信号各发一次且值正确（菜单同步依赖此信号） |

MainWindow 侧持久化无单测先例（测试不构造 MainWindow，依赖 WebEngine），靠人工验证：改面板状态 → 重启 → 状态恢复。

## 5. 收尾事项

- 版本 `+0.0.1`（小增量），同步 `CMakeLists.txt` 的 `project(md-editor VERSION ...)`、`CHANGELOG.md`、打 tag。
- 纯 C++ 改动：改后必须 `cmake --build build` 重编译（无 qrc 资源改动）；push 走 pre-push 预检（clang 容器 + MSVC 编译）。
- 跨平台注意：纯 Qt Widget API（QSignalBlocker / QSettings / setMaximumWidth），无平台差异风险点。
