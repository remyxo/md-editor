# 侧边栏两面板独立折叠动效 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 把侧边栏从 QTabWidget 双 tab 改为上下两个可独立折叠的面板（目录树/大纲），每个面板带展开/收起高度动画；两面板全收时侧边栏整体宽度动画收起。

**Architecture:** 新增通用 `CollapsibleSection` 组件（标题栏 QToolButton + 内容区，QPropertyAnimation 动画内容 `maximumHeight`）；`SideBar` 用两个 CollapsibleSection 上下堆叠替换 QTabWidget，`setFileTreeVisible`/`setOutlineVisible` 接口不变（MainWindow/MenuBar 不改），内部触发面板折叠 + 侧边栏宽度动画。

**Tech Stack:** Qt6 Widgets，`QPropertyAnimation` / `QToolButton` / `QVBoxLayout`，QtTest 单元测试。

## Global Constraints

- 全部注释/文档用中文，代码标识符用英文
- Qt 6.8 最低版本，`QPropertyAnimation` 为 Qt6 内置，无新依赖
- 版本号本次不推进（沿用本周约定，收口时统一决定；执行中如需提交不 bump VERSION/CHANGELOG/tag）
- 提交前缀：新组件/重写用 `feat:`，纯测试调整用 `feat:` 一并
- 每次改源码/资源后必须 `cmake --build build` 重编译再测
- 验收基准：`ctest --test-dir build --output-on-failure` 全量通过 + 二进制时间戳新于源文件

---

### Task 1: CollapsibleSection 通用可折叠组件

**Files:**
- Create: `src/ui/CollapsibleSection.h`
- Create: `src/ui/CollapsibleSection.cpp`
- Test: `tests/test_collapsiblesection.cpp`
- Modify: `src/CMakeLists.txt`（SOURCES/HEADERS 各加一项）
- Modify: `tests/CMakeLists.txt`（新增 test_collapsiblesection 目标）

**Interfaces:**
- Produces（Task 2 依赖）:
  - `class CollapsibleSection : public QWidget`
  - `void setContent(QWidget *content)` — 注入内容区（内部 `layout->addWidget(content, 1)`）
  - `void setTitle(const QString &title)` — 更新标题栏文本（`▼/▶ ` 前缀 + 标题）
  - `bool isCollapsed() const`
  - `void setCollapsed(bool collapsed)` — 切换并带动画
  - `void setCollapsed(bool collapsed, bool animate)` — animate=false 立即生效
  - `int contentMaxHeight() const` — 内容区当前 maximumHeight（测试断言用）
  - `void collapsedChanged(bool collapsed)` — 信号

- [ ] **Step 1: 写失败测试**

`tests/test_collapsiblesection.cpp`:
```cpp
#include <QtTest>
#include "ui/CollapsibleSection.h"
#include <QLabel>

// CollapsibleSection 可折叠面板行为测试
class TestCollapsibleSection : public QObject {
    Q_OBJECT
private slots:
    void initial_expanded();          // 初始未收起，内容可展开
    void collapse_animates_height();  // 收起：高度动画到 0，isCollapsed 立即翻转
    void expand_restores_height();    // 展开：高度动画恢复
    void collapsed_signal();          // collapsedChanged 信号
private:
    QLabel *makeContent(CollapsibleSection &s) const;
};

QLabel *TestCollapsibleSection::makeContent(CollapsibleSection &s) const
{
    QLabel *lbl = new QLabel(QStringLiteral("内容"), &s);
    s.setContent(lbl);
    return lbl;
}

void TestCollapsibleSection::initial_expanded()
{
    CollapsibleSection s;
    QLabel *lbl = makeContent(s);
    s.show();
    QVERIFY(!s.isCollapsed());
    QVERIFY(lbl->maximumHeight() > 0);
}

void TestCollapsibleSection::collapse_animates_height()
{
    CollapsibleSection s;
    QLabel *lbl = makeContent(s);
    s.show();
    s.setCollapsed(true);
    QVERIFY(s.isCollapsed());            // 状态标志立即翻转
    QTest::qWait(400);                   // 等 300ms 动画完成
    QCOMPARE(lbl->maximumHeight(), 0);   // 收起后内容高度为 0
}

void TestCollapsibleSection::expand_restores_height()
{
    CollapsibleSection s;
    QLabel *lbl = makeContent(s);
    s.show();
    s.setCollapsed(true);
    QTest::qWait(400);
    s.setCollapsed(false);
    QVERIFY(!s.isCollapsed());
    QTest::qWait(400);
    QVERIFY(lbl->maximumHeight() > 0);   // 恢复可展开
}

void TestCollapsibleSection::collapsed_signal()
{
    CollapsibleSection s;
    makeContent(s);
    QSignalSpy spy(&s, &CollapsibleSection::collapsedChanged);
    s.setCollapsed(true);
    QCOMPARE(spy.count(), 1);
    QVERIFY(spy.takeFirst().at(0).toBool());
}

QTEST_MAIN(TestCollapsibleSection)
#include "test_collapsiblesection.moc"
```

