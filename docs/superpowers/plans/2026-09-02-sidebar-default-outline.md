# 侧边栏启动默认只显示大纲 + 面板状态持久化 实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 首次启动侧边栏只显示大纲（目录树收起），大纲/目录树两面板的展开状态跨会话记忆（QSettings）。

**Architecture:** `SideBar` 新增 `applyInitialPanes()` 用 `QSignalBlocker` 阻断 `collapsedChanged` 信号链直接置初始态（避免构造期 `rememberExpandedSizes()` 污染 `m_savedSplitSizes`、避免启动动画）；`MainWindow` 构造时读 QSettings 调该方法，并在现有两个 `*VisibleChanged` 信号 lambda 中加写入（视图菜单/标题栏/Ctrl+B 三条路径全汇聚于此）。

**Tech Stack:** Qt6 Widgets（QSignalBlocker / QSettings / QPropertyAnimation 既有机制），QtTest。

**Spec:** `docs/superpowers/specs/2026-09-02-sidebar-default-outline-design.md`

## Global Constraints

- 所有代码注释使用中文；标识符英文。
- SideBar 类自身构造默认**保持双展开**——「首次默认」语义只存在于 MainWindow 的 QSettings 默认值（`sidebar/fileTreeVisible` 默认 `false`、`sidebar/outlineVisible` 默认 `true`）。现有测试零改动。
- 不改 `showEvent` 的 `setSizes({360, 240})`（收起面板受 `maximumHeight` 约束自动 clamp）。
- QSettings 组织/应用名固定 `("md-editor", "md-editor")`，键名 `sidebar/fileTreeVisible` / `sidebar/outlineVisible`。
- 版本 0.13.0 → 0.13.1；收口时同步 `CMakeLists.txt`、`CHANGELOG.md`、打 tag。
- C++ 改动后必须重编译（`cmake --build build`）；push 时 pre-push 预检（clang 容器 + MSVC）会自动触发，属预期。
- 用户目视验证通过前不自动 commit（项目既有约定）。

---

### Task 1: `SideBar::applyInitialPanes` + 4 个单测（TDD）

**Files:**
- Modify: `src/ui/SideBar.h`（public 方法声明区，`setOutlineVisible` 之后）
- Modify: `src/ui/SideBar.cpp`（文件头部 include 区 + `setOutlineVisible` 实现之后）
- Test: `tests/test_sidebar.cpp`（slots 声明区 + 文件尾部实现区）

**Interfaces:**
- Consumes: 既有 `CollapsibleSection::setCollapsed(bool, bool animate)`（无动画重载，`src/ui/CollapsibleSection.h:26`）、`SideBar::updateSectionOrder()`。
- Produces: `void SideBar::applyInitialPanes(bool fileTree, bool outline)`（public，无返回值；发 `fileTreeVisibleChanged`/`outlineVisibleChanged` 各一次）——Task 2 依赖此签名。

- [ ] **Step 1: 写 4 个失败测试**

`tests/test_sidebar.cpp` slots 声明区（`whole_collapse_hides_widget();` 声明行之后）加：

```cpp
    void restore_collapses_file_tree_only();  // 启动恢复：只显示大纲，按钮隐藏
    void restore_all_off_hides_sidebar();     // 启动恢复：全收 → 侧边栏隐藏
    void restore_then_reopen_keeps_ratio();   // 恢复收起后重开目录树，比例未被污染
    void restore_emits_signals();             // 恢复发 visibleChanged 信号（菜单同步依赖）
```

文件尾部（`QTEST_MAIN(TestSideBar)` 之前）加实现：

