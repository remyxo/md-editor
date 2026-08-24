# 侧边栏面板独立开关实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 视图菜单改为「目录树」「大纲」两个独立开关，控制侧边栏两个面板的组合显示，全不勾时侧边栏隐藏；Ctrl+B 整体收/展。

**Architecture:** 双开关即真相——状态仅 SideBar 内两个布尔（目录树可见、大纲可见），侧边栏可见性是二者的或；菜单勾选与面板可见一一对应，无独立 collapsed 状态。菜单 action 只加不删跨任务保持编译绿，死代码最后统一清理。

**Tech Stack:** Qt 6 Widgets（QTabBar::setTabVisible、QShortcut、QTest offscreen）

**设计文档:** `docs/superpowers/specs/2026-08-23-sidebar-pane-toggles-design.md`

## 设计修订（相对已批准文档，需用户知悉）

Ctrl+B 语义从「两勾→全收；有任一未勾→恢复组合」修订为**标准 toggle**：
「侧边栏可见（任一勾）→ 记住组合并全收；不可见（全不勾）→ 恢复组合」。

原因：批准稿的「记录当前组合」只发生在两勾状态（记录值恒为两勾），记忆机制退化；
且「部分勾选时按→变全显」不符合用户批准时要求的「保留一键收/展的原有体验」
（旧版 Ctrl+B 是收/展 toggle）。修订后无需「恢复后重置」的补丁，无死循环。

## Global Constraints

- 注释与文档一律中文，代码标识符英文（CLAUDE.md 规则 1）
- 改动任何 C++ 源码后必须 `cmake --build build`，且确认二进制时间戳新于全部改动源文件（CLAUDE.md 规则 9）
- **用户目视验证通过前不 commit**（用户既定规则；各任务不含 commit 步骤，全部完成后统一提交）
- Qt 兼容基准 6.8.2：`QTabBar::setTabVisible()` 自 Qt 5.15 可用，安全
- 测试跑法：`QT_QPA_PLATFORM=offscreen build/tests/test_sidebar`（或 `ctest --test-dir build`）

---

### Task 1: SideBar 双开关核心逻辑（TDD）

**Files:**
- Create: `tests/test_sidebar.cpp`
- Modify: `src/ui/SideBar.h`（全文 38 行，加接口）
- Modify: `src/ui/SideBar.cpp`（全文，加实现；`setCollapsed` 本任务保留不删）
- Modify: `tests/CMakeLists.txt`（追加目标）

**Interfaces:**
- Produces: `void SideBar::setFileTreeVisible(bool)`、`void SideBar::setOutlineVisible(bool)`（改造现有，行为不变部分沿用）、tab0 文字「目录树」。Task 3 依赖这两个方法名。
- Consumes: 无（首个任务）

- [ ] **Step 1: 写失败测试** `tests/test_sidebar.cpp`