- [ ] **Step 2: 运行测试验证失败**

Run: `cmake --build build -j$(nproc) && cd build/tests && ./test_collapsiblesection`
Expected: 编译失败（CollapsibleSection.h 不存在）

- [ ] **Step 3: 写 CollapsibleSection.h**

`src/ui/CollapsibleSection.h`:
```cpp
#ifndef MD_EDITOR_COLLAPSIBLESECTION_H
#define MD_EDITOR_COLLAPSIBLESECTION_H

#include <QWidget>

class QToolButton;
class QVBoxLayout;
class QPropertyAnimation;

// 通用可折叠面板：标题栏（点击切换折叠）+ 内容区（高度动画展开/收起）。
// 收起时仅标题栏可见；展开时内容区高度动画恢复，让外层布局填充分配空间。
class CollapsibleSection : public QWidget {
    Q_OBJECT
public:
    explicit CollapsibleSection(QWidget *parent = nullptr);

    void setTitle(const QString &title);
    QString title() const;

    void setContent(QWidget *content);
    QWidget *content() const;

    bool isCollapsed() const;
    void setCollapsed(bool collapsed);
    void setCollapsed(bool collapsed, bool animate);

    int contentMaxHeight() const;   // 内容区当前 maximumHeight（测试断言用）

signals:
    void collapsedChanged(bool collapsed);

private:
    void startHeightAnimation(int target);
    void updateHeader();

    QToolButton *m_headerBtn;
    QWidget *m_content = nullptr;
    QVBoxLayout *m_layout;
    QPropertyAnimation *m_heightAnim;
    QString m_title;
    bool m_collapsed = false;
};

#endif // MD_EDITOR_COLLAPSIBLESECTION_H
```

- [ ] **Step 4: 写 CollapsibleSection.cpp**