```cpp
// === 启动恢复（applyInitialPanes）：MainWindow 读 QSettings 后调用 ===

// 恢复「只显示大纲」：目录树收起、按钮隐藏、不触发整体收起
void TestSideBar::restore_collapses_file_tree_only()
{
    SideBar sb;
    sb.applyInitialPanes(false, true);
    sb.show();
    QVERIFY(fileSection(sb)->isCollapsed());
    QVERIFY(!outlineSection(sb)->isCollapsed());
    QVERIFY(!openBtn(sb)->isVisibleTo(&sb));  // 「打开文件夹」跟随目录树隐藏
    QTest::qWait(400);
    QVERIFY(sb.isVisible());
    QVERIFY(sb.maximumWidth() != 0);          // 单面板收起不触发整体收起
    QVERIFY(sb.width() > 0);
}

// 恢复「全收」：侧边栏直接隐藏（无动画）。SideBar 须挂父 widget——顶层直接
// sb.show() 会强行覆盖预置的 setVisible(false)，与真实应用「子 widget 显隐
// 标记」语义不符。
void TestSideBar::restore_all_off_hides_sidebar()
{
    QWidget parent;
    auto *sb = new SideBar(&parent);
    sb->applyInitialPanes(false, false);
    QCOMPARE(sb->maximumWidth(), 0);
    parent.show();
    QVERIFY(!sb->isVisibleTo(&parent));       // 父显示不拉起侧边栏（显隐标记生效）
}

// 关键回归：构造期（未布局）恢复收起 → show → 再展开目录树，大纲仍占大头。
// 证明 m_savedSplitSizes 未被构造期垃圾尺寸污染（若污染，恢复出坏比例）。
void TestSideBar::restore_then_reopen_keeps_ratio()
{
    SideBar sb;
    sb.applyInitialPanes(false, true);
    sb.show();
    QTest::qWait(100);                        // 等 showEvent setSizes({360,240}) 生效
    QVERIFY(fileSection(sb)->isCollapsed());
    sb.setFileTreeVisible(true);              // 用户点开目录树
    QTest::qWait(400);
    QVERIFY(!fileSection(sb)->isCollapsed());
    QVERIFY(outlineSection(sb)->height() > fileSection(sb)->height());  // 6:4 量级
}

// 恢复须发两个 visibleChanged 信号各一次（MainWindow 菜单勾选同步依赖）
void TestSideBar::restore_emits_signals()
{
    SideBar sb;
    QSignalSpy fileSpy(&sb, &SideBar::fileTreeVisibleChanged);
    QSignalSpy outlineSpy(&sb, &SideBar::outlineVisibleChanged);
    sb.applyInitialPanes(false, true);
    QCOMPARE(fileSpy.count(), 1);
    QCOMPARE(fileSpy.takeFirst().at(0).toBool(), false);
    QCOMPARE(outlineSpy.count(), 1);
    QCOMPARE(outlineSpy.takeFirst().at(0).toBool(), true);
}
```

- [ ] **Step 2: 构建 test_sidebar 确认编译失败**

```bash
cmake --build build --target test_sidebar -j$(nproc) 2>&1 | tail -5
```

预期：编译错误 `'applyInitialPanes' was not declared`（测试先行，方法还不存在）。

- [ ] **Step 3: 实现 `applyInitialPanes`**

`src/ui/SideBar.h` public 区（`setOutlineVisible` 声明之后）加：

```cpp
    // 启动恢复面板初始态：MainWindow 读 QSettings 后调用（无动画、阻断信号链直置）
    void applyInitialPanes(bool fileTree, bool outline);
```

`src/ui/SideBar.cpp` include 区加：

```cpp
#include <QSignalBlocker>
```

`setOutlineVisible` 实现之后加：

```cpp
// 启动恢复面板可见性（MainWindow 读 QSettings 后调用）。必须用 QSignalBlocker
// 阻断 collapsedChanged 信号链直置状态：构造期 splitter 尺寸未定形，
// rememberExpandedSizes 会把垃圾比例存进 m_savedSplitSizes（用户重开目录树时
// 恢复出坏比例）；且启动不需要收起动画。末尾统一发 visibleChanged 信号，
// 供 MainWindow 同步菜单勾选与持久化。
void SideBar::applyInitialPanes(bool fileTree, bool outline)
{
    const QSignalBlocker fileBlocker(m_fileSection);
    const QSignalBlocker outlineBlocker(m_outlineSection);
    m_fileSection->setCollapsed(!fileTree, false);   // 无动画重载
    m_outlineSection->setCollapsed(!outline, false);
    m_fileTreeVisible = fileTree;
    m_outlineVisible = outline;
    m_openFolderBtn->setVisible(fileTree);
    updateSectionOrder();   // 恢复「大纲收起+目录树展开」时交换面板顺序
    m_wholeCollapsed = !fileTree && !outline;
    if (m_wholeCollapsed) { // 复现全收态：无动画直接置隐藏
        setMaximumWidth(0);
        setMinimumWidth(0);
        setVisible(false);
    }
    emit fileTreeVisibleChanged(fileTree);
    emit outlineVisibleChanged(outline);
}
```

- [ ] **Step 4: 构建并跑 test_sidebar，确认新用例全过**