```cpp
#include <QtTest>
#include "SideBar.h"
#include <QTabWidget>
#include <QPushButton>

// 侧边栏双开关（目录树/大纲）行为测试
class TestSideBar : public QObject {
    Q_OBJECT
private slots:
    void initial_state();       // 初始：两面板可见，侧边栏可见
    void file_only();           // 只目录树：大纲 tab 隐藏，按钮保留
    void outline_only();        // 只大纲：文件 tab 隐藏，按钮隐藏，停驻页切到 1
    void all_off();             // 全关：侧边栏整体隐藏
    void recover_from_all_off();// 从全关恢复目录树：侧边栏重新显示
    void hiding_current_page(); // 停在大纲页关大纲：切回文件页
private:
    QTabWidget *tabs(const SideBar &sb) const;
    QPushButton *openBtn(const SideBar &sb) const;
};

QTabWidget *TestSideBar::tabs(const SideBar &sb) const
{
    return sb.findChild<QTabWidget *>();
}

QPushButton *TestSideBar::openBtn(const SideBar &sb) const
{
    return sb.findChild<QPushButton *>();
}

void TestSideBar::initial_state()
{
    SideBar sb;
    sb.show();
    QVERIFY(sb.isVisible());
    QVERIFY(tabs(sb)->isTabVisible(0));
    QVERIFY(tabs(sb)->isTabVisible(1));
    QVERIFY(openBtn(sb)->isVisible());
}

void TestSideBar::file_only()
{
    SideBar sb;
    sb.show();
    sb.setOutlineVisible(false);
    QVERIFY(!tabs(sb)->isTabVisible(1));
    QVERIFY(tabs(sb)->isTabVisible(0));
    QVERIFY(sb.isVisible());
    QVERIFY(openBtn(sb)->isVisibleTo(&sb));   // 按钮跟随目录树
}

void TestSideBar::outline_only()
{
    SideBar sb;
    sb.show();
    sb.setFileTreeVisible(false);
    QVERIFY(!tabs(sb)->isTabVisible(0));
    QVERIFY(tabs(sb)->isTabVisible(1));
    QVERIFY(sb.isVisible());
    QVERIFY(!openBtn(sb)->isVisibleTo(&sb));  // 无目录树则按钮隐藏
    QCOMPARE(tabs(sb)->currentIndex(), 1);    // 停驻页自动切到可见页
}

void TestSideBar::all_off()
{
    SideBar sb;
    sb.show();
    sb.setFileTreeVisible(false);
    sb.setOutlineVisible(false);
    QVERIFY(!sb.isVisible());
}

void TestSideBar::recover_from_all_off()
{
    SideBar sb;
    sb.show();
    sb.setFileTreeVisible(false);
    sb.setOutlineVisible(false);
    QVERIFY(!sb.isVisible());
    sb.setFileTreeVisible(true);
    QVERIFY(sb.isVisible());
    QVERIFY(tabs(sb)->isTabVisible(0));
}

void TestSideBar::hiding_current_page()
{
    SideBar sb;
    sb.show();
    tabs(sb)->setCurrentIndex(1);             // 停在大纲页
    sb.setOutlineVisible(false);
    QCOMPARE(tabs(sb)->currentIndex(), 0);    // 切回文件页
}

QTEST_MAIN(TestSideBar)
#include "test_sidebar.moc"
```

- [ ] **Step 2: CMake 注册测试目标** `tests/CMakeLists.txt` 末尾追加

```cmake
add_executable(test_sidebar test_sidebar.cpp
    ../src/ui/SideBar.cpp ../src/ui/FileTree.cpp ../src/ui/OutlineTree.cpp)
target_include_directories(test_sidebar PRIVATE ../src)
target_link_libraries(test_sidebar PRIVATE Qt6::Test Qt6::Widgets)
add_test(NAME test_sidebar COMMAND test_sidebar)
```

- [ ] **Step 3: 重新配置并跑测试，确认编译失败**

Run: `cmake -S . -B build && cmake --build build --target test_sidebar -j$(nproc) && QT_QPA_PLATFORM=offscreen build/tests/test_sidebar`
Expected: 编译 FAIL，报 `setFileTreeVisible` 不存在（SideBar.h 未声明）。

- [ ] **Step 4: 实现 SideBar 接口** `src/ui/SideBar.h`

在 `setCollapsed` 声明后加（`setCollapsed` 本任务保留，Task 4 删）：

```cpp
    // 双开关：目录树/大纲面板独立显隐，全关时侧边栏整体隐藏
    void setFileTreeVisible(bool visible);
    void setOutlineVisible(bool visible);
```

私有段 `m_collapsed` 后加成员 `bool m_fileTreeVisible = true;`、`bool m_outlineVisible = true;`，
私有方法段加 `void updateVisibility();`。

- [ ] **Step 5: 实现 SideBar 逻辑** `src/ui/SideBar.cpp`

`setCollapsed` 实现后追加；原 `setOutlineVisible`（上一轮修复加的）整体替换：

```cpp
void SideBar::setFileTreeVisible(bool visible)
{
    m_fileTreeVisible = visible;
    m_tabWidget->tabBar()->setTabVisible(0, visible);
    // 「打开文件夹...」按钮服务目录树，跟随其显隐
    m_openFolderBtn->setVisible(visible);
    // 停驻页被隐藏时切到另一个可见页
    if (!visible && m_tabWidget->currentIndex() == 0 && m_outlineVisible)
        m_tabWidget->setCurrentIndex(1);
    updateVisibility();
}

void SideBar::setOutlineVisible(bool visible)
{
    m_outlineVisible = visible;
    m_tabWidget->tabBar()->setTabVisible(1, visible);
    if (!visible && m_tabWidget->currentIndex() == 1 && m_fileTreeVisible)
        m_tabWidget->setCurrentIndex(0);
    updateVisibility();
}

void SideBar::updateVisibility()
{
    setVisible(m_fileTreeVisible || m_outlineVisible);
}
```