`src/ui/CollapsibleSection.cpp`:
```cpp
#include "CollapsibleSection.h"
#include <QToolButton>
#include <QVBoxLayout>
#include <QPropertyAnimation>

// 展开态内容区最大高度：取大值让外层布局填充分配空间（不设固定高，
// 否则面板无法随窗口拉高）
static constexpr int kExpandedMax = 10000;

CollapsibleSection::CollapsibleSection(QWidget *parent)
    : QWidget(parent)
    , m_headerBtn(new QToolButton(this))
    , m_layout(new QVBoxLayout(this))
    , m_heightAnim(new QPropertyAnimation(this))
{
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(0);

    m_headerBtn->setObjectName(QStringLiteral("collapsibleHeader"));
    m_headerBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_headerBtn->setCheckable(true);
    m_headerBtn->setAutoRaise(true);
    m_headerBtn->setStyleSheet(
        "QToolButton#collapsibleHeader { border: none; padding: 6px 8px;"
        " text-align: left; font-weight: bold; background: transparent; }");
    // 点击切换折叠：checked 即收起
    connect(m_headerBtn, &QToolButton::clicked, this,
            [this](bool checked) { setCollapsed(checked); });
    m_layout->addWidget(m_headerBtn);

    m_heightAnim->setDuration(300);
    m_heightAnim->setEasingCurve(QEasingCurve::InOutCubic);
}

void CollapsibleSection::setTitle(const QString &title)
{
    m_title = title;
    updateHeader();
}

QString CollapsibleSection::title() const
{
    return m_title;
}

void CollapsibleSection::setContent(QWidget *content)
{
    m_content = content;
    content->setMinimumHeight(0);
    content->setMaximumHeight(m_collapsed ? 0 : kExpandedMax);
    m_layout->addWidget(content, 1);
    m_heightAnim->setTargetObject(content);
    m_heightAnim->setPropertyName("maximumHeight");
}

QWidget *CollapsibleSection::content() const
{
    return m_content;
}

bool CollapsibleSection::isCollapsed() const
{
    return m_collapsed;
}

void CollapsibleSection::setCollapsed(bool collapsed)
{
    setCollapsed(collapsed, true);
}

void CollapsibleSection::setCollapsed(bool collapsed, bool animate)
{
    if (m_collapsed == collapsed)
        return;
    m_collapsed = collapsed;
    m_headerBtn->setChecked(collapsed);
    updateHeader();
    if (!collapsed && m_content)
        m_content->setVisible(true);
    if (m_content)
        startHeightAnimation(collapsed ? 0 : kExpandedMax);
    emit collapsedChanged(collapsed);
}

int CollapsibleSection::contentMaxHeight() const
{
    return m_content ? m_content->maximumHeight() : 0;
}

void CollapsibleSection::startHeightAnimation(int target)
{
    if (!m_content)
        return;
    m_heightAnim->stop();
    m_heightAnim->setStartValue(m_content->maximumHeight());
    m_heightAnim->setEndValue(target);
    m_heightAnim->start();
}

void CollapsibleSection::updateHeader()
{
    m_headerBtn->setText(QString(m_collapsed ? QLatin1String("▶ ")
                                             : QLatin1String("▼ "))
                         + m_title);
}
```

- [ ] **Step 5: 注册新文件到构建**

`src/CMakeLists.txt`：`SOURCES` 列表 `ui/SideBar.cpp` 后加一行 `ui/CollapsibleSection.cpp`；`HEADERS` 列表 `ui/SideBar.h` 后加一行 `ui/CollapsibleSection.h`。

`tests/CMakeLists.txt`：文件末尾追加：
```cmake
add_executable(test_collapsiblesection test_collapsiblesection.cpp
    ../src/ui/CollapsibleSection.cpp)
target_include_directories(test_collapsiblesection PRIVATE ../src)
target_link_libraries(test_collapsiblesection PRIVATE Qt6::Test Qt6::Widgets)
add_test(NAME test_collapsiblesection COMMAND test_collapsiblesection)
```

- [ ] **Step 6: 重编译并跑测试**

Run: `cmake -S . -B build && cmake --build build -j$(nproc) && cd build && ctest --output-on-failure -R collapsiblesection`
Expected: 4/4 PASS

- [ ] **Step 7: 提交**

```bash
git add src/ui/CollapsibleSection.cpp src/ui/CollapsibleSection.h \
        tests/test_collapsiblesection.cpp src/CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat: 新增 CollapsibleSection 通用可折叠面板组件"
```

---

### Task 2: SideBar 重写为两面板独立折叠

**Files:**
- Modify: `src/ui/SideBar.h`（QTabWidget → 两个 CollapsibleSection + 宽度动画）
- Modify: `src/ui/SideBar.cpp`
- Test: `tests/test_sidebar.cpp`（整体重写）
- Modify: `tests/CMakeLists.txt`（test_sidebar 追加 CollapsibleSection.cpp）

**Interfaces:**
- Consumes: Task 1 的 `CollapsibleSection`（setTitle/setContent/setCollapsed/isCollapsed）
- Produces（Task 3 依赖）: `SideBar::setFileTreeVisible(bool)` / `setOutlineVisible(bool)` / `fileTree()` / `outlineTree()` / `openFolderClicked` 信号全部保持原签名，MainWindow 无需改动

- [ ] **Step 1: 重写失败测试**