```bash
cmake --build build --target test_sidebar -j$(nproc)
ctest --test-dir build -R test_sidebar --output-on-failure
```

预期：16 个用例全 PASS（原 12 + 新 4）。

- [ ] **Step 5: 跑全量测试确认无回归**

```bash
cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure
```

预期：全部 PASS（重点关注 test_sidebar / test_collapsiblesection）。

- [ ] **Step 6: Commit**

```bash
git add src/ui/SideBar.h src/ui/SideBar.cpp tests/test_sidebar.cpp
git commit -m "feat: SideBar 新增 applyInitialPanes 启动恢复面板态（阻断信号链防比例污染，附 4 单测）"
```

---

### Task 2: MainWindow 读 QSettings 恢复 + 信号写入持久化

**Files:**
- Modify: `src/ui/MainWindow.cpp:50`（构造，`connectSignals();` 之后）
- Modify: `src/ui/MainWindow.cpp:185-192`（两个信号 lambda）

**Interfaces:**
- Consumes: Task 1 的 `SideBar::applyInitialPanes(bool, bool)`。
- Produces: 无（终端接线）；QSettings 键 `sidebar/fileTreeVisible` / `sidebar/outlineVisible` 的读写行为。

- [ ] **Step 1: 构造函数加恢复读取**

`src/ui/MainWindow.cpp` 构造中，把：

```cpp
    setupUi();
    connectSignals();

    // 命令行传入文件路径（文件管理器双击打开）时优先打开该文件；
```

改为：

```cpp
    setupUi();
    connectSignals();

    // 恢复侧边栏面板可见性（首次启动默认只显示大纲；两面板状态跨会话记忆）。
    // 此时信号 lambda 已连接：applyInitialPanes 发的信号会同步菜单勾选并幂等写回。
    {
        QSettings settings("md-editor", "md-editor");
        m_sideBar->applyInitialPanes(
            settings.value("sidebar/fileTreeVisible", false).toBool(),
            settings.value("sidebar/outlineVisible", true).toBool());
    }

    // 命令行传入文件路径（文件管理器双击打开）时优先打开该文件；
```

- [ ] **Step 2: 两个信号 lambda 加持久化写入**

把（`MainWindow.cpp:185-192`）：

```cpp
    connect(m_sideBar, &SideBar::fileTreeVisibleChanged, this, [this](bool visible) {
        QAction *act = m_menuBar->findChild<QAction *>(QStringLiteral("fileTreeAction"));
        if (act) act->setChecked(visible);
    });
    connect(m_sideBar, &SideBar::outlineVisibleChanged, this, [this](bool visible) {
        QAction *act = m_menuBar->findChild<QAction *>(QStringLiteral("outlineAction"));
        if (act) act->setChecked(visible);
    });
```

改为：

```cpp
    connect(m_sideBar, &SideBar::fileTreeVisibleChanged, this, [this](bool visible) {
        QAction *act = m_menuBar->findChild<QAction *>(QStringLiteral("fileTreeAction"));
        if (act) act->setChecked(visible);
        QSettings settings("md-editor", "md-editor");   // 三条路径（菜单/标题栏/Ctrl+B）全汇聚于此
        settings.setValue("sidebar/fileTreeVisible", visible);
    });
    connect(m_sideBar, &SideBar::outlineVisibleChanged, this, [this](bool visible) {
        QAction *act = m_menuBar->findChild<QAction *>(QStringLiteral("outlineAction"));
        if (act) act->setChecked(visible);
        QSettings settings("md-editor", "md-editor");
        settings.setValue("sidebar/outlineVisible", visible);
    });
```

- [ ] **Step 3: 重编译并跑全量测试**

```bash
cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure
```

预期：编译通过，测试全 PASS。

- [ ] **Step 4: 人工目视验证（请用户操作确认）**

```bash
rm -f ~/.config/md-editor/md-editor.conf   # 清记录，模拟首次启动
./scripts/run-editor.sh
```

清单（每项改完都重启应用验证记忆）：
1. 首次启动：只显示大纲；菜单「视图→目录树」不勾选；「打开文件夹…」按钮隐藏
2. 勾选「目录树」→ 重启 → 目录树仍展开
3. 标题栏点击收起大纲 → 重启 → 大纲收起、目录树在上
4. Ctrl+B 全收 → 重启 → 侧边栏整体隐藏；再 Ctrl+B → 恢复双展开
5. 收起目录树后再展开 → 高度比例正常（大纲占大头，无异常比例）