同时 `setupUi()` 里 `addTab(m_fileTree, tr("文件"))` 改为 `tr("目录树")`，
`retranslateUi()` 里 `setTabText(0, tr("文件"))` 改为 `tr("目录树")`。

- [ ] **Step 6: 跑测试确认通过**

Run: `cmake --build build --target test_sidebar -j$(nproc) && QT_QPA_PLATFORM=offscreen build/tests/test_sidebar`
Expected: 全部 6 个用例 PASS。

- [ ] **Step 7: 全量编译确认无回归**

Run: `cmake --build build -j$(nproc)`
Expected: 100% 无错误（MainWindow 尚未调用新接口，编译不受影响）。

---

### Task 2: MenuBar 新增「目录树」菜单项

**Files:**
- Modify: `src/ui/MenuBar.h:38-41`（信号区）、`:79`（成员）
- Modify: `src/ui/MenuBar.cpp:81-85`（视图菜单区）、`:207`（retranslateUi）

**Interfaces:**
- Produces: 信号 `void toggleFileTree(bool visible);`、action objectName `"fileTreeAction"`、`"outlineAction"`（Task 3 的 findChild 依赖这两个名字，大小写必须一致）
- Consumes: 无（独立于 Task 1）

- [ ] **Step 1: MenuBar.h 信号区**（`toggleSidebar();` 下一行加，本任务不删旧信号）

```cpp
    void toggleFileTree(bool visible);
```

成员声明 `QAction *m_sidebarAct, *m_outlineAct, ...` 中加入 `m_fileTreeAct`。

- [ ] **Step 2: MenuBar.cpp 构造**（`m_sidebarAct` 的 connect 之后加）

```cpp
    m_fileTreeAct = m_viewMenu->addAction(tr("目录树"));
    m_fileTreeAct->setCheckable(true);
    m_fileTreeAct->setChecked(true);
    m_fileTreeAct->setObjectName("fileTreeAction");
    connect(m_fileTreeAct, &QAction::triggered, this, &MenuBar::toggleFileTree);
```

`m_outlineAct` 创建处（87 行附近）补一行 `m_outlineAct->setObjectName("outlineAction");`。

- [ ] **Step 3: retranslateUi**（`m_sidebarAct->setText` 行后加）

```cpp
    m_fileTreeAct->setText(tr("目录树"));
```

- [ ] **Step 4: 编译验证**

Run: `cmake --build build -j$(nproc)`
Expected: 编译通过（中间态菜单顺序为「侧边栏/目录树/大纲」，Task 4 删侧边栏后归位）。

---

### Task 3: MainWindow 接线与 Ctrl+B

**Files:**
- Modify: `src/ui/MainWindow.h`（成员）
- Modify: `src/ui/MainWindow.cpp:177-186`（接线区）、构造区

**Interfaces:**
- Consumes: `SideBar::setFileTreeVisible/setOutlineVisible`（Task 1）、objectName `"fileTreeAction"`/`"outlineAction"`（Task 2）、信号 `MenuBar::toggleFileTree(bool)`
- Produces: 可运行的完整功能（用户目视验收对象）

- [ ] **Step 1: MainWindow.h 加成员**（`m_sideBar` 声明附近）

```cpp
    QShortcut *m_sidebarShortcut = nullptr;
    std::pair<bool, bool> m_lastPaneCombo{true, true};  // Ctrl+B 收起前的面板组合
```

头部补 `#include <QShortcut>`（如无）。文件头部前向声明区确认有 `class QShortcut;` 亦可。

- [ ] **Step 2: MainWindow.cpp 接线区改造**

删除旧的 toggleSidebar 接线：

```cpp
    connect(m_menuBar, &MenuBar::toggleSidebar, this, [this]() {
        m_sideBar->setCollapsed(!m_sideBar->isCollapsed());
    });
```

替换为（`toggleOutline` 接线保留，紧邻其后加目录树接线）：

```cpp
    connect(m_menuBar, &MenuBar::toggleFileTree, this, [this](bool visible) {
        m_sideBar->setFileTreeVisible(visible);
    });
```

- [ ] **Step 3: Ctrl+B 快捷键**（接线区之后）