整体替换 `tests/test_sidebar.cpp`:
```cpp
#include <QtTest>
#include "ui/SideBar.h"
#include "ui/CollapsibleSection.h"
#include <QPushButton>

// 侧边栏双面板（目录树/大纲）独立折叠 + 整体宽度收起行为测试
class TestSideBar : public QObject {
    Q_OBJECT
private slots:
    void initial_state();            // 初始两面板展开，侧边栏可见
    void file_only();                // 只目录树：大纲收起，按钮保留
    void outline_only();             // 只大纲：目录树收起，按钮隐藏
    void all_off_collapses_whole();  // 全收：宽度动画到 0
    void recover_from_all_off();     // 从全收重开目录树：宽度恢复
private:
    CollapsibleSection *fileSection(const SideBar &sb) const;
    CollapsibleSection *outlineSection(const SideBar &sb) const;
    QPushButton *openBtn(const SideBar &sb) const;
};

CollapsibleSection *TestSideBar::fileSection(const SideBar &sb) const
{
    return sb.findChild<CollapsibleSection *>(QStringLiteral("fileTreeSection"));
}

CollapsibleSection *TestSideBar::outlineSection(const SideBar &sb) const
{
    return sb.findChild<CollapsibleSection *>(QStringLiteral("outlineSection"));
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
    QVERIFY(!fileSection(sb)->isCollapsed());
    QVERIFY(!outlineSection(sb)->isCollapsed());
    QVERIFY(openBtn(sb)->isVisibleTo(&sb));
}

void TestSideBar::file_only()
{
    SideBar sb;
    sb.show();
    sb.setOutlineVisible(false);
    QVERIFY(outlineSection(sb)->isCollapsed());
    QVERIFY(!fileSection(sb)->isCollapsed());
    QVERIFY(sb.isVisible());
    QVERIFY(openBtn(sb)->isVisibleTo(&sb));  // 目录树在，按钮保留
    QTest::qWait(400);
    QVERIFY(sb.width() > 0);                 // 单面板收起不触发整体收起
}

void TestSideBar::outline_only()
{
    SideBar sb;
    sb.show();
    sb.setFileTreeVisible(false);
    QVERIFY(fileSection(sb)->isCollapsed());
    QVERIFY(!outlineSection(sb)->isCollapsed());
    QVERIFY(sb.isVisible());
    QVERIFY(!openBtn(sb)->isVisibleTo(&sb)); // 无目录树则按钮隐藏
}

void TestSideBar::all_off_collapses_whole()
{
    SideBar sb;
    sb.show();
    sb.setFileTreeVisible(false);
    sb.setOutlineVisible(false);
    QVERIFY(fileSection(sb)->isCollapsed());
    QVERIFY(outlineSection(sb)->isCollapsed());
    QTest::qWait(400);
    QCOMPARE(sb.maximumWidth(), 0);          // 两面板全收 → 宽度收到 0
}

void TestSideBar::recover_from_all_off()
{
    SideBar sb;
    sb.show();
    sb.setFileTreeVisible(false);
    sb.setOutlineVisible(false);
    QTest::qWait(400);
    sb.setFileTreeVisible(true);
    QVERIFY(!fileSection(sb)->isCollapsed());
    QVERIFY(outlineSection(sb)->isCollapsed());
    QTest::qWait(400);
    QVERIFY(sb.width() > 0);                 // 重开 → 宽度恢复
    QVERIFY(sb.maximumWidth() != 0);
}

QTEST_MAIN(TestSideBar)
#include "test_sidebar.moc"
```

- [ ] **Step 2: 运行测试验证失败**

Run: `cmake --build build -j$(nproc) && cd build/tests && ./test_sidebar`
Expected: 编译失败（SideBar 仍是旧 QTabWidget 机制，`findChild<CollapsibleSection*>` 无结果 / 旧头文件无对应成员）

- [ ] **Step 3: 重写 SideBar.h**