- [ ] **Step 5: 用户确认后 Commit**

```bash
git add src/ui/MainWindow.cpp
git commit -m "feat: 侧边栏面板状态跨会话记忆，首次启动默认只显示大纲（QSettings 读写接线）"
```

---

### Task 3: 版本收口 0.13.1

**Files:**
- Modify: `CMakeLists.txt:2`
- Modify: `CHANGELOG.md`（`## [0.13.0]` 条目之前插入新条目）

**Interfaces:**
- Consumes: Task 1/2 已完成且验证通过。
- Produces: 版本 0.13.1 + tag `v0.13.1`。

- [ ] **Step 1: 升版本号**

`CMakeLists.txt:2`：

```cmake
project(md-editor VERSION 0.13.1 LANGUAGES CXX C)
```

- [ ] **Step 2: 加 CHANGELOG 条目**

`CHANGELOG.md` 中 `## [0.13.0] — 2026-08-30` 之前插入：

```markdown
## [0.13.1] — 2026-09-02

### 新增
- **侧边栏面板状态跨会话记忆** — 大纲/目录树展开状态实时写入 QSettings（`sidebar/outlineVisible`、`sidebar/fileTreeVisible`），视图菜单、标题栏点击、Ctrl+B 三条变更路径全覆盖；启动经 `SideBar::applyInitialPanes` 无动画恢复上次状态，首次启动默认只显示大纲（目录树收起）。恢复路径阻断 `collapsedChanged` 信号链，构造期布局未定形不污染 `m_savedSplitSizes`，重开目录树比例正常
```

- [ ] **Step 3: 重编译验证 + Commit + Tag**

```bash
cmake --build build -j$(nproc)   # MD_EDITOR_VERSION 宏引用编译通过
git add CMakeLists.txt CHANGELOG.md
git commit -m "chore: 版本收口 0.13.1（侧边栏面板状态记忆）"
git tag v0.13.1
```

- [ ] **Step 4: Push（走 pre-push 预检）**

C++ 改动会触发 clang 容器 + MSVC 编译预检，属预期。若 push 前想本地预演：

```bash
scripts/verify-clang.sh
```

---

## Self-Review 记录

- **Spec 覆盖**：§3.1 行为表 → Task 2 Step 4 清单逐项对应；§3.2 applyInitialPanes → Task 1；§3.3 接线 → Task 2；§4 测试 4 用例 → Task 1 Step 1（逐字对应）；§5 收尾 → Task 3。无缺口。
- **占位符扫描**：所有代码步骤均为完整可落地代码，无 TBD/「适当处理」类表述。
- **类型一致性**：`applyInitialPanes(bool fileTree, bool outline)` 在 Task 1（声明/实现/测试）与 Task 2（调用）签名一致；QSettings 键名四处（Task 2 读/写、Task 3 CHANGELOG）一致。

## 执行记录（2026-09-02，subagent-driven）

| 任务 | 状态 | commit |
|------|------|--------|
| Task 1 | ✅ 完成（16/16 单测、全量回归绿，审查通过） | `6c7f4f8` |
| Task 2 | ✅ 完成（用户目视验证 5/5 通过后提交） | `3205e46` |
| Task 3 | ⏸ **用户指示暂缓**（「版本号暂不推进」） | — |

最终全分支审查（fable）：**Ready to merge = Yes**，0 Critical / 0 Important。延后到版本收口时一并消化的加固项：

1. `restore_then_reopen_keeps_ratio` 区分力弱（构造期 `{0,0}` 垃圾经 stretch 重分配观测上等价）——加固：`applyInitialPanes` 前 `splitter(sb)->setSizes({10, 390})` 预置诱饵坏比例，未阻断信号的实现重开后大纲仅剩标题栏高，断言必挂
2. 钉住「无动画」属性——加固：`applyInitialPanes` 后、任何 `qWait` 前断言 `fileSection->maximumHeight() == fileSection->collapsedHeight()`（动画实现此刻 max 仍 10000）
3. 补 `(true, false)` 恢复用例（唯一「未 show 先换序」路径；断言换序后 `splitter->widget(0) == fileSection`、按钮可见）
4. 收起副作用现编码于三处（`SideBar.cpp` 两个 lambda + `applyInitialPanes`）——短期加互指注释，长期评估抽 `syncPaneState()` helper

收口时按 Task 3 步骤执行（版本号、CHANGELOG、tag、push 走 pre-push 预检），顺带做上述加固。