```cpp
    // Ctrl+B 整体收/展：可见→记住组合并全收；全收→恢复组合
    m_sidebarShortcut = new QShortcut(QKeySequence("Ctrl+B"), this);
    connect(m_sidebarShortcut, &QShortcut::activated, this, [this]() {
        QAction *fileAct = m_menuBar->findChild<QAction *>(QStringLiteral("fileTreeAction"));
        QAction *outlineAct = m_menuBar->findChild<QAction *>(QStringLiteral("outlineAction"));
        if (!fileAct || !outlineAct) return;
        if (fileAct->isChecked() || outlineAct->isChecked()) {
            m_lastPaneCombo = {fileAct->isChecked(), outlineAct->isChecked()};
            fileAct->setChecked(false);
            outlineAct->setChecked(false);
        } else {
            fileAct->setChecked(m_lastPaneCombo.first);
            outlineAct->setChecked(m_lastPaneCombo.second);
        }
        // setChecked 不发射 triggered，需手动同步面板
        m_sideBar->setFileTreeVisible(fileAct->isChecked());
        m_sideBar->setOutlineVisible(outlineAct->isChecked());
    });
```

- [ ] **Step 4: 编译 + 全量测试**

Run: `cmake --build build -j$(nproc) && ctest --test-dir build --output-on-failure`
Expected: 编译通过，全部测试 PASS（含 Task 1 的 test_sidebar）。

---

### Task 4: 清理死代码与最终验证

**Files:**
- Modify: `src/ui/MenuBar.h`（删 `toggleSidebar()` 信号、`m_sidebarAct` 成员）
- Modify: `src/ui/MenuBar.cpp:81-85`（删侧边栏项创建）、`:207`（删 retranslateUi 行）
- Modify: `src/ui/SideBar.h/.cpp`（删 `setCollapsed/isCollapsed/m_collapsed`）
- Modify: `src/ui/MainWindow.cpp`（确认无 setCollapsed 残留调用）

**Interfaces:**
- Consumes: Task 1-3 全部
- Produces: 最终交付形态

- [ ] **Step 1: 删除 MenuBar 侧边栏项**（h 与 cpp 各处：信号、成员、创建块、connect、retranslateUi 行）

- [ ] **Step 2: 删除 SideBar collapsed 接口**（h 声明与 cpp 实现、`m_collapsed` 成员及构造初始化列表 `m_collapsed(false)`）

- [ ] **Step 3: 编译 + 全量测试**

Run: `cmake --build build -j$(nproc) && ctest --test-dir build --output-on-failure`
Expected: 编译通过（若报 `toggleSidebar`/`setCollapsed` 未定义，按报错位置清理残留调用），全部测试 PASS。

- [ ] **Step 4: 二进制新鲜度确认**

Run: `stat -c '%y %n' build/src/md-editor src/ui/SideBar.cpp src/ui/MenuBar.cpp src/ui/MainWindow.cpp`
Expected: 二进制时间戳新于全部源文件。

- [ ] **Step 5: 用户目视验证矩阵**（需用户运行 `./scripts/run-editor.sh`）

1. 视图菜单出现「目录树」「大纲」两项（无「侧边栏」），默认都勾选
2. 只勾目录树 → 侧边栏仅文件面板 + 「打开文件夹...」按钮
3. 只勾大纲 → 侧边栏仅大纲面板，按钮消失，停驻页在大纲
4. 全不勾 → 侧边栏隐藏
5. 停在大纲页再取消大纲勾选 → 自动切回目录树页
6. Ctrl+B：两勾→全收；再按→恢复两勾；手动只留大纲→Ctrl+B 收起→再按→恢复只大纲
7. 语言切换后菜单与 tab 文案正确（tab 显示「目录树」）
8. 编辑区有焦点时 Ctrl+B 仍可用（若失效，记录现象——QWebEngine 吞键问题另行处理，不阻塞本功能提交）

- [ ] **Step 6: 用户确认后统一提交**

```bash
git add src/ui/MenuBar.h src/ui/MenuBar.cpp src/ui/SideBar.h src/ui/SideBar.cpp \
        src/ui/MainWindow.h src/ui/MainWindow.cpp tests/test_sidebar.cpp tests/CMakeLists.txt \
        docs/superpowers/specs/2026-08-23-sidebar-pane-toggles-design.md \
        docs/superpowers/plans/2026-08-23-sidebar-pane-toggles.md
git commit -m "feat: 侧边栏改为目录树/大纲独立开关，Ctrl+B 整体收展"
```

版本推进：菜单重构 + 新测试（增量中等），建议 `+0.1.0` → 0.13.0，同步 `CMakeLists.txt` 版本、`CHANGELOG.md`、tag（与用户确认后执行）。