`src/ui/SideBar.h`:
```cpp
#ifndef MD_EDITOR_SIDEBAR_H
#define MD_EDITOR_SIDEBAR_H

#include <QWidget>
#include <QPushButton>

class CollapsibleSection;
class FileTree;
class OutlineTree;
class QPropertyAnimation;

// 侧边栏：目录树/大纲两个可独立折叠面板（CollapsibleSection）上下堆叠。
// 单面板收起 → 收成一条标题栏；两面板全收 → 侧边栏整体宽度动画收起。
class SideBar : public QWidget {
    Q_OBJECT
public:
    explicit SideBar(QWidget *parent = nullptr);

    FileTree *fileTree() const;
    OutlineTree *outlineTree() const;

    // 双开关：目录树/大纲面板独立折叠；两面板全收时侧边栏整体宽度收起
    void setFileTreeVisible(bool visible);
    void setOutlineVisible(bool visible);

    // 语言切换后重设全部文案
    void retranslateUi();

signals:
    void openFolderClicked();

private:
    void setupUi();
    void updateWholeCollapse();
    void animateWidth(int target);
    void onWidthAnimFinished();

    CollapsibleSection *m_fileSection;
    CollapsibleSection *m_outlineSection;
    FileTree *m_fileTree;
    OutlineTree *m_outlineTree;
    QPushButton *m_openFolderBtn;
    QPropertyAnimation *m_widthAnim;
    bool m_fileTreeVisible = true;
    bool m_outlineVisible = true;
    bool m_wholeCollapsed = false;
    int m_savedWidth = 240;
};

#endif // MD_EDITOR_SIDEBAR_H
```

- [ ] **Step 4: 重写 SideBar.cpp**

`src/ui/SideBar.cpp`:
```cpp
#include "SideBar.h"
#include "CollapsibleSection.h"
#include "FileTree.h"
#include "OutlineTree.h"
#include <QVBoxLayout>
#include <QPropertyAnimation>

SideBar::SideBar(QWidget *parent)
    : QWidget(parent)
    , m_fileSection(new CollapsibleSection(this))
    , m_outlineSection(new CollapsibleSection(this))
    , m_fileTree(new FileTree(this))
    , m_outlineTree(new OutlineTree(this))
    , m_openFolderBtn(nullptr)
    , m_widthAnim(new QPropertyAnimation(this, "maximumWidth", this))
{
    setMinimumWidth(0);   // 允许宽度动画收到 0
    setupUi();
    m_widthAnim->setDuration(300);
    m_widthAnim->setEasingCurve(QEasingCurve::InOutCubic);
    connect(m_widthAnim, &QPropertyAnimation::finished,
            this, &SideBar::onWidthAnimFinished);
}

void SideBar::setupUi()
{
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_fileSection->setObjectName(QStringLiteral("fileTreeSection"));
    m_fileSection->setTitle(tr("目录树"));
    m_fileSection->setContent(m_fileTree);   // addWidget 会把 FileTree 重挂到 Section
    layout->addWidget(m_fileSection);

    m_outlineSection->setObjectName(QStringLiteral("outlineSection"));
    m_outlineSection->setTitle(tr("大纲"));
    m_outlineSection->setContent(m_outlineTree);
    layout->addWidget(m_outlineSection);

    m_openFolderBtn = new QPushButton(tr("打开文件夹..."), this);
    connect(m_openFolderBtn, &QPushButton::clicked, this, &SideBar::openFolderClicked);
    layout->addWidget(m_openFolderBtn);

    retranslateUi();
}

FileTree *SideBar::fileTree() const { return m_fileTree; }
OutlineTree *SideBar::outlineTree() const { return m_outlineTree; }

void SideBar::setFileTreeVisible(bool visible)
{
    m_fileTreeVisible = visible;
    m_openFolderBtn->setVisible(visible);   // 「打开文件夹」跟随目录树面板展开状态
    m_fileSection->setCollapsed(!visible);
    updateWholeCollapse();
}

void SideBar::setOutlineVisible(bool visible)
{
    m_outlineVisible = visible;
    m_outlineSection->setCollapsed(!visible);
    updateWholeCollapse();
}

// 两面板全收 → 侧边栏宽度收起；任一面板展开（从全收态）→ 宽度展开
void SideBar::updateWholeCollapse()
{
    const bool bothOff = !m_fileTreeVisible && !m_outlineVisible;
    if (bothOff != m_wholeCollapsed) {
        m_wholeCollapsed = bothOff;
        animateWidth(bothOff ? 0 : m_savedWidth);
    }
}

void SideBar::animateWidth(int target)
{
    m_widthAnim->stop();                 // 快速连续切换：从当前实际值起算
    setMinimumWidth(0);
    if (target == 0) {
        m_savedWidth = width() > 0 ? width() : m_savedWidth;  // 收起前记录可见宽
        setMaximumWidth(m_savedWidth);
        m_widthAnim->setStartValue(m_savedWidth);
    } else {
        m_widthAnim->setStartValue(maximumWidth());           // 可能中途 0 或半值
    }
    m_widthAnim->setEndValue(target);
    m_widthAnim->start();
}

void SideBar::onWidthAnimFinished()
{
    if (m_wholeCollapsed) {
        setMaximumWidth(0);                 // 收起完成：保持 0 宽
        setMinimumWidth(0);
        // 隐藏侧边栏让 QSplitter 把腾出的空间分给编辑区（否则只缩到 0 宽，
        // splitter 不重分配，编辑区旁残留一段空白——问题2 根因）
        setVisible(false);
    } else {
        setMaximumWidth(QWIDGETSIZE_MAX);   // 展开完成：恢复可自由调整
        setMinimumWidth(240);
    }
}

void SideBar::retranslateUi()
{
    m_fileSection->setTitle(tr("目录树"));
    m_outlineSection->setTitle(tr("大纲"));
    if (m_openFolderBtn)
        m_openFolderBtn->setText(tr("打开文件夹..."));
}
```

- [ ] **Step 5: 更新 tests/CMakeLists.txt**

`tests/CMakeLists.txt`：`test_sidebar` 的 `add_executable` 追加 `../src/ui/CollapsibleSection.cpp`：
```cmake
add_executable(test_sidebar test_sidebar.cpp
    ../src/ui/SideBar.cpp ../src/ui/FileTree.cpp ../src/ui/OutlineTree.cpp
    ../src/ui/CollapsibleSection.cpp)
```

- [ ] **Step 6: 重编译并跑测试**

Run: `cmake --build build -j$(nproc) && cd build && ctest --output-on-failure -R "sidebar|collapsible"`
Expected: test_sidebar 5/5 PASS、test_collapsiblesection 4/4 PASS

- [ ] **Step 7: 全量构建确认 MainWindow 无回归**

Run: `cmake --build build -j$(nproc) && ctest --test-dir build --output-on-failure`
Expected: 全部通过（MainWindow 仍调 `setFileTreeVisible`/`setOutlineVisible`，接口未变）

- [ ] **Step 8: 提交**

```bash
git add src/ui/SideBar.cpp src/ui/SideBar.h tests/test_sidebar.cpp tests/CMakeLists.txt
git commit -m "feat: 侧边栏改为目录树/大纲两面板独立折叠，全收时整体宽度动画收起"
```

---

### Task 3: 集成验证与冒烟

**Files:** 无代码改动（纯验证）

- [ ] **Step 1: 全量测试**

Run: `ctest --test-dir build --output-on-failure`
Expected: 10+1 个测试目标全部 PASS（新增 test_collapsiblesection）

- [ ] **Step 2: 时间戳核对**

Run: `stat -c '%y %n' build/src/md-editor src/ui/SideBar.cpp src/ui/CollapsibleSection.cpp`
Expected: 二进制时间戳新于所有改动源文件

- [ ] **Step 3: 启动冒烟**

Run: `timeout 12 ./build/src/md-editor`（后台起、8 秒后查存活、超时正常退出）
Expected: 应用启动无崩溃；人工目视验证：标题栏点击折叠/展开动画平滑、菜单开关同步、两面板全收后编辑区扩展、重开宽度记忆正确、语言切换文案更新

- [ ] **Step 4: 收尾提交（如需）**

若冒烟发现小问题，按 `fix:` 提交；无则本任务无提交。
